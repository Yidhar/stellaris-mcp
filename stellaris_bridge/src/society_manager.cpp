#include "society_manager.hpp"
#include "alert_manager.hpp"
#include <cstring>
#include <cmath>

namespace bridge {

// Safe memory access helpers
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

static bool SafeReadU8(const void* addr, uint8_t* out) {
    __try {
        *out = *(const uint8_t*)addr;
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

static bool SafeLocalizeCall(SocietyManager::FnLocalize fn_localize,
                             SocietyManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(SocietyManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SafePostCommand(SocietyManager::FnPostCommand fn_post, void* cmd) {
    __try {
        fn_post(cmd, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

SocietyManager& SocietyManager::Get() {
    static SocietyManager instance;
    return instance;
}

bool SocietyManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);

    // True Command Vtables discovered via reverse engineering
    activate_tradition_cmd_vtable_ = base_address_ + 0x23429D0;
    add_edict_cmd_vtable_ = base_address_ + 0x2393228;
    remove_edict_cmd_vtable_ = base_address_ + 0x2393398;

    LOGF("[SOCIETY] Initialized (Base: 0x%llX, ActTradVT: 0x%llX, AddEdictVT: 0x%llX, RemEdictVT: 0x%llX)",
        (unsigned long long)base_address_,
        (unsigned long long)activate_tradition_cmd_vtable_,
        (unsigned long long)add_edict_cmd_vtable_,
        (unsigned long long)remove_edict_cmd_vtable_);

    RefreshTraditionCache();

    return fn_engine_alloc_ != nullptr && fn_post_command_ != nullptr;
}

void* SocietyManager::GetPlayerCountry() {
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

uint32_t SocietyManager::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;

    uint32_t cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid);
    return cid;
}

std::string SocietyManager::LocalizeKey(const std::string& key) {
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

void SocietyManager::RefreshTraditionCache() {
    if (!base_address_) return;

    // First principles: read native global CTraditionDatabase directly at base + 0x3110908
    void* tr_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3110908), &tr_db) || !tr_db) {
        return;
    }

    void* tr_arr = nullptr;
    uint32_t tr_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)tr_db + 0x50), &tr_arr) || !tr_arr ||
        !SafeReadU32((const void*)((uintptr_t)tr_db + 0x5C), &tr_cnt) || tr_cnt == 0) {
        return;
    }

    tradition_cache_.clear();
    category_cache_.clear();

    for (uint32_t i = 0; i < tr_cnt && i < 512; ++i) {
        void* tr_p = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)tr_arr + i * 8), &tr_p) || !tr_p) {
            continue;
        }

        std::string k;
        if (SafeReadPdxString((const void*)((uintptr_t)tr_p + 0x20), k) && !k.empty()) {
            tradition_cache_[k] = tr_p;

            // Extract category if available
            void* cat_p = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)tr_p + 0x50), &cat_p) && cat_p) {
                std::string cat_k;
                if (SafeReadPdxString((const void*)((uintptr_t)cat_p + 0x20), cat_k) && !cat_k.empty()) {
                    category_cache_[cat_k] = cat_p;
                }
            }
        }
    }

    LOGF("[SOCIETY] Tradition cache loaded from CTraditionDatabase: %zu traditions, %zu categories indexed",
        tradition_cache_.size(), category_cache_.size());
}

void* SocietyManager::FindTraditionPtr(const std::string& key) {
    if (tradition_cache_.empty()) {
        RefreshTraditionCache();
    }
    auto it = tradition_cache_.find(key);
    if (it != tradition_cache_.end()) {
        return it->second;
    }
    // Try refreshing once more if not found
    RefreshTraditionCache();
    it = tradition_cache_.find(key);
    return it != tradition_cache_.end() ? it->second : nullptr;
}

