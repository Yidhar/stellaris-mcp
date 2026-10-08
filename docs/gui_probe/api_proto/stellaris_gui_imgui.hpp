// For a plugin that compiles its own copy of Dear ImGui and wants to use it inside a host draw callback.
// The copy has to be the same ImGui version as the engine's (1.85 as of Stellaris 4.5.2) with the default imconfig.h; the host says which in
// StlGuiCallbackCtx::imgui_version_num. Dear ImGui keeps its state in globals and its heap per module, so each module that calls it must be
// pointed at the shared context and the shared allocators ("Context and Memory Allocators" in imgui.cpp).
#pragma once
#include "imgui.h"
#include "stellaris_gui_api.h"

// Why the last call refused (nullptr when it did not); a plugin can log it once.
inline const char*& StlGuiBindFailure() {
    static const char* why = nullptr;
    return why;
}

// Call first in every draw callback; draw only when it returns true.
inline bool StlGuiBindImGui(const StlGuiCallbackCtx* c) {
    StlGuiBindFailure() = nullptr;
    if (!c || c->size < sizeof(StlGuiCallbackCtx) || !c->imgui_context) return StlGuiBindFailure() = "no ImGui context", false;
    if (c->imgui_version_num != IMGUI_VERSION_NUM) return StlGuiBindFailure() = "other ImGui version than the engine's", false;
    // the same version can still be configured differently (imconfig.h): compare the layouts of the types the shared context is made of
    if (c->imgui_sizeof_io != sizeof(ImGuiIO)) return StlGuiBindFailure() = "sizeof(ImGuiIO) differs from the engine's (imconfig.h?)", false;
    if (c->imgui_sizeof_style != sizeof(ImGuiStyle)) return StlGuiBindFailure() = "sizeof(ImGuiStyle) differs from the engine's", false;
    if (c->imgui_sizeof_drawvert != sizeof(ImDrawVert)) return StlGuiBindFailure() = "sizeof(ImDrawVert) differs from the engine's", false;
    if (c->imgui_sizeof_drawidx != sizeof(ImDrawIdx)) return StlGuiBindFailure() = "sizeof(ImDrawIdx) differs from the engine's", false;
    static void* bound_alloc = nullptr;
    if (bound_alloc != c->imgui_alloc) {  // once: the engine's allocator pair does not change
        ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)c->imgui_alloc, (ImGuiMemFreeFunc)c->imgui_free, c->imgui_alloc_user);
        bound_alloc = c->imgui_alloc;
    }
    ImGui::SetCurrentContext((ImGuiContext*)c->imgui_context);  // every call: the engine can restart its ImGui, which makes a new context
    return true;
}
