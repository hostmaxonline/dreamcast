#pragma once
#ifndef ZENITH_HOME_WORKER_SECRET
#define ZENITH_HOME_WORKER_SECRET "REPLACE_ME_SECRET"
#endif
#ifndef ZENITH_API_BASE
#define ZENITH_API_BASE "https://dreamcast-zenith.lovable.app"
#endif
#include <string>
namespace zenith {
std::string getDeviceToken();
std::string getDeviceName();
void        storeDeviceToken(const std::string& token, const std::string& name);
bool        isPaired();
bool        pairDevice(const std::string& sixCharCode, const std::string& deviceName, std::string& errOut);
void        sessionStart(const std::string& gameId, const std::string& gameTitle);
void        sessionEnd();
void        init();
void        shutdown();
std::string hmacSha256Hex(const std::string& key, const std::string& data);
}
