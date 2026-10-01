#pragma once

#include "common.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace bridge {

// The galaxy map as the player knows it. Every system and hyperlane is visible (as on the
// in-game map); everything else is gated by the player's own knowledge, asked from the engine:
//   * CCountry::GetIntelLevel(system): 0 none, 1 low, 2 medium, 3 high, 4 full
//   * surveyed planet: CCountry::HasAutoSurveyedSystem || CCountry::_HasSurveyedDepositHolder
// Owners need intel >= 1, planets intel >= 2 (or a survey), deposits a survey of that planet,
// foreign fleets intel >= 3. Own assets are always visible.
class GalaxyManager {
public:
    static GalaxyManager& Get();
    bool Init(uintptr_t base_address);

    // L1: counts and the player's position in the galaxy
    nlohmann::json GetOverviewJson();
    // L1.5: systems within `jumps` hyperlane jumps of `center_system` (default: the capital's),
    // as table rows plus the hyperlanes between them
    nlohmann::json GetMapJson(uint32_t center_system, int jumps);
    // L2: one system
    nlohmann::json GetSystemJson(uint32_t system_id);

    // L3: fleet orders
    nlohmann::json MoveFleet(uint32_t fleet_id, uint32_t system_id, bool queue);
    // planet_id != 0xFFFFFFFF surveys one planet, otherwise the whole system
    nlohmann::json Survey(uint32_t fleet_id, uint32_t system_id, uint32_t planet_id, bool queue);
    // a construction ship builds an outpost (starbase) in the system
    nlohmann::json BuildOutpost(uint32_t fleet_id, uint32_t system_id, bool queue);
    // a colony ship colonizes the planet
    nlohmann::json Colonize(uint32_t fleet_id, uint32_t planet_id, bool queue);
    // cancel every order of these fleets (they stop where they are)
    nlohmann::json CancelFleetOrders(const std::vector<uint32_t>& fleet_ids);
    // follow another fleet (attack: engage it when caught up)
    nlohmann::json FollowFleet(uint32_t fleet_id, uint32_t target_fleet_id, bool attack, bool queue);
    // passive / aggressive / evasive
    nlohmann::json SetFleetStance(uint32_t fleet_id, const std::string& stance);
    // return_home / emergency_ftl (the fleets go missing in action)
    nlohmann::json FleetMia(const std::vector<uint32_t>& fleet_ids, const std::string& type);
    // add (count claims, default 1) or remove (count, default all) the player's claims on a system
    nlohmann::json ClaimSystem(uint32_t system_id, bool remove, int count);

    // in-system orders
    nlohmann::json OrbitPlanet(uint32_t fleet_id, uint32_t planet_id, bool queue);
    nlohmann::json ResearchAnomalies(uint32_t fleet_id, uint32_t system_id, bool queue);
    nlohmann::json ExcavateSite(uint32_t fleet_id, uint32_t site_id, bool queue);
    // jump through a bypass (gateway / wormhole / relay / L-gate) to to_system (one of its leads_to)
    nlohmann::json UseBypass(uint32_t fleet_id, uint32_t bypass_id, uint32_t to_system, bool queue);
    nlohmann::json ExploreBypass(uint32_t fleet_id, uint32_t bypass_id, bool queue);

    // the route the game plans for this fleet from where it is (CFleetPath::Create: closed
    // borders, gateways, wormholes and FTL as the fleet may use them) with its
    // CalcEstimatedDays travel time, the ETA the fleet gets when ordered there
    nlohmann::json FindPath(uint32_t fleet_id, uint32_t to_system);
    // systems for a purpose ("unsurveyed", "outpost", "deposit"), nearest first
    nlohmann::json FindSystems(const std::string& purpose, uint32_t from_system, int limit, uint32_t fleet_id,
                               const std::string& resource, uint32_t species_id = 0xFFFFFFFF);

private:
    GalaxyManager() = default;

    struct System {
        uint32_t id;
        void* obj;
        double x, y;
        std::vector<std::pair<uint32_t, double>> lanes;  // (to, length)
    };
    struct PlanetRef {
        uint32_t id;
        void* obj;
    };

    // One snapshot per request: systems, their planets, the player
    struct Snapshot {
        void* player = nullptr;
        uint32_t player_id = 0xFFFFFFFF;
        std::vector<System> systems;
        std::unordered_map<uint32_t, size_t> index;                  // system id -> systems[]
        std::unordered_map<uint32_t, std::vector<PlanetRef>> planets;  // system id -> planets
    };
    Snapshot Take();

    int Intel(const Snapshot& s, void* system);
    bool PlanetSurveyed(const Snapshot& s, void* system, void* planet);
    bool SystemSurveyed(const Snapshot& s, uint32_t system_id, void* system);
    uint32_t Owner(void* system);
    std::string SystemName(uint32_t id, void* system);
    uint32_t CapitalSystem(const Snapshot& s);
    std::vector<uint32_t> StarbaseIds(void* system);
    std::vector<uint32_t> FleetIds(void* system);
    std::string CountryName(uint32_t country_id);
    std::ptrdiff_t LevelShipSizeField();  // -1 when not found
    std::unordered_map<uint32_t, int> JumpsFrom(const Snapshot& s, uint32_t from);
    bool OwnFleet(const Snapshot& s, uint32_t fleet_id);
    // build_orbital_station_order for an outpost; IsValid only unless post
    bool OutpostCommand(uint32_t fleet_id, uint32_t system_id, bool queue, bool post, std::string* why);

    uintptr_t base_address_{ 0 };
    void* names_db_{ nullptr };  // the system database the name cache belongs to
    std::unordered_map<uint32_t, std::string> names_;
    std::ptrdiff_t level_ship_size_{ -2 };  // -2: not looked up yet
};

}  // namespace bridge
