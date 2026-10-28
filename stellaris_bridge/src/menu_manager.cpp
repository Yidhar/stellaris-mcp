#include "menu_manager.hpp"
#include "command_builder.hpp"
#include "common.hpp"
#include "sdk/stellaris_sdk.hpp"
#include <shlobj.h>
#include <fstream>

namespace bridge {
namespace {

template <typename T>
bool ReadVal(uintptr_t addr, T* out) {
    __try {
        *out = *(const T*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteByte(uintptr_t addr, uint8_t value) {
    __try {
        *(volatile uint8_t*)addr = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

// a campaign folder or file name: no path separators, no parent references
bool SafeName(const std::string& s) {
    return !s.empty() && s.find_first_of("/\\:*?\"<>|") == std::string::npos && s != "." && s != "..";
}

// Engine CString (16-byte header + std::string) built by the engine's own assign
struct EngineCString {
    uint64_t header[2];
    union {
        char buf[16];
        char* heap;
    };
    uint64_t size;
    uint64_t capacity;
};
static_assert(sizeof(EngineCString) == 0x30, "CString is 0x30 bytes");

struct AssignCtx {
    uintptr_t fn;
    void* text;
    const char* chars;
    size_t length;
};

void CallAssign(void* p, void*) {
    auto* x = (AssignCtx*)p;
    ((void* (*)(void*, const char*, size_t))x->fn)(x->text, x->chars, x->length);
}

struct FreeCtx {
    uintptr_t fn;
    void* text;
};

void CallFree(void* p, void*) {
    auto* x = (FreeCtx*)p;
    ((void (*)(void*))x->fn)(x->text);
}

struct SaveCtx {
    uintptr_t fn;
    void* idler;
    void* name;
    bool cloud;
    bool overwrite;
    int status;
};

void CallSave(void* p, void*) {
    auto* x = (SaveCtx*)p;
    x->status = ((int (*)(void*, const void*, bool, bool))x->fn)(x->idler, x->name, x->cloud, x->overwrite);
}

}  // namespace

MenuManager& MenuManager::Get() {
    static MenuManager instance;
    return instance;
}

bool MenuManager::Init(uintptr_t base_address) {
    base_address_ = base_address;
    return true;
}

void* MenuManager::FrontEndIdler() const {
    void* p = nullptr;
    return base_address_ && ReadVal(base_address_ + sdk::glob::g_CurrentFrontEndIdler, &p) ? p : nullptr;
}

void* MenuManager::InGameIdler() const {
    void* p = nullptr;
    return base_address_ && ReadVal(base_address_ + sdk::glob::g_CurrentInGameIdler, &p) ? p : nullptr;
}

bool MenuManager::Loading() const {
    void* progress = nullptr;
    return base_address_ && ReadVal(base_address_ + sdk::glob::s_pLoadGameProgress, &progress) && progress;
}

std::string MenuManager::Screen() const {
    if (Loading()) return "loading";
    if (InGameIdler()) return "in_game";
    if (FrontEndIdler()) return "main_menu";
    return "unknown";
}

std::wstring MenuManager::UserDir() {
    PWSTR docs = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
        out = std::wstring(docs) + L"\\Paradox Interactive\\Stellaris";
    }
    if (docs) CoTaskMemFree(docs);
    return out;
}

bool MenuManager::QuickStartPending() const {
    uint8_t pending = 0;
    return base_address_ && ReadVal(base_address_ + sdk::glob::g_bQuickStart, &pending) && pending;
}

nlohmann::json MenuManager::ScreenJson() const {
    return { {"screen", Screen()}, {"quick_start_pending", QuickStartPending()}, {"user_dir", Utf8(UserDir())} };
}

nlohmann::json MenuManager::LoadSave(const std::string& campaign, const std::string& file) {
    if (Screen() != "main_menu") {
        return { {"success", false}, {"error", "Loading from here needs the main menu (screen: " + Screen() + ")"} };
    }
    if (!SafeName(campaign) || !SafeName(file)) {
        return { {"success", false}, {"error", "campaign and file are a save folder and a file name (stellaris_list_saves)"} };
    }
    std::string stem = file;
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".sav") == 0) stem.resize(stem.size() - 4);
    const std::wstring path = UserDir() + L"\\save games\\" + Wide(campaign) + L"\\" + Wide(stem) + L".sav";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return { {"success", false}, {"error", "No such save: " + campaign + "/" + stem + ".sav"} };
    }
    // The menu's update looks the save up by substring of "save games/<campaign>/<file>.sav" and
    // loads the newest save when nothing matches: "/<campaign>/<file>.sav" matches exactly this
    // one (and, not being a path from the user folder, is not taken for a file of its own)
    const std::string key = "/" + campaign + "/" + stem + ".sav";
    AssignCtx ctx{ base_address_ + sdk::fn::CString_Assign, (void*)(base_address_ + sdk::glob::g_QuickStartSave),
                   key.c_str(), key.size() };
    if (!CommandBuilder::Get().CallGuarded(&CallAssign, &ctx)) {
        return { {"success", false}, {"error", "Setting the save to load failed"} };
    }
    if (!WriteByte(base_address_ + sdk::glob::g_bQuickStart, 1)) {
        return { {"success", false}, {"error", "Starting the load failed"} };
    }
    LOGF("[MENU] loading %s", key.c_str());
    return { {"success", true}, {"campaign", campaign}, {"file", stem + ".sav"},
             {"message", "Loading; the main menu picks the save up on its next frame"} };
}

nlohmann::json MenuManager::FinishLoad() {
    if (Screen() != "in_game") {
        return { {"success", false}, {"error", "No game is in (screen: " + Screen() + ")"} };
    }
    uint8_t pending = 0;
    ReadVal(base_address_ + sdk::glob::g_bQuickStart, &pending);
    if (!pending) return { {"success", true}, {"cleared", false} };
    WriteByte(base_address_ + sdk::glob::g_bQuickStart, 0);
    static const char kEmpty[] = "";
    AssignCtx ctx{ base_address_ + sdk::fn::CString_Assign, (void*)(base_address_ + sdk::glob::g_QuickStartSave), kEmpty, 0 };
    CommandBuilder::Get().CallGuarded(&CallAssign, &ctx);
    return { {"success", true}, {"cleared", true} };
}

nlohmann::json MenuManager::ExitToMainMenu() {
    void* idler = InGameIdler();
    if (!idler || Loading()) {
        return { {"success", false}, {"error", "Not in a game (screen: " + Screen() + ")"} };
    }
    uint8_t resigned = 0;
    ReadVal((uintptr_t)idler + sdk::rt::CInGameIdler_resigned, &resigned);
    if (resigned) return { {"success", true}, {"message", "Already leaving for the main menu"} };
    // the dialog's accept, deferred: the idler's update calls DoResign when it can
    if (!WriteByte((uintptr_t)idler + sdk::rt::CInGameIdler_resign_pending, 1)) {
        return { {"success", false}, {"error", "Leaving the game failed"} };
    }
    LOG("[MENU] leaving for the main menu");
    return { {"success", true}, {"message", "Leaving for the main menu (ironman games are saved first)"} };
}

nlohmann::json MenuManager::SaveGame(const std::string& name, bool overwrite) {
    void* idler = InGameIdler();
    if (!idler || Loading()) {
        return { {"success", false}, {"error", "Not in a game (screen: " + Screen() + ")"} };
    }
    std::string stem = name;
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".sav") == 0) stem.resize(stem.size() - 4);
    if (!SafeName(stem)) return { {"success", false}, {"error", "name is a file name without folders"} };

