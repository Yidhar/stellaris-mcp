#include "species_manager.hpp"
#include <cstring>
#include <algorithm>

namespace bridge {

// Safe memory read helpers
static bool SafeReadPtr(const void* addr, void** out) {
    __try {
        *out = *(void**)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(const void* addr, uint32_t* out) {
    __try {
        *out = *(const uint32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU64(const void* addr, uint64_t* out) {
    __try {
        *out = *(const uint64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeCopyChars(char* dest, const char* src, size_t count) {
    __try {
        memcpy(dest, src, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct RawPdxString {
    union {
        char buf[16];
        char* heap_ptr;
    };
    uint64_t size;
    uint64_t capacity;
};

static bool SafeReadPdxString(const void* pdx_str_addr, std::string& out) {
    out.clear();
    if (!pdx_str_addr) return false;

    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)pdx_str_addr, sizeof(RawPdxString))) {
        return false;
    }

    if (raw.size == 0 || raw.size > 1024) return false;

    if (raw.capacity < 16) {
        size_t len = raw.size < 16 ? (size_t)raw.size : 15;
        char temp[16]{ 0 };
        if (SafeCopyChars(temp, raw.buf, len)) {
            out = std::string(temp, len);
            return true;
        }
    } else if (raw.heap_ptr) {
        uintptr_t addr = (uintptr_t)raw.heap_ptr;
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
            size_t len = raw.size < 512 ? (size_t)raw.size : 512;
            std::string temp(len, '\0');
            if (SafeCopyChars(&temp[0], raw.heap_ptr, len)) {
                out = temp;
                return true;
            }
        }
    }
    return false;
}

static bool SafeLocalizeCall(SpeciesManager::FnLocalize fn_localize,
                             SpeciesManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(SpeciesManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void* SafeGetSpeciesRightsCall(SpeciesManager::FnGetSpeciesRights fn_get, void* pRightsMgr, void* pSpecies, uint8_t* out_is_specific) {
    __try {
        return fn_get(pRightsMgr, pSpecies, out_is_specific);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool SafePostCommand(SpeciesManager::FnPostCommand fn_post, void* cmd) {
    __try {
        fn_post(cmd, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void* SafeCallSetSpeciesRightCtor(SpeciesManager::FnSetSpeciesRightCmdCtor fn_ctor,
                                        void* this_ptr, void* pCountry, void* pSpecies,
                                        const void* pRights, uint8_t is_specific) {
    __try {
        return fn_ctor(this_ptr, pCountry, pSpecies, pRights, is_specific);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

SpeciesManager& SpeciesManager::Get() {
    static SpeciesManager instance;
    return instance;
}

bool SpeciesManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_get_species_rights_ = (FnGetSpeciesRights)(base_address_ + 0x7F68D0);
    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
    fn_set_species_right_cmd_ctor_ = (FnSetSpeciesRightCmdCtor)(base_address_ + 0x6C7460);

    rights_cache_.clear();
    rights_catalog_.clear();

    EnsureDatabasesLoaded();

    LOGF("[SPECIES_MGR] Initialized with base address: 0x%llX", (unsigned long long)base_address_);
    return true;
}

void* SpeciesManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112F50), &mgr) && mgr) {
        void* countries_arr = nullptr;
        uint32_t count = 0;
        if (SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &countries_arr) && countries_arr &&
            SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &count) && count > 0) {
            void* country_0 = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)countries_arr + 8), &country_0) && country_0) {
                return country_0;
            }
        }
    }
    return nullptr;
}

uint32_t SpeciesManager::GetCurrentGameHours() {
    if (!base_address_) return 0;

    void* global_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &global_mgr) && global_mgr) {
        uint32_t raw_hours = 0;
        if (SafeReadU32((const void*)((uintptr_t)global_mgr + 0xC0), &raw_hours)) {
            return raw_hours;
        }
    }
    return 0;
}

std::string SpeciesManager::LocalizeKey(const std::string& key) {
    if (key.empty() || !fn_localize_) return key;

    RawPdxString in_key{};
    in_key.size = key.size();
    in_key.capacity = 15;
    if (key.size() < 16) {
        memcpy(in_key.buf, key.data(), key.size());
    } else {
        return key;
    }

    RawPdxString out_str{};
    if (!SafeLocalizeCall(fn_localize_, fn_free_pdx_str_, &in_key, &out_str)) {
        return key;
    }

    std::string result;
    if (out_str.size > 0 && out_str.size < 4096) {
        if (out_str.capacity < 16) {
            char temp[16]{ 0 };
            size_t len = out_str.size < 16 ? (size_t)out_str.size : 15;
            memcpy(temp, out_str.buf, len);
            result = std::string(temp, len);
        } else if (out_str.heap_ptr) {
            size_t len = out_str.size < 512 ? (size_t)out_str.size : 512;
            result = std::string(out_str.heap_ptr, len);
        }
    }

    if (fn_free_pdx_str_) {
        SafeFreePdxStr(fn_free_pdx_str_, &out_str);
    }

    return result.empty() ? key : result;
}

void SpeciesManager::LoadRightDatabase(const std::string& category, uintptr_t db_rva) {
    if (!base_address_) return;

    void* db_ptr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + db_rva), &db_ptr) || !db_ptr) {
        return;
    }

    void* arr_ptr = nullptr;
    uint32_t count = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)db_ptr + 0x50), &arr_ptr) || !arr_ptr ||
        !SafeReadU32((const void*)((uintptr_t)db_ptr + 0x5C), &count) || count == 0) {
        return;
    }

    for (uint32_t i = 0; i < count && i < 100; ++i) {
        void* elem = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr_ptr + i * 8), &elem) || !elem) {
            continue;
        }

        std::string key;
        if (SafeReadPdxString((const void*)((uintptr_t)elem + 0x20), key) && !key.empty()) {
            rights_cache_[category][key] = elem;
            rights_catalog_[category].push_back({ key, LocalizeKey(key) });
        }
    }
}

