#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace bridge {

class MarketManager {
public:
    static MarketManager& Get();

    using FnLocalize = void(*)(void* out_pdx_str, const void* in_key);
    using FnFreePdxStr = void(*)(void* pdx_str);
    using FnEngineAlloc = void*(*)(size_t size);
    using FnPostCommand = void(*)(void* cmd, int unk);

    bool Init(uintptr_t base_address);

    // Queries
    nlohmann::json GetMarketInfo();
    nlohmann::json GetSummaryJson();

    // Commands
    bool ExecuteInstantTrade(const std::string& resource_key, const std::string& action,
                            uint32_t units, std::string& out_message);

    bool SetMonthlyTrade(const std::string& resource_key, const std::string& action,
                         double amount, double price_limit, bool cancel, std::string& out_message);

private:
    MarketManager() = default;

    void* GetPlayerCountry();
    std::string LocalizeKey(const std::string& key);

    void* FindStrategicResource(const std::string& resource_key);
    bool IsResourceUnlocked(void* country, const std::string& key);

    uintptr_t base_address_{ 0 };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
};

} // namespace bridge