SocietySummary SocietyManager::GetSummary() {
    SocietySummary summary{};
    void* country = GetPlayerCountry();
    if (!country) return summary;

    SafeReadU32((const void*)((uintptr_t)country + 0x30B4), &summary.adopted_trees_count);
    SafeReadU32((const void*)((uintptr_t)country + 0x30CC), &summary.unlocked_traditions_count);
    SafeReadU32((const void*)((uintptr_t)country + 0x287C), &summary.active_edicts_count);

    // Tradition cost: standard Stellaris defines formula
    // Base 300.0, scaling with unlocked count
    if (summary.unlocked_traditions_count == 0) {
        summary.next_tradition_cost = 300.0;
    } else {
        double exp_factor = std::pow((double)summary.unlocked_traditions_count * 8.0, 1.8);
        summary.next_tradition_cost = std::round((300.0 + exp_factor) * 100.0) / 100.0;
    }

    // Check can_unlock_tradition:
    // 1. Check if Alert 17 (alert_unlock_tradition) is currently active
    auto alerts = AlertManager::Get().GetAlerts();
    for (const auto& a : alerts) {
        if (a.alert_id == 17) {
            summary.can_unlock_tradition = true;
            break;
        }
    }

    // 2. Also check if unity balance exceeds next cost
    void* res_mgr = nullptr;
    if (!summary.can_unlock_tradition && SafeReadPtr((const void*)((uintptr_t)country + 0x1878), &res_mgr) && res_mgr) {
        // Resources stockpile
    }

    return summary;
}

nlohmann::json SocietyManager::GetSummaryJson() {
    SocietySummary s = GetSummary();
    return {
        {"can_unlock_tradition", s.can_unlock_tradition},
        {"next_tradition_cost", s.next_tradition_cost},
        {"adopted_trees_count", s.adopted_trees_count},
        {"unlocked_traditions_count", s.unlocked_traditions_count},
        {"active_edicts_count", s.active_edicts_count},
        {"edict_fund", s.edict_fund}
    };
}

