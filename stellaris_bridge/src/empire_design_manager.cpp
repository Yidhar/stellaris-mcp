#include "empire_design_manager.hpp"
#include "command_builder.hpp"
#include "common.hpp"
#include "fleet_access.hpp"
#include "menu_manager.hpp"
#include "sdk/stellaris_sdk.hpp"

namespace bridge {
namespace {

namespace ed = sdk::ent::CEmpireDesign;
namespace sd = sdk::ent::CSpeciesDesign;

// CPdxArray<T*>: +0x8 data, +0x14 size (the SDK's array fields are the size)
constexpr std::ptrdiff_t kArrayData = 0x8, kArraySize = 0x14;
// a script type's key (CEthic, CGovernmentCivicType, CGovernmentAuthorityType, CSpeciesClass ...)
constexpr std::ptrdiff_t kTypeKey = 0x20;
// the design's arrays: ethics (the SDK's `ethic` is its size), then civics right after it
constexpr std::ptrdiff_t kEthics = ed::ethic - kArraySize;
constexpr std::ptrdiff_t kCivics = kEthics + 0x18;
constexpr std::ptrdiff_t kTraits = sd::trait - kArraySize;  // inside a CSpeciesDesign

template <typename T>
bool ReadVal(uintptr_t addr, T* out) {
    __try {
        *out = *(const T*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CopyChars(char* dst, const char* src, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// a std::string (the text part of an engine CString)
std::string Text(uintptr_t addr) {
    struct Raw {
        union {
            char buf[16];
            const char* ptr;
        };
        uint64_t size;
        uint64_t cap;
    } raw{};
    if (!ReadVal(addr, &raw) || raw.size == 0 || raw.size > 4096) return "";
    std::string out(raw.size, '\0');
    return CopyChars(&out[0], raw.cap >= 16 ? raw.ptr : (const char*)addr, raw.size) ? out : "";
}

std::string KeyOfPtr(uintptr_t field) {
    void* p = nullptr;
    return ReadVal(field, &p) && p ? Text((uintptr_t)p + kTypeKey) : "";
}

std::vector<void*> Array(uintptr_t base) {
    std::vector<void*> out;
    void* data = nullptr;
    int32_t n = 0;
    if (!ReadVal(base + kArrayData, &data) || !data || !ReadVal(base + kArraySize, &n) || n <= 0 || n > 4096) return out;
    for (int32_t i = 0; i < n; ++i) {
        void* e = nullptr;
        if (ReadVal((uintptr_t)data + i * 8, &e) && e) out.push_back(e);
    }
    return out;
}

nlohmann::json KeyName(uintptr_t base, const std::string& key) {
    return { {"key", key}, {"name", key.empty() ? "" : SafeLocalize(base, key)} };
}

struct Call1 {
    uintptr_t fn;
    void* a;
    uint8_t result;
};

void CallBool1(void* p, void*) {
    auto* x = (Call1*)p;
    x->result = ((uint8_t(*)(void*))x->fn)(x->a);
}

void CallVoid1(void* p, void*) {
    auto* x = (Call1*)p;
    ((void (*)(void*))x->fn)(x->a);
}

struct SetDesignCtx {
    uintptr_t fn;
    void* fake;
    void* design;
};

void CallSetDesign(void* p, void*) {
    auto* x = (SetDesignCtx*)p;
    ((void (*)(void*, void*, bool))x->fn)(x->fake, x->design, true);
}

struct ValidCtx {
    uintptr_t fn;
    void* design;
    void* country;
    uint8_t result;
};

void CallValid(void* p, void* reason) {
    auto* x = (ValidCtx*)p;
    x->result = ((uint8_t(*)(uint8_t, void*, void*, void*))x->fn)(3, x->design, x->country, reason);
}

struct ReadFileCtx {
    uintptr_t fn;
    void* db;
    const char* path;
};

void CallInitFromFile(void* p, void*) {
    auto* x = (ReadFileCtx*)p;
    ((void (*)(void*, const char*, bool))x->fn)(x->db, x->path, true);
}

// the design's deleting destructor (vtable slot, flag 1 = free), as DeleteUserDesign deletes one
void CallDeleteDesign(void* p, void*) {
    void** vt = *(void***)p;
    ((void (*)(void*, int))vt[sdk::vt::CEmpireDesign_Delete])(p, 1);
}

// a CPdxArray<T*> edited in place (the game's own DeleteUserDesign compacts it the same way):
// drop the element at `index`, or put `with` there instead
bool ArrayRemoveAt(uintptr_t arr, int32_t index) {
    __try {
        void** data = *(void***)(arr + kArrayData);
        int32_t& n = *(int32_t*)(arr + kArraySize);
        if (index < 0 || index >= n) return false;
        memmove(&data[index], &data[index + 1], (size_t)(n - index - 1) * sizeof(void*));
        --n;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ArraySet(uintptr_t arr, int32_t index, void* with) {
    __try {
        void** data = *(void***)(arr + kArrayData);
        if (index < 0 || index >= *(int32_t*)(arr + kArraySize)) return false;
        data[index] = with;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int32_t ArrayIndexOf(uintptr_t arr, void* e) {
    const std::vector<void*> all = Array(arr);
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == e) return (int32_t)i;
    }
    return -1;
}

}  // namespace

EmpireDesignManager& EmpireDesignManager::Get() {
    static EmpireDesignManager instance;
    return instance;
}

bool EmpireDesignManager::Init(uintptr_t base_address) {
    base_address_ = base_address;
    return true;
}

void* EmpireDesignManager::Find(const std::string& key, std::string* type) {
    void* db = nullptr;
    if (key.empty() || !ReadVal(base_address_ + sdk::glob::CEmpireDesignDatabase_pInstance, &db) || !db) return nullptr;
    for (auto [off, kind] : { std::pair<std::ptrdiff_t, const char*>{ sdk::rt::CEmpireDesignDatabase_prescripted, "prescripted" },
                              { sdk::rt::CEmpireDesignDatabase_user, "user" } }) {
        for (void* d : Array((uintptr_t)db + off)) {
            if (Text((uintptr_t)d + ed::key) == key) {
                if (type) *type = kind;
                return d;
            }
        }
    }
    return nullptr;
}

nlohmann::json EmpireDesignManager::Summary(void* design, const std::string& type) {
    const uintptr_t d = (uintptr_t)design;
    nlohmann::json ethics = nlohmann::json::array();
    for (void* e : Array(d + kEthics)) ethics.push_back(SafeLocalize(base_address_, Text((uintptr_t)e + kTypeKey)));
    std::string origin = KeyOfPtr(d + ed::origin);
    Call1 playable{ base_address_ + sdk::fn::CEmpireDesign_IsPlayable, design, 0 };
    const bool can_play = CommandBuilder::Get().CallGuarded(&CallBool1, &playable) && playable.result;
    return { {"key", Text(d + ed::key)},
             {"name", PersistentNameText((const void*)(d + ed::name))},
             {"type", type},
             {"species", PersistentNameText((const void*)(d + ed::species + sd::species_name))},
             {"ethics", ethics},
             {"origin", origin.empty() ? "" : SafeLocalize(base_address_, origin)},
             {"playable", can_play} };
}

nlohmann::json EmpireDesignManager::ListJson() {
    void* db = nullptr;
    if (!ReadVal(base_address_ + sdk::glob::CEmpireDesignDatabase_pInstance, &db) || !db) {
        return { {"error", "The empire design database is not loaded"} };
    }
    nlohmann::json out = nlohmann::json::array();
    std::vector<void*> all;
    for (auto [off, kind] : { std::pair<std::ptrdiff_t, const char*>{ sdk::rt::CEmpireDesignDatabase_prescripted, "prescripted" },
                              { sdk::rt::CEmpireDesignDatabase_user, "user" } }) {
        for (void* d : Array((uintptr_t)db + off)) {
            out.push_back(Summary(d, kind));
            all.push_back(d);
        }
    }
    // on the main menu, the game's validation of each design (the empire list runs the same)
    const bool menu = MenuManager::Get().Screen() == "main_menu";
    if (menu) {
        std::vector<int> valid = ValidateAll(all, nullptr);
        for (size_t i = 0; i < valid.size(); ++i) {
            if (valid[i] >= 0) out[i]["valid"] = valid[i] != 0;
        }
    }
    return { {"designs", out}, {"count", out.size()}, {"user_file", UserFile()},
             {"note", std::string("playable: the design's own playable condition; ") +
                      (menu ? "valid: the game's validation for playing it (why not: stellaris_get_empire_design). " :
                              "valid is checked on the main menu. ") +
                      "The game's empire list hides prescripted designs that fail either."} };
}

std::vector<int> EmpireDesignManager::ValidateAll(const std::vector<void*>& designs, std::vector<std::string>* reasons) {
    // SScopedFakeCountry: a scratch country (its id at +0) rebuilt from each design before the
    // check, as the empire list validates every design; destroyed at the end
    std::vector<int> out(designs.size(), -1);
    if (reasons) reasons->assign(designs.size(), "");
    alignas(16) uint8_t fake[0x400] = {};
    Call1 create{ base_address_ + sdk::fn::SScopedFakeCountry_Create, fake, 0 };
    if (!CommandBuilder::Get().CallGuarded(&CallVoid1, &create)) return out;
    for (size_t i = 0; i < designs.size(); ++i) {
        SetDesignCtx set{ base_address_ + sdk::fn::SScopedFakeCountry_SetDesign, fake, designs[i] };
        if (!CommandBuilder::Get().CallGuarded(&CallSetDesign, &set)) continue;
        uint32_t id = 0xFFFFFFFF;
        ReadVal((uintptr_t)fake, &id);
        void* country = nullptr;
        void* cdb = nullptr;
        void* slots = nullptr;
        uint32_t cap = 0;
        if (ReadVal(base_address_ + sdk::db::CCountry, &cdb) && cdb && ReadVal((uintptr_t)cdb + 0x18, &slots) && slots &&
            ReadVal((uintptr_t)cdb + 0x20, &cap) && (id & 0xFFFFFF) < cap) {
            ReadVal((uintptr_t)slots + (id & 0xFFFFFF) * 16 + 8, &country);
        }
        if (!country) ReadVal(base_address_ + sdk::glob::TPdxNullObject_CCountry_pInstance, &country);
        ValidCtx v{ base_address_ + sdk::fn::IsEmpireDesignValid, designs[i], country, 0 };
        std::string why;
        if (CommandBuilder::Get().CallForText(&CallValid, &v, &why)) out[i] = v.result != 0;
        if (reasons) (*reasons)[i] = why;
    }
    Call1 destroy{ base_address_ + sdk::fn::SScopedFakeCountry_Destroy, fake, 0 };
    CommandBuilder::Get().CallGuarded(&CallVoid1, &destroy);
    return out;
}

bool EmpireDesignManager::Validate(void* design, bool* valid, std::string* reason) {
    std::vector<std::string> reasons;
    std::vector<int> r = ValidateAll({ design }, reason ? &reasons : nullptr);
    if (r[0] < 0) return false;
    *valid = r[0] != 0;
    if (reason) *reason = reasons[0];
    return true;
}

nlohmann::json EmpireDesignManager::DetailJson(const std::string& key) {
    std::string type;
    void* design = Find(key, &type);
    if (!design) return { {"error", "No empire design with key " + key + " (stellaris_list_empire_designs)"} };
    const uintptr_t d = (uintptr_t)design;
    nlohmann::json out = Summary(design, type);
    out["adjective"] = PersistentNameText((const void*)(d + ed::adjective));

    auto species = [&](uintptr_t s) {
        nlohmann::json traits = nlohmann::json::array();
        for (void* t : Array(s + kTraits)) traits.push_back(KeyName(base_address_, Text((uintptr_t)t + sdk::rt::CTrait_key)));
        int32_t gender = 0;
        ReadVal(s + sd::gender, &gender);
        return nlohmann::json{
            {"class", KeyName(base_address_, KeyOfPtr(s + sd::class_))},
            {"portrait", Text(s + sd::portrait)},
            {"name", PersistentNameText((const void*)(s + sd::species_name))},
            {"plural", PersistentNameText((const void*)(s + sd::species_plural))},
            {"adjective", PersistentNameText((const void*)(s + sd::species_adjective))},
            {"name_list", Text(s + sd::name_list)},
            {"gender", gender},
            {"traits", traits} };
    };
    out["species"] = species(d + ed::species);
    if (!KeyOfPtr(d + ed::secondary_species + sd::class_).empty()) out["secondary_species"] = species(d + ed::secondary_species);

    nlohmann::json ethics = nlohmann::json::array(), civics = nlohmann::json::array();
    for (void* e : Array(d + kEthics)) ethics.push_back(KeyName(base_address_, Text((uintptr_t)e + kTypeKey)));
    for (void* c : Array(d + kCivics)) civics.push_back(KeyName(base_address_, Text((uintptr_t)c + kTypeKey)));
    out["ethics"] = ethics;
    out["civics"] = civics;
    out["authority"] = KeyName(base_address_, KeyOfPtr(d + ed::authority));
    out["government"] = KeyName(base_address_, KeyOfPtr(d + ed::government));
    out["origin"] = KeyName(base_address_, KeyOfPtr(d + ed::origin));
    // the home world: planet class (its key sits at +0x28), system initializer, names
    void* pc = nullptr;
    out["home"] = { {"planet_class", ReadVal(d + ed::planet_class, &pc) && pc ? KeyName(base_address_, Text((uintptr_t)pc + 0x28)) : nlohmann::json()},
                    {"planet_name", PersistentNameText((const void*)(d + ed::planet_name))},
                    {"system_name", PersistentNameText((const void*)(d + ed::system_name))},
                    {"initializer", Text(d + ed::initializer)} };
    out["ship_prefix"] = PersistentNameText((const void*)(d + ed::ship_prefix));
    out["ship_set"] = Text(d + ed::graphical_culture);
    out["city_set"] = Text(d + ed::city_graphical_culture);
    out["room"] = Text(d + ed::room);
    out["advisor_voice"] = Text(d + ed::advisor_voice_type);
    uint8_t no_edit = 0;
    ReadVal(d + ed::disallow_editing, &no_edit);
    if (no_edit) out["disallow_editing"] = true;
    if (type == "user") {
        int32_t spawn = 0;
        ReadVal(d + ed::spawn_enabled, &spawn);
        out["spawn"] = spawn == 2 ? "always" : spawn == 1 ? "yes" : "no";
    }

    if (MenuManager::Get().Screen() == "main_menu") {
        bool valid = false;
        std::string reason;
        if (Validate(design, &valid, &reason)) {
            out["valid"] = valid;
            if (!valid) out["invalid_reason"] = reason;  // the game's text, markup already rendered
        } else {
            out["valid"] = nullptr;
            out["validation_error"] = "the game's validation raised an exception";
        }
    } else {
        out["valid"] = nullptr;
        out["validation_note"] = "Validation runs on the main menu (it builds a scratch country, as the empire list does)";
    }
    return out;
}

std::string EmpireDesignManager::UserFile() {
    // UserDesignsFileNames: CPdxArray<CString> (0x30 each, text at +0x10); the game writes the last
    const std::vector<std::string> names = [&] {
        std::vector<std::string> v;
        void* data = nullptr;
        int32_t n = 0;
        const uintptr_t arr = base_address_ + sdk::glob::UserDesignsFileNames;
        if (ReadVal(arr + kArrayData, &data) && data && ReadVal(arr + kArraySize, &n) && n > 0 && n < 16) {
            for (int32_t i = 0; i < n; ++i) v.push_back(Text((uintptr_t)data + i * 0x30 + 0x10));
        }
        return v;
    }();
    return names.empty() ? "" : names.back();
}

nlohmann::json EmpireDesignManager::SaveFromFile(const std::string& vfs_file, bool validate_only, bool overwrite) {
    if (MenuManager::Get().Screen() != "main_menu") {
        return { {"error", "Empire designs are edited on the main menu (screen: " + MenuManager::Get().Screen() + ")"} };
    }
    void* db = nullptr;
    if (!ReadVal(base_address_ + sdk::glob::CEmpireDesignDatabase_pInstance, &db) || !db) {
        return { {"error", "The empire design database is not loaded"} };
    }
    // only the designs in memory change here; the caller keeps the user design file (the game's
    // SaveUserFile would rewrite every user design and drop what unloaded mods defined)
    const uintptr_t user = (uintptr_t)db + sdk::rt::CEmpireDesignDatabase_user;
    const std::vector<void*> before = Array(user);
    auto drop = [&](void* d) {
        if (ArrayRemoveAt(user, ArrayIndexOf(user, d))) CommandBuilder::Get().CallGuarded(&CallDeleteDesign, d);
    };

    // InitFromFile(db, path, user = true) appends every design in the file to the user designs
    ReadFileCtx read{ base_address_ + sdk::fn::CEmpireDesignDatabase_InitFromFile, db, vfs_file.c_str() };
    if (!CommandBuilder::Get().CallGuarded(&CallInitFromFile, &read)) {
        return { {"error", "The game's design reader raised an exception on " + vfs_file} };
    }
    std::vector<void*> added = Array(user);
    added.erase(added.begin(), added.begin() + (std::min)(added.size(), before.size()));
    if (added.size() != 1) {
        for (void* d : added) drop(d);
        return { {"error", added.empty() ? "The game read no design from the text (check its syntax: the game's error.log "
                                           "names the line)"
                                         : "The text holds " + std::to_string(added.size()) + " designs; save one at a time"} };
    }
    void* design = added[0];
    const std::string key = Text((uintptr_t)design + ed::key);
    Call1 fix{ base_address_ + sdk::fn::CEmpireDesign_PostReadInit, design, 0 };
    CommandBuilder::Get().CallGuarded(&CallBool1, &fix);

    for (void* d : Array((uintptr_t)db + sdk::rt::CEmpireDesignDatabase_prescripted)) {
        if (Text((uintptr_t)d + ed::key) == key) {
            drop(design);
            return { {"error", "\"" + key + "\" is a prescripted empire's key; give the design another key"} };
        }
    }
    void* old = nullptr;
    for (void* d : before) {
        if (Text((uintptr_t)d + ed::key) == key) old = d;
    }
    if (old && !overwrite && !validate_only) {
        drop(design);
        return { {"error", "A user design with key \"" + key + "\" exists; pass overwrite to replace it"} };
    }

    nlohmann::json out = Summary(design, "user");
    bool valid = false;
    std::string reason;
    if (!Validate(design, &valid, &reason)) {
        drop(design);
        return { {"error", "The game's validation raised an exception on \"" + key + "\""} };
    }
    out["valid"] = valid;
    if (!valid) out["invalid_reason"] = reason;
    if (validate_only || !valid) {
        drop(design);
        out["saved"] = false;
        if (!validate_only) out["note"] = "Not saved: the game refuses this design (invalid_reason)";
        return out;
    }
    if (old) {
        // the new design takes the old one's place in the list; the old one is deleted
        const int32_t at = ArrayIndexOf(user, old);
        if (!ArrayRemoveAt(user, ArrayIndexOf(user, design)) || !ArraySet(user, at, design)) {
            return { {"error", "Could not replace the user design \"" + key + "\" in memory"} };
        }
        CommandBuilder::Get().CallGuarded(&CallDeleteDesign, old);
        out["replaced"] = true;
    }
    out["saved"] = true;
    return out;
}

nlohmann::json EmpireDesignManager::Delete(const std::string& key) {
    if (MenuManager::Get().Screen() != "main_menu") {
        return { {"error", "Empire designs are edited on the main menu (screen: " + MenuManager::Get().Screen() + ")"} };
    }
    std::string type;
    void* design = Find(key, &type);
    if (!design) return { {"error", "No empire design with key " + key + " (stellaris_list_empire_designs)"} };
    if (type != "user") return { {"error", "\"" + key + "\" is a prescripted empire; only user designs can be deleted"} };
    void* db = nullptr;
    ReadVal(base_address_ + sdk::glob::CEmpireDesignDatabase_pInstance, &db);
    const uintptr_t user = (uintptr_t)db + sdk::rt::CEmpireDesignDatabase_user;
    if (!ArrayRemoveAt(user, ArrayIndexOf(user, design))) return { {"error", "Could not remove \"" + key + "\" in memory"} };
    CommandBuilder::Get().CallGuarded(&CallDeleteDesign, design);
    return { {"deleted", key} };
}

}  // namespace bridge
