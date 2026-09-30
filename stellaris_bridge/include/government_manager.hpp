#pragma once

#include "common.hpp"
#include "leader_manager.hpp"
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
    std::vector<LeaderTraitDetail> traits;
    int32_t trait_selections_available{ 0 };
    std::vector<LeaderTraitDetail> trait_options;
    std::vector<LeaderTraitDetail> trait_upgrade_options;
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
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);

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
    // Starts a new council agenda (CSetCouncilAgendaCommand); see available_agendas.
    nlohmann::json SetCouncilAgenda(const std::string& agenda_key);

    // Layer 2: civics -- current civics, civic points, and the civics the game would accept
    // added now (each checked with CChangeGovernmentCommand::IsValid). With `civic_key`, only
    // that civic, with the game's reason when it cannot be taken.
    nlohmann::json GetCivicsJson(const std::string& civic_key);
    // Reforms the government's civics (CChangeGovernmentCommand): adds `add`, removes `remove`,
    // keeping the authority. Checked against civic points and the game's own validation.
    nlohmann::json ChangeCivics(const std::vector<std::string>& add, const std::vector<std::string>& remove);

private:
    GovernmentManager() = default;

    uintptr_t base_address_{ 0 };

    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };


    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();
    std::string LocalizeKey(const std::string& key);

    void* FindLeaderPtr(uint32_t leader_id);
    LeaderDetail ReadLeader(uint32_t leader_id);
    nlohmann::json AvailableAgendasJson();
    std::vector<void*> CurrentCivics(void* country);
    void* FindCivicType(const std::string& key);
    // Validates (and optionally posts) a CChangeGovernmentCommand with these civics.
    bool CheckCivics(void* country, const std::vector<void*>& civics, bool post, std::string* why);
    // Reform cooldown/unity check, the unity cost of a reform and the unity stockpile.
    nlohmann::json ReformStatusJson(void* country);
    // The civic's requirements as the game lists them for this empire (flags: can add).
    std::string CivicRequirementsText(void* country, void* civic, bool* possible);
    std::string AgendaName(const std::string& key);
};

} // namespace bridge
