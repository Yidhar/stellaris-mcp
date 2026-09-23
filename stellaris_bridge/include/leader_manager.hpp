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

struct HiredLeaderDetail {
    uint32_t id{ 0 };
    std::string key;
    std::string name;
    std::string class_key;
    std::string class_name;
    std::string subclass_key;
    std::string subclass_name;
    uint32_t level{ 0 };
    uint32_t age{ 0 };
    std::string ethic_key;
    std::string ethic_name;
    uint8_t assignment_type{ 0 };
    std::string assignment_type_name;
    uint32_t assignment_target{ 0 };
    uint32_t hire_date{ 0 };
    std::vector<std::string> traits;
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
    using FnEngineAlloc = void* (*)(size_t);
    using FnPostCommand = void (*)(void* cmd, int unk);
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);

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

    // Helpers
    void* FindLeaderPtr(uint32_t leader_id);
    HiredLeaderDetail ReadLeader(uint32_t leader_id);

private:
    LeaderManager() = default;

    uintptr_t base_address_{ 0 };

    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };

    uintptr_t hire_leader_cmd_vtable_{ 0 };
    uintptr_t fire_leader_cmd_vtable_{ 0 };
    uintptr_t assign_leader_cmd_vtable_{ 0 };

    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();
    std::string LocalizeKey(const std::string& key);
};

} // namespace bridge
