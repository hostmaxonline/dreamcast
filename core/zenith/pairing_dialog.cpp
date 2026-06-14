#include "pairing_dialog.h"
#include "zenith_client.h"
#include "deps/imgui/imgui.h"
#include <cstring>
#include <string>
#include <thread>
#include <atomic>

// DC Archive color palette
// Cream:      #FDF0E8  (0.99, 0.94, 0.91)
// Navy:       #1A1F3A  (0.10, 0.12, 0.23)
// Orange:     #E8521A  (0.91, 0.32, 0.10)
// Teal:       #4ECDC4  (0.31, 0.80, 0.77)
// Card bg:    #FFF8F2  (1.00, 0.97, 0.95)
// Muted text: #8B7355  (0.55, 0.45, 0.33)

namespace {
static bool        g_open       = false;
static char        g_code[8]    = {};
static char        g_name[128]  = {};
static std::string g_status;
static bool        g_busy       = false;
static bool        g_success    = false;
static std::atomic<bool> g_done{false};
static std::string g_err;
static std::thread g_worker;

static const ImVec4 kNavy   = {0.10f, 0.12f, 0.23f, 1.f};
static const ImVec4 kOrange = {0.91f, 0.32f, 0.10f, 1.f};
static const ImVec4 kTeal   = {0.31f, 0.80f, 0.77f, 1.f};
static const ImVec4 kCream  = {0.99f, 0.94f, 0.91f, 1.f};
static const ImVec4 kCard   = {1.00f, 0.97f, 0.95f, 1.f};
static const ImVec4 kMuted  = {0.55f, 0.45f, 0.33f, 1.f};
static const ImVec4 kWhite  = {1.00f, 1.00f, 1.00f, 1.f};
static const ImVec4 kBorder = {0.96f, 0.90f, 0.85f, 1.f};
}

void zenith::showPairingDialog() {
    g_open    = true;
    g_busy    = false;
    g_success = false;
    g_status.clear();
    memset(g_code, 0, sizeof(g_code));
    const char* host =
#ifdef _WIN32
        getenv("COMPUTERNAME");
#else
        getenv("HOSTNAME");
#endif
    strncpy(g_name, host ? host : "My PC", sizeof(g_name) - 1);
    if (zenith::isPaired())
        g_status = "Already paired — re-enter code to switch accounts.";
}

void zenith::renderPairingDialog() {
    if (!g_open) return;

    if (g_busy && g_done.load()) {
        g_busy = false;
        if (g_err.empty()) {
            g_success = true;
            g_status  = "";
        } else {
            g_status = g_err;
        }
        if (g_worker.joinable()) g_worker.join();
    }

    // Style push
    ImGui::PushStyleColor(ImGuiCol_PopupBg,          ImVec4(0.99f,0.94f,0.91f,1.f));
    ImGui::PushStyleColor(ImGuiCol_Text,             kNavy);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,          kCard);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   kBorder);
    ImGui::PushStyleColor(ImGuiCol_Button,           kOrange);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,    ImVec4(0.80f,0.28f,0.08f,1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,     ImVec4(0.70f,0.22f,0.06f,1.f));
    ImGui::PushStyleColor(ImGuiCol_Border,           kBorder);
    ImGui::PushStyleColor(ImGuiCol_TitleBg,          kNavy);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,    kNavy);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(22.f, 20.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(12.f, 8.f));

    ImGui::SetNextWindowSize({440, 0}, ImGuiCond_Always);
    ImGui::OpenPopup("Zenith##dlg");

    if (ImGui::BeginPopupModal("Zenith##dlg", &g_open,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {

        // Header
        ImGui::TextColored(kOrange, "o"); ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
        ImGui::Text("Dreamcast Zenith");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "// DEVICE.PAIRING");
        ImGui::Separator();
        ImGui::Spacing();

        if (g_success) {
            ImGui::TextColored(kTeal,   "// SYSTEM.LINKED");
            ImGui::Spacing();
            ImGui::TextColored(kNavy,   "You're connected.");
            ImGui::Spacing();
            ImGui::TextColored(kMuted,
                "Boot any Dreamcast ROM — your Now Playing\n"
                "widget updates within 30 seconds.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button,       kNavy);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.20f,0.25f,0.45f,1.f));
            ImGui::PushStyleColor(ImGuiCol_Text,         kWhite);
            if (ImGui::Button("Close", {ImGui::GetContentRegionAvail().x, 38})) {
                g_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
        } else {
            // How-to card
            ImGui::TextColored(kMuted, "// HOW.TO.PAIR");
            ImGui::Spacing();
            ImGui::TextColored(kNavy, "1. dreamcast-zenith.lovable.app/member");
            ImGui::TextColored(kNavy, "2. Settings > Emulator Devices > Add Device");
            ImGui::TextColored(kNavy, "3. Copy the 6-character code shown");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Code input
            ImGui::TextColored(kMuted, "// ENTER.CODE");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::PushStyleColor(ImGuiCol_Border, kOrange);
            if (ImGui::InputText("##code", g_code, sizeof(g_code),
                    ImGuiInputTextFlags_CharsUppercase |
                    ImGuiInputTextFlags_CharsNoBlank)) {
                if (strlen(g_code) > 6) g_code[6] = '\0';
            }
            ImGui::PopStyleColor();
            ImGui::TextColored(kMuted, "6 characters, e.g. X7KP2Q");
            ImGui::Spacing();

            // Device name
            ImGui::TextColored(kMuted, "// DEVICE.NAME");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##name", g_name, sizeof(g_name));
            ImGui::Spacing();

            // Status
            if (!g_status.empty()) {
                ImGui::TextColored(kOrange, "! %s", g_status.c_str());
                ImGui::Spacing();
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Pair button
            bool canPair = strlen(g_code) == 6 && strlen(g_name) > 0 && !g_busy;
            if (!canPair) {
                ImGui::PushStyleColor(ImGuiCol_Button,       kBorder);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,kBorder);
                ImGui::PushStyleColor(ImGuiCol_Text,         kMuted);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
                ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
                ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
            }
            const char* lbl = g_busy ? "Pairing..." : "Pair this device";
            if (ImGui::Button(lbl, {ImGui::GetContentRegionAvail().x, 40}) && canPair) {
                std::string code(g_code), name(g_name);
                g_busy = true;
                g_status = "Contacting Zenith...";
                g_done = false;
                g_err.clear();
                g_worker = std::thread([code, name](){
                    std::string e;
                    zenith::pairDevice(code, name, e);
                    g_err  = e;
                    g_done = true;
                });
            }
            ImGui::PopStyleColor(3);
            ImGui::Spacing();
            ImGui::TextColored(kMuted, "Token: ~/.config/flycast/zenith.ini");
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(10);
}