void SpeciesManager::EnsureDatabasesLoaded() {
    if (!rights_cache_.empty()) return;

    LoadRightDatabase("citizenship", 0x3110B90);
    LoadRightDatabase("living_standards", 0x3110B80);
    LoadRightDatabase("military_service", 0x3110B70);
    LoadRightDatabase("slavery_type", 0x3110B58);
    LoadRightDatabase("purge_type", 0x3110B60);
    LoadRightDatabase("population_controls", 0x3110B68);
    LoadRightDatabase("colonization_controls", 0x3110B88);
    LoadRightDatabase("migration_controls", 0x3110B78);
    LoadRightDatabase("subspecies_integration", 0x3110B50);

    LOGF("[SPECIES_MGR] Loaded %zu rights categories into catalog.", rights_cache_.size());
}

void* SpeciesManager::GetSpeciesRightType(const std::string& category, const std::string& key) {
    EnsureDatabasesLoaded();
    auto cat_it = rights_cache_.find(category);
    if (cat_it != rights_cache_.end()) {
        auto key_it = cat_it->second.find(key);
        if (key_it != cat_it->second.end()) {
            return key_it->second;
        }
    }
    return nullptr;
}

void* SpeciesManager::FindSpeciesPtr(uint32_t species_id) {
    if (!base_address_ || species_id == 0) return nullptr;

    void* smgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3112F58), &smgr) || !smgr) {
        return nullptr;
    }

    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)smgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)smgr + 0x20), &cap) || cap == 0) {
        return nullptr;
    }

    uint32_t direct_slot = species_id & 0xFFFFFF;
    if (direct_slot < cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + direct_slot * 16 + 8), &ptr) && ptr) {
            uint32_t check_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)ptr + 0x10), &check_id) && check_id == species_id) {
                return ptr;
            }
        }
    }

    for (uint32_t i = 0; i < cap && i < 1024; ++i) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + i * 16 + 8), &ptr) && ptr) {
            uint32_t check_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)ptr + 0x10), &check_id) && check_id == species_id) {
                return ptr;
            }
        }
    }

    return nullptr;
}

std::vector<TraitInfo> SpeciesManager::ReadTraits(void* pSpecies) {
    std::vector<TraitInfo> traits;
    if (!pSpecies) return traits;

    void* traits_arr = nullptr;
    uint32_t count = 0;
    // TraitManager is embedded at +0x1D8, traits array pointer is at +0x1D8 + 0x90, count at +0x1D8 + 0x9C
    if (SafeReadPtr((const void*)((uintptr_t)pSpecies + 0x1D8 + 0x90), &traits_arr) && traits_arr &&
        SafeReadU32((const void*)((uintptr_t)pSpecies + 0x1D8 + 0x9C), &count) && count > 0 && count < 64) {
        for (uint32_t i = 0; i < count; ++i) {
            void* trait_instance = nullptr;
            // Each entry in traits_arr is 32 bytes (0x20), trait instance pointer is at offset 0
            if (SafeReadPtr((const void*)((uintptr_t)traits_arr + i * 32), &trait_instance) && trait_instance) {
                void* trait_type = nullptr;
                // Trait static database object pointer is at trait_instance + 0xA0
                if (SafeReadPtr((const void*)((uintptr_t)trait_instance + 0xA0), &trait_type) && trait_type) {
                    std::string key;
                    // Trait key PdxString is at trait_type + 0x20
                    if (SafeReadPdxString((const void*)((uintptr_t)trait_type + 0x20), key) && !key.empty()) {
                        traits.push_back({ key, LocalizeKey(key) });
                    }
                }
            }
        }
    }
    return traits;
}

