#pragma once

#include "common.hpp"

namespace bridge::armies {

struct ArmyInfo {
    uint32_t id{ 0xFFFFFFFF };
    std::string name;          // engine-rendered (CPersistentName)
    std::string type_key;      // e.g. "defense_army", "assault_army"
    bool defensive{ false };   // army type "defensive = yes"
    bool occupation{ false };  // army type "occupation = yes"
    bool has_morale{ false };
    double health{ 0 }, max_health{ 0 };
    double morale{ 0 }, max_morale{ 0 };
    double power{ 0 };         // CArmy::CalcMilitaryPower
    uint32_t species{ 0xFFFFFFFF };
    uint32_t owner{ 0xFFFFFFFF };
    uint32_t colony{ 0xFFFFFFFF };  // where it is stationed (0xFFFFFFFF when embarked)
    uint32_t ship{ 0xFFFFFFFF };    // transport ship carrying it
};

// The CArmy with this id (TPdxRef<CArmy>::_pDatabase, id at +0x10), or nullptr.
void* Find(uintptr_t base, uint32_t army_id);

// Every living army the country owns.
std::vector<void*> Owned(uintptr_t base, uint32_t country_id);

bool Read(void* army, ArmyInfo& out);
nlohmann::json ToJson(const ArmyInfo& a);

}  // namespace bridge::armies
