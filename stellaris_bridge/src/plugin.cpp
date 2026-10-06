#include "plugin.hpp"
#include "common.hpp"

namespace bridge::plugin {
namespace {

constexpr wchar_t kSettingsFile[] = L"stellaris_mcp.ini";
constexpr wchar_t kLogFile[] = L"stellaris_mcp.log";
constexpr ULONGLONG kPollMs = 2000;

struct Settings {
    bool log_enabled = true;
    bool log_debug = false;
};

FILETIME g_settings_time{};
ULONGLONG g_next_poll = 0;

std::wstring SettingsPath() { return Dir() + L"config\\" + kSettingsFile; }

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// [log] enabled = true|false, level = info|debug. Unknown keys and sections are ignored; a missing
// file or key keeps the default.
Settings ReadSettings(const std::wstring& path, bool* found) {
    Settings s;
    std::ifstream in(path.c_str(), std::ios::binary);
    *found = in.is_open();
    if (!in) return s;
    std::string line, section;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);  // UTF-8 BOM
        }
        first = false;
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Lower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Lower(Trim(line.substr(0, eq)));
        std::string value = Lower(Trim(line.substr(eq + 1)));
        size_t comment = value.find_first_of(";#");
        if (comment != std::string::npos) value = Trim(value.substr(0, comment));
        if (section == "log" && key == "enabled") {
            s.log_enabled = !(value == "false" || value == "0" || value == "no" || value == "off");
        } else if (section == "log" && key == "level") {
            s.log_debug = value == "debug";
        }
    }
    return s;
}

bool ModifiedTime(const std::wstring& path, FILETIME* out) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    *out = data.ftLastWriteTime;
    return true;
}

void Apply(const Settings& s) {
    Logger::Get().SetLevel(s.log_enabled, s.log_debug);
}

}  // namespace

const std::wstring& Dir() {
    static const std::wstring dir = [] {
        HMODULE self = nullptr;
        wchar_t path[MAX_PATH * 4] = {};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&Dir, &self)) {
            GetModuleFileNameW(self, path, (DWORD)(sizeof(path) / sizeof(path[0])));
        }
        std::wstring p(path);
        size_t slash = p.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
    }();
    return dir;
}

void Init() {
    bool found = false;
    const std::wstring settings = SettingsPath();
    Apply(ReadSettings(settings, &found));
    if (!ModifiedTime(settings, &g_settings_time)) g_settings_time = {};
    g_next_poll = GetTickCount64() + kPollMs;

    const std::wstring logs = Dir() + L"logs";
    CreateDirectoryW(logs.c_str(), nullptr);
    Logger::Get().Init(logs + L"\\" + kLogFile);
    LOGF("[PLUGIN] folder %ls, settings %ls (%s)", Dir().c_str(), settings.c_str(),
         found ? "read" : "missing: built-in defaults");
}

void PollSettings() {
    const ULONGLONG now = GetTickCount64();
    if (now < g_next_poll) return;
    g_next_poll = now + kPollMs;
    const std::wstring settings = SettingsPath();
    FILETIME t{};
    if (!ModifiedTime(settings, &t)) t = {};
    if (CompareFileTime(&t, &g_settings_time) == 0) return;
    g_settings_time = t;
    bool found = false;
    Settings s = ReadSettings(settings, &found);
    // announce the change while logging is still on, whichever way it goes
    Logger::Get().SetLevel(true, false);
    LOGF("[PLUGIN] settings %s: log %s, level %s", found ? "reloaded" : "removed (built-in defaults)",
         s.log_enabled ? "on" : "off", s.log_debug ? "debug" : "info");
    Apply(s);
}

}  // namespace bridge::plugin
