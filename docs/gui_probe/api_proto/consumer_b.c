/* Prototype consumer B: plain C, no Dear ImGui anywhere in the plugin (built with /MT). Everything it draws goes through StlGuiUi.
 * Unload events: Local\gui_consumer_b_unload_<pid> (unregisters first) and Local\gui_consumer_b_rude_<pid> (leaves without unregistering). */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "stellaris_gui_api.h"
#include "stellaris_gui_client.h"

static HMODULE g_module;
static char g_dir[MAX_PATH];
static FILE* g_log;
static const struct StlGuiApi* g_api;
static int g_handle;

static void logf_(const char* msg) {
    SYSTEMTIME t;
    if (!g_log) return;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d] %s\n", t.wHour, t.wMinute, t.wSecond, msg);
    fflush(g_log);
}

static void draw(const StlGuiCallbackCtx* c, void* user) {
    static int check = 1;
    static float slider = 0.5f;
    StlGuiSnapshot s;
    char line[160];
    const StlGuiUi* ui = c->ui;
    float pos[2], avail[2];
    (void)user;
    ui->text_colored(0xFF80FF80, "consumer B: plain C, no ImGui in this plugin");
    sprintf(line, "ImGui of the engine: version number %u", c->imgui_version_num);
    ui->text(line);
    ui->separator();
    memset(&s, 0, sizeof(s));
    s.size = sizeof(s);
    if (c->api->get_snapshot(&s)) {
        sprintf(line, "%s  %04u.%02u.%02u  speed %u%s", s.country_name, s.year, s.month, s.day, s.speed, s.paused ? " (paused)" : "");
        ui->text(line);
        if (s.resource_count > 0) {
            float frac = s.resources[0].max > 0 ? (float)(s.resources[0].stock / s.resources[0].max) : 0.f;
            sprintf(line, "%s %.0f", s.resources[0].key, s.resources[0].stock);
            ui->progress_bar(frac, -1.f, 14.f, line);
        }
    } else {
        ui->text("not in a game");
    }
    ui->separator();
    if (ui->button("pause / resume")) c->api->set_paused(!s.paused);
    ui->same_line();
    if (ui->button("speed 1")) c->api->set_speed(1);
    ui->same_line();
    if (ui->button("speed 5")) c->api->set_speed(5);
    ui->tooltip("goes through the engine's own setters, between ticks");
    ui->checkbox("a checkbox", &check);
    ui->slider_float("a slider", &slider, 0.f, 1.f);
    ui->get_cursor_screen_pos(pos);
    ui->get_content_region_avail(avail);
    ui->dummy(10.f, 36.f);
    ui->draw_rect_filled(pos[0], pos[1], pos[0] + avail[0] * slider, pos[1] + 30.f, 0xFFD08030, 6.f);
    ui->draw_line(pos[0], pos[1] + 34.f, pos[0] + avail[0], pos[1] + 34.f, 0xFFFFFFFF, 2.f);
    ui->draw_circle_filled(pos[0] + 14.f, pos[1] + 15.f, 9.f, 0xFF40E0FF);
    ui->draw_text(pos[0] + 32.f, pos[1] + 7.f, 0xFFFFFFFF, "drawn through StlGuiUi");
}

static DWORD WINAPI worker(LPVOID arg) {
    char lp[MAX_PATH], n1[64], n2[64];
    HANDLE ev[2];
    DWORD waited = WAIT_TIMEOUT;
    int i;
    (void)arg;
    GetModuleFileNameA(g_module, g_dir, MAX_PATH);
    strrchr(g_dir, '\\')[1] = 0;
    sprintf(lp, "%sconsumer_b.log", g_dir);
    g_log = fopen(lp, "a");
    logf_("loaded; looking for the GUI host");
    sprintf(n1, "Local\\gui_consumer_b_unload_%lu", GetCurrentProcessId());
    sprintf(n2, "Local\\gui_consumer_b_rude_%lu", GetCurrentProcessId());
    ev[0] = CreateEventA(NULL, TRUE, FALSE, n1);
    ev[1] = CreateEventA(NULL, TRUE, FALSE, n2);
    for (i = 0; i < 4800 && !g_api; ++i) {
        g_api = stl_gui_try_connect(L"gui_showcase.dll", STL_GUI_API_VERSION);
        if (g_api) break;
        waited = WaitForMultipleObjects(2, ev, FALSE, 250);
        if (waited != WAIT_TIMEOUT) break;
    }
    if (g_api && waited == WAIT_TIMEOUT) {
        StlGuiPanelDesc d;
        char msg[160];
        memset(&d, 0, sizeof(d));
        d.size = sizeof(d);
        d.flags = STL_PANEL_WINDOW;
        d.id = "consumer-b.demo";
        d.title = "Consumer B (C only, /MT)";
        d.draw = draw;
        g_handle = g_api->register_panel(&d);
        sprintf(msg, "host found (api version %u); panel handle %d", g_api->version, g_handle);
        logf_(msg);
        waited = WaitForMultipleObjects(2, ev, FALSE, INFINITE);
    }
    if (waited == WAIT_OBJECT_0 && g_api && g_handle) {
        g_api->unregister_panel(g_handle);
        logf_("unregistered, leaving");
        Sleep(500);
    } else if (waited == WAIT_OBJECT_0 + 1) {
        logf_("leaving WITHOUT unregistering");
    }
    if (g_log) fclose(g_log);
    if (waited == WAIT_OBJECT_0 || waited == WAIT_OBJECT_0 + 1) FreeLibraryAndExitThread(g_module, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE t;
        DisableThreadLibraryCalls(module);
        g_module = module;
        t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