uint32_t SpeciesManager::CalculateEmpirePops(uint32_t* out_colony_count) {
    void* country = GetPlayerCountry();
    if (!country) return 0;

    void* colony_vec = nullptr;
    uint32_t colony_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x2F68), &colony_vec);
    SafeReadU32((const void*)((uintptr_t)country + 0x2F74), &colony_cnt);

    if (out_colony_count) {
        *out_colony_count = colony_cnt;
    }

    if (!colony_vec || colony_cnt == 0 || colony_cnt > 1000) return 0;

    void* colony_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113148), &colony_mgr) || !colony_mgr) {
        return 0;
    }

    void* colony_arr = nullptr;
    uint32_t colony_mgr_cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)colony_mgr + 0x18), &colony_arr) || !colony_arr ||
        !SafeReadU32((const void*)((uintptr_t)colony_mgr + 0x20), &colony_mgr_cap) || colony_mgr_cap == 0) {
        return 0;
    }

    uint32_t total_pops = 0;
    for (uint32_t i = 0; i < colony_cnt; ++i) {
        uint32_t cid = 0;
        if (!SafeReadU32((const void*)((uintptr_t)colony_vec + i * 4), &cid)) continue;

        uint32_t slot = cid & 0xFFFFFF;
        if (slot < colony_mgr_cap) {
            void* c_obj = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)colony_arr + slot * 16 + 8), &c_obj) && c_obj) {
                uint32_t p_cnt = 0;
                if (SafeReadU32((const void*)((uintptr_t)c_obj + 0x4A4), &p_cnt)) {
                    total_pops += p_cnt;
                }
            }
        }
    }

    return total_pops;
}

SpeciesRights SpeciesManager::ReadRights(void* pRightsMgr, void* pSpecies) {
    SpeciesRights r;
    if (!pRightsMgr || !pSpecies || !fn_get_species_rights_) {
        return r;
    }

    uint8_t dummy = 0;
    void* pRights = SafeGetSpeciesRightsCall(fn_get_species_rights_, pRightsMgr, pSpecies, &dummy);
    if (!pRights) {
        return r;
    }

    auto read_type_key = [](void* field_ptr) -> std::string {
        void* type_obj = nullptr;
        if (SafeReadPtr(field_ptr, &type_obj) && type_obj) {
            std::string k;
            if (SafeReadPdxString((const void*)((uintptr_t)type_obj + 0x20), k)) {
                return k;
            }
        }
        return "";
    };

    r.citizenship = read_type_key((void*)((uintptr_t)pRights + 0x10));
    r.citizenship_localized = LocalizeKey(r.citizenship);

    r.living_standards = read_type_key((void*)((uintptr_t)pRights + 0x18));
    r.living_standards_localized = LocalizeKey(r.living_standards);

    r.military_service = read_type_key((void*)((uintptr_t)pRights + 0x20));
    r.military_service_localized = LocalizeKey(r.military_service);

    r.slavery_type = read_type_key((void*)((uintptr_t)pRights + 0x28));
    r.slavery_type_localized = LocalizeKey(r.slavery_type);

    r.purge_type = read_type_key((void*)((uintptr_t)pRights + 0x30));
    r.purge_type_localized = LocalizeKey(r.purge_type);

    r.population_controls = read_type_key((void*)((uintptr_t)pRights + 0x38));
    r.population_controls_localized = LocalizeKey(r.population_controls);

    r.colonization_controls = read_type_key((void*)((uintptr_t)pRights + 0x40));
    r.colonization_controls_localized = LocalizeKey(r.colonization_controls);

    r.migration_controls = read_type_key((void*)((uintptr_t)pRights + 0x48));
    r.migration_controls_localized = LocalizeKey(r.migration_controls);

    r.subspecies_integration = read_type_key((void*)((uintptr_t)pRights + 0x50));
    r.subspecies_integration_localized = LocalizeKey(r.subspecies_integration);

    // Check cooldown timestamps at +0xA4 .. +0xC0 (8 categories * 4 bytes)
    uint32_t max_change_hour = 0;
    for (uint32_t off = 0xA4; off <= 0xC0; off += 4) {
        uint32_t h = 0;
        if (SafeReadU32((const void*)((uintptr_t)pRights + off), &h) && h > max_change_hour) {
            max_change_hour = h;
        }
    }

    uint32_t cur_hour = GetCurrentGameHours();
    const uint32_t COOLDOWN_HOURS = 86400; // 10 years
    if (max_change_hour > 0 && cur_hour > 0 && cur_hour < max_change_hour + COOLDOWN_HOURS) {
        r.cooldown_remaining_days = (max_change_hour + COOLDOWN_HOURS - cur_hour) / 24;
    } else {
        r.cooldown_remaining_days = 0;
    }

    return r;
}