nlohmann::json SocietyManager::GetTraditionsJson() {
    SocietySummary summary = GetSummary();
    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"error", {
                {"code", -32071},
                {"message", "Player country not found"}
            }}
        };
    }

    RefreshTraditionCache();

    // 1. Adopted Trees from +0x30A8
    nlohmann::json adopted_trees_json = nlohmann::json::array();
    std::unordered_map<std::string, bool> adopted_tree_keys;

    void* cat_arr = nullptr;
    uint32_t cat_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x30A8), &cat_arr) && cat_arr &&
        SafeReadU32((const void*)((uintptr_t)country + 0x30B4), &cat_cnt)) {
        for (uint32_t i = 0; i < cat_cnt && i < 16; ++i) {
            void* cat_p = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)cat_arr + i * 8), &cat_p) || !cat_p) {
                continue;
            }

            std::string cat_k;
            SafeReadPdxString((const void*)((uintptr_t)cat_p + 0x20), cat_k);
            if (!cat_k.empty()) {
                adopted_tree_keys[cat_k] = true;
                adopted_trees_json.push_back({
                    {"key", cat_k},
                    {"name", LocalizeKey(cat_k)}
                });
            }
        }
    }

    // 2. Unlocked Traditions from +0x30C0
    nlohmann::json unlocked_trads_json = nlohmann::json::array();
    std::unordered_map<std::string, uint32_t> tree_unlocked_counts;

    void* tr_arr = nullptr;
    uint32_t tr_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x30C0), &tr_arr) && tr_arr &&
        SafeReadU32((const void*)((uintptr_t)country + 0x30CC), &tr_cnt)) {
        for (uint32_t i = 0; i < tr_cnt && i < 128; ++i) {
            void* tr_p = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)tr_arr + i * 8), &tr_p) || !tr_p) {
                continue;
            }

            std::string tr_k;
            SafeReadPdxString((const void*)((uintptr_t)tr_p + 0x20), tr_k);

            void* cat_p = nullptr;
            std::string cat_k;
            if (SafeReadPtr((const void*)((uintptr_t)tr_p + 0x50), &cat_p) && cat_p) {
                SafeReadPdxString((const void*)((uintptr_t)cat_p + 0x20), cat_k);
            }

            if (!cat_k.empty()) {
                tree_unlocked_counts[cat_k]++;
            }

            unlocked_trads_json.push_back({
                {"key", tr_k},
                {"name", LocalizeKey(tr_k)},
                {"tree_key", cat_k},
                {"tree_name", LocalizeKey(cat_k)}
            });
        }
    }

    // Attach tree unlocked count & finish status to adopted trees
    for (auto& t : adopted_trees_json) {
        std::string k = t["key"].get<std::string>();
        uint32_t c = tree_unlocked_counts[k];
        t["unlocked_count"] = c;
        t["is_finished"] = (c >= 6); // adopt + 5 traditions = 6 items
    }

    // 3. Available Tree Catalog: dynamically built from engine-discovered categories
    nlohmann::json available_trees_json = nlohmann::json::array();
    for (const auto& [cat_k, cat_p] : category_cache_) {
        if (cat_k == "tradition_dummy" || cat_k.empty()) continue;
        if (adopted_tree_keys.find(cat_k) == adopted_tree_keys.end()) {
            std::string stem = (cat_k.rfind("tradition_", 0) == 0 ? cat_k.substr(10) : cat_k);
            std::string candidate_perk = "tr_" + stem + "_adopt";
            if (tradition_cache_.find(candidate_perk) != tradition_cache_.end()) {
                available_trees_json.push_back({
                    {"key", cat_k},
                    {"name", LocalizeKey(cat_k)},
                    {"adopt_tradition_key", candidate_perk},
                    {"adopt_tradition_name", LocalizeKey(candidate_perk)}
                });
            }
        }
    }

    // 4. Ascension Perks
    nlohmann::json perks_json = nlohmann::json::array();
    void* ap_arr = nullptr;
    uint32_t ap_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x30D0), &ap_arr) && ap_arr &&
        SafeReadU32((const void*)((uintptr_t)country + 0x30E4), &ap_cnt)) {
        for (uint32_t i = 0; i < ap_cnt && i < 16; ++i) {
            void* ap_p = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)ap_arr + i * 8), &ap_p) || !ap_p) {
                continue;
            }
            std::string ap_k;
            SafeReadPdxString((const void*)((uintptr_t)ap_p + 0x20), ap_k);
            perks_json.push_back({
                {"key", ap_k},
                {"name", LocalizeKey(ap_k)}
            });
        }
    }

    uint32_t finished_trees_count = 0;
    for (const auto& t : adopted_trees_json) {
        if (t["is_finished"].get<bool>()) {
            finished_trees_count++;
        }
    }
    uint32_t available_perk_slots = finished_trees_count > perks_json.size()
        ? (finished_trees_count - (uint32_t)perks_json.size()) : 0;

    return {
        {"summary", {
            {"can_unlock_tradition", summary.can_unlock_tradition},
            {"next_tradition_cost", summary.next_tradition_cost},
            {"adopted_trees_count", summary.adopted_trees_count},
            {"max_trees_count", 7},
            {"unlocked_traditions_count", summary.unlocked_traditions_count},
            {"ascension_perk_slots_available", available_perk_slots}
        }},
        {"adopted_trees", adopted_trees_json},
        {"unlocked_traditions", unlocked_trads_json},
        {"available_trees", available_trees_json},
        {"ascension_perks", {
            {"adopted", perks_json},
            {"total_slots", 8},
            {"available_slots", available_perk_slots}
        }}
    };
}

