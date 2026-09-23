#pragma once

#include "common.hpp"
#include <string>
#include <vector>

namespace bridge {

struct SituationItem {
    uint32_t id{ 0 };
    std::string key;
    std::string name;
    std::string current_approach;
    std::string current_approach_name;
    double progress{ 0.0 };
    double monthly_change{ 0.0 };
    uint32_t owner_country_id{ 0 };
    bool is_player{ false };
};

struct SpecialProjectItem {
    uint32_t id{ 0 };
    std::string key;
    std::string name;
};

struct AnomalyItem {
    uint32_t id{ 0 };
    std::string key;
    std::string name;
};

struct SituationLogSummary {
    uint32_t situations_count{ 0 };
    uint32_t special_projects_count{ 0 };
    uint32_t anomalies_count{ 0 };
};

struct FullSituationLogState {
    SituationLogSummary summary;
    std::vector<SituationItem> situations;
    std::vector<SpecialProjectItem> special_projects;
    std::vector<AnomalyItem> anomalies;
};

class SituationLogManager {
public:
    using FnEngineAlloc = void* (*)(size_t);
    using FnPostCommand = void (*)(void* cmd, int unk);
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);
    using FnPdxStringAssign = void* (*)(void* pdx_str, const char* src, size_t len);

    static SituationLogManager& Get();

    bool Init(uintptr_t base_address);

    // Layer 1: High-level overview/indicator
    SituationLogSummary GetSummary();

    // Layer 2: Detailed domain query
    FullSituationLogState GetSituationLogState(bool player_only = true);
    nlohmann::json GetSituationLogJson(bool player_only = true);

    // Layer 3: Action command execution
    nlohmann::json SetSituationApproach(uint32_t situation_id, const std::string& approach_key);

private:
    SituationLogManager() = default;

    uintptr_t base_address_{ 0 };

    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
    FnPdxStringAssign fn_pdx_string_assign_{ nullptr };

    uintptr_t command_vtable_{ 0 };

    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();

    std::string LocalizeKey(const std::string& key);
};

} // namespace bridge
