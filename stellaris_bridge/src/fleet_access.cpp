#include "fleet_access.hpp"
#include "command_builder.hpp"
#include "sdk/stellaris_sdk.hpp"

namespace bridge::fleets {
namespace {

// CFleet's own id (what TPdxRef<CFleet> lookups compare, e.g. CColonyCarrier's blockader scan).
constexpr std::ptrdiff_t kFleetId = 0x30;
// CPdxArray<CCountryFleetsManager::SOwnedFleetEntry>: data at owned_fleets, size at +0xC.
constexpr std::ptrdiff_t kOwnedFleetsSize = 0xC;
constexpr std::ptrdiff_t kOwnedFleetEntrySize = 0x20;

template <typename T>
bool Read(const void* addr, T* out) {
    __try {
        *out = *(const T*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void CallBuildString(void* ctx, void* out) {
    using FnBuildString = void* (*)(const void* persistent_name, void* out_cstring);
    auto* args = (std::pair<uintptr_t, const void*>*)ctx;
    ((FnBuildString)args->first)(args->second, out);
}

const char* ShipClassKey(ShipClass c) {
    switch (c) {
        case ShipClass::Military: return "shipclass_military";
        case ShipClass::Constructor: return "shipclass_constructor";
        case ShipClass::Colonizer: return "shipclass_colonizer";
        case ShipClass::ScienceShip: return "shipclass_science_ship";
        case ShipClass::Transport: return "shipclass_transport";
        case ShipClass::MiningStation: return "shipclass_mining_station";
        case ShipClass::ResearchStation: return "shipclass_research_station";
        case ShipClass::MilitaryStation: return "shipclass_military_station";
        case ShipClass::HabitatStation: return "shipclass_habitat_station";
        case ShipClass::ObservationStation: return "shipclass_observation_station";
        case ShipClass::Starbase: return "shipclass_starbase";
        case ShipClass::MilitarySpecial: return "shipclass_military_special";
        case ShipClass::GravitySnare: return "shipclass_gravity_snare";
        case ShipClass::EntropyConduit: return "shipclass_entropy_conduit";
        default: return "none";
    }
}

bool IsCivilianShip(ShipClass c) {
    return c == ShipClass::Constructor || c == ShipClass::Colonizer || c == ShipClass::ScienceShip ||
           c == ShipClass::Transport;
}

bool IsStation(ShipClass c) {
    return c >= ShipClass::MiningStation && c <= ShipClass::Starbase || c == ShipClass::GravitySnare ||
           c == ShipClass::EntropyConduit;
}

void* Find(uintptr_t base, uint32_t fleet_id) {
    void* db = nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    void* fleet = nullptr;
    uint32_t check = 0xFFFFFFFF;
    if (!base || fleet_id == 0xFFFFFFFF || !Read((const void*)(base + sdk::db::CFleet), &db) || !db ||
        !Read((const void*)((uintptr_t)db + 0x18), &arr) || !arr ||
        !Read((const void*)((uintptr_t)db + 0x20), &cap) || (fleet_id & 0xFFFFFF) >= cap ||
        !Read((const void*)((uintptr_t)arr + (fleet_id & 0xFFFFFF) * 16 + 8), &fleet) || !fleet ||
        !Read((const void*)((uintptr_t)fleet + kFleetId), &check) || check != fleet_id) {
        return nullptr;
    }
    return fleet;
}

std::vector<uint32_t> Owned(void* country) {
    std::vector<uint32_t> ids;
    const uintptr_t owned = (uintptr_t)country + sdk::ent::CCountryFleetsManager::owned_fleets;
    uintptr_t data = 0;
    int32_t size = 0;
    if (!country || !Read((const void*)owned, &data) || !data ||
        !Read((const void*)(owned + kOwnedFleetsSize), &size) || size <= 0 || size > 100000) {
        return ids;
    }
    for (int32_t i = 0; i < size; ++i) {
        uint32_t id = 0xFFFFFFFF;
        if (Read((const void*)(data + i * kOwnedFleetEntrySize + sdk::ent::CCountryFleetsManager_SOwnedFleetEntry::fleet), &id) &&
            id != 0xFFFFFFFF) {
            ids.push_back(id);
        }
    }
    return ids;
}

ShipClass ClassOf(void* fleet) {
    uint8_t c = 0xFF;
    return fleet && Read((const void*)((uintptr_t)fleet + sdk::ent::CFleet::ship_class), &c) ? (ShipClass)c : ShipClass::Unknown;
}

double MilitaryPower(void* fleet) {
    int64_t raw = 0;
    return fleet && Read((const void*)((uintptr_t)fleet + sdk::ent::CFleet::military_power), &raw) ? raw / 100000.0 : 0.0;
}

uint32_t TemplateId(void* fleet) {
    uint32_t id = 0xFFFFFFFF;
    return fleet && Read((const void*)((uintptr_t)fleet + sdk::ent::CFleet::fleet_template), &id) ? id : 0xFFFFFFFF;
}

std::string Name(void* fleet) {
    return fleet ? PersistentNameText((const void*)((uintptr_t)fleet + sdk::ent::CFleet::name)) : "";
}

namespace {
void CallBuildOrders(void* ctx, void* out) {
    using FnBuildOrders = void* (*)(const void* fleet, void* out_cstring, bool, bool);
    auto* args = (std::pair<uintptr_t, const void*>*)ctx;
    ((FnBuildOrders)args->first)(args->second, out, true, false);
}
}  // namespace

std::string OrdersText(void* fleet) {
    if (!fleet) return "";
    auto& cb = CommandBuilder::Get();
    std::pair<uintptr_t, const void*> args{ cb.Base() + sdk::fn::CFleet_BuildOrdersString, fleet };
    std::string text;
    cb.CallForText(&CallBuildOrders, &args, &text);
    return text;
}

}  // namespace bridge::fleets

namespace bridge {

std::string PersistentNameText(const void* persistent_name) {
    if (!persistent_name) return "";
    auto& cb = CommandBuilder::Get();
    std::pair<uintptr_t, const void*> args{ cb.Base() + sdk::fn::CPersistentName_BuildString, persistent_name };
    std::string name;
    cb.CallForText(&fleets::CallBuildString, &args, &name);
    return name;
}

}  // namespace bridge
