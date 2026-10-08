// Feasibility probe: draw our own Dear ImGui panel inside the engine's own ImGui frame (Stellaris 4.5.2, exe 0x6ABEAA3F).
//
// No Present hook, no window subclass, no own backend: the engine already runs ImGui 1.85 (DX11 + input) when `imgui on` is
// typed into the console. This DLL carries its own copy of ImGui 1.85, points that copy's GImGui at the engine's context, and
// draws between the engine's NewFrame and Render. Everything below that is throwaway probe code: the RVAs are for this one build.
//
// Unload: set the event Local\gui_poc_unload_<pid>.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "MinHook.h"

namespace {

constexpr uint32_t kExeTimestamp = 0x6ABEAA3F;
// RVAs read from the 4.5.2 exe (see the research notes): the engine's ImGui and the game state globals.
constexpr uintptr_t kGImGui = 0x28E2D58;            // ImGuiContext* GImGui
constexpr uintptr_t kNewFrame = 0x1E9D240;          // ImGui::NewFrame (the only user of the string "Debug##Default")
constexpr uintptr_t kAllocFunc = 0x27FD320;         // GImAllocatorAllocFunc
constexpr uintptr_t kFreeFunc = 0x27FD328;          // GImAllocatorFreeFunc
constexpr uintptr_t kAllocUser = 0x28E2D68;         // GImAllocatorUserData
constexpr uintptr_t kGameState = 0x3114700;         // g_CurrentGameState
constexpr uintptr_t kIdler = 0x3114E68;             // g_CurrentInGameIdler
constexpr uintptr_t kSetPaused = 0x935340;          // CInGameIdler::SetPaused(idler, SPauseGameSettings*)
constexpr uintptr_t kSetGameSpeed = 0x934C50;       // CInGameIdler::SetGameSpeed(idler, int)
constexpr ptrdiff_t kDateHours = 0xC0, kSpeed = 0x590, kPaused = 0x594;

uintptr_t g_base = 0;
HMODULE g_module = nullptr;
HANDLE g_unload_event = nullptr;
using FnNewFrame = void (*)();
FnNewFrame g_orig_new_frame = nullptr;
volatile LONG g_in_detour = 0;
volatile LONG g_tick_depth = 0;
volatile LONG64 g_frames_total = 0, g_frames_in_tick = 0, g_ticks = 0;
bool g_allocators_set = false;
bool g_show_demo = false;
FILE* g_log = nullptr;

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

struct PauseSettings {  // SPauseGameSettings (Windows), as the bench DLL calls it
    uint64_t unknown[2]{};
    char who[16]{};
    uint64_t who_size = 0;
    uint64_t who_capacity = 15;
    uint8_t paused = 1;
    uint8_t source = 2;
    uint8_t pad[14]{};
};

bool ReadGame(uint32_t* hours, uint32_t* speed, bool* paused, void** idler_out) {
    __try {
        void* state = *(void**)(g_base + kGameState);
        void* idler = *(void**)(g_base + kIdler);
        if (!state || !idler) return false;
        *hours = *(uint32_t*)((char*)state + kDateHours);
        *speed = *(uint32_t*)((char*)idler + kSpeed);
        *paused = (*(uint32_t*)((char*)idler + kPaused) & 0xFF) != 0;
        *idler_out = idler;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SetPaused(void* idler, bool paused) {
    PauseSettings s;
    s.paused = paused ? 1 : 0;
    __try {
        ((void (*)(void*, const void*))(g_base + kSetPaused))(idler, &s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void SetSpeed(void* idler, int speed) {
    __try {
        ((void (*)(void*, int))(g_base + kSetGameSpeed))(idler, speed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void DrawPanels() {
    ImGuiContext* ctx = *(ImGuiContext**)(g_base + kGImGui);
    if (!ctx) return;
    if (!g_allocators_set) {
        // Memory that this copy of ImGui allocates inside the shared context is freed by the engine's copy (and the reverse): use the
        // engine's allocator pair.
        ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc) * (void**)(g_base + kAllocFunc), (ImGuiMemFreeFunc) * (void**)(g_base + kFreeFunc),
                                     *(void**)(g_base + kAllocUser));
        g_allocators_set = true;
        Log("allocators set; engine ctx %p", (void*)ctx);
    }
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();

    static int first_frame = -1;
    static std::vector<float> dts(120, 0.f);
    static int dt_pos = 0;
    static char text[64] = "type here";
    static float slider = 0.5f;
    static bool check = false;
    if (first_frame < 0) {
        first_frame = ImGui::GetFrameCount();
        Log("first frame %d, display %.0fx%.0f, version %s, sizeof(ImGuiIO)=%zu sizeof(ImDrawIdx)=%zu", first_frame, io.DisplaySize.x,
            io.DisplaySize.y, ImGui::GetVersion(), sizeof(ImGuiIO), sizeof(ImDrawIdx));
    }
    dts[dt_pos++ % dts.size()] = io.DeltaTime * 1000.f;

    ImGui::SetNextWindowPos(ImVec2(520, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("stellaris-gui probe")) {
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "our ImGui %s drawing in the engine's context %p", ImGui::GetVersion(), (void*)ctx);
        ImGui::Text("frame %d (+%d since the probe started)   display %.0fx%.0f   dt %.2f ms", ImGui::GetFrameCount(),
                    ImGui::GetFrameCount() - first_frame, io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime * 1000.f);
        ImGui::Text("WantCaptureMouse=%d  WantCaptureKeyboard=%d  allocations=%d", (int)io.WantCaptureMouse, (int)io.WantCaptureKeyboard,
                    io.MetricsActiveAllocations);
        ImGui::Text("NewFrame calls: %lld total, %lld inside HandleTurnTick (%.0f%%), ticks seen %lld", (long long)g_frames_total,
                    (long long)g_frames_in_tick, g_frames_total ? 100.0 * g_frames_in_tick / g_frames_total : 0.0, (long long)g_ticks);
        ImGui::Separator();

        uint32_t hours = 0, speed = 0;
        bool paused = false;
        void* idler = nullptr;
        if (ReadGame(&hours, &speed, &paused, &idler)) {
            const uint32_t day = hours / 24 - 1825000;  // the engine counts from 5000 years (365 days each) before 1.1.1
            ImGui::Text("game date  %u.%02u.%02u   (hours=%u)", day / 360, 1 + (day % 360) / 30, 1 + day % 30, hours);
            ImGui::Text("speed %u   %s", speed, paused ? "PAUSED" : "running");
            if (ImGui::Button(paused ? "Resume" : "Pause")) SetPaused(idler, !paused);
            for (int s = 1; s <= 5; ++s) {
                ImGui::SameLine();
                char label[8];
                snprintf(label, sizeof(label), "x%d", s);
                if (ImGui::Button(label)) SetSpeed(idler, s);
            }
        } else {
            ImGui::TextDisabled("no game state (not in a game)");
        }
        ImGui::Separator();
        ImGui::InputText("ASCII input", text, sizeof(text));
        ImGui::SliderFloat("slider", &slider, 0.f, 1.f);
        ImGui::Checkbox("check", &check);
        ImGui::Checkbox("show the ImGui demo window (our copy of imgui_demo.cpp)", &g_show_demo);
        ImGui::PlotLines("frame ms", dts.data(), (int)dts.size(), dt_pos % (int)dts.size(), nullptr, 0.f, 50.f, ImVec2(0, 50));
        ImGui::Separator();
        ImGui::Text("CJK test (default font has no glyphs): \xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95");
    }
    ImGui::End();
    if (g_show_demo) ImGui::ShowDemoWindow(&g_show_demo);
}

// ---- turnkey start: a per-frame callback on the main thread (the Present slot of DXGI's shared swap chain vtable, as the bench DLL does)
// that starts the engine's ImGui itself (NImGuiWrapper::ImGuiInit) once a game is running, so nobody has to type `imgui on`.
constexpr uintptr_t kImGuiInit = 0x1B11090;  // NImGuiWrapper::ImGuiInit
constexpr uintptr_t kHandleTurnTick = 0x251800;  // CGameState::HandleTurnTick (4.5.2)
using FnTick = void (*)(void*, void*);
FnTick g_orig_tick = nullptr;
DWORD g_main_tid = 0;
void TickDetour(void* gs, void* cmds) {
    g_main_tid = GetCurrentThreadId();
    InterlockedIncrement(&g_tick_depth);
    g_orig_tick(gs, cmds);
    InterlockedDecrement(&g_tick_depth);
    InterlockedIncrement64(&g_ticks);
}
using FnPresent = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
void** g_present_slot = nullptr;
FnPresent g_orig_present = nullptr;
volatile LONG g_in_present = 0;
bool g_auto_start = true;

void DrawPanels();
void AddCjkFont() {
    ImGuiContext* ctx = *(ImGuiContext**)(g_base + kGImGui);
    if (!ctx) return;
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc) * (void**)(g_base + kAllocFunc), (ImGuiMemFreeFunc) * (void**)(g_base + kFreeFunc),
                                 *(void**)(g_base + kAllocUser));
    g_allocators_set = true;
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    char fonts[MAX_PATH];
    GetWindowsDirectoryA(fonts, MAX_PATH);
    strcat(fonts, "\\Fonts\\msyh.ttc");
    io.Fonts->AddFontDefault();
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.OversampleH = 1;
    ImFont* f = io.Fonts->AddFontFromFileTTF(fonts, 15.0f, &cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    Log("merged CJK font %s -> %p, atlas fonts=%d", fonts, (void*)f, io.Fonts->Fonts.Size);
}

void AutoStart() {
    static int frames = 0;
    static bool done = false;
    if (done || !g_auto_start || ++frames < 600) return;  // 10 s at 60 fps: let the game settle
    uint32_t h, s;
    bool p;
    void* idler;
    if (!ReadGame(&h, &s, &p, &idler)) return;  // not in a game yet
    done = true;
    __try {
        if (*(void**)(g_base + kGImGui)) {
            Log("engine ImGui already running (%p)", *(void**)(g_base + kGImGui));
            return;
        }
        Log("calling the engine's ImGuiInit from the Present hook (main thread)");
        ((void (*)())(g_base + kImGuiInit))();
        Log("ImGuiInit returned, context %p", *(void**)(g_base + kGImGui));
        AddCjkFont();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("exception in ImGuiInit");
    }
}

HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* sc, UINT sync, UINT flags) {
    InterlockedIncrement(&g_in_present);
    AutoStart();
    const HRESULT hr = g_orig_present(sc, sync, flags);
    InterlockedDecrement(&g_in_present);
    return hr;
}

void** FindPresentSlot() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"gui_poc_dummy";
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
    if (!wnd) return nullptr;
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &swap_chain,
                                               &device, nullptr, &context);
    void** slot = (SUCCEEDED(hr) && swap_chain) ? &(*(void***)swap_chain)[8] : nullptr;
    if (swap_chain) swap_chain->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return slot;
}

bool WriteSlot(void** slot, void* value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, value);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

void NewFrameDetour() {
    InterlockedIncrement(&g_in_detour);
    InterlockedIncrement64(&g_frames_total);
    if (g_tick_depth > 0) InterlockedIncrement64(&g_frames_in_tick);
    g_orig_new_frame();  // the engine's frame is now open; the engine's own views are updated after this returns
    __try {
        DrawPanels();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) Log("exception in DrawPanels");
        logged = true;
    }
    InterlockedDecrement(&g_in_detour);
}

DWORD WINAPI Worker(LPVOID) {
    char dir[MAX_PATH];
    GetModuleFileNameA(g_module, dir, MAX_PATH);
    strcpy(strrchr(dir, '\\') + 1, "gui_poc.log");
    g_log = fopen(dir, "a");
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(g_base + ((const IMAGE_DOS_HEADER*)g_base)->e_lfanew);
    Log("loaded, base 0x%llX, exe timestamp 0x%08X", (unsigned long long)g_base, nt->FileHeader.TimeDateStamp);
    if (nt->FileHeader.TimeDateStamp != kExeTimestamp) {
        Log("wrong game build, not hooking");
    } else if (MH_Initialize() == MH_OK && MH_CreateHook((void*)(g_base + kNewFrame), (void*)&NewFrameDetour, (void**)&g_orig_new_frame) == MH_OK &&
               MH_EnableHook((void*)(g_base + kNewFrame)) == MH_OK) {
        Log("hooked ImGui::NewFrame at 0x%llX", (unsigned long long)(g_base + kNewFrame));
        if (MH_CreateHook((void*)(g_base + kHandleTurnTick), (void*)&TickDetour, (void**)&g_orig_tick) == MH_OK &&
            MH_EnableHook((void*)(g_base + kHandleTurnTick)) == MH_OK) {
            Log("hooked HandleTurnTick");
        }
    } else {
        Log("hook failed");
    }
    if (nt->FileHeader.TimeDateStamp == kExeTimestamp) {
        g_present_slot = FindPresentSlot();
        if (g_present_slot) {
            g_orig_present = (FnPresent)*g_present_slot;  // the original, read before our hook is written
            if (!WriteSlot(g_present_slot, (void*)&PresentHook)) g_orig_present = nullptr;
        }
        Log("present slot %p", (void*)g_present_slot);
    }
    WaitForSingleObject(g_unload_event, INFINITE);
    MH_DisableHook(MH_ALL_HOOKS);
    if (g_present_slot && g_orig_present) WriteSlot(g_present_slot, (void*)g_orig_present);
    for (int i = 0; i < 300 && (g_in_detour || g_in_present); ++i) Sleep(10);
    Sleep(300);
    MH_Uninitialize();
    Log("unloading");
    if (g_log) fclose(g_log);
    CloseHandle(g_unload_event);
    FreeLibraryAndExitThread(g_module, 0);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_module = module;
        char name[64];
        wsprintfA(name, "Local\\gui_poc_unload_%lu", GetCurrentProcessId());
        g_unload_event = CreateEventA(nullptr, TRUE, FALSE, name);
        if (!g_unload_event) return FALSE;
        HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
