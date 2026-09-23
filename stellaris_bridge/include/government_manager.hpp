#pragma once

#include "common.hpp"
#include <string>
#include <vector>

namespace bridge {

struct LeaderDetail {
    uint32_t id{ 0 };
    std::string key;
    std::string name;
    std::string class_key;
    std::string class_name;
    uint32_t level{ 0 };
    uint32_t age{ 0 };
    std::string ethic_key;
    std::string ethic_name;
};

struct CouncilSeatDetail {
    uint32_t seat_index{ 0 };
    std::string position_key;
    std::string position_name;
    bool is_ruler{ false };
    bool is_assigned{ false };
    LeaderDetail leader;
};

struct CouncilAgendaDetail {
    std::string key;
    std::string name;
    double progress{ 0.0 };
    double cost{ 0.0 };
    bool is_ready{ false };
};

struct CouncilSummary {
    std::string ruler_name;
    std::string active_agenda;
    std::string active_agenda_name;
    double agenda_progress{ 0.0 };
    double agenda_cost{ 0.0 };
    bool agenda_ready{ false };
    uint32_t councilor_count{ 0 };
};

struct FullGovernmentState {
    CouncilSummary summary;
    std::string authority;
    std::string authority_name;
    std::string government_type;
    std::string government_type_name;
    std::string origin;
    std::string origin_name;
    std::vector<std::pair<std::string, std::string>> civics; // key, name
    std::vector<std::pair<std::string, std::string>> ethics; // key, name
    LeaderDetail ruler;
    CouncilAgendaDetail agenda;
    std::vector<CouncilSeatDetail> seats;
};

class GovernmentManager {
public:
    using FnEngineAlloc = void* (*)(size_t);
    using FnPostCommand = void (*)(void* cmd, int unk);
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);
    using FnGetAgendaCost = int64_t* (*)(void* agenda, int64_t* out_cost, void* country, void* unk);

    static GovernmentManager& Get();

    bool Init(uintptr_t base_address);

    // Layer 1: High-level overview indicator for stellaris_get_status
    CouncilSummary GetSummary();
    nlohmann::json GetSummaryJson();

    // Layer 2: Detailed domain query for stellaris_get_government
    FullGovernmentState GetGovernmentState();
    nlohmann::json GetGovernmentJson();

    // Layer 3: Action command execution for stellaris_launch_council_agenda
    nlohmann::json LaunchCouncilAgenda();

private:
    GovernmentManager() = default;

    uintptr_t base_address_{ 0 };

    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
    FnGetAgendaCost fn_get_agenda_cost_{ nullptr };

    uintptr_t finish_agenda_cmd_vtable_{ 0 };

    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();
    std::string LocalizeKey(const std::string& key);

    void* FindLeaderPtr(uint32_t leader_id);
    LeaderDetail ReadLeader(uint32_t leader_id);
};

} // namespace bridge
