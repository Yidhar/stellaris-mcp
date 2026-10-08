// Showcase: a custom in-game UI for Stellaris 4.5.2 that shares nothing with the vanilla look, drawn with Dear ImGui inside the engine's own
// ImGui frame (see docs/gui_imgui_feasibility.md, architecture A).
//
//  * Data: a snapshot of the player's country is taken only while no turn tick is running (the UI also runs inside HandleTurnTick), so the
//    drawing code never reads state a worker may be mutating.
//  * Writes: game speed/pause go through the engine's own setters; script effects go through CExecuteButtonEffectCommand, built with the
//    engine factory, checked with the command's own IsValid and posted with PostCommandToSession (the same path a GUI effectButtonType uses).
//  * Control from outside: write lines such as "deck 1", "tab 3", "post zz_gui_grant_energy", "dump" into gui_showcase.cmd next to the DLL.
//
// Unload: set the event Local\gui_showcase_unload_<pid>.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // sizeof(ImGuiContext) and member offsets, for the layout guard below
#include "MinHook.h"
#include "sdk/stellaris_sdk.hpp"
#include "ui_glyphs.inc"

#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif

// The engine's Dear ImGui is compiled into stellaris.exe and this DLL carries its own copy, drawing into the engine's context. The SDK
// (tools/sdk_dumper, anchors.py) reads the engine's layout constants out of its code; if the engine's ImGui ever differs from the 1.85
// this copy is built from, the build stops here instead of corrupting the shared context at run time.
static_assert(sizeof(ImGuiContext) == sdk::rt::ImGuiContext_sizeof, "the engine's ImGuiContext differs from the ImGui this DLL is built with");
static_assert(offsetof(ImGuiContext, IO) + offsetof(ImGuiIO, MetricsActiveAllocations) == sdk::rt::ImGuiContext_io_MetricsActiveAllocations,
              "ImGuiContext::IO layout differs from the engine's");
static_assert(offsetof(ImGuiIO, ImeWindowHandle) == sdk::rt::ImGuiIO_ImeWindowHandle, "ImGuiIO::ImeWindowHandle differs from the engine's");
static_assert(offsetof(ImGuiIO, BackendPlatformUserData) == sdk::rt::ImGuiIO_BackendPlatformUserData,
              "ImGuiIO::BackendPlatformUserData differs from the engine's");

