#pragma once

#include "common.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace bridge {

class MarketManager {
public:
    static MarketManager& Get();


    bool Init(uintptr_t base_address);

    // Queries
    nlohmann::json GetMarketInfo();
    nlohmann::json GetSummaryJson();

    // Commands
    bool ExecuteInstantTrade(const std::string& resource_key, const std::string& action,
                            uint32_t units, std::string& out_message);

    // cancel=true removes the order with id `order_id` (the engine's monthly trade item id).
    bool SetMonthlyTrade(const std::string& resource_key, const std::string& action,
                         double amount, double price_limit, bool cancel, int32_t order_id, std::string& out_message);

private:
    // A recurring market order as stored in CMarket (SMonthlyTradeData).
    struct MonthlyOrder {
        void* resource{ nullptr };
        uint32_t type{ 0 };         // 0 = buy, 1 = sell
        uint32_t country{ 0xFFFFFFFF };
        int32_t amount{ 0 };
        int32_t price{ 0 };         // max unit price in trade value, 0 = no limit
        int32_t id{ -1 };
    };
    std::vector<MonthlyOrder> ReadMonthlyOrders(uint32_t country_id);
    nlohmann::json ReadMonthlyTrades(uint32_t country_id);
    MarketManager() = default;

    void* GetPlayerCountry();
    std::string LocalizeKey(const std::string& key);

    void* FindStrategicResource(const std::string& resource_key);
    bool IsResourceUnlocked(void* country, const std::string& key);

    uintptr_t base_address_{ 0 };
};

} // namespace bridge
