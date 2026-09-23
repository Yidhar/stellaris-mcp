#pragma once

#include "common.hpp"
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

    GameDate ReadDate();
    EmpireStats ReadEmpireStats(void* country = nullptr);
    std::unordered_map<std::string, ResourceDetail> ReadResources(void* country = nullptr);

private:
    GameState() = default;
    uintptr_t base_address_{ 0 };
    uintptr_t in_game_idler_rva_{ 0x3113180 };

    std::vector<std::string> cached_resource_names_;
    void EnsureResourceNamesLoaded();
};

} // namespace bridge