nlohmann::json SocietyManager::GetEdictsJson() {
    SocietySummary summary = GetSummary();
    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"error", {
                {"code", -32072},
                {"message", "Player country not found"}
            }}
        };
    }

    // 1. Active Edicts from [country + 0x2870]
    nlohmann::json active_edicts_json = nlohmann::json::array();
    std::unordered_map<std::string, bool> active_map;

    void* edict_entries = nullptr;
    uint32_t edict_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x2870), &edict_entries) && edict_entries &&
        SafeReadU32((const void*)((uintptr_t)country + 0x287C), &edict_cnt)) {
        for (uint32_t i = 0; i < edict_cnt && i < 64; ++i) {
            void* edict_p = nullptr;
            // entry + 0x10 holds CEdict* pointer
            if (!SafeReadPtr((const void*)((uintptr_t)edict_entries + i * 0x20 + 0x10), &edict_p) || !edict_p) {
                continue;
            }

            std::string edict_k;
            SafeReadPdxString((const void*)((uintptr_t)edict_p + 0x20), edict_k);
            if (!edict_k.empty()) {
                active_map[edict_k] = true;
                active_edicts_json.push_back({
                    {"key", edict_k},
                    {"name", LocalizeKey(edict_k)}
                });
            }
        }
    }

    // 2. Standard Edicts Catalog
    static const std::vector<std::string> kKnownEdicts = {
        "map_the_stars",
        "subsidize_farming",
        "subsidize_mining",
        "subsidize_energy",
        "subsidize_research",
        "extended_shifts",
        "peace_festivals",
        "information_quarantine",
        "fortify_the_border",
        "diplomatic_grants",
        "tracking_implants",
        "land_appropriation",
        "enhanced_surveillance",
        "evacuation_protocols"
    };

    nlohmann::json all_edicts_json = nlohmann::json::array();
    for (const auto& ek : kKnownEdicts) {
        bool active = (active_map.find(ek) != active_map.end());
        all_edicts_json.push_back({
            {"key", ek},
            {"name", LocalizeKey(ek)},
            {"is_active", active}
        });
    }

    return {
        {"summary", {
            {"active_edicts_count", summary.active_edicts_count},
            {"edict_fund", summary.edict_fund}
        }},
        {"active_edicts", active_edicts_json},
        {"available_edicts", all_edicts_json}
    };
}

nlohmann::json SocietyManager::AdoptTradition(const std::string& tradition_key) {
    if (tradition_key.empty()) {
        return {
            {"error", {
                {"code", -32073},
                {"message", "tradition_key must not be empty"}
            }}
        };
    }

    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"error", {
                {"code", -32074},
                {"message", "Player country not found"}
            }}
        };
    }

    void* tr_ptr = FindTraditionPtr(tradition_key);
    if (!tr_ptr) {
        return {
            {"error", {
                {"code", -32075},
                {"message", "Tradition definition '" + tradition_key + "' not found in game engine"}
            }}
        };
    }

    if (!fn_engine_alloc_ || !fn_post_command_ || !activate_tradition_cmd_vtable_) {
        return {
            {"error", {
                {"code", -32076},
                {"message", "Native command dispatch functions not initialized"}
            }}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &date_mgr) && date_mgr) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[SOCIETY] Posting CActivateTraditionCommand for tradition '%s' (0x%llX), tick %u...",
        tradition_key.c_str(), (unsigned long long)tr_ptr, tick_timestamp);

    void* cmd = fn_engine_alloc_(0x30);
    if (!cmd) {
        return {
            {"error", {
                {"code", -32077},
                {"message", "Engine allocator returned null for CActivateTraditionCommand"}
            }}
        };
    }

    memset(cmd, 0, 0x30);
    *(void**)cmd = (void*)activate_tradition_cmd_vtable_;
    *(uint32_t*)((uintptr_t)cmd + 0x08) = tick_timestamp;
    *(uint32_t*)((uintptr_t)cmd + 0x0C) = 0;
    *(uint16_t*)((uintptr_t)cmd + 0x10) = 0xFFFF;
    *(uint16_t*)((uintptr_t)cmd + 0x12) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x14) = 1; // satisfies IsValid()
    *(uint32_t*)((uintptr_t)cmd + 0x20) = country_id;
    *(void**)((uintptr_t)cmd + 0x28) = tr_ptr;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[SOCIETY] Exception occurred executing PostCommand for CActivateTraditionCommand!");
        return {
            {"error", {
                {"code", -32078},
                {"message", "Exception occurred executing PostCommand for CActivateTraditionCommand"}
            }}
        };
    }

    LOGF("[SOCIETY] CActivateTraditionCommand successfully posted.");
    return {
        {"success", true},
        {"tradition_key", tradition_key},
        {"tradition_name", LocalizeKey(tradition_key)},
        {"country_id", country_id},
        {"message", "Tradition adopted successfully"}
    };
}

