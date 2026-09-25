#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace bridge {

class ContactsManager {
public:
    static ContactsManager& Get();

    using FnLocalize = void(*)(void* out_pdx_str, const void* in_key);
    using FnFreePdxStr = void(*)(void* pdx_str);

    bool Init(uintptr_t base_address);

    // Queries
    nlohmann::json GetContactsInfo(const std::string& mode);
    nlohmann::json GetSummaryJson();

private:
    ContactsManager() = default;

    void* GetPlayerCountry();
    std::string LocalizeKey(const std::string& key);

    uintptr_t base_address_{ 0 };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
};

} // namespace bridge