namespace {

// ---- the engine's ImGui, located by tools/sdk_dumper (functions.py / anchors.py) for the exe this SDK was dumped from
constexpr uintptr_t kGImGui = sdk::glob::GImGui;                        // ImGuiContext* GImGui
constexpr uintptr_t kNewFrame = sdk::fn::ImGui_NewFrame;                // ImGui::NewFrame
constexpr uintptr_t kAllocFunc = sdk::glob::GImAllocatorAllocFunc;      // GImAllocatorAllocFunc
constexpr uintptr_t kFreeFunc = sdk::glob::GImAllocatorFreeFunc;        // GImAllocatorFreeFunc
constexpr uintptr_t kAllocUser = sdk::glob::GImAllocatorUserData;       // GImAllocatorUserData
constexpr uintptr_t kImGuiInit = sdk::fn::NImGuiWrapper_ImGuiInit;      // NImGuiWrapper::ImGuiInit

// ---------------------------------------------------------------------------------------------------------------- plumbing
uintptr_t g_base = 0;
HMODULE g_module = nullptr;
HANDLE g_unload_event = nullptr;
FILE* g_log = nullptr;
char g_dir[MAX_PATH] = {};

using FnVoid = void (*)();
using FnTick = void (*)(void*, void*);
FnVoid g_orig_new_frame = nullptr;
FnVoid g_orig_imgui_init = nullptr;
FnTick g_orig_tick = nullptr;
volatile LONG g_in_detour = 0, g_tick_depth = 0;
volatile LONG64 g_frames_total = 0, g_frames_in_tick = 0, g_ticks = 0;
bool g_allocators_set = false;

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

// Fault-tolerant reads of engine memory: a bad pointer must never take the game down.
bool Rd(const void* p, void* out, size_t n) {
    __try {
        memcpy(out, p, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <typename T>
T RdOr(uintptr_t addr, T fallback) {
    T v;
    return Rd((const void*)addr, &v, sizeof(T)) ? v : fallback;
}

struct RawStr {  // MSVC std::string as the engine lays it out
    union {
        char buf[16];
        char* ptr;
    };
    uint64_t size;
    uint64_t cap;
};
struct RawCStr {  // engine CString: 16-byte header + std::string
    uint64_t header[2];
    RawStr s;
};
static_assert(sizeof(RawCStr) == 0x30, "CString is 0x30 bytes");

std::string RdStr(uintptr_t addr) {
    RawStr raw{};
    if (!Rd((const void*)addr, &raw, sizeof(raw)) || raw.size == 0 || raw.size > 255) return {};
    const char* src = raw.cap > 15 ? raw.ptr : (const char*)addr;
    char tmp[256];
    if (!Rd(src, tmp, (size_t)raw.size)) return {};
    return std::string(tmp, (size_t)raw.size);
}

// Renders the engine's rich text as plain text: 0x13 wraps an icon name (the pass/fail marks become characters, other icons are dropped),
// any other control byte starts a colour code whose one-letter key ('Y', 'R' ... or '!') is dropped with it.
std::string StripMarkup(const std::string& in) {
    std::string out;
    const size_t n = in.size();
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)in[i];
        if (c == 0x13) {
            std::string icon;
            size_t j = i + 1;
            for (; j < n && (unsigned char)in[j] != 0x13 && in[j] != ' ' && (unsigned char)in[j] >= 0x20; ++j)
                if (in[j] == '|') break;
                else icon += in[j];
            if (icon == "trigger_no") out += "\xC3\x97";   // multiplication sign
            else if (icon == "trigger_yes") out += "\xE2\x88\x9A";  // square root sign, the closest tick the fonts have
            while (j < n && (unsigned char)in[j] != 0x13 && in[j] != ' ' && (unsigned char)in[j] >= 0x20) ++j;
            i = (j < n && (unsigned char)in[j] == 0x13) ? j : j - 1;
        } else if (c >= 0x20 || c == '\n') {
            out += (char)c;
        } else if (i + 1 < n) {
            const unsigned char k = (unsigned char)in[i + 1];
            if (k == '!' || (k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z')) ++i;
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    for (char& ch : out)
        if (ch == '\n') ch = ' ';
    return out;
}

// ---- guarded engine calls
void* CallGetPlayerCountry() {
    __try {
        return ((void* (*)())(g_base + sdk::fn::GetPlayerCountry))();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
bool CallBuildString(const void* persistent_name, RawCStr* out) {
    __try {
        ((void* (*)(const void*, void*))(g_base + sdk::fn::CPersistentName_BuildString))(persistent_name, out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void CallFreeCString(RawCStr* s) {
    __try {
        ((void (*)(void*))(g_base + sdk::fn::CString_Free))(s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
std::string TakeCString(RawCStr& t) {
    std::string out;
    if (t.s.size > 0 && t.s.size < 4096) {
        const char* p = t.s.cap > 15 ? t.s.ptr : t.s.buf;
        char tmp[4100];
        if (p && Rd(p, tmp, (size_t)t.s.size)) out.assign(tmp, (size_t)t.s.size);
    }
    if (t.s.cap > 15) CallFreeCString(&t);
    return StripMarkup(out);
}
bool CallResourceMax(void* res, void* country, int64_t* out) {
    __try {
        ((void* (*)(const void*, int64_t*, const void*))(g_base + sdk::fn::CStrategicResource_GetMaximumForCountry))(res, out, country);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
struct PauseSettings {  // SPauseGameSettings, as the bench DLL calls it
    uint64_t unknown[2]{};
    char who[16]{};
    uint64_t who_size = 0;
    uint64_t who_capacity = 15;
    uint8_t paused = 1;
    uint8_t source = 2;
    uint8_t pad[14]{};
};
void CallSetPaused(void* idler, bool paused) {
    PauseSettings s;
    s.paused = paused ? 1 : 0;
    __try {
        ((void (*)(void*, const void*))(g_base + sdk::fn::CInGameIdler_SetPaused))(idler, &s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
void CallSetSpeed(void* idler, int speed) {
    __try {
        ((void (*)(void*, int))(g_base + sdk::fn::CInGameIdler_SetGameSpeed))(idler, speed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ------------------------------------------------------------------------------------------------------------- game snapshot
struct ResInfo {
    std::string key;
    double stock = 0, income = 0, expense = 0, net = 0, max = -1;
};
struct Snapshot {
    bool in_game = false;
    uint32_t hours = 0, speed = 0;
    bool paused = false;
    void* idler = nullptr;
    uint32_t year = 0, month = 0, day = 0, day_index = 0;
    void* country = nullptr;
    uint32_t country_id = 0xFFFFFFFF;
    std::string name;
    std::vector<ResInfo> res;
    uint32_t colonies = 0, pops = 0;
    int32_t empire_size = 0;
    double mil = 0, tech = 0, eco = 0;
    double mil_max = 1, tech_max = 1, eco_max = 1, col_max = 1, pop_max = 1;
    int64_t tick = 0, serial = 0;
};
Snapshot g_snap;
int64_t g_snap_serial = 0;
LONG64 g_force_snapshot_frame = 0;
std::vector<std::string> g_res_names;
std::vector<void*> g_res_ptrs;
struct Hist {
    std::deque<float> stock, net;
};
std::vector<Hist> g_hist;
uint32_t g_last_day_index = 0xFFFFFFFF;
constexpr size_t kHistLen = 160;

void LoadResourceNames() {
    if (!g_res_names.empty()) return;
    uintptr_t db = RdOr<uintptr_t>(g_base + sdk::glob::CStrategicResourceDatabase_pInstance, 0);
    if (db < 0x10000) return;
    uint32_t count = RdOr<uint32_t>(db + 0x14, 0);
    uintptr_t arr = RdOr<uintptr_t>(db + 0x08, 0);
    if (!count || count > 256 || !arr) return;
    std::vector<std::string> names;
    std::vector<void*> ptrs;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t r = RdOr<uintptr_t>(arr + i * 8, 0);
        ptrs.push_back((void*)r);
        names.push_back(r ? RdStr(r + 0x30) : "");
    }
    g_res_names = std::move(names);
    g_res_ptrs = std::move(ptrs);
    g_hist.assign(g_res_names.size(), Hist{});
    Log("%zu resources: %s ...", g_res_names.size(), g_res_names.size() > 3 ? (g_res_names[0] + "," + g_res_names[1] + "," + g_res_names[2]).c_str() : "");
}

// military_power & co. are "ptr:fixed_point" in the SDK: follow the pointer when it is one, else take the value itself.
double ReadPower(uintptr_t country, std::ptrdiff_t off) {
    uintptr_t p = RdOr<uintptr_t>(country + off, 0);
    int64_t v = 0;
    if (p > 0x10000 && Rd((const void*)p, &v, sizeof(v))) return v / 100000.0;
    return (double)(int64_t)p / 100000.0;
}

template <typename F>
void ForEachCountry(F&& f) {
    uintptr_t db = RdOr<uintptr_t>(g_base + sdk::db::CCountry, 0);
    uintptr_t arr = db ? RdOr<uintptr_t>(db + 0x18, 0) : 0;
    uint32_t cap = db ? RdOr<uint32_t>(db + 0x20, 0) : 0;
    if (!arr || cap > 100000) return;
    for (uint32_t i = 0; i < cap; ++i) {
        uintptr_t obj = RdOr<uintptr_t>(arr + (uintptr_t)i * 16 + 8, 0);
        if (obj > 0x10000) f(obj);
    }
}

void TakeSnapshot() {
    Snapshot s;
    s.tick = g_ticks;
    s.serial = ++g_snap_serial;
    uintptr_t state = RdOr<uintptr_t>(g_base + sdk::glob::g_CurrentGameState, 0);
    uintptr_t idler = RdOr<uintptr_t>(g_base + sdk::glob::g_CurrentInGameIdler, 0);
    if (state < 0x10000 || idler < 0x10000) {
        g_snap = Snapshot{};
        return;
    }
    s.hours = RdOr<uint32_t>(state + sdk::rt::CGameState_date_hours, 0);
    if (s.hours < 24u * 1825000u) {
        g_snap = Snapshot{};
        return;
    }
    s.speed = RdOr<uint32_t>(idler + sdk::rt::CInGameIdler_speed, 0);
    s.paused = (RdOr<uint32_t>(idler + sdk::rt::CInGameIdler_paused, 0) & 0xFF) != 0;
    s.idler = (void*)idler;
    s.day_index = s.hours / 24 - 1825000;
    s.year = s.day_index / 360;
    s.month = 1 + (s.day_index % 360) / 30;
    s.day = 1 + s.day_index % 30;

    void* country = CallGetPlayerCountry();
    uint32_t cid = country ? RdOr<uint32_t>((uintptr_t)country + sdk::rt::CCountry_id, 0xFFFFFFFF) : 0xFFFFFFFF;
    if (!country || cid == 0xFFFFFFFF) {
        s.in_game = false;
        g_snap = s;
        return;
    }
    s.in_game = true;
    s.country = country;
    s.country_id = cid;
    const uintptr_t c = (uintptr_t)country;

    static int name_refresh = 0;
    if (g_snap.name.empty() || g_snap.country_id != cid || ++name_refresh > 120) {
        name_refresh = 0;
        RawCStr out{};
        out.s.cap = 15;
        if (CallBuildString((const void*)(c + sdk::ent::CCountry::name), &out)) s.name = TakeCString(out);
    } else {
        s.name = g_snap.name;
    }

    LoadResourceNames();
    uintptr_t bal = RdOr<uintptr_t>(c + sdk::ent::CCountry::type + 0x18, 0);
    uintptr_t stock_arr = bal ? RdOr<uintptr_t>(bal + 0x30, 0) : 0;
    uintptr_t inc_arr = RdOr<uintptr_t>(c + sdk::ent::CCountry::budget + 0x2D8, 0);
    uintptr_t exp_arr = RdOr<uintptr_t>(c + sdk::ent::CCountry::budget + 0x2F8, 0);
    uintptr_t net_arr = RdOr<uintptr_t>(c + sdk::ent::CCountry::budget + 0x318, 0);
    for (size_t i = 0; i < g_res_names.size(); ++i) {
        if (g_res_names[i].empty()) continue;
        ResInfo r;
        r.key = g_res_names[i];
        if (stock_arr) r.stock = RdOr<int64_t>(stock_arr + i * 8, 0) / 100000.0;
        if (inc_arr) r.income = RdOr<int64_t>(inc_arr + i * 8, 0) / 100000.0;
        if (exp_arr) r.expense = RdOr<int64_t>(exp_arr + i * 8, 0) / 100000.0;
        if (net_arr) r.net = RdOr<int64_t>(net_arr + i * 8, 0) / 100000.0;
        int64_t base_max = RdOr<int64_t>((uintptr_t)g_res_ptrs[i] + 0x110, -1);
        int64_t mx = 0;
        if (base_max >= 0 && CallResourceMax(g_res_ptrs[i], country, &mx)) r.max = mx / 100000.0;
        s.res.push_back(std::move(r));
    }

    s.colonies = RdOr<uint32_t>(c + sdk::ent::CCountry::owned_planets + 0x14, 0);
    s.pops = (uint32_t)std::max(0, RdOr<int32_t>(c + sdk::ent::CCountry::num_sapient_pops, 0));
    s.empire_size = RdOr<int32_t>(c + sdk::ent::CCountry::empire_size, 0);
    s.mil = ReadPower(c, sdk::ent::CCountry::military_power);
    s.tech = ReadPower(c, sdk::ent::CCountry::tech_power);
    s.eco = ReadPower(c, sdk::ent::CCountry::economy_power);

    // The best empire in the galaxy per axis, so the radar can show where the player stands. Refreshed once per game day.
    s.mil_max = g_snap.mil_max;
    s.tech_max = g_snap.tech_max;
    s.eco_max = g_snap.eco_max;
    s.col_max = g_snap.col_max;
    s.pop_max = g_snap.pop_max;
    if (s.day_index != g_last_day_index) {
        double mm = 1, tm = 1, em = 1, cm = 1, pm = 1;
        ForEachCountry([&](uintptr_t o) {
            if (RdOr<uint32_t>(o + sdk::rt::CCountry_id, 0xFFFFFFFF) == 0xFFFFFFFF) return;
            mm = std::max(mm, ReadPower(o, sdk::ent::CCountry::military_power));
            tm = std::max(tm, ReadPower(o, sdk::ent::CCountry::tech_power));
            em = std::max(em, ReadPower(o, sdk::ent::CCountry::economy_power));
            cm = std::max(cm, (double)RdOr<uint32_t>(o + sdk::ent::CCountry::owned_planets + 0x14, 0));
            pm = std::max(pm, (double)std::max(0, RdOr<int32_t>(o + sdk::ent::CCountry::num_sapient_pops, 0)));
        });
        s.mil_max = std::max(mm, s.mil);
        s.tech_max = std::max(tm, s.tech);
        s.eco_max = std::max(em, s.eco);
        s.col_max = std::max(cm, (double)s.colonies);
        s.pop_max = std::max(pm, (double)s.pops);
        // one history sample per game day
        for (size_t i = 0; i < s.res.size() && i < g_hist.size(); ++i) {
            size_t slot = 0;
            for (; slot < g_res_names.size(); ++slot)
                if (g_res_names[slot] == s.res[i].key) break;
            if (slot >= g_hist.size()) continue;
            g_hist[slot].stock.push_back((float)s.res[i].stock);
            g_hist[slot].net.push_back((float)s.res[i].net);
            if (g_hist[slot].stock.size() > kHistLen) g_hist[slot].stock.pop_front();
            if (g_hist[slot].net.size() > kHistLen) g_hist[slot].net.pop_front();
        }
        g_last_day_index = s.day_index;
    }
    g_snap = std::move(s);
}

const ResInfo* FindRes(const char* key) {
    for (const auto& r : g_snap.res)
        if (r.key == key) return &r;
    return nullptr;
}
const Hist* HistOf(const std::string& key) {
    for (size_t i = 0; i < g_res_names.size() && i < g_hist.size(); ++i)
        if (g_res_names[i] == key) return &g_hist[i];
    return nullptr;
}

// ------------------------------------------------------------------------------------ script channel (CExecuteButtonEffectCommand)
struct ButtonDef {
    const char* key;
    const char* title;
    const char* desc;
};
const ButtonDef kButtons[] = {
    { "zz_gui_grant_energy", "注入能量", "向国库增加 100 能量币。allow 恒为真，用来验证写入通道本身。" },
    { "zz_gui_set_mark", "写入国家旗标", "set_country_flag zz_gui_marked。已写入时引擎判定为不可用。" },
    { "zz_gui_clear_mark", "清除国家旗标", "remove_country_flag。只有旗标存在时才可用，与上一个互为开关。" },
    { "zz_gui_rich_only", "富豪特权", "需要 100 万合金才通过 allow：展示引擎给出的拒绝原因。" },
};
struct ButtonState {
    bool found = false, valid = false;
    std::string reason;
};
ButtonState g_button_state[4];
double g_button_eval_time = -10;

void* FindButtonEffect(const char* key) {
    static std::unordered_map<std::string, std::pair<uintptr_t, void*>> cache;  // key -> {db, entry}
    uintptr_t db = RdOr<uintptr_t>(g_base + sdk::glob::TGameDatabase_CButtonEffectDatabase_pInstance, 0);
    if (db < 0x10000) return nullptr;
    auto it = cache.find(key);
    if (it != cache.end() && it->second.first == db) return it->second.second;
    uintptr_t arr = RdOr<uintptr_t>(db + 0x50, 0);
    int32_t n = RdOr<int32_t>(db + 0x5C, 0);
    if (!arr || n <= 0 || n > 20000) return nullptr;
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t p = RdOr<uintptr_t>(arr + (uintptr_t)i * 8, 0);
        if (p > 0x10000 && RdStr(p + 0x20) == key) {
            cache[key] = { db, (void*)p };
            return (void*)p;
        }
    }
    return nullptr;
}

bool CallFactory(uintptr_t fn, void** out) {
    __try {
        *out = ((void* (*)())fn)();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallIsValidSlot(void* cmd, RawCStr* reason, bool* ok) {
    __try {
        auto fn = *(bool (**)(void*, void*))(*(uintptr_t*)cmd + 8 * sizeof(void*));  // CCommand::IsValid(CString*)
        *ok = fn(cmd, reason);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void CallDestroyCommand(void* cmd) {
    __try {
        (*(void* (**)(void*, unsigned))(*(uintptr_t*)cmd))(cmd, 1);  // scalar deleting destructor
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
bool CallPostCommand(void* cmd) {
    __try {
        ((void (*)(void*, bool))(g_base + sdk::fn::PostCommand))(cmd, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool TokenMatches(uintptr_t vtable, uint32_t token) {
    uint8_t code[6];
    uintptr_t fn = RdOr<uintptr_t>(vtable + 10 * sizeof(void*), 0);
    if (!fn || !Rd((const void*)fn, code, sizeof(code)) || code[0] != 0xB8 || code[5] != 0xC3) return false;
    return *(uint32_t*)(code + 1) == token;
}

// Builds the command for `key` with the player country as This/From/Root, asks the engine whether it is valid and, if `post`, queues it.
// Returns false when the command could not be built or posted; `valid` / `reason` carry the engine's verdict.
bool RunButtonEffect(const char* key, bool post, bool* valid, std::string* reason) {
    namespace spec = sdk::cmd::execute_button_effect;
    *valid = false;
    if (!g_snap.in_game) {
        *reason = "不在游戏中";
        return false;
    }
    void* effect = FindButtonEffect(key);
    if (!effect) {
        *reason = "未找到 button_effect（测试 mod 没有启用？）";
        return false;
    }
    if (!TokenMatches(g_base + spec::kVtableRva, spec::kToken)) {
        *reason = "命令 vtable 与 SDK 不符";
        return false;
    }
    void* cmd = nullptr;
    if (!CallFactory(g_base + spec::kFactoryRva, &cmd) || !cmd || *(uintptr_t*)cmd != g_base + spec::kVtableRva) {
        *reason = "命令工厂失败";
        return false;
    }
    const uintptr_t c = (uintptr_t)cmd;
    const uintptr_t scope = c + spec::scope;
    // The engine's default CEventScope points root/from/prev at itself; if that is not what we see the layout is not what this code expects.
    if (RdOr<uintptr_t>(scope + 0x30, 0) != scope || RdOr<uintptr_t>(scope + 0x38, 0) != scope) {
        CallDestroyCommand(cmd);
        *reason = "CEventScope 布局不符";
        return false;
    }
    // CScopeObjectReference::SetCountry: type 4, id, two zeroed words.
    *(uint64_t*)(scope + 0x08) = 4;
    *(uint32_t*)(scope + 0x10) = g_snap.country_id;
    *(uint64_t*)(scope + 0x14) = 0;
    *(uint64_t*)(scope + 0x1C) = 0;
    *(void**)(c + spec::effect) = effect;

    RawCStr why{};
    why.s.cap = 15;
    bool ok = false;
    if (!CallIsValidSlot(cmd, &why, &ok)) {
        CallDestroyCommand(cmd);
        *reason = "IsValid 触发异常";
        return false;
    }
    std::string text = TakeCString(why);
    *valid = ok;
    if (!ok) {
        *reason = text.empty() ? "引擎未给出原因（potential 不成立）" : text;
        CallDestroyCommand(cmd);
        return false;
    }
    if (!post) {
        CallDestroyCommand(cmd);
        return true;
    }
    if (!CallPostCommand(cmd)) {
        *reason = "PostCommand 触发异常";
        return false;  // the engine may own it by now: do not free
    }
    *reason = "已入队";
    return true;
}

struct ScriptLogEntry {
    std::string key, title, result;
    double time = 0;
    int64_t tick_posted = 0, serial_posted = 0;
    bool done = false, ok = false;
    double energy_before = 0, energy_after = 0;
    bool energy_known = false;
};
std::deque<ScriptLogEntry> g_script_log;

// ---------------------------------------------------------------------------------------------------- deferred actions + UI state
struct Pending {
    enum Kind { Speed, Pause, Button } kind;
    int ival = 0;
    std::string key;
};
std::vector<Pending> g_pending;
bool g_deck_open = true, g_hud_open = true, g_stars = true;
int g_tab = 0, g_theme = 0, g_sel_res = 0;
double g_T = 0;
float g_DT = 0.016f;
float g_S0 = 1.f, g_S = 1.f, g_fit = 1.f;
ImFont *g_font_body = nullptr, *g_font_bold = nullptr, *g_font_title = nullptr, *g_font_num = nullptr, *g_font_num_s = nullptr;
ImGuiContext* g_fonts_ctx = nullptr;
std::deque<float> g_frame_ms, g_tick_rate;
std::unordered_map<std::string, float> g_anim;

float Smooth(const char* key, float target, float rate = 12.f) {
    auto it = g_anim.try_emplace(key, target).first;
    it->second += (target - it->second) * (1.f - expf(-rate * g_DT));
    return it->second;
}
void Push(std::deque<float>& d, float v, size_t cap) {
    d.push_back(v);
    while (d.size() > cap) d.pop_front();
}

void RunPending() {
    for (const Pending& p : g_pending) {
        if (!g_snap.in_game || !g_snap.idler) break;
        switch (p.kind) {
        case Pending::Speed:
            CallSetSpeed(g_snap.idler, p.ival);
            Log("speed -> %d", p.ival);
            break;
        case Pending::Pause:
            CallSetPaused(g_snap.idler, p.ival != 0);
            Log("pause -> %d", p.ival);
            break;
        case Pending::Button: {
            ScriptLogEntry e;
            e.key = p.key;
            e.title = p.key;
            for (const auto& b : kButtons)
                if (p.key == b.key) e.title = b.title;
            e.time = g_T;
            e.tick_posted = g_ticks;
            e.serial_posted = g_snap_serial;
            g_force_snapshot_frame = g_frames_total + 8;
            if (const ResInfo* en = FindRes("energy")) {
                e.energy_before = en->stock;
                e.energy_known = true;
            }
            bool valid = false;
            std::string why;
            bool ok = RunButtonEffect(p.key.c_str(), true, &valid, &why);
            e.ok = ok;
            e.result = why;
            e.done = !ok;  // a posted command is finished once a tick has run
            Log("button %s: posted=%d valid=%d '%s'", p.key.c_str(), (int)ok, (int)valid, why.c_str());
            g_script_log.push_front(std::move(e));
            while (g_script_log.size() > 12) g_script_log.pop_back();
            g_button_eval_time = -10;  // re-evaluate soon
            break;
        }
        }
    }
    g_pending.clear();
}

void EvalButtons() {
    for (size_t i = 0; i < 4; ++i) {
        ButtonState& st = g_button_state[i];
        st.found = FindButtonEffect(kButtons[i].key) != nullptr;
        bool valid = false;
        std::string why;
        RunButtonEffect(kButtons[i].key, false, &valid, &why);
        st.valid = valid;
        st.reason = valid ? "" : why;
    }
}

// ---------------------------------------------------------------------------------------------------------------------- style
struct Theme {
    ImU32 a, b;
    const char* name;
};
#define C(r, g, b, a) IM_COL32(r, g, b, a)
const Theme kThemes[] = {
    { C(0, 229, 200, 255), C(150, 100, 255, 255), "极光  AURORA" },
    { C(255, 184, 64, 255), C(255, 90, 160, 255), "余烬  EMBER" },
    { C(90, 235, 150, 255), C(60, 170, 255, 255), "翡翠  VERDANT" },
    { C(255, 100, 110, 255), C(255, 214, 90, 255), "赤焰  CRIMSON" },
};
ImU32 Acc() { return kThemes[g_theme].a; }
ImU32 Acc2() { return kThemes[g_theme].b; }
ImU32 Al(ImU32 c, float a) {
    uint32_t base = (c >> 24) & 255;
    uint32_t v = (uint32_t)std::clamp(base * a, 0.f, 255.f);
    return (c & 0x00FFFFFF) | (v << 24);
}
ImU32 Mix(ImU32 x, ImU32 y, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [&](int sh) { return (uint32_t)(((x >> sh) & 255) * (1 - t) + ((y >> sh) & 255) * t); };
    return (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
const ImU32 kText = C(226, 233, 255, 255), kDim = C(138, 150, 188, 255), kGood = C(90, 235, 150, 255), kBad = C(255, 100, 110, 255),
            kWarn = C(255, 190, 80, 255);

ImU32 ResColor(const std::string& k) {
    if (k == "energy") return C(255, 214, 64, 255);
    if (k == "minerals") return C(255, 104, 96, 255);
    if (k == "food") return C(124, 232, 112, 255);
    if (k == "consumer_goods") return C(255, 152, 64, 255);
    if (k == "alloys") return C(132, 192, 255, 255);
    if (k == "influence") return C(192, 132, 255, 255);
    if (k == "unity") return C(255, 122, 212, 255);
    if (k == "physics_research") return C(84, 204, 255, 255);
    if (k == "society_research") return C(112, 255, 172, 255);
    if (k == "engineering_research") return C(255, 172, 84, 255);
    return C(170, 180, 215, 255);
}
const char* ResName(const std::string& k) {
    static const std::unordered_map<std::string, const char*> m = {
        { "energy", "能量币" }, { "minerals", "矿物" }, { "food", "食物" }, { "consumer_goods", "消费品" }, { "alloys", "合金" },
        { "influence", "影响力" }, { "unity", "团结" }, { "physics_research", "物理研究" }, { "society_research", "社会研究" },
        { "engineering_research", "工程研究" }, { "nanites", "纳米机器人" }, { "exotic_gases", "奇异气体" }, { "rare_crystals", "稀有水晶" },
        { "volatile_motes", "挥发性微粒" }, { "sr_zro", "佐罗" }, { "sr_dark_matter", "暗物质" }, { "sr_living_metal", "活体金属" },
        { "minor_artifacts", "次级遗物" }, { "menace", "威胁" }, { "trade", "贸易" }, { "astral_threads", "星界之线" },
    };
    auto it = m.find(k);
    return it == m.end() ? k.c_str() : it->second;
}

// ---------------------------------------------------------------------------------------------------------------- draw helpers
ImFont* F(ImFont* f) { return (g_fonts_ctx == ImGui::GetCurrentContext() && f) ? f : ImGui::GetFont(); }

ImVec2 TextSz(ImFont* f, float k, const char* s) {
    f = F(f);
    return f->CalcTextSizeA(f->FontSize * k * g_fit, FLT_MAX, 0.f, s);
}
// align: 0 left, 1 centre, 2 right (of p.x)
void Txt(ImDrawList* dl, ImFont* f, float k, ImVec2 p, ImU32 col, const char* s, int align = 0) {
    f = F(f);
    const float size = f->FontSize * k * g_fit;
    if (align) {
        ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0.f, s);
        p.x -= align == 1 ? sz.x * 0.5f : sz.x;
    }
    dl->AddText(f, size, p, col, s);
}
void TxtF(ImDrawList* dl, ImFont* f, float k, ImVec2 p, ImU32 col, int align, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Txt(dl, f, k, p, col, buf, align);
}
ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
ImVec2 operator-(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
ImVec2 operator*(ImVec2 a, float s) { return ImVec2(a.x * s, a.y * s); }

void Fmt(char* out, size_t n, double v, bool sign = false) {
    const double a = fabs(v);
    const char* sg = sign && v > 0.004 ? "+" : "";
    if (a >= 1e9) snprintf(out, n, "%s%.2fB", sg, v / 1e9);
    else if (a >= 1e6) snprintf(out, n, "%s%.2fM", sg, v / 1e6);
    else if (a >= 1e4) snprintf(out, n, "%s%.1fk", sg, v / 1e3);
    else if (a >= 100) snprintf(out, n, "%s%.0f", sg, v);
    else snprintf(out, n, "%s%.1f", sg, v);
}

void Glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float strength) {
    const int N = 14;
    for (int i = 0; i < N; ++i) {
        const float t = (float)i / N;
        dl->AddCircleFilled(c, r * (1.f - t * 0.97f), Al(col, strength / N * (0.4f + 1.2f * t)), 40);
    }
}

void Stars(ImDrawList* dl, ImVec2 a, ImVec2 b, double t) {
    uint32_t s = 20260717u;
    auto rnd = [&]() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / 16777216.f;
    };
    const float w = b.x - a.x, h = b.y - a.y;
    for (int i = 0; i < 120; ++i) {
        const float u = rnd(), v = rnd(), d = 0.25f + 0.75f * rnd(), ph = rnd() * 6.28f;
        const float x = a.x + fmodf(u + (float)t * 0.004f * d, 1.f) * w, y = a.y + v * h;
        const float tw = 0.55f + 0.45f * sinf((float)t * (0.7f + d * 1.5f) + ph);
        const int al = (int)(255 * tw * d * 0.75f);
        dl->AddCircleFilled(ImVec2(x, y), (0.6f + d * 1.0f) * g_S, C(205, 222, 255, al), 6);
        if (d > 0.92f) {
            const float L = 5.f * g_S * tw;
            dl->AddLine(ImVec2(x - L, y), ImVec2(x + L, y), C(205, 222, 255, al / 3));
            dl->AddLine(ImVec2(x, y - L), ImVec2(x, y + L), C(205, 222, 255, al / 3));
        }
    }
    const float cyc = fmodf((float)t, 9.f) / 1.1f;  // a shooting star every 9 s
    if (cyc < 1.f) {
        const ImVec2 p0(a.x + w * (0.15f + 0.5f * cyc), a.y + h * (0.05f + 0.45f * cyc));
        for (int k = 0; k < 10; ++k)
            dl->AddLine(p0 - ImVec2(k * 9.f * g_S, k * 4.f * g_S), p0 - ImVec2((k + 1) * 9.f * g_S, (k + 1) * 4.f * g_S),
                        C(220, 235, 255, (int)(200 * (1 - cyc) * (1 - k / 10.f))), 1.6f * g_S);
    }
}

void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImU32 accent, float alpha = 1.f) {
    dl->AddRectFilled(a, b, C(11, 15, 32, (int)(205 * alpha)), r);
    dl->AddRectFilledMultiColor(ImVec2(a.x + r * 0.4f, a.y + 1), ImVec2(b.x - r * 0.4f, a.y + (b.y - a.y) * 0.45f), C(255, 255, 255, 12),
                                C(255, 255, 255, 12), C(255, 255, 255, 0), C(255, 255, 255, 0));
    dl->AddRect(a, b, C(255, 255, 255, 24), r, 0, 1.f);
    dl->AddRectFilledMultiColor(ImVec2(a.x + r, a.y), ImVec2(a.x + r + (b.x - a.x - 2 * r) * 0.35f, a.y + 2.f * g_S), Al(accent, 0.f), accent,
                                accent, Al(accent, 0.f));
}

void CardTitle(ImDrawList* dl, ImVec2 a, const char* en, const char* zh) {
    Txt(dl, g_font_bold, 0.62f, ImVec2(a.x + 18 * g_S, a.y + 14 * g_S), Al(Acc(), 0.85f), en);
    Txt(dl, g_font_bold, 0.95f, ImVec2(a.x + 18 * g_S, a.y + 28 * g_S), kText, zh);
}

// Smooth gradient area under a polyline (one quad per segment, vertex colours fade to the baseline).
void AreaFill(ImDrawList* dl, const ImVec2* p, int n, float base_y, ImU32 top, ImU32 bottom) {
    if (n < 2) return;
    dl->PrimReserve((n - 1) * 6, (n - 1) * 4);
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    for (int i = 0; i < n - 1; ++i) {
        const ImDrawIdx idx = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(p[i], uv, top);
        dl->PrimWriteVtx(p[i + 1], uv, top);
        dl->PrimWriteVtx(ImVec2(p[i + 1].x, base_y), uv, bottom);
        dl->PrimWriteVtx(ImVec2(p[i].x, base_y), uv, bottom);
        dl->PrimWriteIdx(idx);
        dl->PrimWriteIdx(idx + 1);
        dl->PrimWriteIdx(idx + 2);
        dl->PrimWriteIdx(idx);
        dl->PrimWriteIdx(idx + 2);
        dl->PrimWriteIdx(idx + 3);
    }
}

// 270 degree gauge with a gradient arc and a glowing tip.
void Ring(ImDrawList* dl, ImVec2 c, float r, float th, float frac, ImU32 ca, ImU32 cb) {
    const float a0 = IM_PI * 0.75f, span = IM_PI * 1.5f;
    dl->PathArcTo(c, r, a0, a0 + span, 64);
    dl->PathStroke(C(255, 255, 255, 18), 0, th);
    frac = std::clamp(frac, 0.f, 1.f);
    if (frac <= 0.002f) return;
    const int segs = std::max(2, (int)(48 * frac));
    for (int k = 0; k < segs; ++k) {
        const float f0 = frac * k / segs, f1 = frac * (k + 1) / segs;
        dl->PathArcTo(c, r, a0 + span * f0, a0 + span * f1 + 0.012f, 3);
        dl->PathStroke(Mix(ca, cb, (f0 + f1) * 0.5f / std::max(frac, 0.05f)), 0, th);
    }
    const float ae = a0 + span * frac;
    const ImVec2 tip(c.x + cosf(ae) * r, c.y + sinf(ae) * r);
    dl->AddCircleFilled(tip, th * 0.5f, cb, 12);
    dl->AddCircleFilled(c + ImVec2(cosf(a0) * r, sinf(a0) * r), th * 0.5f, ca, 12);
    Glow(dl, tip, th * 1.7f, cb, 0.55f);
}

void Icon(ImDrawList* dl, const std::string& key, ImVec2 c, float r, ImU32 col) {
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    const float th = std::max(1.2f, r * 0.2f);
    if (key == "energy") {
        const ImVec2 p[6] = { P(0.25f, -1), P(-0.55f, 0.15f), P(-0.05f, 0.15f), P(-0.25f, 1), P(0.6f, -0.25f), P(0.05f, -0.25f) };
        dl->AddTriangleFilled(p[0], p[1], p[2], col);
        dl->AddTriangleFilled(p[0], p[2], p[5], col);
        dl->AddTriangleFilled(p[5], p[4], p[3], col);
        dl->AddTriangleFilled(p[5], p[3], p[2], col);
    } else if (key == "minerals") {
        const ImVec2 gem[4] = { P(0, -1), P(0.85f, -0.2f), P(0, 1), P(-0.85f, -0.2f) };
        dl->AddConvexPolyFilled(gem, 4, Al(col, 0.85f));
        dl->AddTriangleFilled(P(0, -1), P(0.85f, -0.2f), P(0, -0.2f), Al(C(255, 255, 255, 255), 0.35f));
        dl->AddLine(P(-0.85f, -0.2f), P(0.85f, -0.2f), C(0, 0, 0, 90), 1.f);
        dl->AddLine(P(0, -0.2f), P(0, 1), C(0, 0, 0, 70), 1.f);
    } else if (key == "food") {
        ImVec2 pts[20];
        for (int i = 0; i < 10; ++i) {
            const float u = -1.f + 2.f * i / 9.f, w = 0.62f * powf(std::max(0.f, 1 - u * u), 0.85f);
            pts[i] = P((u - w) * 0.7071f, (-u - w) * 0.7071f);
            pts[19 - i] = P((u + w) * 0.7071f, (-u + w) * 0.7071f);
        }
        dl->AddConvexPolyFilled(pts, 20, col);
        dl->AddLine(P(-0.7f, 0.7f), P(0.7f, -0.7f), C(0, 0, 0, 90), 1.2f);
    } else if (key == "consumer_goods") {
        dl->AddRectFilled(P(-0.8f, -0.45f), P(0.8f, 0.9f), Al(col, 0.9f), r * 0.2f);
        dl->AddRectFilled(P(-0.9f, -0.75f), P(0.9f, -0.35f), col, r * 0.15f);
        dl->AddRectFilled(P(-0.12f, -0.75f), P(0.12f, 0.9f), C(0, 0, 0, 90));
    } else if (key == "alloys") {
        const ImVec2 lower[4] = { P(-0.95f, 0.85f), P(0.95f, 0.85f), P(0.65f, 0.1f), P(-0.65f, 0.1f) };
        const ImVec2 upper[4] = { P(-0.55f, 0.0f), P(0.55f, 0.0f), P(0.3f, -0.75f), P(-0.3f, -0.75f) };
        dl->AddConvexPolyFilled(lower, 4, Al(col, 0.85f));
        dl->AddConvexPolyFilled(upper, 4, col);
    } else if (key == "influence") {
        const float rot[2] = { 0.f, IM_PI / 4 };
        for (float a : rot) {
            ImVec2 q[4];
            for (int i = 0; i < 4; ++i) q[i] = P(cosf(a + i * IM_PI / 2 + IM_PI / 4) * 1.1f, sinf(a + i * IM_PI / 2 + IM_PI / 4) * 1.1f);
            dl->AddConvexPolyFilled(q, 4, Al(col, 0.8f));
        }
        dl->AddCircleFilled(c, r * 0.28f, C(11, 15, 32, 255), 12);
    } else if (key == "unity") {
        for (int i = 0; i < 3; ++i) {
            const float a = -IM_PI / 2 + i * 2 * IM_PI / 3;
            dl->AddCircle(P(cosf(a) * 0.45f, sinf(a) * 0.45f), r * 0.55f, Al(col, 0.9f), 20, th);
        }
    } else if (key == "physics_research") {
        for (int e = 0; e < 3; ++e) {
            ImVec2 pts[24];
            for (int i = 0; i < 24; ++i) {
                const float t = i * 2 * IM_PI / 24, x = cosf(t) * 1.0f, y = sinf(t) * 0.38f, a = e * IM_PI / 3;
                pts[i] = P(x * cosf(a) - y * sinf(a), x * sinf(a) + y * cosf(a));
            }
            dl->AddPolyline(pts, 24, Al(col, 0.9f), ImDrawFlags_Closed, th * 0.8f);
        }
        dl->AddCircleFilled(c, r * 0.22f, col, 12);
    } else if (key == "society_research") {
        const ImVec2 n[3] = { P(0, -0.8f), P(0.8f, 0.6f), P(-0.8f, 0.6f) };
        for (int i = 0; i < 3; ++i) dl->AddLine(n[i], n[(i + 1) % 3], Al(col, 0.7f), th * 0.8f);
        for (int i = 0; i < 3; ++i) dl->AddCircleFilled(n[i], r * 0.3f, col, 12);
    } else if (key == "engineering_research") {
        for (int i = 0; i < 8; ++i) {
            const float a = i * IM_PI / 4, ca = cosf(a), sa = sinf(a);
            dl->AddLine(P(ca * 0.6f, sa * 0.6f), P(ca * 0.98f, sa * 0.98f), col, r * 0.34f);
        }
        dl->AddCircle(c, r * 0.62f, col, 20, th * 1.3f);
        dl->AddCircleFilled(c, r * 0.2f, col, 10);
    } else {
        ImVec2 h[6];
        for (int i = 0; i < 6; ++i) h[i] = P(cosf(i * IM_PI / 3) * 0.95f, sinf(i * IM_PI / 3) * 0.95f);
        dl->AddPolyline(h, 6, col, ImDrawFlags_Closed, th);
    }
}

// Rail icons drawn from primitives (no icon font).
void TabIcon(ImDrawList* dl, int tab, ImVec2 c, float r, ImU32 col) {
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    const float th = std::max(1.5f, r * 0.14f);
    switch (tab) {
    case 0:  // overview: four tiles
        for (int i = 0; i < 4; ++i) {
            const float x = (i % 2) ? 0.08f : -0.92f, y = (i / 2) ? 0.08f : -0.92f;
            dl->AddRectFilled(P(x, y), P(x + 0.84f, y + 0.84f), Al(col, i == 0 ? 1.f : 0.6f), r * 0.14f);
        }
        break;
    case 1:  // economy: bar chart
        for (int i = 0; i < 4; ++i) {
            const float h[4] = { 0.5f, 1.1f, 0.8f, 1.6f };
            dl->AddRectFilled(P(-1.0f + i * 0.52f, 0.9f - h[i]), P(-0.62f + i * 0.52f, 0.9f), Al(col, 0.5f + 0.5f * (i / 3.f)), r * 0.08f);
        }
        break;
    case 2:  // time: clock
        dl->AddCircle(c, r * 0.95f, col, 28, th);
        dl->AddLine(c, P(0, -0.6f), col, th);
        dl->AddLine(c, P(0.45f, 0.2f), col, th);
        break;
    case 3: {  // script: chevrons and cursor
        const ImVec2 chev[3] = { P(-0.9f, -0.6f), P(-0.2f, 0.0f), P(-0.9f, 0.6f) };
        dl->AddPolyline(chev, 3, col, 0, th);
        dl->AddLine(P(0.05f, 0.65f), P(0.95f, 0.65f), col, th);
        break;
    }
    default:  // settings: sliders
        for (int i = 0; i < 3; ++i) {
            const float y = -0.6f + i * 0.6f, kx[3] = { -0.35f, 0.45f, -0.05f };
            dl->AddLine(P(-0.95f, y), P(0.95f, y), Al(col, 0.55f), th);
            dl->AddCircleFilled(P(kx[i], y), r * 0.2f, col, 12);
        }
        break;
    }
}

bool Hit(ImVec2 a, ImVec2 b, const char* id) {
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(id, ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)));
    return ImGui::IsItemClicked();
}

// ----------------------------------------------------------------------------------------------------------------------- HUD
void DrawSpeedPips(ImDrawList* dl, ImVec2 origin, float h, const char* idp) {
    const float pw = 26 * g_S, gap = 5 * g_S;
    // pause button
    const float bw = h;
    char id[64];
    snprintf(id, sizeof(id), "%s_pause", idp);
    const ImVec2 pa = origin, pb(origin.x + bw, origin.y + h);
    const bool hov = ImGui::IsMouseHoveringRect(pa, pb) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (Hit(pa, pb, id)) g_pending.push_back({ Pending::Pause, g_snap.paused ? 0 : 1, {} });
    const float hv = Smooth(id, hov ? 1.f : 0.f);
    dl->AddRectFilled(pa, pb, g_snap.paused ? Al(kWarn, 0.28f + 0.2f * hv) : C(255, 255, 255, (int)(18 + 22 * hv)), h * 0.3f);
    const ImVec2 cc((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f);
    if (g_snap.paused) {
        dl->AddTriangleFilled(cc + ImVec2(-5 * g_S, -7 * g_S), cc + ImVec2(-5 * g_S, 7 * g_S), cc + ImVec2(8 * g_S, 0), kWarn);
    } else {
        dl->AddRectFilled(cc + ImVec2(-6 * g_S, -7 * g_S), cc + ImVec2(-2 * g_S, 7 * g_S), kText, 1.5f);
        dl->AddRectFilled(cc + ImVec2(2 * g_S, -7 * g_S), cc + ImVec2(6 * g_S, 7 * g_S), kText, 1.5f);
    }
    for (int i = 1; i <= 5; ++i) {
        const ImVec2 a(origin.x + bw + 10 * g_S + (i - 1) * (pw + gap), origin.y + h * 0.18f), b(a.x + pw, origin.y + h * 0.82f);
        snprintf(id, sizeof(id), "%s_s%d", idp, i);
        const bool hv2 = ImGui::IsMouseHoveringRect(a, b) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (Hit(a, b, id)) g_pending.push_back({ Pending::Speed, i, {} });
        const bool on = (int)g_snap.speed >= i;
        const float lit = Smooth(id, on ? 1.f : 0.f, 14.f), hv3 = Smooth((std::string(id) + "h").c_str(), hv2 ? 1.f : 0.f);
        const ImU32 col = Mix(C(255, 255, 255, 30 + (int)(26 * hv3)), Mix(Acc(), Acc2(), i / 5.f), lit);
        dl->AddRectFilled(a, b, Al(col, g_snap.paused && on ? 0.45f : 1.f), (b.y - a.y) * 0.5f);
        if (on && !g_snap.paused) Glow(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 20 * g_S, col, 0.18f);
    }
}

void DrawHud(const ImGuiIO& io) {
    const float W = std::min(1060 * g_S, io.DisplaySize.x - 20), H = 68 * g_S;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - W) * 0.5f, io.DisplaySize.y - H - 24 * g_S), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##sc_hud", nullptr, fl)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetWindowPos(), b(a.x + W, a.y + H);
        // pill with a soft shadow, a lit rim and the accent glow underneath
        for (int i = 6; i >= 1; --i) dl->AddRectFilled(a - ImVec2(i * 2.f, i * 2.f - 6), b + ImVec2(i * 2.f, i * 2.f + 6), C(0, 0, 0, 10), H * 0.5f + i * 2.f);
        dl->AddRectFilled(a, b, C(8, 11, 24, 232), H * 0.5f);
        dl->AddRectFilledMultiColor(a + ImVec2(H * 0.5f, 1), ImVec2(b.x - H * 0.5f, a.y + H * 0.5f), C(255, 255, 255, 14), C(255, 255, 255, 14), C(255, 255, 255, 0),
                                    C(255, 255, 255, 0));
        dl->AddRect(a, b, C(255, 255, 255, 30), H * 0.5f, 0, 1.f);
        dl->AddRectFilledMultiColor(ImVec2(a.x + H, b.y - 2.f * g_S), ImVec2(b.x - H, b.y), Al(Acc(), 0.f), Acc(), Acc2(), Al(Acc2(), 0.f));

        // orb: opens the deck
        const ImVec2 oc(a.x + H * 0.5f, a.y + H * 0.5f);
        const float orad = H * 0.34f;
        const bool ohov = ImGui::IsMouseHoveringRect(oc - ImVec2(orad, orad), oc + ImVec2(orad, orad));
        if (Hit(oc - ImVec2(orad, orad), oc + ImVec2(orad, orad), "orb")) g_deck_open = !g_deck_open;
        const float oh = Smooth("orbh", ohov ? 1.f : 0.f), spin = (float)g_T * (g_snap.paused ? 0.25f : 0.9f);
        Glow(dl, oc, orad * (1.7f + 0.3f * oh), Acc(), 0.35f + 0.3f * oh);
        dl->AddCircleFilled(oc, orad, C(10, 14, 30, 255), 32);
        for (int i = 0; i < 3; ++i) {
            dl->PathArcTo(oc, orad - i * 4.f * g_S, spin * (i % 2 ? -1 : 1) + i, spin * (i % 2 ? -1 : 1) + i + IM_PI * 1.2f, 20);
            dl->PathStroke(Mix(Acc(), Acc2(), i / 2.f), 0, 2.f * g_S);
        }
        dl->AddCircleFilled(oc, 3.2f * g_S, g_deck_open ? kText : Acc(), 12);

        float x = a.x + H + 6 * g_S;
        if (!g_snap.in_game) {
            Txt(dl, g_font_body, 1.f, ImVec2(x, a.y + H * 0.5f - 10 * g_S), kDim, "未进入游戏  ·  NO GAME LOADED");
        } else {
            // date
            char date[32];
            snprintf(date, sizeof(date), "%04u.%02u.%02u", g_snap.year, g_snap.month, g_snap.day);
            Txt(dl, g_font_body, 0.6f, ImVec2(x, a.y + 10 * g_S), Al(Acc(), 0.9f), "STELLAR DATE");
            Txt(dl, g_font_num_s, 1.f, ImVec2(x, a.y + 24 * g_S), kText, date);
            x += 168 * g_S;
            DrawSpeedPips(dl, ImVec2(x, a.y + 15 * g_S), H - 30 * g_S, "hud");
            x += (H - 30 * g_S) + 10 * g_S + 5 * (26 + 5) * g_S + 6 * g_S;
            dl->AddLine(ImVec2(x, a.y + 16 * g_S), ImVec2(x, b.y - 16 * g_S), C(255, 255, 255, 28));
            x += 14 * g_S;
            static const char* chips[] = { "energy", "minerals", "food", "alloys", "influence" };
            const float cw = (b.x - H * 0.5f - x) / 5.f;
            for (int i = 0; i < 5; ++i) {
                const ResInfo* r = FindRes(chips[i]);
                if (!r) continue;
                const float cx = x + i * cw;
                const ImU32 rc = ResColor(r->key);
                Icon(dl, r->key, ImVec2(cx + 12 * g_S, a.y + H * 0.5f), 9 * g_S, rc);
                char v[32], n[32];
                Fmt(v, sizeof(v), r->stock);
                Fmt(n, sizeof(n), r->net, true);
                Txt(dl, g_font_bold, 1.f, ImVec2(cx + 28 * g_S, a.y + 13 * g_S), kText, v);
                Txt(dl, g_font_body, 0.76f, ImVec2(cx + 28 * g_S, a.y + 36 * g_S), r->net < -0.004 ? kBad : (r->net > 0.004 ? kGood : kDim), n);
            }
        }
        ImGui::SetCursorScreenPos(a);
        ImGui::Dummy(ImVec2(W, H));
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ----------------------------------------------------------------------------------------------------------------------- tabs
void RadarCard(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    Panel(dl, a, b, 16 * g_S, Acc());
    CardTitle(dl, a, "STANDING", "国力对比");
    const ImVec2 c((a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.56f);
    const float R = std::min(b.x - a.x - 140 * g_S, b.y - a.y - 150 * g_S) * 0.5f;
    struct Axis {
        const char* label;
        double v, m;
    } ax[5] = { { "军事", g_snap.mil, g_snap.mil_max },  { "科技", g_snap.tech, g_snap.tech_max }, { "经济", g_snap.eco, g_snap.eco_max },
                { "疆域", (double)g_snap.colonies, g_snap.col_max }, { "人口", (double)g_snap.pops, g_snap.pop_max } };
    for (int ring = 1; ring <= 4; ++ring) {
        ImVec2 pts[5];
        for (int i = 0; i < 5; ++i) {
            const float t = -IM_PI / 2 + i * 2 * IM_PI / 5;
            pts[i] = c + ImVec2(cosf(t), sinf(t)) * (R * ring / 4.f);
        }
        dl->AddPolyline(pts, 5, C(255, 255, 255, ring == 4 ? 44 : 20), ImDrawFlags_Closed, 1.f);
    }
    ImVec2 poly[5];
    for (int i = 0; i < 5; ++i) {
        const float t = -IM_PI / 2 + i * 2 * IM_PI / 5;
        const ImVec2 dir(cosf(t), sinf(t));
        dl->AddLine(c, c + dir * R, C(255, 255, 255, 22));
        char key[16];
        snprintf(key, sizeof(key), "radar%d", i);
        const float frac = Smooth(key, (float)std::clamp(ax[i].m > 0 ? ax[i].v / ax[i].m : 0.0, 0.0, 1.0), 4.f);
        poly[i] = c + dir * (R * (0.06f + 0.94f * frac));
        const ImVec2 lp = c + dir * (R + 26 * g_S);
        Txt(dl, g_font_bold, 0.9f, lp - ImVec2(0, 14 * g_S), kText, ax[i].label, 1);
        TxtF(dl, g_font_body, 0.8f, lp + ImVec2(0, 3 * g_S), Al(Acc(), 0.95f), 1, "%.0f%%", frac * 100.f);
    }
    for (int i = 0; i < 5; ++i) dl->AddTriangleFilled(c, poly[i], poly[(i + 1) % 5], Al(Mix(Acc(), Acc2(), i / 4.f), 0.22f));
    for (int i = 0; i < 5; ++i) dl->AddLine(poly[i], poly[(i + 1) % 5], Mix(Acc(), Acc2(), i / 4.f), 2.2f * g_S);
    for (int i = 0; i < 5; ++i) {
        Glow(dl, poly[i], 11 * g_S, Mix(Acc(), Acc2(), i / 4.f), 0.5f);
        dl->AddCircleFilled(poly[i], 3.6f * g_S, kText, 12);
    }
    Txt(dl, g_font_body, 0.74f, ImVec2((a.x + b.x) * 0.5f, b.y - 24 * g_S), kDim, "100% = 银河系中该项最强的帝国", 1);
}

void RingsCard(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    Panel(dl, a, b, 16 * g_S, Acc2());
    CardTitle(dl, a, "STOCKPILES", "国库");
    static const char* keys[] = { "energy", "minerals", "food", "consumer_goods", "alloys", "influence" };
    const float cw = (b.x - a.x - 24 * g_S) / 3.f, ch = (b.y - a.y - 70 * g_S) / 2.f;
    for (int i = 0; i < 6; ++i) {
        const ResInfo* r = FindRes(keys[i]);
        if (!r) continue;
        const ImVec2 c(a.x + 12 * g_S + cw * (i % 3 + 0.5f), a.y + 62 * g_S + ch * (i / 3 ? 1.5f : 0.5f));
        const float rad = std::min(cw, ch) * 0.34f;
        const Hist* h = HistOf(r->key);
        float peak = (float)std::max(1.0, r->stock);
        if (h)
            for (float v : h->stock) peak = std::max(peak, v);
        double frac = r->max > 0 ? r->stock / r->max : r->stock / (peak * 1.15);
        char key[24];
        snprintf(key, sizeof(key), "ring%d", i);
        const float f = Smooth(key, (float)std::clamp(frac, 0.0, 1.0), 4.f);
        const ImU32 rc = ResColor(r->key);
        Ring(dl, c, rad, 9 * g_S, f, Al(rc, 0.55f), rc);
        char v[32], n[32];
        Fmt(v, sizeof(v), r->stock);
        Fmt(n, sizeof(n), r->net, true);
        Txt(dl, g_font_num_s, 1.0f, c - ImVec2(0, 15 * g_S), kText, v, 1);
        Txt(dl, g_font_body, 0.8f, c + ImVec2(0, 10 * g_S), r->net < -0.004 ? kBad : (r->net > 0.004 ? kGood : kDim), n, 1);
        Icon(dl, r->key, c + ImVec2(0, rad + 20 * g_S), 8 * g_S, rc);
        Txt(dl, g_font_body, 0.82f, c + ImVec2(0, rad + 32 * g_S), kDim, ResName(r->key), 1);
    }
}

void StatTile(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* label, const char* value, ImU32 col) {
    dl->AddRectFilled(a, b, C(255, 255, 255, 10), 12 * g_S);
    dl->AddRectFilled(ImVec2(a.x, a.y + 12 * g_S), ImVec2(a.x + 3 * g_S, b.y - 12 * g_S), col, 2.f);
    Txt(dl, g_font_num_s, 1.15f, ImVec2(a.x + 16 * g_S, a.y + 12 * g_S), kText, value);
    Txt(dl, g_font_body, 0.8f, ImVec2(a.x + 16 * g_S, b.y - 28 * g_S), kDim, label);
}

void TabOverview(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const float gap = 16 * g_S, top_h = 132 * g_S;
    // identity card
    const ImVec2 ib(a.x + (b.x - a.x) * 0.46f, a.y + top_h);
    Panel(dl, a, ib, 16 * g_S, Acc());
    Glow(dl, ImVec2(ib.x - 60 * g_S, a.y + 40 * g_S), 150 * g_S, Acc2(), 0.35f);
    Txt(dl, g_font_bold, 0.62f, ImVec2(a.x + 22 * g_S, a.y + 18 * g_S), Al(Acc(), 0.85f), "EMPIRE");
    const bool ascii = std::all_of(g_snap.name.begin(), g_snap.name.end(), [](char ch) { return (uint8_t)ch < 0x80; });
    Txt(dl, ascii ? g_font_title : g_font_body, ascii ? 1.f : 1.55f, ImVec2(a.x + 22 * g_S, a.y + 34 * g_S), kText, g_snap.name.empty() ? "—" : g_snap.name.c_str());
    TxtF(dl, g_font_body, 0.85f, ImVec2(a.x + 22 * g_S, a.y + top_h - 34 * g_S), kDim, 0, "第 %u 日   ·   %04u.%02u.%02u", g_snap.day_index, g_snap.year, g_snap.month,
         g_snap.day);
    // stat tiles
    const float tx = ib.x + gap, tw = (b.x - tx - 3 * gap) / 4.f;
    char v[4][32];
    snprintf(v[0], 32, "%u", g_snap.colonies);
    snprintf(v[1], 32, "%u", g_snap.pops);
    Fmt(v[2], 32, g_snap.mil);
    snprintf(v[3], 32, "%d", g_snap.empire_size);
    const char* lab[4] = { "殖民地", "人口", "军事力量", "帝国规模" };
    const ImU32 cols[4] = { Acc(), Mix(Acc(), Acc2(), 0.4f), Mix(Acc(), Acc2(), 0.7f), Acc2() };
    for (int i = 0; i < 4; ++i) StatTile(dl, ImVec2(tx + i * (tw + gap), a.y), ImVec2(tx + i * (tw + gap) + tw, a.y + top_h), lab[i], v[i], cols[i]);
    // lower row
    const float y2 = a.y + top_h + gap, rw = (b.x - a.x) * 0.36f;
    RadarCard(dl, ImVec2(a.x, y2), ImVec2(a.x + rw, b.y));
    RingsCard(dl, ImVec2(a.x + rw + gap, y2), b);
}

void TabEconomy(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const float gap = 16 * g_S, lw = 380 * g_S;
    Panel(dl, a, ImVec2(a.x + lw, b.y), 16 * g_S, Acc());
    CardTitle(dl, a, "LEDGER", "资源账本");
    std::vector<const ResInfo*> rows;
    for (const auto& r : g_snap.res)
        if (fabs(r.stock) > 0.001 || fabs(r.income) > 0.001 || fabs(r.expense) > 0.001) rows.push_back(&r);
    if (rows.empty()) return;
    g_sel_res = std::clamp(g_sel_res, 0, (int)rows.size() - 1);
    const float rh = std::min(46.f * g_S, (b.y - a.y - 70 * g_S) / (float)rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        const ResInfo& r = *rows[i];
        const ImVec2 ra(a.x + 10 * g_S, a.y + 62 * g_S + i * rh), rb(a.x + lw - 10 * g_S, ra.y + rh - 4 * g_S);
        char id[24];
        snprintf(id, sizeof(id), "row%zu", i);
        const bool hov = ImGui::IsMouseHoveringRect(ra, rb) && ImGui::IsWindowHovered();
        if (Hit(ra, rb, id)) g_sel_res = (int)i;
        const float sel = Smooth(id, (int)i == g_sel_res ? 1.f : 0.f), hv = Smooth((std::string(id) + "h").c_str(), hov ? 1.f : 0.f);
        const ImU32 rc = ResColor(r.key);
        dl->AddRectFilled(ra, rb, Al(rc, 0.10f * sel + 0.05f * hv), 10 * g_S);
        if (sel > 0.02f) dl->AddRectFilled(ra + ImVec2(0, 8 * g_S), ImVec2(ra.x + 3 * g_S, rb.y - 8 * g_S), Al(rc, sel), 2.f);
        Icon(dl, r.key, ImVec2(ra.x + 24 * g_S, (ra.y + rb.y) * 0.5f), 9 * g_S, rc);
        Txt(dl, g_font_body, 0.95f, ImVec2(ra.x + 44 * g_S, (ra.y + rb.y) * 0.5f - 10 * g_S), kText, ResName(r.key));
        char v[32], n[32];
        Fmt(v, sizeof(v), r.stock);
        Fmt(n, sizeof(n), r.net, true);
        const ImU32 net_col = r.net < -0.004 ? kBad : (r.net > 0.004 ? kGood : kDim);
        if (rh >= 40 * g_S) {  // roomy: stock over net
            Txt(dl, g_font_bold, 0.95f, ImVec2(rb.x - 78 * g_S, ra.y + 4 * g_S), kText, v, 2);
            Txt(dl, g_font_body, 0.76f, ImVec2(rb.x - 78 * g_S, ra.y + rh * 0.5f), net_col, n, 2);
        } else {  // crowded (many non-zero resources): one line, net to the left of the stock
            const float ty = (ra.y + rb.y) * 0.5f - F(g_font_bold)->FontSize * 0.95f * g_fit * 0.5f;
            Txt(dl, g_font_bold, 0.95f, ImVec2(rb.x - 78 * g_S, ty), kText, v, 2);
            Txt(dl, g_font_body, 0.76f, ImVec2(rb.x - 150 * g_S, ty + 2 * g_S), net_col, n, 2);
        }
        // inline sparkline
        if (const Hist* h = HistOf(r.key)) {
            if (h->stock.size() > 2) {
                float lo = FLT_MAX, hi = -FLT_MAX;
                for (float s : h->stock) {
                    lo = std::min(lo, s);
                    hi = std::max(hi, s);
                }
                if (hi - lo < 1e-3f) hi = lo + 1.f;
                std::vector<ImVec2> pts;
                const float sx = rb.x - 66 * g_S, sw = 56 * g_S;
                for (size_t k = 0; k < h->stock.size(); ++k)
                    pts.push_back(ImVec2(sx + sw * k / (float)(h->stock.size() - 1), rb.y - 10 * g_S - (h->stock[k] - lo) / (hi - lo) * (rb.y - ra.y - 20 * g_S)));
                dl->AddPolyline(pts.data(), (int)pts.size(), Al(rc, 0.9f), 0, 1.5f * g_S);
            }
        }
    }
    // detail
    const ResInfo& r = *rows[g_sel_res];
    const ImVec2 da(a.x + lw + gap, a.y), db(b.x, b.y);
    const ImU32 rc = ResColor(r.key);
    Panel(dl, da, db, 16 * g_S, rc);
    Glow(dl, ImVec2(db.x - 90 * g_S, da.y + 70 * g_S), 190 * g_S, rc, 0.28f);
    Icon(dl, r.key, ImVec2(da.x + 40 * g_S, da.y + 46 * g_S), 17 * g_S, rc);
    Txt(dl, g_font_bold, 0.62f, ImVec2(da.x + 72 * g_S, da.y + 18 * g_S), Al(rc, 0.9f), r.key.c_str());
    Txt(dl, g_font_body, 1.5f, ImVec2(da.x + 72 * g_S, da.y + 32 * g_S), kText, ResName(r.key));
    char v[32];
    Fmt(v, sizeof(v), r.stock);
    Txt(dl, g_font_num, 1.f, ImVec2(db.x - 28 * g_S, da.y + 22 * g_S), kText, v, 2);
    // flow strip: income vs expense
    const float fy = da.y + 110 * g_S, fw = db.x - da.x - 56 * g_S, fx = da.x + 28 * g_S;
    const double tot = std::max(1e-6, r.income + r.expense);
    dl->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + fw, fy + 12 * g_S), C(255, 255, 255, 14), 6 * g_S);
    dl->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + fw * (float)(r.income / tot), fy + 12 * g_S), Al(kGood, 0.85f), 6 * g_S);
    dl->AddRectFilled(ImVec2(fx + fw * (float)(r.income / tot), fy), ImVec2(fx + fw, fy + 12 * g_S), Al(kBad, 0.85f), 6 * g_S);
    char i1[32], e1[32], n1[32];
    Fmt(i1, 32, r.income, true);
    Fmt(e1, 32, -r.expense);
    Fmt(n1, 32, r.net, true);
    TxtF(dl, g_font_body, 0.9f, ImVec2(fx, fy + 20 * g_S), kGood, 0, "收入 %s", i1);
    TxtF(dl, g_font_body, 0.9f, ImVec2(fx + fw * 0.5f, fy + 20 * g_S), kBad, 1, "支出 %s", e1);
    TxtF(dl, g_font_bold, 0.95f, ImVec2(fx + fw, fy + 20 * g_S), r.net < 0 ? kBad : kGood, 2, "净 %s / 月", n1);
    // history chart
    const Hist* h = HistOf(r.key);
    const ImVec2 ca(fx, fy + 62 * g_S), cb(fx + fw, db.y - 110 * g_S);
    dl->AddRectFilled(ca - ImVec2(8 * g_S, 8 * g_S), cb + ImVec2(8 * g_S, 8 * g_S), C(255, 255, 255, 8), 12 * g_S);
    Txt(dl, g_font_bold, 0.62f, ca - ImVec2(0, 22 * g_S), Al(rc, 0.85f), "STOCKPILE · 每个游戏日采样一次");
    if (h && h->stock.size() >= 2) {
        float lo = FLT_MAX, hi = -FLT_MAX;
        for (float s : h->stock) {
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
        if (hi - lo < 1e-3f) {
            hi += 1.f;
            lo -= 1.f;
        }
        const float pad = (hi - lo) * 0.12f;
        lo -= pad;
        hi += pad;
        for (int g = 0; g <= 3; ++g) {
            const float y = ca.y + (cb.y - ca.y) * g / 3.f;
            dl->AddLine(ImVec2(ca.x, y), ImVec2(cb.x, y), C(255, 255, 255, 14));
            char t[32];
            Fmt(t, 32, hi - (hi - lo) * g / 3.f);
            Txt(dl, g_font_body, 0.7f, ImVec2(ca.x + 4 * g_S, y - 14 * g_S), kDim, t);
        }
        std::vector<ImVec2> pts;
        const size_t n = h->stock.size();
        for (size_t k = 0; k < n; ++k)
            pts.push_back(ImVec2(ca.x + (cb.x - ca.x) * k / (float)(n - 1), cb.y - (h->stock[k] - lo) / (hi - lo) * (cb.y - ca.y)));
        AreaFill(dl, pts.data(), (int)pts.size(), cb.y, Al(rc, 0.38f), Al(rc, 0.0f));
        dl->AddPolyline(pts.data(), (int)pts.size(), rc, 0, 2.4f * g_S);
        Glow(dl, pts.back(), 12 * g_S, rc, 0.6f);
        dl->AddCircleFilled(pts.back(), 3.6f * g_S, kText, 12);
        if (ImGui::IsMouseHoveringRect(ca, cb) && ImGui::IsWindowHovered()) {
            const float mx = ImGui::GetIO().MousePos.x;
            const size_t k = (size_t)std::clamp((mx - ca.x) / (cb.x - ca.x) * (n - 1) + 0.5f, 0.f, (float)(n - 1));
            dl->AddLine(ImVec2(pts[k].x, ca.y), ImVec2(pts[k].x, cb.y), C(255, 255, 255, 50));
            dl->AddCircleFilled(pts[k], 5 * g_S, rc, 14);
            char t[32];
            Fmt(t, 32, h->stock[k]);
            ImGui::SetTooltip("%zu 日前\n%s: %s", n - 1 - k, ResName(r.key), t);
        }
        // net per month bars under the chart
        const float by = db.y - 78 * g_S, bh = 46 * g_S;
        float amax = 1e-3f;
        for (float s : h->net) amax = std::max(amax, fabsf(s));
        dl->AddLine(ImVec2(ca.x, by + bh * 0.5f), ImVec2(cb.x, by + bh * 0.5f), C(255, 255, 255, 24));
        const float bw = (cb.x - ca.x) / (float)n;
        for (size_t k = 0; k < h->net.size(); ++k) {
            const float vv = h->net[k] / amax * bh * 0.5f, x0 = ca.x + bw * k;
            dl->AddRectFilled(ImVec2(x0 + 0.5f, by + bh * 0.5f - std::max(vv, 0.f)), ImVec2(x0 + std::max(1.f, bw - 1.f), by + bh * 0.5f - std::min(vv, 0.f)),
                              h->net[k] >= 0 ? Al(kGood, 0.8f) : Al(kBad, 0.8f), 1.5f);
        }
        Txt(dl, g_font_bold, 0.62f, ImVec2(ca.x, by - 15 * g_S), Al(rc, 0.85f), "NET / MONTH");
    } else {
        Txt(dl, g_font_body, 0.95f, (ca + cb) * 0.5f, kDim, "游戏日推进后开始绘制曲线（先取消暂停）", 1);
    }
    if (r.max > 0) TxtF(dl, g_font_body, 0.8f, ImVec2(db.x - 28 * g_S, da.y + 78 * g_S), kDim, 2, "容量上限 %.0f", r.max);
}

void TabTime(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const float gap = 16 * g_S, lw = (b.x - a.x) * 0.5f;
    // speed dial
    const ImVec2 la(a.x, a.y), lb(a.x + lw - gap * 0.5f, b.y);
    Panel(dl, la, lb, 16 * g_S, Acc());
    CardTitle(dl, la, "CHRONOMETER", "时间控制");
    const ImVec2 c((la.x + lb.x) * 0.5f, la.y + (lb.y - la.y) * 0.5f + 10 * g_S);
    const float R = std::min((lb.x - la.x) * 0.36f, (lb.y - la.y) * 0.33f);
    const float level = g_snap.paused ? 0.f : (float)g_snap.speed;
    const float sm = Smooth("dial", level, 7.f);
    Ring(dl, c, R, 16 * g_S, sm / 5.f, Acc(), Acc2());
    for (int i = 0; i <= 5; ++i) {
        const float ang = IM_PI * 0.75f + IM_PI * 1.5f * i / 5.f;
        const ImVec2 d(cosf(ang), sinf(ang));
        dl->AddLine(c + d * (R + 18 * g_S), c + d * (R + 28 * g_S), i == (int)level ? kText : C(255, 255, 255, 60), 2.f);
        char t[4];
        snprintf(t, sizeof(t), "%d", i);
        Txt(dl, g_font_body, 0.8f, c + d * (R + 44 * g_S) - ImVec2(0, 8 * g_S), i == (int)level ? kText : kDim, i == 0 ? "‖" : t, 1);
    }
    if (g_snap.paused) Txt(dl, g_font_num, 1.5f, c - ImVec2(0, 38 * g_S), kWarn, "II", 1);
    else TxtF(dl, g_font_num, 1.5f, c - ImVec2(0, 38 * g_S), kText, 1, "x%u", g_snap.speed);
    Txt(dl, g_font_body, 0.9f, c + ImVec2(0, 34 * g_S), g_snap.paused ? kWarn : Al(Acc(), 0.95f), g_snap.paused ? "已暂停" : "运行中", 1);
    DrawSpeedPips(dl, ImVec2(c.x - (44 + 5 * 31 + 10) * g_S * 0.5f, lb.y - 74 * g_S), 44 * g_S, "tab");

    // calendar + diagnostics
    const ImVec2 ra(la.x + lw + gap * 0.5f, a.y);
    Panel(dl, ra, b, 16 * g_S, Acc2());
    CardTitle(dl, ra, "CALENDAR", "星历");
    char date[32];
    snprintf(date, sizeof(date), "%04u.%02u.%02u", g_snap.year, g_snap.month, g_snap.day);
    const ImVec2 rc((ra.x + b.x) * 0.5f, ra.y + 190 * g_S);
    const float day_f = Smooth("cal_day", (g_snap.day - 1 + 0.5f) / 30.f, 8.f), mon_f = Smooth("cal_mon", (g_snap.month - 1 + day_f) / 12.f, 8.f);
    Ring(dl, rc, 104 * g_S, 11 * g_S, mon_f, Acc2(), Acc());
    Ring(dl, rc, 80 * g_S, 11 * g_S, day_f, Acc(), Acc2());
    Txt(dl, g_font_num, 0.5f, rc - ImVec2(0, 24 * g_S), kText, date, 1);
    TxtF(dl, g_font_body, 0.74f, rc + ImVec2(0, 14 * g_S), kDim, 1, "外环 %u/12 月", g_snap.month);
    TxtF(dl, g_font_body, 0.74f, rc + ImVec2(0, 32 * g_S), kDim, 1, "内环 %u/30 日", g_snap.day);
    // perf sparklines
    auto spark = [&](const std::deque<float>& d, float y, const char* label, ImU32 col, float vmax, const char* unit) {
        const ImVec2 sa(ra.x + 24 * g_S, y), sb(b.x - 24 * g_S, y + 52 * g_S);
        Txt(dl, g_font_bold, 0.62f, sa - ImVec2(0, 16 * g_S), Al(col, 0.9f), label);
        dl->AddRectFilled(sa, sb, C(255, 255, 255, 8), 8 * g_S);
        if (d.size() > 2) {
            std::vector<ImVec2> pts;
            for (size_t k = 0; k < d.size(); ++k)
                pts.push_back(ImVec2(sa.x + (sb.x - sa.x) * k / (float)(d.size() - 1), sb.y - 4 * g_S - std::min(d[k] / vmax, 1.f) * (sb.y - sa.y - 8 * g_S)));
            AreaFill(dl, pts.data(), (int)pts.size(), sb.y, Al(col, 0.3f), Al(col, 0.f));
            dl->AddPolyline(pts.data(), (int)pts.size(), col, 0, 1.8f * g_S);
            TxtF(dl, g_font_body, 0.8f, ImVec2(sb.x - 8 * g_S, sa.y + 4 * g_S), kText, 2, "%.1f %s", d.back(), unit);
        }
    };
    spark(g_frame_ms, ra.y + 340 * g_S, "FRAME TIME", Acc(), 40.f, "ms");
    spark(g_tick_rate, ra.y + 340 * g_S + 82 * g_S, "TURN TICKS / SECOND", Acc2(), 120.f, "tick/s");
}

void TabScript(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const float gap = 16 * g_S, cw = (b.x - a.x - gap) * 0.5f, ch = 150 * g_S;
    if (g_T - g_button_eval_time > 0.5 && g_tick_depth == 0) {
        EvalButtons();
        g_button_eval_time = g_T;
    }
    for (int i = 0; i < 4; ++i) {
        const ButtonDef& d = kButtons[i];
        const ButtonState& st = g_button_state[i];
        const ImVec2 ca(a.x + (i % 2) * (cw + gap), a.y + (i / 2) * (ch + gap)), cb(ca.x + cw, ca.y + ch);
        const ImU32 col = !st.found ? kDim : (st.valid ? kGood : kBad);
        Panel(dl, ca, cb, 16 * g_S, col);
        Txt(dl, g_font_bold, 0.62f, ImVec2(ca.x + 20 * g_S, ca.y + 16 * g_S), Al(Acc(), 0.85f), d.key);
        Txt(dl, g_font_bold, 1.05f, ImVec2(ca.x + 20 * g_S, ca.y + 30 * g_S), kText, d.title);
        // wrapped description
        dl->AddText(F(g_font_body), F(g_font_body)->FontSize * 0.86f * g_fit, ImVec2(ca.x + 20 * g_S, ca.y + 58 * g_S), kDim, d.desc, nullptr, cw - 150 * g_S);
        // state chip
        const char* chip = !st.found ? "未加载" : (st.valid ? "可执行" : "不可用");
        const ImVec2 sz = TextSz(g_font_bold, 0.8f, chip);
        const ImVec2 pa(ca.x + 20 * g_S, cb.y - 36 * g_S), pb(pa.x + sz.x + 22 * g_S, pa.y + 22 * g_S);
        dl->AddRectFilled(pa, pb, Al(col, 0.18f), 11 * g_S);
        dl->AddCircleFilled(ImVec2(pa.x + 11 * g_S, (pa.y + pb.y) * 0.5f), 3.f * g_S, col, 10);
        Txt(dl, g_font_bold, 0.8f, ImVec2(pa.x + 19 * g_S, pa.y + 3 * g_S), col, chip);
        if (!st.valid && !st.reason.empty()) {
            std::string r = st.reason;
            for (char& ch2 : r)
                if (ch2 == '\n') ch2 = ' ';
            dl->AddText(F(g_font_body), F(g_font_body)->FontSize * 0.74f * g_fit, ImVec2(pb.x + 10 * g_S, pa.y + 3 * g_S), kDim, r.c_str(), nullptr,
                        cb.x - pb.x - 160 * g_S);
        }
        // run button
        const ImVec2 ba(cb.x - 112 * g_S, cb.y - 44 * g_S), bb(cb.x - 18 * g_S, cb.y - 14 * g_S);
        char id[24];
        snprintf(id, sizeof(id), "run%d", i);
        const bool hov = st.valid && ImGui::IsMouseHoveringRect(ba, bb) && ImGui::IsWindowHovered();
        if (Hit(ba, bb, id) && st.valid) g_pending.push_back({ Pending::Button, 0, d.key });
        const float hv = Smooth(id, hov ? 1.f : 0.f);
        if (st.valid) {
            dl->AddRectFilledMultiColor(ba, bb, Acc(), Acc2(), Acc2(), Acc());
            if (hv > 0.01f) Glow(dl, (ba + bb) * 0.5f, 54 * g_S, Acc(), 0.3f * hv);
            Txt(dl, g_font_bold, 0.95f, (ba + bb) * 0.5f - ImVec2(10 * g_S, 9 * g_S), C(8, 12, 28, 255), "执行", 1);
            const ImVec2 ar = ImVec2((ba.x + bb.x) * 0.5f + 22 * g_S, (ba.y + bb.y) * 0.5f);
            dl->AddTriangleFilled(ar + ImVec2(-4 * g_S, -6 * g_S), ar + ImVec2(-4 * g_S, 6 * g_S), ar + ImVec2(5 * g_S, 0), C(8, 12, 28, 255));
        } else {
            dl->AddRectFilled(ba, bb, C(255, 255, 255, 14), 8 * g_S);
            Txt(dl, g_font_bold, 0.95f, (ba + bb) * 0.5f - ImVec2(10 * g_S, 9 * g_S), C(255, 255, 255, 70), "执行", 1);
            const ImVec2 ar = ImVec2((ba.x + bb.x) * 0.5f + 22 * g_S, (ba.y + bb.y) * 0.5f);
            dl->AddTriangleFilled(ar + ImVec2(-4 * g_S, -6 * g_S), ar + ImVec2(-4 * g_S, 6 * g_S), ar + ImVec2(5 * g_S, 0), C(255, 255, 255, 70));
        }
    }
    // log + path explainer
    const float y = a.y + 2 * (ch + gap);
    const ImVec2 la(a.x, y), lb(b.x, b.y);
    Panel(dl, la, lb, 16 * g_S, Acc2());
    CardTitle(dl, la, "COMMAND LOG", "执行记录");
    const char* flow = "ImGui 按钮  →  引擎工厂建命令  →  命令自带 IsValid  →  PostCommandToSession  →  下一 tick 锁步执行  →  脚本 effect";
    Txt(dl, g_font_body, 0.8f, ImVec2(lb.x - 18 * g_S, la.y + 20 * g_S), Al(Acc(), 0.8f), flow, 2);
    float ly = la.y + 66 * g_S;
    if (g_script_log.empty()) Txt(dl, g_font_body, 0.9f, ImVec2(la.x + 20 * g_S, ly), kDim, "还没有执行过。点上面的「执行」，这里会显示引擎的返回值和国库变化。");
    int shown = 0;
    for (const ScriptLogEntry& e : g_script_log) {
        if (ly + 22 * g_S > lb.y - 10 * g_S || shown++ >= 6) break;
        const ImU32 col = !e.ok ? kBad : (e.done ? kGood : kWarn);
        dl->AddCircleFilled(ImVec2(la.x + 28 * g_S, ly + 10 * g_S), 4 * g_S, col, 10);
        TxtF(dl, g_font_bold, 0.9f, ImVec2(la.x + 44 * g_S, ly), kText, 0, "%s", e.title.c_str());
        std::string res = e.ok ? (e.done ? "已执行" : "已入队，等待 tick") : e.result;
        if (e.ok && e.done && e.energy_known && fabs(e.energy_after - e.energy_before) > 1e-6) {
            char t[64];
            snprintf(t, sizeof(t), "  ·  能量币 %.1f → %.1f", e.energy_before, e.energy_after);
            res += t;
        }
        dl->AddText(F(g_font_body), F(g_font_body)->FontSize * 0.86f * g_fit, ImVec2(la.x + 200 * g_S, ly + 1), col, res.c_str());
        TxtF(dl, g_font_body, 0.76f, ImVec2(lb.x - 18 * g_S, ly + 2), kDim, 2, "%.0fs 前", g_T - e.time);
        ly += 28 * g_S;
    }
}

void TabSettings(ImDrawList* dl, ImVec2 a, ImVec2 b, const ImGuiIO& io) {
    const float gap = 16 * g_S, lw = (b.x - a.x - gap) * 0.5f;
    Panel(dl, a, ImVec2(a.x + lw, b.y), 16 * g_S, Acc());
    CardTitle(dl, a, "APPEARANCE", "外观");
    for (int i = 0; i < 4; ++i) {
        const ImVec2 sa(a.x + 20 * g_S, a.y + 76 * g_S + i * 58 * g_S), sb(a.x + lw - 20 * g_S, sa.y + 48 * g_S);
        char id[16];
        snprintf(id, sizeof(id), "theme%d", i);
        const bool hov = ImGui::IsMouseHoveringRect(sa, sb) && ImGui::IsWindowHovered();
        if (Hit(sa, sb, id)) g_theme = i;
        const float sel = Smooth(id, g_theme == i ? 1.f : 0.f), hv = Smooth((std::string(id) + "h").c_str(), hov ? 1.f : 0.f);
        dl->AddRectFilled(sa, sb, C(255, 255, 255, (int)(10 + 14 * hv + 12 * sel)), 12 * g_S);
        dl->AddRectFilledMultiColor(sa + ImVec2(14 * g_S, 12 * g_S), ImVec2(sa.x + 118 * g_S, sb.y - 12 * g_S), kThemes[i].a, kThemes[i].b, kThemes[i].b, kThemes[i].a);
        Txt(dl, g_font_bold, 0.95f, ImVec2(sa.x + 134 * g_S, sa.y + 14 * g_S), kText, kThemes[i].name);
        if (sel > 0.02f) dl->AddRect(sa, sb, Al(kThemes[i].a, sel), 12 * g_S, 0, 1.6f * g_S);
    }
    // toggles
    struct T {
        const char* label;
        bool* v;
    } toggles[2] = { { "星空与流星背景", &g_stars }, { "底部状态胶囊", &g_hud_open } };
    for (int i = 0; i < 2; ++i) {
        const ImVec2 ta(a.x + 20 * g_S, a.y + 76 * g_S + (4 + i) * 58 * g_S), tb(a.x + lw - 20 * g_S, ta.y + 44 * g_S);
        char id[16];
        snprintf(id, sizeof(id), "tog%d", i);
        if (Hit(ta, tb, id)) *toggles[i].v = !*toggles[i].v;
        const float on = Smooth(id, *toggles[i].v ? 1.f : 0.f, 14.f);
        Txt(dl, g_font_body, 0.95f, ImVec2(ta.x + 4 * g_S, ta.y + 10 * g_S), kText, toggles[i].label);
        const ImVec2 sa(tb.x - 54 * g_S, ta.y + 8 * g_S), sb(tb.x - 4 * g_S, ta.y + 32 * g_S);
        dl->AddRectFilled(sa, sb, Mix(C(255, 255, 255, 30), Acc(), on), 12 * g_S);
        dl->AddCircleFilled(ImVec2(sa.x + 12 * g_S + (sb.x - sa.x - 24 * g_S) * on, (sa.y + sb.y) * 0.5f), 8.5f * g_S, kText, 16);
    }

    const ImVec2 ra(a.x + lw + gap, a.y);
    Panel(dl, ra, b, 16 * g_S, Acc2());
    CardTitle(dl, ra, "RUNTIME", "运行信息");
    struct L {
        const char* k;
        char v[96];
    } lines[8] = {};
    int n = 0;
    auto add = [&](const char* k, const char* fmt, auto... args) {
        lines[n].k = k;
        snprintf(lines[n].v, sizeof(lines[n].v), fmt, args...);
        ++n;
    };
    add("Dear ImGui", "%s（引擎自带，本 DLL 另编一份同版本）", ImGui::GetVersion());
    add("ImGui 上下文", "%p（与引擎共用）", (void*)ImGui::GetCurrentContext());
    add("显示区域", "%.0f × %.0f   UI 缩放 %.2f", io.DisplaySize.x, io.DisplaySize.y, g_S);
    add("绘制规模", "%d 顶点  ·  %d 索引  ·  %d 个窗口", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderWindows);
    add("帧回调", "%lld 次，其中 %.0f%% 发生在回合 tick 内", (long long)g_frames_total, g_frames_total ? 100.0 * g_frames_in_tick / g_frames_total : 0.0);
    add("数据读取", "仅在 tick 之间取快照（第 %lld 次 tick）", (long long)g_ticks);
    add("输入", "WantCaptureMouse=%d  WantCaptureKeyboard=%d", (int)io.WantCaptureMouse, (int)io.WantCaptureKeyboard);
    add("快捷键", "Ctrl + Shift + G  显示 / 隐藏面板");
    for (int i = 0; i < n; ++i) {
        const float y = ra.y + 74 * g_S + i * 46 * g_S;
        Txt(dl, g_font_bold, 0.62f, ImVec2(ra.x + 22 * g_S, y), Al(Acc(), 0.8f), lines[i].k);
        Txt(dl, g_font_body, 0.92f, ImVec2(ra.x + 22 * g_S, y + 14 * g_S), kText, lines[i].v);
    }
}

void DrawDeck(const ImGuiIO& io) {
    const float W = std::min(1180 * g_S, io.DisplaySize.x - 30), H = std::min(700 * g_S, io.DisplaySize.y - 130 * g_S);
    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f - 40 * g_S), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##sc_deck", &g_deck_open, fl)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetWindowPos(), b(a.x + W, a.y + H);
        const float R = 22 * g_S;
        for (int i = 8; i >= 1; --i) dl->AddRectFilled(a - ImVec2(i * 3.f, i * 3.f - 10), b + ImVec2(i * 3.f, i * 3.f + 10), C(0, 0, 0, 9), R + i * 3.f);
        dl->AddRectFilled(a, b, C(6, 9, 20, 247), R);
        dl->PushClipRect(a, b, true);
        Glow(dl, ImVec2(b.x - W * 0.12f, a.y + H * 0.2f), 460 * g_S, Acc2(), 0.34f);
        Glow(dl, ImVec2(a.x + W * 0.22f, b.y - H * 0.05f), 420 * g_S, Acc(), 0.26f);
        if (g_stars) Stars(dl, a, b, g_T);
        dl->PopClipRect();
        dl->AddRect(a, b, C(255, 255, 255, 34), R, 0, 1.f);
        dl->AddRectFilledMultiColor(ImVec2(a.x + R, a.y), ImVec2(b.x - R, a.y + 2.5f * g_S), Acc(), Acc2(), Acc2(), Acc());

        // header
        Txt(dl, g_font_bold, 0.62f, ImVec2(a.x + 100 * g_S, a.y + 18 * g_S), Al(Acc(), 0.9f), "ASTRAL COMMAND  ·  v0.1 PREVIEW");
        Txt(dl, g_font_title, 1.f, ImVec2(a.x + 100 * g_S, a.y + 30 * g_S), kText, "Command Deck");
        Txt(dl, g_font_body, 0.9f, ImVec2(a.x + 100 * g_S + TextSz(g_font_title, 1.f, "Command Deck").x + 14 * g_S, a.y + 40 * g_S), kDim, "星图指挥台");
        // close
        const ImVec2 cc(b.x - 36 * g_S, a.y + 40 * g_S);
        const bool chov = ImGui::IsMouseHoveringRect(cc - ImVec2(16 * g_S, 16 * g_S), cc + ImVec2(16 * g_S, 16 * g_S)) && ImGui::IsWindowHovered();
        if (Hit(cc - ImVec2(16 * g_S, 16 * g_S), cc + ImVec2(16 * g_S, 16 * g_S), "close")) g_deck_open = false;
        const float chv = Smooth("closeh", chov ? 1.f : 0.f);
        dl->AddCircleFilled(cc, 16 * g_S, C(255, 255, 255, (int)(14 + 40 * chv)), 24);
        dl->AddLine(cc - ImVec2(5 * g_S, 5 * g_S), cc + ImVec2(5 * g_S, 5 * g_S), kText, 1.8f * g_S);
        dl->AddLine(cc - ImVec2(-5 * g_S, 5 * g_S), cc + ImVec2(-5 * g_S, 5 * g_S), kText, 1.8f * g_S);

        // rail
        const char* names[5] = { "总览", "经济", "时间", "脚本", "设置" };
        const char* en[5] = { "OVERVIEW", "ECONOMY", "TIME", "SCRIPT", "SETUP" };
        const float rail_x = a.x + 18 * g_S, item_h = 82 * g_S, top = a.y + 96 * g_S;
        const float ind_y = Smooth("rail", (float)g_tab, 9.f);
        dl->AddRectFilledMultiColor(ImVec2(rail_x - 18 * g_S, top + ind_y * item_h + 10 * g_S), ImVec2(rail_x - 14 * g_S, top + ind_y * item_h + item_h - 14 * g_S), Acc(), Acc(),
                                    Acc2(), Acc2());
        for (int i = 0; i < 5; ++i) {
            const ImVec2 ia(rail_x, top + i * item_h), ib(rail_x + 62 * g_S, ia.y + item_h - 6 * g_S);
            char id[16];
            snprintf(id, sizeof(id), "tab%d", i);
            const bool hov = ImGui::IsMouseHoveringRect(ia, ib) && ImGui::IsWindowHovered();
            if (Hit(ia, ib, id)) g_tab = i;
            const float act = Smooth(id, g_tab == i ? 1.f : 0.f, 10.f), hv = Smooth((std::string(id) + "h").c_str(), hov ? 1.f : 0.f);
            dl->AddRectFilled(ia, ib, Al(Mix(Acc(), Acc2(), i / 4.f), 0.16f * act + 0.06f * hv), 14 * g_S);
            const ImU32 col = Mix(kDim, kText, std::max(act, hv * 0.7f));
            TabIcon(dl, i, ImVec2((ia.x + ib.x) * 0.5f, ia.y + 26 * g_S), 11 * g_S, Mix(col, Mix(Acc(), Acc2(), i / 4.f), act));
            Txt(dl, g_font_bold, 0.7f, ImVec2((ia.x + ib.x) * 0.5f, ia.y + 47 * g_S), col, names[i], 1);
            Txt(dl, g_font_body, 0.5f, ImVec2((ia.x + ib.x) * 0.5f, ia.y + 60 * g_S), Al(col, 0.6f), en[i], 1);
        }

        const ImVec2 ca(a.x + 100 * g_S, a.y + 82 * g_S), cb(b.x - 24 * g_S, b.y - 24 * g_S);
        if (!g_snap.in_game && g_tab != 4) {
            Txt(dl, g_font_body, 1.2f, (ca + cb) * 0.5f, kDim, "未进入游戏，没有可显示的数据", 1);
        } else {
            switch (g_tab) {
            case 0: TabOverview(dl, ca, cb); break;
            case 1: TabEconomy(dl, ca, cb); break;
            case 2: TabTime(dl, ca, cb); break;
            case 3: TabScript(dl, ca, cb); break;
            default: TabSettings(dl, ca, cb, io); break;
            }
        }
        ImGui::SetCursorScreenPos(a);
        ImGui::Dummy(ImVec2(W, H));
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ------------------------------------------------------------------------------------------------------------ frame entry points
bool RunConsole(const char* line);
void PollCommandFile() {
    static int n = 0;
    if (++n % 20) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sgui_showcase.cmd", g_dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char cmd[32] = {}, arg[128] = {};
        if (sscanf(line, "%31s %127s", cmd, arg) < 1) continue;
        const int iv = atoi(arg);
        if (!strcmp(cmd, "deck")) g_deck_open = iv != 0;
        else if (!strcmp(cmd, "hud")) g_hud_open = iv != 0;
        else if (!strcmp(cmd, "tab")) g_tab = std::clamp(iv, 0, 4);
        else if (!strcmp(cmd, "theme")) g_theme = std::clamp(iv, 0, 3);
        else if (!strcmp(cmd, "res")) g_sel_res = iv;
        else if (!strcmp(cmd, "speed")) g_pending.push_back({ Pending::Speed, iv, {} });
        else if (!strcmp(cmd, "pause")) g_pending.push_back({ Pending::Pause, iv, {} });
        else if (!strcmp(cmd, "post")) g_pending.push_back({ Pending::Button, 0, arg });
        else if (!strcmp(cmd, "console")) {  // "console imgui hide": one short console line, run as if typed (not for `imgui off`: that must not run mid-frame)
            std::string rest(line);
            rest.erase(0, rest.find(' ') == std::string::npos ? rest.size() : rest.find(' ') + 1);
            while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r')) rest.pop_back();
            Log("console '%s' -> %d", rest.c_str(), (int)RunConsole(rest.c_str()));
        } else if (!strcmp(cmd, "dump")) {
            {
                ImGuiIO& dio = ImGui::GetIO();
                ImDrawData* dd = ImGui::GetDrawData();
                Log("imgui: ctx %p frame %d display %.0fx%.0f windows %d active %d vtx %d idx %d drawdata %s cmdlists %d totalvtx %d capture mouse %d kbd %d | ui: hud %d deck %d tab %d S %.2f fit %.2f fonts_ctx %p",
                    (void*)ImGui::GetCurrentContext(), ImGui::GetFrameCount(), dio.DisplaySize.x, dio.DisplaySize.y, dio.MetricsRenderWindows, dio.MetricsActiveWindows,
                    dio.MetricsRenderVertices, dio.MetricsRenderIndices, dd ? (dd->Valid ? "valid" : "invalid") : "null", dd ? dd->CmdListsCount : -1,
                    dd ? dd->TotalVtxCount : -1, (int)dio.WantCaptureMouse, (int)dio.WantCaptureKeyboard, (int)g_hud_open, (int)g_deck_open, g_tab, g_S, g_fit,
                    (void*)g_fonts_ctx);
            }
            Log("dump: in_game=%d date=%04u.%02u.%02u speed=%u paused=%d country=%u name='%s' colonies=%u pops=%u size=%d mil=%.1f/%.1f tech=%.1f/%.1f eco=%.1f/%.1f",
                (int)g_snap.in_game, g_snap.year, g_snap.month, g_snap.day, g_snap.speed, (int)g_snap.paused, g_snap.country_id, g_snap.name.c_str(), g_snap.colonies,
                g_snap.pops, g_snap.empire_size, g_snap.mil, g_snap.mil_max, g_snap.tech, g_snap.tech_max, g_snap.eco, g_snap.eco_max);
            for (const auto& r : g_snap.res)
                if (fabs(r.stock) > 0 || fabs(r.net) > 0)
                    Log("  %-22s stock %12.2f  income %9.2f  expense %9.2f  net %9.2f  max %.0f", r.key.c_str(), r.stock, r.income, r.expense, r.net, r.max);
            for (size_t i = 0; i < 4; ++i)
                Log("  button %-22s found=%d valid=%d reason='%s'", kButtons[i].key, (int)g_button_state[i].found, (int)g_button_state[i].valid,
                    g_button_state[i].reason.c_str());
            for (const auto& e : g_script_log)
                Log("  log %-22s ok=%d done=%d '%s' energy %.2f -> %.2f", e.key.c_str(), (int)e.ok, (int)e.done, e.result.c_str(), e.energy_before, e.energy_after);
        } else if (!strcmp(cmd, "eval")) {
            EvalButtons();
            g_button_eval_time = g_T;
        }
    }
    fclose(f);
    DeleteFileA(path);
}

bool g_hotkey_down = false;
void PollHotkey() {
    const bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState('G') & 0x8000);
    if (down && !g_hotkey_down) {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId()) {
            g_deck_open = !g_deck_open;
            g_hud_open = g_deck_open || g_hud_open;
        }
    }
    g_hotkey_down = down;
}

void UpdateTickRate() {
    static LONG64 last_ticks = 0;
    static double last_t = 0;
    if (g_T - last_t >= 0.5) {
        Push(g_tick_rate, (float)((g_ticks - last_ticks) / (g_T - last_t)), 120);
        last_ticks = g_ticks;
        last_t = g_T;
    }
}

void AddFontsToContext();
void FixPlatformWindowHandle(ImGuiIO& io);

void Frame() {
    ImGuiContext* ctx = *(ImGuiContext**)(g_base + kGImGui);
    if (!ctx) return;
    if (!g_allocators_set) {
        // Memory this copy of ImGui allocates inside the shared context is freed by the engine's copy (and the reverse): use the engine's allocators.
        ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc) * (void**)(g_base + kAllocFunc), (ImGuiMemFreeFunc) * (void**)(g_base + kFreeFunc),
                                     *(void**)(g_base + kAllocUser));
        g_allocators_set = true;
    }
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the engine would write imgui.ini into the game folder; every frame, as a restarted context can reuse the address
    if (!io.ImeWindowHandle) FixPlatformWindowHandle(io);  // a context started while the game window was inactive (see FixPlatformWindowHandle)
    static ImGuiContext* seen = nullptr;
    if (ctx != seen) {
        seen = ctx;
        Log("engine context %p, ImGui %s, fonts %s", (void*)ctx, ImGui::GetVersion(), g_fonts_ctx == ctx ? "ours" : "default only");
    }
    {
        static float last_w = -2, last_h = -2;
        if (io.DisplaySize.x != last_w || io.DisplaySize.y != last_h) {
            Log("display size %.0fx%.0f -> %.0fx%.0f (frame %d, ctx %p)", last_w, last_h, io.DisplaySize.x, io.DisplaySize.y, ImGui::GetFrameCount(), (void*)ctx);
            last_w = io.DisplaySize.x;
            last_h = io.DisplaySize.y;
        }
    }
    g_T = ImGui::GetTime();
    g_DT = std::clamp(io.DeltaTime, 0.001f, 0.1f);
    g_fit = std::clamp(std::min(io.DisplaySize.x / (1230.f * g_S0), io.DisplaySize.y / (840.f * g_S0)), 0.55f, 1.f);
    g_S = g_S0 * g_fit;
    Push(g_frame_ms, io.DeltaTime * 1000.f, 120);
    UpdateTickRate();
    PollCommandFile();
    PollHotkey();
    if (g_tick_depth == 0) {
        RunPending();
        static LONG64 last_frame = 0;
        if (g_snap.tick != g_ticks || g_frames_total - last_frame >= 90 || (g_force_snapshot_frame && g_frames_total >= g_force_snapshot_frame)) {
            g_force_snapshot_frame = 0;
            last_frame = g_frames_total;
            TakeSnapshot();
            // fill in the outcome of posted script commands once a tick has run
            for (auto& e : g_script_log)
                if (e.ok && !e.done && g_snap.serial > e.serial_posted) {
                    e.done = true;
                    if (const ResInfo* en = FindRes("energy")) e.energy_after = en->stock;
                }
        }
    }

    // Our style only for our windows: the engine's own ImGui views keep theirs.
    ImGui::PushStyleColor(ImGuiCol_PopupBg, C(9, 12, 26, 240));
    ImGui::PushStyleColor(ImGuiCol_Border, C(255, 255, 255, 40));
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f * g_S);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * g_S, 10 * g_S));
    ImGui::PushFont(F(g_font_body));
    if (g_hud_open) DrawHud(io);
    if (g_deck_open) DrawDeck(io);
    ImGui::PopFont();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
}

void NewFrameDetour() {
    InterlockedIncrement(&g_in_detour);
    InterlockedIncrement64(&g_frames_total);
    if (g_tick_depth > 0) InterlockedIncrement64(&g_frames_in_tick);
    g_orig_new_frame();  // the engine's frame is open; the engine's own views are updated after this returns
    __try {
        Frame();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 3) Log("exception 0x%08lX in Frame", (unsigned long)GetExceptionCode());
    }
    InterlockedDecrement(&g_in_detour);
}

void AutoStart();
void TickDetour(void* gs, void* cmds) {
    if (g_tick_depth == 0) AutoStart();
    InterlockedIncrement(&g_tick_depth);
    g_orig_tick(gs, cmds);
    InterlockedDecrement(&g_tick_depth);
    InterlockedIncrement64(&g_ticks);
}

// ---- fonts: added right after the engine creates its context, before its first NewFrame builds the atlas
bool FileExists(const char* p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

// The engine's ImGuiInit takes the window for its Win32 backend from GetActiveWindow(), which is null while the game window is not the
// active one (alt-tabbed away, or a DLL reloaded from outside). It then stores 0 as io.ImeWindowHandle and in the backend data, and from
// then on the engine skips the platform NewFrame (it is guarded by io.ImeWindowHandle != 0): DisplaySize stays -1 and nothing is drawn.
// Fills both in from the process's own main window.
HWND FindGameWindow() {
    struct Ctx {
        DWORD pid;
        HWND found;
    } c{ GetCurrentProcessId(), nullptr };
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* ctx = (Ctx*)p;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            RECT r;
            if (pid == ctx->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && GetClientRect(h, &r) && r.right > 200 && r.bottom > 200) {
                ctx->found = h;
                return FALSE;
            }
            return TRUE;
        },
        (LPARAM)&c);
    return c.found;
}
void FixPlatformWindowHandle(ImGuiIO& io) {
    if (io.ImeWindowHandle) return;
    HWND hwnd = FindGameWindow();
    void** backend = (void**)io.BackendPlatformUserData;  // imgui_impl_win32 data: hWnd is its first member
    if (!hwnd || !backend) return;
    io.ImeWindowHandle = hwnd;
    if (!*backend) *backend = hwnd;
    Log("the engine's ImGui had no window handle (game window was not active when it started): set it to %p", (void*)hwnd);
}

void AddFontsToContext() {
    ImGuiContext* ctx = *(ImGuiContext**)(g_base + kGImGui);
    if (!ctx) return;
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc) * (void**)(g_base + kAllocFunc), (ImGuiMemFreeFunc) * (void**)(g_base + kFreeFunc), *(void**)(g_base + kAllocUser));
    g_allocators_set = true;
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    FixPlatformWindowHandle(io);
    if (io.Fonts->IsBuilt()) {
        Log("atlas already built: keeping the default font");
        return;
    }
    char win[MAX_PATH], seg[MAX_PATH], segb[MAX_PATH], yh[MAX_PATH], yhb[MAX_PATH], bahn[MAX_PATH];
    GetWindowsDirectoryA(win, MAX_PATH);
    snprintf(seg, MAX_PATH, "%s\\Fonts\\segoeui.ttf", win);
    snprintf(segb, MAX_PATH, "%s\\Fonts\\segoeuib.ttf", win);
    snprintf(yh, MAX_PATH, "%s\\Fonts\\msyh.ttc", win);
    snprintf(yhb, MAX_PATH, "%s\\Fonts\\msyhbd.ttc", win);
    snprintf(bahn, MAX_PATH, "%s\\Fonts\\bahnschrift.ttf", win);
    const float s = g_S0;
    // Latin plus the arrows / geometric shapes / dashes the UI uses; the CJK face adds the common Chinese set on top.
    static const ImWchar extra[] = { 0x2010, 0x2027, 0x2190, 0x21FF, 0x2212, 0x221A, 0x25A0, 0x25FF, 0 };
    static ImVector<ImWchar> latin_ranges, cjk_ranges;
    latin_ranges.clear();
    cjk_ranges.clear();
    {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddRanges(extra);
        b.BuildRanges(&latin_ranges);
        ImFontGlyphRangesBuilder c;
        c.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        c.AddRanges(extra);
        c.AddText(kUiText);
        c.BuildRanges(&cjk_ranges);
    }
    ImFontConfig base;
    base.OversampleH = 2;
    base.OversampleV = 1;
    auto face = [&](const char* latin, const char* cjk, float px, bool with_cjk) -> ImFont* {
        ImFont* f = FileExists(latin) ? io.Fonts->AddFontFromFileTTF(latin, px, &base, latin_ranges.Data) : io.Fonts->AddFontDefault();
        if (with_cjk && FileExists(cjk)) {
            ImFontConfig m = base;
            m.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(cjk, px, &m, cjk_ranges.Data);
        }
        return f;
    };
    // The first font added becomes io.FontDefault, which is what the engine's own ImGui views use: keep ImGui's built-in font in that slot so they
    // look as before, and select ours explicitly (PushFont) for our windows.
    io.Fonts->AddFontDefault();
    g_font_body = face(seg, yh, 17.f * s, true);
    g_font_bold = face(segb, yhb, 17.f * s, true);
    g_font_title = face(segb, yhb, 30.f * s, false);
    g_font_num = face(bahn, bahn, 46.f * s, false);
    g_font_num_s = face(bahn, bahn, 26.f * s, false);
    g_fonts_ctx = ctx;
    Log("fonts added to context %p (scale %.2f, %d fonts)", (void*)ctx, s, io.Fonts->Fonts.Size);
}

void ImGuiInitDetour() {
    g_orig_imgui_init();
    __try {
        AddFontsToContext();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("exception 0x%08lX while adding fonts", (unsigned long)GetExceptionCode());
    }
}

bool CallRunCommandNow(void* console, RawCStr* cmd) {
    __try {
        ((void (*)(void*, const void*))(g_base + sdk::fn::CConsole_RunCommandNow))(console, cmd);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Runs one console line as if typed into the in-game console. A line longer than the CString's inline buffer (15 characters) lives in a block
// from the engine's allocator, like command string payloads; RunCommandNow only reads it.
bool RunConsole(const char* line) {
    const uintptr_t console = RdOr<uintptr_t>(g_base + sdk::glob::CConsole_pInstance, 0);
    const size_t n = strlen(line);
    if (console < 0x10000 || n == 0 || n > 255) return false;
    RawCStr cmd{};
    if (n <= 15) {
        memcpy(cmd.s.buf, line, n);
        cmd.s.cap = 15;
    } else {
        char* heap = (char*)((void* (*)(size_t))(g_base + sdk::kRvaEngineAlloc))(n + 1);
        if (!heap) return false;
        memcpy(heap, line, n + 1);
        cmd.s.ptr = heap;
        cmd.s.cap = n;
    }
    cmd.s.size = n;
    const bool ok = CallRunCommandNow((void*)console, &cmd);
    if (cmd.s.cap > 15) CallFreeCString(&cmd);
    return ok;
}

// ---- turnkey start: once a game is running and has ticked a while, start the engine's ImGui ourselves (what `imgui on` does).
// Runs from the HandleTurnTick hook, i.e. on the main thread between ticks. The Present vtable slot is deliberately not used: the Steam
// overlay re-patches that shared slot whenever a swap chain is created, which turns a second slot hook into endless recursion.
void AutoStart() {
    static bool done = false;
    if (done || g_ticks < 30) return;
    if (RdOr<uintptr_t>(g_base + sdk::glob::g_CurrentInGameIdler, 0) < 0x10000 || RdOr<uintptr_t>(g_base + sdk::glob::g_CurrentGameState, 0) < 0x10000) return;
    done = true;
    __try {
        void* ctx = *(void**)(g_base + kGImGui);
        if (ctx && (void*)g_fonts_ctx == ctx) return;  // already ours
        char flag[MAX_PATH];
        snprintf(flag, sizeof(flag), "%snorestart", g_dir);
        if (ctx && FileExists(flag)) {  // diagnostics: look at a running context as it is
            Log("engine ImGui already running (%p), 'norestart' present: leaving it alone (default font only)", ctx);
            return;
        }
        if (ctx) {
            // ImGui was started before this DLL (`imgui on` by hand, or an earlier copy of this DLL): its atlas is built and cannot take our
            // fonts any more, so let the engine tear it down and start it again; ImGuiInit goes through our detour.
            Log("engine ImGui already running (%p): restarting it to get our fonts into the atlas", ctx);
            RunConsole("imgui off");
            RunConsole("imgui on");
        } else {
            Log("starting the engine's ImGui from the tick hook");
            ((void (*)())(g_base + kImGuiInit))();  // goes through our detour, which adds the fonts
        }
        Log("context now %p", *(void**)(g_base + kGImGui));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("exception in ImGuiInit");
    }
}

DWORD WINAPI Worker(LPVOID) {
    GetModuleFileNameA(g_module, g_dir, MAX_PATH);
    strcpy(strrchr(g_dir, '\\') + 1, "");
    char logp[MAX_PATH];
    snprintf(logp, sizeof(logp), "%sgui_showcase.log", g_dir);
    g_log = fopen(logp, "a");
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(g_base + ((const IMAGE_DOS_HEADER*)g_base)->e_lfanew);
    Log("loaded, base 0x%llX, exe timestamp 0x%08X", (unsigned long long)g_base, nt->FileHeader.TimeDateStamp);
    g_S0 = std::clamp(GetSystemMetrics(SM_CYSCREEN) / 1080.f, 1.f, 2.f);
    if (nt->FileHeader.TimeDateStamp != sdk::kExeTimestamp) {
        Log("wrong game build (SDK 0x%08X), not hooking", sdk::kExeTimestamp);
    } else if (MH_Initialize() == MH_OK && MH_CreateHook((void*)(g_base + kNewFrame), (void*)&NewFrameDetour, (void**)&g_orig_new_frame) == MH_OK &&
               MH_CreateHook((void*)(g_base + kImGuiInit), (void*)&ImGuiInitDetour, (void**)&g_orig_imgui_init) == MH_OK &&
               MH_CreateHook((void*)(g_base + sdk::fn::CGameState_HandleTurnTick), (void*)&TickDetour, (void**)&g_orig_tick) == MH_OK &&
               MH_EnableHook(MH_ALL_HOOKS) == MH_OK) {
        Log("hooked NewFrame, ImGuiInit and HandleTurnTick");
    } else {
        Log("hook failed");
    }
    WaitForSingleObject(g_unload_event, INFINITE);
    MH_DisableHook(MH_ALL_HOOKS);
    for (int i = 0; i < 300 && (g_in_detour || g_tick_depth); ++i) Sleep(10);
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
        wsprintfA(name, "Local\\gui_showcase_unload_%lu", GetCurrentProcessId());
        g_unload_event = CreateEventA(nullptr, TRUE, FALSE, name);
        if (!g_unload_event) return FALSE;
        HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