SpeciesSummary SpeciesManager::ReadSummary() {
    SpeciesSummary s;
    void* country = GetPlayerCountry();
    if (!country) return s;

    uint32_t fid = 0;
    if (SafeReadU32((const void*)((uintptr_t)country + 0x1634), &fid)) {
        s.founder_species_id = fid;
        void* pSpecies = FindSpeciesPtr(fid);
        if (pSpecies) {
            std::string name_key;
            if (SafeReadPdxString((const void*)((uintptr_t)pSpecies + 0x60), name_key)) {
                s.founder_species_name = LocalizeKey(name_key);
            }
        }
    }

    void* smgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112F58), &smgr) && smgr) {
        uint32_t total = 0;
        if (SafeReadU32((const void*)((uintptr_t)smgr + 0x24), &total)) {
            s.total_species_in_galaxy = total;
        }
    }

    s.total_empire_pops = CalculateEmpirePops(&s.empire_colonies_count);
    return s;
}

nlohmann::json SpeciesManager::GetSummaryJson() {
    SpeciesSummary s = ReadSummary();
    return {
        {"founder_species_id", s.founder_species_id},
        {"founder_species_name", s.founder_species_name},
        {"total_species_in_galaxy", s.total_species_in_galaxy},
        {"empire_colonies_count", s.empire_colonies_count},
        {"total_empire_pops", s.total_empire_pops}
    };
}

