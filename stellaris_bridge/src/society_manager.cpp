#include "society_manager.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "alert_manager.hpp"
#include "command_builder.hpp"
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

SocietyManager& SocietyManager::Get() {
    static SocietyManager instance;
    return instance;
}

bool SocietyManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);

    LOGF("[SOCIETY] Initialized (Base: 0x%llX)", (unsigned long long)base_address_);

    RefreshTraditionCache();

    return true;
}

void* SocietyManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CCountry), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
    if (mgr && (uintptr_t)mgr >= 0x10000) {
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
    return SafeLocalize(base_address_, key);
}

void SocietyManager::RefreshTraditionCache() {
    if (!base_address_) return;

    // First principles: read native global CTraditionDatabase directly at base + sdk::glob::TGameDatabase_CTraditionTypeDatabase_pInstance
    void* tr_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::TGameDatabase_CTraditionTypeDatabase_pInstance), &tr_db) || !tr_db) {
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
    // CCountry::edicts is a CPdxArray<SCountryActiveEdict>: data at +0, count at +0xC.
    SafeReadU32((const void*)((uintptr_t)country + sdk::ent::CCountry::edicts + 0xC), &summary.active_edicts_count);

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

    // 1. Active edicts: CCountry::edicts (CPdxArray<SCountryActiveEdict>, 0x20-byte entries)
    nlohmann::json active_edicts_json = nlohmann::json::array();
    std::unordered_map<std::string, bool> active_map;

    void* edict_entries = nullptr;
    uint32_t edict_cnt = 0;
    constexpr std::ptrdiff_t kEdicts = sdk::ent::CCountry::edicts;
    if (SafeReadPtr((const void*)((uintptr_t)country + kEdicts), &edict_entries) && edict_entries &&
        SafeReadU32((const void*)((uintptr_t)country + kEdicts + 0xC), &edict_cnt)) {
        for (uint32_t i = 0; i < edict_cnt && i < 64; ++i) {
            void* edict_p = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)edict_entries + i * 0x20 + sdk::ent::SCountryActiveEdict::edict),
                             &edict_p) || !edict_p) {
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

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &date_mgr) && date_mgr && (uintptr_t)date_mgr >= 0x10000) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[SOCIETY] Posting CActivateTraditionCommand for tradition '%s' (0x%llX), tick %u...",
        tradition_key.c_str(), (unsigned long long)tr_ptr, tick_timestamp);

    namespace trad = sdk::cmd::activate_tradition_command;
    auto cmd = CommandBuilder::Get().Create(trad::kSpec);
    cmd.Set<uint32_t>(trad::country, country_id)
       .Set<void*>(trad::object, tr_ptr);
    if (!cmd.Post()) {
        return {
            {"error", {
                {"code", -32078},
                {"message", cmd.error()}
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

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &date_mgr) && date_mgr && (uintptr_t)date_mgr >= 0x10000) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[SOCIETY] Posting %s for edict '%s', tick %u...",
        enabled ? "CAddEdictCommand" : "CRemoveEdictCommand", edict_key.c_str(), tick_timestamp);

    namespace add = sdk::cmd::add_edict_command;
    namespace remove = sdk::cmd::remove_edict_command;
    static_assert(add::name == remove::name && add::country == remove::country,
                  "add/remove edict commands are expected to share one payload layout");
    auto cmd = CommandBuilder::Get().Create(enabled ? add::kSpec : remove::kSpec);
    cmd.SetString(add::name, edict_key)
       .Set<uint32_t>(add::country, country_id);
    if (!cmd.Post()) {
        return {
            {"error", {
                {"code", -32084},
                {"message", cmd.error()}
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