nlohmann::json SocietyManager::ToggleEdict(const std::string& edict_key, bool enabled) {
    if (edict_key.empty()) {
        return {
            {"error", {
                {"code", -32080},
                {"message", "edict_key must not be empty"}
            }}
        };
    }

    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"error", {
                {"code", -32081},
                {"message", "Player country not found"}
            }}
        };
    }

    if (!fn_engine_alloc_ || !fn_post_command_ || !add_edict_cmd_vtable_ || !remove_edict_cmd_vtable_) {
        return {
            {"error", {
                {"code", -32082},
                {"message", "Native command dispatch functions not initialized"}
            }}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &date_mgr) && date_mgr) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[SOCIETY] Posting %s for edict '%s', tick %u...",
        enabled ? "CAddEdictCommand" : "CRemoveEdictCommand", edict_key.c_str(), tick_timestamp);

    void* cmd = fn_engine_alloc_(0x58);
    if (!cmd) {
        return {
            {"error", {
                {"code", -32083},
                {"message", "Engine allocator returned null for edict command"}
            }}
        };
    }

    memset(cmd, 0, 0x58);
    *(void**)cmd = (void*)(enabled ? add_edict_cmd_vtable_ : remove_edict_cmd_vtable_);
    *(uint32_t*)((uintptr_t)cmd + 0x08) = tick_timestamp;
    *(uint32_t*)((uintptr_t)cmd + 0x0C) = 0;
    *(uint16_t*)((uintptr_t)cmd + 0x10) = 0xFFFF;
    *(uint16_t*)((uintptr_t)cmd + 0x12) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x14) = 1; // satisfies IsValid()

    // Key string at +0x30
    RawPdxString* str = (RawPdxString*)((uintptr_t)cmd + 0x30);
    str->size = edict_key.size();
    str->capacity = 15;
    if (edict_key.size() < 16) {
        memcpy(str->buf, edict_key.data(), edict_key.size());
    } else {
        // Fallback for long keys: engine alloc
        char* heap_str = (char*)fn_engine_alloc_(edict_key.size() + 1);
        if (heap_str) {
            memcpy(heap_str, edict_key.data(), edict_key.size());
            heap_str[edict_key.size()] = '\0';
            str->heap_ptr = heap_str;
            str->capacity = edict_key.size() + 1;
        } else {
            memcpy(str->buf, edict_key.data(), 15);
            str->size = 15;
        }
    }

    *(uint32_t*)((uintptr_t)cmd + 0x50) = country_id;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[SOCIETY] Exception occurred executing PostCommand for edict command!");
        return {
            {"error", {
                {"code", -32084},
                {"message", "Exception occurred executing PostCommand for edict command"}
            }}
        };
    }

    LOGF("[SOCIETY] Edict command successfully posted.");
    return {
        {"success", true},
        {"edict_key", edict_key},
        {"edict_name", LocalizeKey(edict_key)},
        {"enabled", enabled},
        {"country_id", country_id},
        {"message", enabled ? "Edict enabled successfully" : "Edict disabled successfully"}
    };
}

} // namespace bridge
