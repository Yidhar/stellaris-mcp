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
    std::string kind;                   // project, species_modification, uplift, debris
    int32_t days_left{ -1 };            // -1: no deadline
    uint32_t species_id{ 0xFFFFFFFF };  // species modification / uplift: the resulting species
};

// A discovered anomaly the country has not researched yet (the planet still holds its category).
struct AnomalyItem {
    uint32_t planet_id{ 0xFFFFFFFF };
    std::string planet_name;
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

    // CCountry::events (CCountryEventManager) of the country
    std::vector<SpecialProjectItem> ReadSpecialProjects(void* country);
    std::vector<AnomalyItem> ReadAnomalies(void* country);

    // Layer 3: Action command execution
    nlohmann::json SetSituationApproach(uint32_t situation_id, const std::string& approach_key);

private:
    SituationLogManager() = default;

    uintptr_t base_address_{ 0 };

    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
    FnPdxStringAssign fn_pdx_string_assign_{ nullptr };


    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();

    std::string LocalizeKey(const std::string& key);
};

} // namespace bridge
