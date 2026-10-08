/* Finding the GUI host from another plugin (header only, plain C, Windows). See stellaris_gui_api.h.
 *
 * The launcher loads plugins one after another in playset order and no plugin may load another, so the host can show up before or after the
 * plugin that wants it. A plugin therefore looks for it on a thread of its own (never in DllMain: the loader lock) until it appears. */
#ifndef STELLARIS_GUI_CLIENT_H
#define STELLARIS_GUI_CLIENT_H

#include <windows.h>
#include "stellaris_gui_api.h"

/* The host's function table, or NULL when the host is not loaded (yet), is too old for `version`, or is not the host. */
static __inline const struct StlGuiApi* stl_gui_try_connect(const wchar_t* host_dll_name, uint32_t version) {
    HMODULE host = GetModuleHandleW(host_dll_name);
    if (!host) return NULL;
    {
        StlGui_GetApi_fn get_api = (StlGui_GetApi_fn)GetProcAddress(host, STL_GUI_EXPORT_NAME);
        return get_api ? get_api(version) : NULL;
    }
}

#endif
