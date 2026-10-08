// Prototype consumer A: a plugin with its OWN copy of Dear ImGui (built with /MT) that draws a panel through the GUI host.
// Control for the experiments: lines in consumer_a.cmd next to the DLL (fault / leak / clear); unload events
// Local\gui_consumer_a_unload_<pid> (unregisters first) and Local\gui_consumer_a_rude_<pid> (just leaves, as a crashed or force-unloaded plugin would).
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>

#include "imgui.h"
#include "stellaris_gui_api.h"
#include "stellaris_gui_client.h"
#include "stellaris_gui_imgui.hpp"

#ifndef CONSUMER_TAG
#define CONSUMER_TAG "a"  /* names the log, command file, events and panel */
#endif

namespace {

HMODULE g_module = nullptr;
char g_dir[MAX_PATH] = {};
FILE* g_log = nullptr;
const StlGuiApi* g_api = nullptr;
int g_handle = 0;
volatile LONG g_fault = 0, g_leak = 0;
std::deque<float> g_energy;
char g_effect_reason[256] = {};
int g_effect_state = -2;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

void PollCommands() {
    static int n = 0;
    if (++n % 30) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sconsumer_" CONSUMER_TAG ".cmd", g_dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "fault", 5)) InterlockedExchange(&g_fault, 1);
        if (!strncmp(line, "leak", 4)) InterlockedExchange(&g_leak, 1);
    }
    fclose(f);
    DeleteFileA(path);
}

void Draw(const StlGuiCallbackCtx* c, void*) {
    if (!StlGuiBindImGui(c)) {  // another ImGui version or configuration than the engine's: draw nothing, say why once
        static bool said = false;
        if (!said) {
            said = true;
            Log("refusing to draw: %s", StlGuiBindFailure());
        }
        return;
    }
    PollCommands();

    if (InterlockedExchange(&g_fault, 0)) {
        Log("raising an access violation inside the draw callback, on purpose");
        volatile int* p = nullptr;
        *p = 1;
    }
    if (InterlockedExchange(&g_leak, 0)) {
        Log("leaving a window, a group, a colour, a style var and a font open, on purpose");
        ImGui::PushStyleColor(ImGuiCol_Text, 0xFF00FFFF);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.9f);
        ImGui::PushFont((ImFont*)c->font_bold);
        ImGui::BeginGroup();
        ImGui::Begin("leaked by consumer A");
        return;
    }

    ImGui::PushFont((ImFont*)c->font_bold);
    ImGui::TextColored(ImVec4(0.4f, 1.f, 0.8f, 1.f), "consumer A: its own ImGui %s, /MT", ImGui::GetVersion());
    ImGui::PopFont();
    ImGui::Text("engine ImGui version number %u, context %p (frame %d)", c->imgui_version_num, (void*)c->imgui_context, ImGui::GetFrameCount());
    ImGui::Separator();

    StlGuiSnapshot s{};
    s.size = sizeof(s);
    if (g_api->get_snapshot(&s)) {
        ImGui::Text("%s  %04u.%02u.%02u  speed %u%s", s.country_name, s.year, s.month, s.day, s.speed, s.paused ? " (paused)" : "");
        for (uint32_t i = 0; i < s.resource_count; ++i) {
            if (strcmp(s.resources[i].key, "energy")) continue;
            static int64_t last_tick = -1;
            if (s.tick != last_tick) {
                last_tick = s.tick;
                g_energy.push_back((float)s.resources[i].stock);
                if (g_energy.size() > 120) g_energy.pop_front();
            }
            ImGui::Text("energy %.1f  (net %+.1f)", s.resources[i].stock, s.resources[i].net);
        }
        if (g_energy.size() > 2) {
            float tmp[120];
            int n = 0;
            for (float v : g_energy) tmp[n++] = v;
            ImGui::PlotLines("##energy", tmp, n, 0, "energy per tick", FLT_MAX, FLT_MAX, ImVec2(0, 50));
        }
    } else {
        ImGui::TextDisabled("not in a game");
    }
    ImGui::Separator();
    if (ImGui::Button("effect state")) g_effect_state = g_api->effect_state("zz_gui_grant_energy", g_effect_reason, sizeof(g_effect_reason));
    ImGui::SameLine();
    if (ImGui::Button("post zz_gui_grant_energy")) g_api->post_effect("zz_gui_grant_energy");
    if (g_effect_state != -2) ImGui::Text("state %d  %s", g_effect_state, g_effect_reason);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("mouse captured by ImGui: %d", (int)io.WantCaptureMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(p.x + 20, p.y + 20), 14.f, IM_COL32(255, 160, 40, 255), 24);
    dl->AddText(ImVec2(p.x + 44, p.y + 12), IM_COL32(255, 255, 255, 255), "drawn with consumer A's own draw list");
    ImGui::Dummy(ImVec2(10, 40));
}

DWORD WINAPI Worker(LPVOID) {
    GetModuleFileNameA(g_module, g_dir, MAX_PATH);
    strcpy(strrchr(g_dir, '\\') + 1, "");
    char lp[MAX_PATH];
    snprintf(lp, sizeof(lp), "%sconsumer_" CONSUMER_TAG ".log", g_dir);
    g_log = fopen(lp, "a");
    Log("loaded; looking for the GUI host");
    char n1[64], n2[64];
    snprintf(n1, sizeof(n1), "Local\\gui_consumer_" CONSUMER_TAG "_unload_%lu", GetCurrentProcessId());
    snprintf(n2, sizeof(n2), "Local\\gui_consumer_" CONSUMER_TAG "_rude_%lu", GetCurrentProcessId());
    HANDLE ev[2] = { CreateEventA(nullptr, TRUE, FALSE, n1), CreateEventA(nullptr, TRUE, FALSE, n2) };
    DWORD waited = WAIT_TIMEOUT;
    for (int i = 0; i < 4800 && !g_api; ++i) {  // up to 20 minutes, every 250 ms
        g_api = stl_gui_try_connect(L"gui_showcase.dll", STL_GUI_API_VERSION);
        if (g_api) break;
        waited = WaitForMultipleObjects(2, ev, FALSE, 250);
        if (waited != WAIT_TIMEOUT) break;
    }
    if (g_api && waited == WAIT_TIMEOUT) {
        StlGuiPanelDesc d{};
        d.size = sizeof(d);
        d.flags = STL_PANEL_WINDOW;
        d.id = "consumer-" CONSUMER_TAG ".demo";
        d.title = "Consumer " CONSUMER_TAG " (own ImGui, /MT)";
        d.draw = Draw;
        g_handle = g_api->register_panel(&d);
        Log("host found (api version %u, built for exe 0x%08X); panel handle %d", g_api->version, g_api->game_exe_timestamp, g_handle);
        waited = WaitForMultipleObjects(2, ev, FALSE, INFINITE);
    }
    if (waited == WAIT_OBJECT_0 && g_api && g_handle) {
        g_api->unregister_panel(g_handle);
        Log("unregistered, leaving");
        Sleep(500);  // a frame that had copied the panel list may still be about to call it
    } else if (waited == WAIT_OBJECT_0 + 1) {
        Log("leaving WITHOUT unregistering");
    }
    if (g_log) fclose(g_log);
    if (waited == WAIT_OBJECT_0 || waited == WAIT_OBJECT_0 + 1) FreeLibraryAndExitThread(g_module, 0);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_module = module;
        HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
