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

private:
    ContactsManager() = default;

    void* GetPlayerCountry();
    std::string LocalizeKey(const std::string& key);

    uintptr_t base_address_{ 0 };
};

} // namespace bridge
