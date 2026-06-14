#include "zenith_client.h"
#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <iomanip>
#include <cstdlib>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
static std::string configDir() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return std::string(path) + "\\flycast";
    return ".";
}
#elif defined(__APPLE__)
static std::string configDir() {
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/flycast" : ".";
}
#else
static std::string configDir() {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/flycast";
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.config/flycast" : ".";
}
#endif

static std::string iniPath() { return configDir() + "/zenith.ini"; }

namespace {
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
static const u32 k256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define ROTR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROTR32(x,2)^ROTR32(x,13)^ROTR32(x,22))
#define EP1(x) (ROTR32(x,6)^ROTR32(x,11)^ROTR32(x,25))
#define SIG0(x)(ROTR32(x,7)^ROTR32(x,18)^((x)>>3))
#define SIG1(x)(ROTR32(x,17)^ROTR32(x,19)^((x)>>10))
struct Sha256Ctx { u8 data[64]; u32 datalen,state[8]; u64 bitlen; };
static void sha256_transform(Sha256Ctx& c,const u8* d){
    u32 a,b,cc,dd,e,f,g,h,i,t1,t2,m[64];
    for(i=0;i<16;++i) m[i]=(d[i*4]<<24)|(d[i*4+1]<<16)|(d[i*4+2]<<8)|d[i*4+3];
    for(;i<64;++i) m[i]=SIG1(m[i-2])+m[i-7]+SIG0(m[i-15])+m[i-16];
    a=c.state[0];b=c.state[1];cc=c.state[2];dd=c.state[3];
    e=c.state[4];f=c.state[5];g=c.state[6];h=c.state[7];
    for(i=0;i<64;++i){
        t1=h+EP1(e)+CH(e,f,g)+k256[i]+m[i];
        t2=EP0(a)+MAJ(a,b,cc);
        h=g;g=f;f=e;e=dd+t1;dd=cc;cc=b;b=a;a=t1+t2;
    }
    c.state[0]+=a;c.state[1]+=b;c.state[2]+=cc;c.state[3]+=dd;
    c.state[4]+=e;c.state[5]+=f;c.state[6]+=g;c.state[7]+=h;
}
static void sha256_init(Sha256Ctx& c){
    c.datalen=0;c.bitlen=0;
    c.state[0]=0x6a09e667;c.state[1]=0xbb67ae85;
    c.state[2]=0x3c6ef372;c.state[3]=0xa54ff53a;
    c.state[4]=0x510e527f;c.state[5]=0x9b05688c;
    c.state[6]=0x1f83d9ab;c.state[7]=0x5be0cd19;
}
static void sha256_update(Sha256Ctx& c,const u8* d,size_t len){
    for(size_t i=0;i<len;++i){
        c.data[c.datalen++]=d[i];
        if(c.datalen==64){sha256_transform(c,c.data);c.bitlen+=512;c.datalen=0;}
    }
}
static void sha256_final(Sha256Ctx& c,u8 h[32]){
    u32 i=c.datalen;
    c.data[i++]=0x80;
    if(c.datalen<56){while(i<56)c.data[i++]=0;}
    else{while(i<64)c.data[i++]=0;sha256_transform(c,c.data);memset(c.data,0,56);i=56;}
    c.bitlen+=c.datalen*8;
    for(int j=0;j<8;j++){c.data[56+j]=(c.bitlen>>(56-j*8))&0xff;}
    sha256_transform(c,c.data);
    for(i=0;i<4;++i){
        h[i]=(c.state[0]>>(24-i*8))&0xff;h[i+4]=(c.state[1]>>(24-i*8))&0xff;
        h[i+8]=(c.state[2]>>(24-i*8))&0xff;h[i+12]=(c.state[3]>>(24-i*8))&0xff;
        h[i+16]=(c.state[4]>>(24-i*8))&0xff;h[i+20]=(c.state[5]>>(24-i*8))&0xff;
        h[i+24]=(c.state[6]>>(24-i*8))&0xff;h[i+28]=(c.state[7]>>(24-i*8))&0xff;
    }
}
static std::string rawHmac(const std::string& k,const std::string& msg){
    u8 kp[64]={},ko[64]={},ki[64]={};
    if(k.size()>64){Sha256Ctx c;sha256_init(c);sha256_update(c,(const u8*)k.data(),k.size());sha256_final(c,kp);}
    else memcpy(kp,k.data(),k.size());
    for(int i=0;i<64;i++){ki[i]=kp[i]^0x36;ko[i]=kp[i]^0x5c;}
    u8 inner[32];
    {Sha256Ctx c;sha256_init(c);sha256_update(c,ki,64);sha256_update(c,(const u8*)msg.data(),msg.size());sha256_final(c,inner);}
    u8 outer[32];
    {Sha256Ctx c;sha256_init(c);sha256_update(c,ko,64);sha256_update(c,inner,32);sha256_final(c,outer);}
    std::ostringstream ss;
    for(int i=0;i<32;i++) ss<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)outer[i];
    return ss.str();
}
static size_t curlWrite(void* ptr,size_t sz,size_t nmemb,std::string* out){
    out->append((char*)ptr,sz*nmemb);return sz*nmemb;
}
static long doPost(const std::string& url,const std::string& body,std::string& out,bool sign=false){
    CURL* c=curl_easy_init();
    if(!c) return -1;
    struct curl_slist* hdrs=nullptr;
    hdrs=curl_slist_append(hdrs,"Content-Type: application/json");
    if(sign){
        std::string tok=zenith::getDeviceToken();
        std::string sig=zenith::hmacSha256Hex(ZENITH_HOME_WORKER_SECRET,body);
        hdrs=curl_slist_append(hdrs,("X-Device-Token: "+tok).c_str());
        hdrs=curl_slist_append(hdrs,("X-Signature: "+sig).c_str());
    }
    curl_easy_setopt(c,CURLOPT_URL,url.c_str());
    curl_easy_setopt(c,CURLOPT_POSTFIELDS,body.c_str());
    curl_easy_setopt(c,CURLOPT_POSTFIELDSIZE,(long)body.size());
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,hdrs);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,curlWrite);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&out);
    curl_easy_setopt(c,CURLOPT_TIMEOUT,10L);
    curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
    CURLcode res=curl_easy_perform(c);
    long code=-1;
    if(res==CURLE_OK) curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);
    curl_slist_free_all(hdrs);curl_easy_cleanup(c);
    return code;
}
struct BgTask{
    enum class Type{SessionStart,SessionEnd,Heartbeat}type;
    std::string gameId,gameTitle;int playtimeS=0;
};
static std::thread g_thread;
static std::mutex g_mu;
static std::condition_variable g_cv;
static std::queue<BgTask> g_queue;
static std::atomic<bool> g_running{false};
static std::string g_sessionId;
static std::chrono::steady_clock::time_point g_sessionStart;
static std::atomic<bool> g_inSession{false};
static int elapsed(){return(int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-g_sessionStart).count();}
static void doSessionStart(const std::string& id,const std::string& title){
    std::string body="{\"game_id\":\""+id+"\",\"game_title\":\""+title+"\"}";
    std::string resp;
    if(doPost(std::string(ZENITH_API_BASE)+"/api/public/telemetry/start",body,resp,true)==200){
        auto p=resp.find("\"session_id\":\"");
        if(p!=std::string::npos){
            p+=14;auto e=resp.find('"',p);
            g_sessionId=resp.substr(p,e-p);
            g_sessionStart=std::chrono::steady_clock::now();
            g_inSession=true;
        }
    }
}
static void doSessionEnd(int t){
    if(g_sessionId.empty()) return;
    std::string body="{\"session_id\":\""+g_sessionId+"\",\"playtime_s\":"+std::to_string(t)+"}";
    std::string resp;
    doPost(std::string(ZENITH_API_BASE)+"/api/public/telemetry/end",body,resp,true);
    g_sessionId.clear();g_inSession=false;
}
static void doHeartbeat(int t){
    if(g_sessionId.empty()) return;
    std::string body="{\"session_id\":\""+g_sessionId+"\",\"playtime_s\":"+std::to_string(t)+"}";
    std::string resp;
    doPost(std::string(ZENITH_API_BASE)+"/api/public/telemetry/heartbeat",body,resp,true);
}
static void bgLoop(){
    auto nextHb=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(g_running){
        std::unique_lock<std::mutex> lk(g_mu);
        g_cv.wait_until(lk,nextHb,[](){return !g_queue.empty()||!g_running;});
        while(!g_queue.empty()){
            BgTask t=std::move(g_queue.front());g_queue.pop();lk.unlock();
            switch(t.type){
                case BgTask::Type::SessionStart: doSessionStart(t.gameId,t.gameTitle);break;
                case BgTask::Type::SessionEnd:   doSessionEnd(t.playtimeS);break;
                case BgTask::Type::Heartbeat:    doHeartbeat(t.playtimeS);break;
            }
            lk.lock();
        }
        if(std::chrono::steady_clock::now()>=nextHb){
            nextHb=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            if(g_inSession){lk.unlock();doHeartbeat(elapsed());lk.lock();}
        }
    }
}
} // anon

