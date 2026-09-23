#pragma once

#include "common.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace bridge {

struct SocietySummary {
    bool can_unlock_tradition{ false };
    double next_tradition_cost{ 300.0 };
    uint32_t adopted_trees_count{ 0 };
    uint32_t unlocked_traditions_count{ 0 };
    uint32_t active_edicts_count{ 0 };
    double edict_fund{ 0.0 };
};

class SocietyManager {
public:
    using FnEngineAlloc = void* (*)(size_t);
    using FnPostCommand = void (*)(void* cmd, int unk);
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);

    static SocietyManager& Get();

    bool Init(uintptr_t base_address);

    // Layer 1: High-level overview indicator for stellaris_get_status
    SocietySummary GetSummary();
    nlohmann::json GetSummaryJson();

    // Layer 2: Detailed domain query
    nlohmann::json GetTraditionsJson();
    nlohmann::json GetEdictsJson();

    // Layer 3: Action command execution
    nlohmann::json AdoptTradition(const std::string& tradition_key);
    nlohmann::json ToggleEdict(const std::string& edict_key, bool enabled);

private:
    SocietyManager() = default;

    uintptr_t base_address_{ 0 };

    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };

    uintptr_t activate_tradition_cmd_vtable_{ 0 };
    uintptr_t add_edict_cmd_vtable_{ 0 };
    uintptr_t remove_edict_cmd_vtable_{ 0 };

    // Cached pointers for all CTradition* definitions indexed by key
    std::unordered_map<std::string, void*> tradition_cache_;
    std::unordered_map<std::string, void*> category_cache_;

    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();
    std::string LocalizeKey(const std::string& key);

    void RefreshTraditionCache();
    void* FindTraditionPtr(const std::string& key);
};

} // namespace bridge
