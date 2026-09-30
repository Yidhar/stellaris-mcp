#pragma once

#include "common.hpp"
#include "sdk/stellaris_sdk.hpp"
#include <unordered_map>

namespace bridge {

struct GameDate {
    uint32_t year{ 0 };
    uint32_t month{ 0 };
    uint32_t day{ 0 };
    std::string formatted;
};

struct ResourceDetail {
    double stockpile{ 0.0 };
    double income{ 0.0 };
    double expense{ 0.0 };
    double net{ 0.0 };
    double max{ -1.0 };  // storage cap for this country, -1 when the resource has none
};

struct CapacityInfo {
    uint32_t used{ 0 };
    uint32_t capacity{ 0 };
};

struct EmpireStats {
    uint32_t empire_size{ 0 };
    uint32_t colonies{ 0 };
    CapacityInfo starbases;
    CapacityInfo naval_capacity;
};

struct GameStatus {
    bool in_game{ false };
    bool is_paused{ false };
    uint32_t speed{ 0 };
    GameDate date;
    EmpireStats stats;
    std::unordered_map<std::string, ResourceDetail> resources;
    uint32_t situations_count{ 0 };
    uint32_t special_projects_count{ 0 };
    uint32_t anomalies_count{ 0 };
};

class GameState {
public:
    static GameState& Get();

    void Init(uintptr_t base_address);

    // Read current status safely on main thread
    GameStatus ReadStatus();

    // Export as JSON for MCP/IPC
    nlohmann::json GetStatusJson();

    void* GetInGameIdler();
    void* GetPlayerCountry();
    uint32_t GetPlayerCountryId();

    GameDate ReadDate();
    EmpireStats ReadEmpireStats(void* country = nullptr);
    std::unordered_map<std::string, ResourceDetail> ReadResources(void* country = nullptr);
    // Script keys of the strategic resources, in the engine's resource index order (the index
    // every per-resource array uses).
    const std::vector<std::string>& ResourceNames();
    // A per-resource CFixedPoint table as {key: value}, for tables reached as
    // `holder -> {data, ..., size at +0xC}` (CColony produced/upkeep/profits, ...). Zeros are skipped.
    nlohmann::json ResourceTableJson(const void* table_ptr_field);

private:
    GameState() = default;
    uintptr_t base_address_{ 0 };
    uintptr_t in_game_idler_rva_{ sdk::glob::g_CurrentInGameIdler };

    std::vector<std::string> cached_resource_names_;
    std::vector<void*> cached_resource_ptrs_;  // CStrategicResource*, same order
    void EnsureResourceNamesLoaded();
};

} // namespace bridge

