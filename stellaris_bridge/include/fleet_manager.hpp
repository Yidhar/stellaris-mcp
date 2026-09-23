#pragma once

#include "common.hpp"
#include <string>
#include <vector>
#include <memory>

namespace bridge {

struct ShipDesignQuotaInfo {
    uint32_t design_id{ 0 };
    std::string design_name;
    uint32_t actual_count{ 0 };
    uint32_t target_quota{ 0 };
    int32_t deficit{ 0 };
};

struct FleetInfo {
    uint32_t fleet_id{ 0 };
    uint32_t template_id{ 0 };
    std::string name;
    double military_power{ 0.0 };
    uint32_t total_ships{ 0 };
    uint32_t total_quota{ 0 };
    bool can_reinforce{ false };
    std::vector<ShipDesignQuotaInfo> designs;
};

struct FleetSummary {
    uint32_t military_fleets_count{ 0 };
    uint32_t civilian_fleets_count{ 0 };
    double total_military_power{ 0.0 };
    uint32_t total_reinforceable_fleets{ 0 };
};

class FleetManager {
public:
    static FleetManager& Get();

    bool Init(uintptr_t base_address);

    // Layer 1
    FleetSummary GetFleetSummary();
    nlohmann::json GetSummaryJson();

    // Layer 2
    std::vector<FleetInfo> GetFleets(bool include_civilian = false, uint32_t specific_fleet_id = 0xFFFFFFFF);
    nlohmann::json GetFleetsJson(const nlohmann::json& params);

    // Layer 3
    bool ReinforceFleet(uint32_t fleet_id, std::string& out_message);
    nlohmann::json ReinforceFleetJson(const nlohmann::json& params);

    bool SetFleetTemplateQuota(uint32_t fleet_id, uint32_t design_id, uint32_t target_quota, std::string& out_message);
    nlohmann::json SetFleetTemplateQuotaJson(const nlohmann::json& params);

private:
    FleetManager() = default;
    ~FleetManager() = default;

    void* GetPlayerCountry();
    void* FindFleetTemplate(uint32_t template_id);
    void* FindFleet(uint32_t fleet_id);

    uintptr_t base_address_{ 0 };

    using FnEngineAlloc = void* (*)(size_t size);
    using FnPostCommand = void (*)(void* pCmd, int flag);

    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
};

} // namespace bridge