nlohmann::json SpeciesManager::GetSpeciesJson(const nlohmann::json& req) {
    EnsureDatabasesLoaded();

    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"success", false},
            {"error", "Player country not available or game not loaded"}
        };
    }

    void* rights_mgr = nullptr;
    SafeReadPtr((const void*)((uintptr_t)country + 0x2B70), &rights_mgr);

    uint32_t founder_id = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x1634), &founder_id);

    bool galaxy_mode = false;
    if (req.contains("mode") && req["mode"].is_string()) {
        galaxy_mode = (req["mode"] == "galaxy");
    }

    uint32_t target_species_id = 0;
    if (req.contains("species_id") && req["species_id"].is_number_unsigned()) {
        target_species_id = req["species_id"].get<uint32_t>();
    }

    // Build list of species to return
    std::vector<void*> species_to_process;

    void* smgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3112F58), &smgr);
    void* arr = nullptr;
    uint32_t cap = 0;
    if (smgr) {
        SafeReadPtr((const void*)((uintptr_t)smgr + 0x18), &arr);
        SafeReadU32((const void*)((uintptr_t)smgr + 0x20), &cap);
    }

    if (target_species_id != 0) {
        void* sp = FindSpeciesPtr(target_species_id);
        if (sp) species_to_process.push_back(sp);
    } else if (galaxy_mode && arr && cap > 0) {
        for (uint32_t i = 0; i < cap && i < 1024; ++i) {
            void* sp = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)arr + i * 16 + 8), &sp) && sp) {
                species_to_process.push_back(sp);
            }
        }
    } else {
        // Empire mode: include founder species always
        if (founder_id != 0) {
            void* founder_sp = FindSpeciesPtr(founder_id);
            if (founder_sp) {
                species_to_process.push_back(founder_sp);
            }
        }
        // Also check if secondary species exists or species with custom rights in rights_mgr
        if (arr && cap > 0) {
            for (uint32_t i = 0; i < cap && i < 1024; ++i) {
                void* sp = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)arr + i * 16 + 8), &sp) && sp) {
                    uint32_t sid = 0;
                    if (SafeReadU32((const void*)((uintptr_t)sp + 0x10), &sid) && sid != founder_id) {
                        uint8_t is_specific = 0;
                        if (fn_get_species_rights_ && rights_mgr) {
                            void* r = SafeGetSpeciesRightsCall(fn_get_species_rights_, rights_mgr, sp, &is_specific);
                            if (r && is_specific != 0) {
                                species_to_process.push_back(sp);
                            }
                        }
                    }
                }
            }
        }
    }

    nlohmann::json species_list = nlohmann::json::array();
    for (void* sp : species_to_process) {
        uint32_t sid = 0;
        SafeReadU32((const void*)((uintptr_t)sp + 0x10), &sid);

        std::string raw_name;
        SafeReadPdxString((const void*)((uintptr_t)sp + 0x60), raw_name);

        std::string raw_plural;
        SafeReadPdxString((const void*)((uintptr_t)sp + 0xB8), raw_plural);

        std::string raw_adj;
        SafeReadPdxString((const void*)((uintptr_t)sp + 0x110), raw_adj);

        std::string portrait_class;
        SafeReadPdxString((const void*)((uintptr_t)sp + 0x190), portrait_class);

        auto traits = ReadTraits(sp);
        nlohmann::json traits_json = nlohmann::json::array();
        for (const auto& t : traits) {
            traits_json.push_back({
                {"key", t.key},
                {"name", t.localized_name}
            });
        }

        SpeciesRights r = ReadRights(rights_mgr, sp);

        nlohmann::json sp_json = {
            {"species_id", sid},
            {"key", raw_name},
            {"name", LocalizeKey(raw_name)},
            {"plural", LocalizeKey(raw_plural)},
            {"adjective", LocalizeKey(raw_adj)},
            {"class", portrait_class},
            {"is_founder", (sid == founder_id)},
            {"traits", traits_json},
            {"rights", {
                {"citizenship", {{"key", r.citizenship}, {"name", r.citizenship_localized}}},
                {"living_standards", {{"key", r.living_standards}, {"name", r.living_standards_localized}}},
                {"military_service", {{"key", r.military_service}, {"name", r.military_service_localized}}},
                {"slavery_type", {{"key", r.slavery_type}, {"name", r.slavery_type_localized}}},
                {"purge_type", {{"key", r.purge_type}, {"name", r.purge_type_localized}}},
                {"population_controls", {{"key", r.population_controls}, {"name", r.population_controls_localized}}},
                {"colonization_controls", {{"key", r.colonization_controls}, {"name", r.colonization_controls_localized}}},
                {"migration_controls", {{"key", r.migration_controls}, {"name", r.migration_controls_localized}}},
                {"subspecies_integration", {{"key", r.subspecies_integration}, {"name", r.subspecies_integration_localized}}},
                {"cooldown_remaining_days", r.cooldown_remaining_days}
            }}
        };

        species_list.push_back(sp_json);
    }

    nlohmann::json catalog_json = nlohmann::json::object();
    for (const auto& [cat, opts] : rights_catalog_) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& opt : opts) {
            arr.push_back({
                {"key", opt.key},
                {"name", opt.localized_name}
            });
        }
        catalog_json[cat] = arr;
    }

    SpeciesSummary summary = ReadSummary();

    return {
        {"success", true},
        {"mode", galaxy_mode ? "galaxy" : "empire"},
        {"founder_species_id", founder_id},
        {"founder_species_name", summary.founder_species_name},
        {"total_empire_pops", summary.total_empire_pops},
        {"empire_colonies_count", summary.empire_colonies_count},
        {"total_species_in_galaxy", summary.total_species_in_galaxy},
        {"species_count", species_list.size()},
        {"species", species_list},
        {"available_rights_catalog", catalog_json}
    };
}

