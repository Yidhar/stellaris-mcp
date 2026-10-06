#pragma once
// The bridge as a Stellaris launcher plugin (DLL plugin spec v2, schema 2): its own folder, settings
// in config\stellaris_mcp.ini, logs in logs\. The launcher installs it to
// Documents\Paradox Interactive\Stellaris\plugins\stellaris-mcp\ and loads it into the game.
#include <string>

namespace bridge::plugin {

// The folder this DLL was loaded from (with a trailing backslash): the plugin folder when the
// launcher loaded it, the build folder when injected by hand during development.
const std::wstring& Dir();

// Reads config\stellaris_mcp.ini (built-in defaults when it is missing) and opens the log.
void Init();

// Re-reads the settings when the file changed; cheap, called from the Present hook.
void PollSettings();

}  // namespace bridge::plugin
