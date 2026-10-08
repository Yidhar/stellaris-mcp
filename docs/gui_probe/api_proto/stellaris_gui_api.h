/* Draft of the public C interface of the Stellaris GUI host plugin (prototype for docs/gui_plugin_api_investigation.md).
 *
 * Rules the interface is built on:
 *  - Pure C, no C++ types, no ownership crossing the boundary: plugins are built with their own static CRT (/MT), so memory allocated on one
 *    side is never freed on the other and nothing like std::string is passed.
 *  - Every struct starts with `uint32_t size`. The caller fills it with sizeof() of the struct it was compiled against; the receiver only reads
 *    or writes the members that fit into `size`. New members are only ever appended: an old plugin keeps working with a new host and the
 *    other way round.
 *  - Strings are UTF-8, NUL-terminated, copied by the receiver when it keeps them.
 *  - Draw callbacks run on the game's main thread, inside the engine's ImGui frame; nothing in this interface is thread-safe.
 *  - The host never calls into a plugin outside a callback the plugin registered, and drops a panel whose code is no longer mapped.
 */
#ifndef STELLARIS_GUI_API_H
#define STELLARIS_GUI_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STL_GUI_API_VERSION 1

/* ---------------------------------------------------------------------------------------- discovery
 * The host plugin's DLL exports one function. A plugin finds it with GetModuleHandleW(L"stellaris_gui.dll") + GetProcAddress, on a thread of
 * its own: plugins are loaded in playset order with no dependency between them, so the host may appear before or after the plugin. */
struct StlGuiApi;
typedef const struct StlGuiApi* (*StlGui_GetApi_fn)(uint32_t requested_version);
#define STL_GUI_EXPORT_NAME "StlGui_GetApi"

/* ---------------------------------------------------------------------------------------- what a draw callback receives */
struct StlGuiUi;

typedef struct StlGuiCallbackCtx {
    uint32_t size;
    uint32_t api_version;
    /* The engine's Dear ImGui (ImGui 1.85 as of Stellaris 4.5.2). A plugin that compiles its own copy of ImGui binds it with these two calls
     * (stellaris_gui_imgui.hpp does it): ImGui::SetAllocatorFunctions(alloc, free, user); ImGui::SetCurrentContext(imgui_context).
     * The pointer changes when the engine restarts its ImGui, so it is passed on every call. */
    void* imgui_context;
    void* imgui_alloc;
    void* imgui_free;
    void* imgui_alloc_user;
    uint32_t imgui_version_num; /* IMGUI_VERSION_NUM of the engine's ImGui, e.g. 18500 */
    uint32_t reserved0;
    /* The same drawing calls without any ImGui in the plugin (see StlGuiUi). */
    const struct StlGuiUi* ui;
    const struct StlGuiApi* api;
    /* Fonts of the host, as ImFont* of the engine's atlas (usable with ImGui::PushFont by a plugin that has its own ImGui). */
    void* font_body;
    void* font_bold;
    void* font_numbers;
    /* sizeof() of the engine's ImGui types, for a plugin with its own ImGui to compare with its own: a copy built with another imconfig.h
     * (32-bit draw indices, a custom ImDrawVert, wide ImWchar ...) has other layouts and must not touch the shared context. */
    uint32_t imgui_sizeof_io;
    uint32_t imgui_sizeof_style;
    uint32_t imgui_sizeof_drawvert;
    uint32_t imgui_sizeof_drawidx;
} StlGuiCallbackCtx;

typedef void (*StlGuiDrawFn)(const StlGuiCallbackCtx* ctx, void* user);

/* ---------------------------------------------------------------------------------------- panels */
enum {
    STL_PANEL_WINDOW = 1,  /* the host opens a window titled `title` around the callback and closes it again, whatever the callback does */
    STL_PANEL_OVERLAY = 2, /* the callback draws anything it likes (a HUD, its own windows); the host only restores ImGui's stacks afterwards */
};