nlohmann::json SpeciesManager::SetSpeciesRight(uint32_t species_id, const std::string& category, const std::string& right_value) {
    if (!fn_engine_alloc_ || !fn_post_command_ || !fn_set_species_right_cmd_ctor_ || !fn_get_species_rights_) {
        return {
            {"success", false},
            {"error", "SpeciesManager command interface not fully initialized"}
        };
    }

    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"success", false},
            {"error", "Player country not found"}
        };
    }

    void* rights_mgr = nullptr;
    SafeReadPtr((const void*)((uintptr_t)country + 0x2B70), &rights_mgr);
    if (!rights_mgr) {
        return {
            {"success", false},
            {"error", "Species rights manager not found on country"}
        };
    }

    void* species = FindSpeciesPtr(species_id);
    if (!species) {
        return {
            {"success", false},
            {"error", "Species not found with ID " + std::to_string(species_id)}
        };
    }

    // Determine field offset and timestamp offset in CSpeciesRights (0xC8 bytes)
    uint32_t field_offset = 0;
    uint32_t ts_offset = 0;

    if (category == "citizenship") {
        field_offset = 0x10;
        ts_offset = 0xA4;
    } else if (category == "living_standards") {
        field_offset = 0x18;
        ts_offset = 0xA8;
    } else if (category == "military_service") {
        field_offset = 0x20;
        ts_offset = 0xAC;
    } else if (category == "slavery_type") {
        field_offset = 0x28;
        ts_offset = 0xB0;
    } else if (category == "purge_type") {
        field_offset = 0x30;
        ts_offset = 0xB4;
    } else if (category == "population_controls") {
        field_offset = 0x38;
        ts_offset = 0xB8;
    } else if (category == "colonization_controls") {
        field_offset = 0x40;
        ts_offset = 0xBC;
    } else if (category == "migration_controls") {
        field_offset = 0x48;
        ts_offset = 0xC0;
    } else if (category == "subspecies_integration") {
        field_offset = 0x50;
        ts_offset = 0; // integration doesn't have a 10-year cooldown timestamp
    } else {
        return {
            {"success", false},
            {"error", "Invalid rights category: '" + category + "'. Valid categories: citizenship, living_standards, military_service, slavery_type, purge_type, population_controls, colonization_controls, migration_controls, subspecies_integration"}
        };
    }

    void* new_right_type = GetSpeciesRightType(category, right_value);
    if (!new_right_type) {
        return {
            {"success", false},
            {"error", "Right value '" + right_value + "' not found in category '" + category + "' catalog"}
        };
    }

    uint8_t is_specific = 0;
    void* current_rights = SafeGetSpeciesRightsCall(fn_get_species_rights_, rights_mgr, species, &is_specific);
    if (!current_rights) {
        return {
            {"success", false},
            {"error", "Failed to retrieve current species rights from engine"}
        };
    }

    // Check cooldown
    if (ts_offset != 0) {
        uint32_t last_change_hour = 0;
        SafeReadU32((const void*)((uintptr_t)current_rights + ts_offset), &last_change_hour);
        uint32_t cur_hour = GetCurrentGameHours();
        const uint32_t COOLDOWN_HOURS = 86400; // 10 years
        if (last_change_hour > 0 && cur_hour > 0 && cur_hour < last_change_hour + COOLDOWN_HOURS) {
            uint32_t rem_days = (last_change_hour + COOLDOWN_HOURS - cur_hour) / 24;
            return {
                {"success", false},
                {"error", "Rights category '" + category + "' is on cooldown for " + std::to_string(rem_days) + " more days"}
            };
        }
    }

    // Prepare modified rights structure (0xC8 bytes)
    uint8_t modified_rights[0xC8];
    if (!SafeCopyChars((char*)modified_rights, (const char*)current_rights, 0xC8)) {
        return {
            {"success", false},
            {"error", "Failed to copy current rights buffer"}
        };
    }

    // Replace the 64-bit pointer
    *(void**)(modified_rights + field_offset) = new_right_type;

    // Allocate CSetSpeciesRightCommand (size 0xF8 = 248 bytes)
    void* cmd = fn_engine_alloc_(0xF8);
    if (!cmd) {
        return {
            {"success", false},
            {"error", "Engine allocator failed for CSetSpeciesRightCommand"}
        };
    }

    void* constructed_cmd = SafeCallSetSpeciesRightCtor(fn_set_species_right_cmd_ctor_, cmd, country, species, modified_rights, 1);
    if (!constructed_cmd) {
        return {
            {"success", false},
            {"error", "Exception in CSetSpeciesRightCommand constructor"}
        };
    }

    if (!SafePostCommand(fn_post_command_, constructed_cmd)) {
        return {
            {"success", false},
            {"error", "Exception in PostCommand"}
        };
    }

    LOGF("[SPECIES_MGR] Dispatched CSetSpeciesRightCommand for species_id %u, %s -> %s",
         species_id, category.c_str(), right_value.c_str());

    return {
        {"success", true},
        {"species_id", species_id},
        {"category", category},
        {"right_value", right_value},
        {"message", "Species right modification command dispatched successfully"}
    };
}

} // namespace bridge