std::string zenith::hmacSha256Hex(const std::string& k,const std::string& d){return rawHmac(k,d);}
namespace {
struct ZenithIni { std::string deviceToken,deviceName; };
static ZenithIni readIni(){
    ZenithIni ini;
    std::ifstream f(iniPath());
    if(!f.is_open()) return ini;
    std::string line,section;
    while(std::getline(f,line)){
        if(line.empty()||line[0]=='#') continue;
        if(line[0]=='['){section=line.substr(1,line.find(']')-1);continue;}
        if(section!="Zenith") continue;
        auto eq=line.find('=');
        if(eq==std::string::npos) continue;
        std::string k=line.substr(0,eq),v=line.substr(eq+1);
        if(k=="DeviceToken") ini.deviceToken=v;
        if(k=="DeviceName") ini.deviceName=v;
    }
    return ini;
}
static void writeIni(const ZenithIni& ini){
    (void)system(("mkdir -p \""+configDir()+"\"").c_str());
    std::ofstream f(iniPath());
    f<<"[Zenith]\nDeviceToken="<<ini.deviceToken<<"\nDeviceName="<<ini.deviceName<<"\n";
}
}
std::string zenith::getDeviceToken(){return readIni().deviceToken;}
std::string zenith::getDeviceName(){return readIni().deviceName;}
bool zenith::isPaired(){return !getDeviceToken().empty();}
void zenith::storeDeviceToken(const std::string& t,const std::string& n){writeIni({t,n});}
void zenith::init(){curl_global_init(CURL_GLOBAL_DEFAULT);g_running=true;g_thread=std::thread(bgLoop);}
void zenith::shutdown(){
    if(g_inSession) sessionEnd();
    g_running=false;g_cv.notify_all();
    if(g_thread.joinable()) g_thread.join();
    curl_global_cleanup();
}
void zenith::sessionStart(const std::string& id,const std::string& title){
    if(!isPaired()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_queue.push({BgTask::Type::SessionStart,id,title});g_cv.notify_one();
}
void zenith::sessionEnd(){
    if(!isPaired()) return;
    int t=g_inSession?elapsed():0;
    std::lock_guard<std::mutex> lk(g_mu);
    BgTask bt;bt.type=BgTask::Type::SessionEnd;bt.playtimeS=t;
    g_queue.push(bt);g_cv.notify_one();
}
bool zenith::pairDevice(const std::string& code,const std::string& name,std::string& err){
    std::string body="{\"code\":\""+code+"\",\"device_name\":\""+name+"\"}";
    std::string resp;
    long s=doPost(std::string(ZENITH_API_BASE)+"/api/public/pair",body,resp,false);
    if(s!=200){err="HTTP "+std::to_string(s)+": "+resp;return false;}
    auto p=resp.find("\"device_token\":\"");
    if(p==std::string::npos){err="Bad response: "+resp;return false;}
    p+=16;auto e=resp.find('"',p);
    storeDeviceToken(resp.substr(p,e-p),name);
    return true;
}