typedef struct StlGuiPanelDesc {
    uint32_t size;
    uint32_t flags;      /* STL_PANEL_* */
    const char* id;      /* stable, unique: "<plugin id>.<panel>" */
    const char* title;   /* shown to the user */
    StlGuiDrawFn draw;
    void* user;
} StlGuiPanelDesc;

/* ---------------------------------------------------------------------------------------- game data (read only, snapshot taken between turn ticks) */
typedef struct StlGuiResource {
    char key[32];
    double stock;
    double net;
    double max; /* < 0: no cap */
} StlGuiResource;

typedef struct StlGuiSnapshot {
    uint32_t size;
    uint32_t in_game;
    uint32_t year, month, day;
    uint32_t speed;
    uint32_t paused;
    uint32_t player_country_id;
    uint32_t resource_count; /* entries written to resources[] */
    int64_t tick;            /* snapshot counter, changes with every turn tick */
    char country_name[96];
    StlGuiResource resources[32];
} StlGuiSnapshot;

/* ---------------------------------------------------------------------------------------- the host's function table */
typedef struct StlGuiApi {
    uint32_t size;
    uint32_t version;
    uint32_t game_exe_timestamp; /* PE TimeDateStamp of the stellaris.exe this host was built for */
    uint32_t reserved0;

    /* returns a handle > 0, or 0 when the descriptor is rejected (duplicate id, no callback) */
    int (*register_panel)(const StlGuiPanelDesc* desc);
    /* a plugin that unloads (development only: the launcher never does) unregisters first; a panel whose code is gone is dropped by the host */
    void (*unregister_panel)(int handle);

    /* fills *out up to out->size bytes; returns 1, or 0 before a game is running */
    int (*get_snapshot)(StlGuiSnapshot* out);

    /* Script effects (common/button_effects of a mod), run by the engine's own command path (valid in single player, the engine rechecks it).
     * effect_state: 1 = may run now, 0 = the engine refuses (reason, UTF-8, filled when given), -1 = unknown right now.
     * post_effect: queues it for the next safe moment; returns 1 when queued. */
    int (*effect_state)(const char* effect_key, char* reason, uint32_t reason_cap);
    int (*post_effect)(const char* effect_key);

    /* the game's own speed / pause, through the engine's setters (queued, applied between ticks) */
    void (*set_speed)(int speed);
    void (*set_paused)(int paused);

    /* one line into the host's log, prefixed with the plugin's id */
    void (*log)(const char* plugin_id, const char* utf8_line);
} StlGuiApi;

/* ---------------------------------------------------------------------------------------- drawing without ImGui (for plugins in any language)
 * Thin wrappers over the host's ImGui; only valid inside a draw callback. Colours are 0xAABBGGRR as ImGui's ImU32. */
typedef struct StlGuiUi {
    uint32_t size;
    uint32_t reserved0;
    void (*text)(const char* utf8);
    void (*text_colored)(uint32_t rgba, const char* utf8);
    int (*button)(const char* label);              /* 1 when clicked */
    int (*checkbox)(const char* label, int* value); /* 1 when changed */
    int (*slider_float)(const char* label, float* value, float lo, float hi);
    void (*same_line)(void);
    void (*separator)(void);
    void (*progress_bar)(float fraction, float width, float height, const char* overlay);
    void (*tooltip)(const char* utf8); /* shown when the previous item is hovered */
    void (*get_cursor_screen_pos)(float* xy);
    void (*get_content_region_avail)(float* xy);
    void (*dummy)(float w, float h); /* reserve space, e.g. for a custom drawing */
    /* the current window's draw list */
    void (*draw_line)(float x1, float y1, float x2, float y2, uint32_t col, float thickness);
    void (*draw_rect_filled)(float x1, float y1, float x2, float y2, uint32_t col, float rounding);
    void (*draw_circle_filled)(float x, float y, float radius, uint32_t col);
    void (*draw_text)(float x, float y, uint32_t col, const char* utf8);
} StlGuiUi;

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* STELLARIS_GUI_API_H */
