#pragma once

#include "common.hpp"
#include <string>
#include <vector>

namespace bridge {

struct LeaderSummary {
    uint32_t total_hired{ 0 };
    uint32_t leader_capacity{ 0 };
    uint32_t pool_count{ 0 };
    bool has_unspent_trait_points{ false };
};

struct LeaderTraitDetail {
    std::string key;
    std::string name;
    uint32_t tier{ 1 };
};

// [{key, name, tier}, ...] for API responses.
nlohmann::json TraitsJson(const std::vector<LeaderTraitDetail>& traits);

struct HiredLeaderDetail {
    uint32_t id{ 0 };
    std::string key;
    std::string name;
    std::string title;
    std::string class_key;
    std::string class_name;
    std::string background_job_key;   // job held before becoming a leader
    std::string background_job_name;
    uint32_t level{ 0 };
    double experience{ 0.0 };
    uint32_t age{ 0 };
    std::string ethic_key;
    std::string ethic_name;
    uint8_t assignment_type{ 0 };
    std::string assignment_type_name;
    uint32_t assignment_target{ 0 };
    uint32_t hire_date{ 0 };
    std::vector<LeaderTraitDetail> traits;
    bool has_unspent_trait_points{ false };
    bool is_councilor{ false };
    // Level-up trait picks offered by the engine (CLeader::available_trait / available_trait_2).
    int32_t trait_selections_available{ 0 };
    std::vector<LeaderTraitDetail> trait_options;
    std::vector<LeaderTraitDetail> trait_upgrade_options;
};

struct CandidateDetail {
    uint32_t id{ 0 };
    std::string key;
    std::string name;
    std::string class_key;
    std::string class_name;
    uint32_t level{ 0 };
    uint32_t age{ 0 };
    double hire_cost{ 0.0 };
    std::string ethic_key;
    std::string ethic_name;
};

class LeaderManager {
public:
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);
    using FnGetLocalizedLeaderName = void (*)(void* out_str, void* name_obj, int mode);

    static LeaderManager& Get();

    bool Init(uintptr_t base_address);

    // Layer 1: High-level overview indicator for stellaris_get_status
    LeaderSummary GetSummary();
    nlohmann::json GetSummaryJson();

    // Layer 2: Detailed domain query for stellaris_get_leaders
    nlohmann::json GetLeadersJson();

    // Layer 3: Action commands
    nlohmann::json HireLeader(uint32_t candidate_id);
    nlohmann::json DismissLeader(uint32_t leader_id);
    nlohmann::json AssignLeader(uint32_t leader_id, uint8_t assignment_type, uint32_t target_id);
    // Picks one of the traits the engine currently offers this leader (level-up selection).
    nlohmann::json SelectTrait(uint32_t leader_id, const std::string& trait_key);

    // Helpers
    void* FindLeaderPtr(uint32_t leader_id);
    HiredLeaderDetail ReadLeader(uint32_t leader_id);

private:
    LeaderManager() = default;

    uintptr_t base_address_{ 0 };

    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
    FnGetLocalizedLeaderName fn_get_localized_leader_name_{ nullptr };


    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();
    std::string LocalizeKey(const std::string& key);
};

} // namespace bridge
