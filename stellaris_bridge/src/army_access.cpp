#include "army_access.hpp"
#include "command_builder.hpp"
#include "fleet_access.hpp"
#include "sdk/stellaris_sdk.hpp"

namespace bridge::armies {

namespace {

constexpr std::ptrdiff_t kArmyId = 0x10;
// Not serialized; as CArmy::GetToolTip reads them.
constexpr std::ptrdiff_t kMorale = 0x158;       // current morale (CFixedPoint)
constexpr std::ptrdiff_t kArmyModifier = 0x380; // the army's CModifier, passed to CArmyType::CalcMorale
// CArmyType flags (CArmyType::ReadMember; the has_morale byte is what the tooltip tests).
constexpr std::ptrdiff_t kTypeKey = 0x20;
constexpr std::ptrdiff_t kTypeOccupation = 0x351;
constexpr std::ptrdiff_t kTypeDefensive = 0x352;
constexpr std::ptrdiff_t kTypeHasMorale = 0x355;

template <typename T>
bool Read(const void* addr, T* out) {
    __try {
        *out = *(const T*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadKey(const void* obj, std::string& out) {
    struct { char* ptr_or_buf[2]; uint64_t size; uint64_t cap; } raw{};
    if (!Read((const void*)((uintptr_t)obj + kTypeKey), &raw) || raw.size == 0 || raw.size > 256) return false;
    const char* p = raw.cap >= 16 ? raw.ptr_or_buf[0] : (const char*)((uintptr_t)obj + kTypeKey);
    std::string s(raw.size, '\0');
    for (size_t i = 0; i < raw.size; ++i) {
        if (!Read(p + i, &s[i])) return false;
    }
    out = s;
    return true;
}

struct FixedCall {
    uintptr_t fn;
    const void* a;
    const void* c;
    int64_t result;
};

void CallPower(void* ctx, void*) {
    auto* x = (FixedCall*)ctx;
    ((int64_t* (*)(const void*, int64_t*))x->fn)(x->a, &x->result);
}

void CallMorale(void* ctx, void*) {
    auto* x = (FixedCall*)ctx;
    ((int64_t* (*)(const void*, int64_t*, const void*))x->fn)(x->a, &x->result, x->c);
}

}  // namespace

void* Find(uintptr_t base, uint32_t army_id) {
    void* db = nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    void* army = nullptr;
    uint32_t check = 0xFFFFFFFF;
    if (!base || army_id == 0xFFFFFFFF || !Read((const void*)(base + sdk::db::CArmy), &db) || !db ||
        !Read((const void*)((uintptr_t)db + 0x18), &arr) || !arr ||
        !Read((const void*)((uintptr_t)db + 0x20), &cap) || (army_id & 0xFFFFFF) >= cap ||
        !Read((const void*)((uintptr_t)arr + (army_id & 0xFFFFFF) * 16 + 8), &army) || !army ||
        !Read((const void*)((uintptr_t)army + kArmyId), &check) || check != army_id) {
        return nullptr;
    }
    return army;
}

std::vector<void*> Owned(uintptr_t base, uint32_t country_id) {
    std::vector<void*> out;
    void* db = nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    if (!base || !Read((const void*)(base + sdk::db::CArmy), &db) || !db ||
        !Read((const void*)((uintptr_t)db + 0x18), &arr) || !arr ||
        !Read((const void*)((uintptr_t)db + 0x20), &cap) || cap > 1000000) {
        return out;
    }
    for (uint32_t i = 0; i < cap; ++i) {
        void* army = nullptr;
        uint32_t owner = 0xFFFFFFFF;
        uint8_t alive = 0;
        if (Read((const void*)((uintptr_t)arr + i * 16 + 8), &army) && army &&
            Read((const void*)((uintptr_t)army + sdk::ent::CArmy::owner), &owner) && owner == country_id &&
            Read((const void*)((uintptr_t)army + sdk::ent::CArmy::alive), &alive) && alive) {
            out.push_back(army);
        }
    }
    return out;
}

bool Read(void* army, ArmyInfo& a) {
    if (!army) return false;
    const uintptr_t p = (uintptr_t)army;
    int64_t hp = 0, max_hp = 0, morale = 0;
    void* type = nullptr;
    if (!Read((const void*)(p + kArmyId), &a.id) || !Read((const void*)(p + sdk::ent::CArmy::type), &type) || !type) {
        return false;
    }
    Read((const void*)(p + sdk::ent::CArmy::health), &hp);
    Read((const void*)(p + sdk::ent::CArmy::max_health), &max_hp);
    Read((const void*)(p + kMorale), &morale);
    Read((const void*)(p + sdk::ent::CArmy::species), &a.species);
    Read((const void*)(p + sdk::ent::CArmy::owner), &a.owner);
    Read((const void*)(p + sdk::ent::CArmy::colony), &a.colony);
    Read((const void*)(p + sdk::ent::CArmy::ship), &a.ship);
    a.health = hp / 100000.0;
    a.max_health = max_hp / 100000.0;

    uint8_t flag = 0;
    ReadKey(type, a.type_key);
    a.defensive = Read((const void*)((uintptr_t)type + kTypeDefensive), &flag) && flag;
    a.occupation = Read((const void*)((uintptr_t)type + kTypeOccupation), &flag) && flag;
    a.has_morale = Read((const void*)((uintptr_t)type + kTypeHasMorale), &flag) && flag;

    auto& cb = CommandBuilder::Get();
    FixedCall power{ cb.Base() + sdk::fn::CArmy_CalcMilitaryPower, army, nullptr, 0 };
    if (cb.CallGuarded(&CallPower, &power)) a.power = power.result / 100000.0;
    if (a.has_morale) {
        a.morale = morale / 100000.0;
        FixedCall max_morale{ cb.Base() + sdk::fn::CArmyType_CalcMorale, type, (const void*)(p + kArmyModifier), 0 };
        if (cb.CallGuarded(&CallMorale, &max_morale)) a.max_morale = max_morale.result / 100000.0;
    }
    a.name = PersistentNameText((const void*)(p + sdk::ent::CArmy::name));
    return true;
}

nlohmann::json ToJson(const ArmyInfo& a) {
    auto pct = [](double v, double max) { return max > 0 ? std::round(v * 1000.0 / max) / 10.0 : 0.0; };
    auto r1 = [](double v) { return std::round(v * 10.0) / 10.0; };
    nlohmann::json j = {
        {"army_id", a.id},
        {"name", a.name},
        {"type_key", a.type_key},
        {"is_defense", a.defensive},
        {"is_occupation", a.occupation},
        {"power", r1(a.power)},
        {"health", r1(a.health)},
        {"max_health", r1(a.max_health)},
        {"health_percent", pct(a.health, a.max_health)},
        {"species_id", a.species},
        {"embarked", a.ship != 0xFFFFFFFF},
    };
    if (a.has_morale) {
        j["morale"] = r1(a.morale);
        j["max_morale"] = r1(a.max_morale);
        j["morale_percent"] = pct(a.morale, a.max_morale);
    }
    return j;
}

}  // namespace bridge::armies
