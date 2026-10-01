#pragma once

#include "common.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace bridge {

class DiscoveriesManager {
public:
    static DiscoveriesManager& Get();


    bool Init(uintptr_t base_address);

    // Queries
    nlohmann::json GetDiscoveriesInfo(const std::string& tab);
    nlohmann::json GetSummaryJson();

    // Actions
    bool ActivateRelic(const std::string& relic_key, std::string& out_message);

private:
    DiscoveriesManager() = default;

    void* GetPlayerCountry();
    std::string LocalizeKey(const std::string& key);

    uintptr_t base_address_{ 0 };
};

} // namespace bridge