    EngineCString cname{};
    cname.capacity = 15;
    AssignCtx assign{ base_address_ + sdk::fn::CString_Assign, &cname, stem.c_str(), stem.size() };
    if (!CommandBuilder::Get().CallGuarded(&CallAssign, &assign)) {
        return { {"success", false}, {"error", "Building the save name failed"} };
    }
    // local save (the game's cloud-save option is not followed)
    SaveCtx save{ base_address_ + sdk::fn::CInGameIdler_SaveGame, idler, &cname, false, overwrite, -1 };
    const bool called = CommandBuilder::Get().CallGuarded(&CallSave, &save);
    FreeCtx free_ctx{ base_address_ + sdk::fn::CString_Free, &cname };
    CommandBuilder::Get().CallGuarded(&CallFree, &free_ctx);
    if (!called) return { {"success", false}, {"error", "The save call raised an exception"} };
    switch (save.status) {
    case 0:
        LOGF("[MENU] saving %s", stem.c_str());
        return { {"success", true}, {"name", stem + ".sav"}, {"message", "Saving; the file is written over the next frames"} };
    case 1:
        return { {"success", false}, {"error", "A save named " + stem + " exists; pass overwrite to replace it"} };
    case 2:
        return { {"success", false}, {"error", "This campaign has as many saves as the game allows; delete some first"} };
    default:
        return { {"success", false}, {"error", "The game refused the save (status " + std::to_string(save.status) + ")"} };
    }
}

}  // namespace bridge
