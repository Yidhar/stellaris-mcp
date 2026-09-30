#include "fleet_access.hpp"
#include "command_builder.hpp"
#include "sdk/stellaris_sdk.hpp"
#include <cstring>

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

namespace {

// TPdxNullObject-style references: vtable slot 1 reports whether the pointee is a real object.
struct ValidCtx {
    void* obj;
    bool result;
};

void CallIsValidObject(void* c, void*) {
    auto* x = (ValidCtx*)c;
    x->result = (*(bool (**)(void*))(*(uintptr_t*)x->obj + 8))(x->obj);
}

// PdxLocalize with one named parameter ("$NAME$" / "$NAME|fmt$" in the text), as the outliner
// tooltips build "OUTLINER_PLANET_BLOCKADED" with BLOCKADER. The value is an engine CString that
// the function only reads, so it can borrow our buffer.
struct LocParamCtx {
    uintptr_t fn;
    const char* key;
    int32_t key_len;
    const char* param;
    const void* value;
};

void CallLocalizeParam(void* c, void* out) {
    auto* x = (LocParamCtx*)c;
    struct KeyView {
        const char* ptr;
        int32_t len;
        uint8_t flag;
        uint8_t pad[3];
    } key{ x->key, x->key_len, 0, {} };
    ((void* (*)(void*, const void*, const char*, const void*))x->fn)(out, &key, x->param, x->value);
}

}  // namespace

namespace {
struct HabitabilityCtx {
    uintptr_t fn;
    const void* species;
    const void* carrier;
    const void* country;
    const void* planet_class;
    int64_t out;
};
void CallHabitability(void* c, void*) {
    auto* x = (HabitabilityCtx*)c;
    using Fn = int64_t* (*)(int64_t*, const void*, const void*, const void*, const void*, const void*, const void*);
    ((Fn)x->fn)(&x->out, x->species, x->carrier, x->country, x->planet_class, nullptr, nullptr);
}
}  // namespace

double Habitability(uintptr_t base, const void* species, const void* planet, const void* country) {
    // the colony carrier is the planet's CDepositHolder base (+0x20); its planet class is the
    // carrier's +0x128, i.e. CPlanet::planet_class
    const void* planet_class = nullptr;
    if (!species || !planet || !country) return -1;
    __try {
        planet_class = *(const void* const*)((uintptr_t)planet + sdk::ent::CPlanet::planet_class);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    if (!planet_class) return -1;
    HabitabilityCtx ctx{ base + sdk::fn::NHabitability_CalcHabitability, species, (const void*)((uintptr_t)planet + 0x20),
                         country, planet_class, 0 };
    if (!CommandBuilder::Get().CallGuarded(&CallHabitability, &ctx)) return -1;
    return ctx.out / 100000.0;
}

bool IsRealObject(void* obj) {
    if (!obj) return false;
    ValidCtx ctx{ obj, false };
    return CommandBuilder::Get().CallGuarded(&CallIsValidObject, &ctx) && ctx.result;
}

std::string LocalizeWithParam(uintptr_t base, const std::string& key, const char* param, const std::string& value) {
    struct BorrowedCString {
        uint8_t header[16];
        union {
            char buf[16];
            const char* heap_ptr;
        };
        uint64_t size;
        uint64_t capacity;
    } v{};
    if (value.size() < 16) {
        memcpy(v.buf, value.c_str(), value.size() + 1);
        v.capacity = 15;
    } else {
        v.heap_ptr = value.c_str();
        v.capacity = value.size();
    }
    v.size = value.size();
    LocParamCtx ctx{ base + sdk::fn::PdxLocalize_OneParam, key.c_str(), (int32_t)key.size(), param, &v };
    std::string text;
    if (!CommandBuilder::Get().CallForText(&CallLocalizeParam, &ctx, &text) || text.empty()) {
        return SafeLocalize(base, key);
    }
    return text;
}

std::string PersistentNameText(const void* persistent_name) {
    if (!persistent_name) return "";
    auto& cb = CommandBuilder::Get();
    std::pair<uintptr_t, const void*> args{ cb.Base() + sdk::fn::CPersistentName_BuildString, persistent_name };
    std::string name;
    cb.CallForText(&fleets::CallBuildString, &args, &name);
    return name;
}

}  // namespace bridge
