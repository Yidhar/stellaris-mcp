#pragma once

#include "common.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace bridge {

class ContactsManager {
public:
    static ContactsManager& Get();


    bool Init(uintptr_t base_address);

    // Queries
    nlohmann::json GetContactsInfo(const std::string& mode);
    nlohmann::json GetSummaryJson();

    // Diplomatic actions toward one empire, as the diplomacy view lists them (Layer 2): every
    // CDiplomaticActionType the game shows for that pair, with whether the player can propose it
    // now and, when not, the game's reason (CDiploActionProposeCommand::IsValid)
    nlohmann::json GetDiplomaticActionsJson(uint32_t country_id);
    // propose one (native CDiploActionProposeCommand, as the view's confirm button)
    nlohmann::json ProposeDiplomaticAction(uint32_t country_id, const std::string& action);

private:
    ContactsManager() = default;
    // a new CDiplomaticAction of that type from the player to the country, or null; *shown: the
    // game would list it (CDiplomaticAction::IsPotential). The caller owns it
    void* NewAction(void* type, uint32_t country_id, bool* shown);

    void* GetPlayerCountry();
    std::string LocalizeKey(const std::string& key);

    uintptr_t base_address_{ 0 };
};

} // namespace bridge
