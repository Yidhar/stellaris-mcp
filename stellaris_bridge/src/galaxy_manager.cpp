#include "galaxy_manager.hpp"
#include "command_builder.hpp"
#include "fleet_access.hpp"
#include "game_state.hpp"
#include "outliner_manager.hpp"
#include "situation_log_manager.hpp"
#include "species_manager.hpp"
#include "sdk/stellaris_sdk.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstring>
#include <deque>
#include <queue>
#include <set>
#include <unordered_set>

namespace bridge {
namespace {

constexpr uint32_t kInvalidId = 0xFFFFFFFF;
constexpr double kFixed = 100000.0;  // CFixedPoint
constexpr int kMaxJumps = 12;

// CPdxArray object {vtable, data @ +8, capacity @ +0x10, size @ +0x14} (ref_array fields such as
// CGalacticObject::starbases / fleet_presence); CGalacticObject::hyperlane is a plain
// {data, size @ +8} array of CHyperlane (0x20 bytes each).
constexpr std::ptrdiff_t kArrData = 0x8;
constexpr std::ptrdiff_t kArrSize = 0x14;
constexpr std::ptrdiff_t kPlainSize = 0x8;
constexpr size_t kHyperlaneSize = 0x20;
// A planet's CDepositHolder base (holder type at +0xB4 is 0 for planets; the id before it at +0x18);
// what CCountry::_HasSurveyedDepositHolder and the survey command take.
constexpr std::ptrdiff_t kPlanetDepositHolder = 0x20;
// CMetaRef<CDepositHolder> {vtable, type @ +8, id @ +0xC}; type 0 = planet, 3 = none
constexpr std::ptrdiff_t kMetaRefType = 0x8;
constexpr std::ptrdiff_t kMetaRefId = 0xC;
constexpr uint32_t kHolderPlanet = 0;
constexpr uint32_t kHolderNone = 3;
// key strings of script types: planet class (+0x28), deposit type and starbase level (+0x20)
constexpr std::ptrdiff_t kPlanetClassKey = 0x28;
constexpr std::ptrdiff_t kTypeKey = 0x20;
// CColony::carrier {planet id, carrier type (0 = planet)}
constexpr std::ptrdiff_t kCarrierId = 0x0;

template <typename T>
bool Read(const void* addr, T* out) {
    __try {
        *out = *(const T*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
T ReadOr(uintptr_t addr, T fallback) {
    T v{};
    return Read((const void*)addr, &v) ? v : fallback;
}

bool CopyChars(char* dst, const char* src, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// MSVC std::string: inline buffer / heap pointer [16], size, capacity
std::string ReadString(uintptr_t addr) {
    struct Raw {
        union {
            char buf[16];
            const char* ptr;
        };
        uint64_t size;
        uint64_t cap;
    } raw{};
    if (!Read((const void*)addr, &raw) || raw.size == 0 || raw.size > 512) return "";
    std::string out(raw.size, '\0');
    const char* src = raw.cap >= 16 ? raw.ptr : (const char*)addr;
    return CopyChars(&out[0], src, raw.size) ? out : "";
}

std::string KeyOf(uintptr_t type_ptr_addr, std::ptrdiff_t key_off) {
    void* t = nullptr;
    return Read((const void*)type_ptr_addr, &t) && t ? ReadString((uintptr_t)t + key_off) : "";
}

// TPdxRefDatabase<T>: arr at +0x18 (16-byte slots, object at +8), capacity at +0x20
void* RefLookup(uintptr_t base, uintptr_t db_rva, uint32_t id) {
    if (id == kInvalidId) return nullptr;
    void* db = nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    if (!Read((const void*)(base + db_rva), &db) || !db || !Read((const void*)((uintptr_t)db + 0x18), &arr) ||
        !arr || !Read((const void*)((uintptr_t)db + 0x20), &cap) || (id & 0xFFFFFF) >= cap) {
        return nullptr;
    }
    void* obj = nullptr;
    return Read((const void*)((uintptr_t)arr + (id & 0xFFFFFF) * 16 + 8), &obj) ? obj : nullptr;
}

template <typename F>
void ForEachRef(uintptr_t base, uintptr_t db_rva, F&& f) {
    void* db = nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    if (!Read((const void*)(base + db_rva), &db) || !db || !Read((const void*)((uintptr_t)db + 0x18), &arr) ||
        !arr || !Read((const void*)((uintptr_t)db + 0x20), &cap) || cap > 1000000) {
        return;
    }
    for (uint32_t i = 0; i < cap; ++i) {
        void* obj = nullptr;
        if (Read((const void*)((uintptr_t)arr + i * 16 + 8), &obj) && obj) f(i, obj);
    }
}

std::vector<uint32_t> RefArray(uintptr_t arr_obj) {
    std::vector<uint32_t> out;
    void* data = nullptr;
    int32_t n = 0;
    if (!Read((const void*)(arr_obj + kArrData), &data) || !data || !Read((const void*)(arr_obj + kArrSize), &n) ||
        n <= 0 || n > 100000) {
        return out;
    }
    for (int32_t i = 0; i < n; ++i) {
        uint32_t v = kInvalidId;
        // empty slots hold 0xFFFFFFFF (a system without a starbase keeps one)
        if (Read((const void*)((uintptr_t)data + i * 4), &v) && v != kInvalidId) out.push_back(v);
    }
    return out;
}

// bool/uint8 engine calls: fn(a, b)
struct Call2Ctx {
    uintptr_t fn;
    const void* a;
    const void* b;
    uint8_t result;
};

void CallByte2(void* c, void*) {
    auto* x = (Call2Ctx*)c;
    x->result = ((uint8_t(*)(const void*, const void*))x->fn)(x->a, x->b);
}

bool EngineByte(uintptr_t fn, const void* a, const void* b, uint8_t* out) {
    Call2Ctx ctx{ fn, a, b, 0 };
    if (!CommandBuilder::Get().CallGuarded(&CallByte2, &ctx)) return false;
    *out = ctx.result;
    return true;
}

double Fixed(uintptr_t addr) { return std::round(ReadOr<int64_t>(addr, 0) / kFixed * 100.0) / 100.0; }

const char* kIntelNames[] = { "none", "low", "medium", "high", "full" };

}  // namespace

GalaxyManager& GalaxyManager::Get() {
    static GalaxyManager instance;
    return instance;
}

bool GalaxyManager::Init(uintptr_t base_address) {
    base_address_ = base_address;
    LOGF("[GALAXY] Initialized (Base: 0x%llX)", (unsigned long long)base_address_);
    return true;
}

GalaxyManager::Snapshot GalaxyManager::Take() {
    Snapshot s;
    s.player = GameState::Get().GetPlayerCountry();
    if (s.player) s.player_id = ReadOr<uint32_t>((uintptr_t)s.player + 0x20, kInvalidId);

    namespace go = sdk::ent::CGalacticObject;
    namespace cc = sdk::ent::CCelestialCoordinate;
    namespace hl = sdk::ent::CHyperlane;
    ForEachRef(base_address_, sdk::db::CGalacticObject, [&](uint32_t, void* obj) {
        System sys{};
        sys.id = ReadOr<uint32_t>((uintptr_t)obj + 8, kInvalidId);
        if (sys.id == kInvalidId) return;
        sys.obj = obj;
        sys.x = Fixed((uintptr_t)obj + go::coordinate + cc::x);
        sys.y = Fixed((uintptr_t)obj + go::coordinate + cc::y);
        void* lanes = nullptr;
        int32_t n = 0;
        if (Read((const void*)((uintptr_t)obj + go::hyperlane), &lanes) && lanes &&
            Read((const void*)((uintptr_t)obj + go::hyperlane + kPlainSize), &n) && n > 0 && n < 64) {
            for (int32_t i = 0; i < n; ++i) {
                uintptr_t e = (uintptr_t)lanes + i * kHyperlaneSize;
                uint32_t to = ReadOr<uint32_t>(e + hl::to, kInvalidId);
                if (to != kInvalidId) sys.lanes.push_back({ to, Fixed(e + hl::length) });
            }
        }
        s.index[sys.id] = s.systems.size();
        s.systems.push_back(std::move(sys));
    });

    // planets by system (the planet's coordinate origin)
    ForEachRef(base_address_, sdk::db::CPlanet, [&](uint32_t, void* obj) {
        uint32_t pid = ReadOr<uint32_t>((uintptr_t)obj + 0x18, kInvalidId);
        uint32_t sid = ReadOr<uint32_t>((uintptr_t)obj + sdk::ent::CPlanet::coordinate + cc::origin, kInvalidId);
        if (pid != kInvalidId && sid != kInvalidId) s.planets[sid].push_back({ pid, obj });
    });
    return s;
}

int GalaxyManager::Intel(const Snapshot& s, void* system) {
    uint8_t level = 0;
    if (!s.player || !EngineByte(base_address_ + sdk::fn::CCountry_GetIntelLevel, s.player, system, &level)) return 0;
    return level > 4 ? 4 : level;
}

bool GalaxyManager::PlanetSurveyed(const Snapshot& s, void* system, void* planet) {
    uint8_t v = 0;
    if (!s.player) return false;
    if (EngineByte(base_address_ + sdk::fn::CCountry_HasAutoSurveyedSystem, s.player, system, &v) && v) return true;
    return EngineByte(base_address_ + sdk::fn::CCountry_HasSurveyedDepositHolder, s.player,
                      (const void*)((uintptr_t)planet + kPlanetDepositHolder), &v) && v;
}

bool GalaxyManager::SystemSurveyed(const Snapshot& s, uint32_t system_id, void* system) {
    auto it = s.planets.find(system_id);
    if (it == s.planets.end()) return true;
    for (const auto& p : it->second) {
        if (!PlanetSurveyed(s, system, p.obj)) return false;
    }
    return true;
}

uint32_t GalaxyManager::Owner(void* system) {
    return ReadOr<uint32_t>((uintptr_t)system + sdk::rt::CGalacticObject_owner, kInvalidId);
}

std::string GalaxyManager::SystemName(uint32_t id, void* system) {
    void* db = nullptr;
    Read((const void*)(base_address_ + sdk::db::CGalacticObject), &db);
    if (db != names_db_) {  // another save: drop the cache
        names_.clear();
        names_db_ = db;
    }
    auto it = names_.find(id);
    if (it != names_.end()) return it->second;
    std::string name = PersistentNameText((const void*)((uintptr_t)system + sdk::ent::CGalacticObject::name));
    names_[id] = name;
    return name;
}

uint32_t GalaxyManager::CapitalSystem(const Snapshot& s) {
    if (!s.player) return kInvalidId;
    uint32_t colony_id = ReadOr<uint32_t>((uintptr_t)s.player + sdk::ent::CCountry::capital, kInvalidId);
    void* colony = RefLookup(base_address_, sdk::db::CColony, colony_id);
    if (!colony) return kInvalidId;
    uint32_t planet_id = ReadOr<uint32_t>((uintptr_t)colony + sdk::ent::CColony::carrier + kCarrierId, kInvalidId);
    void* planet = RefLookup(base_address_, sdk::db::CPlanet, planet_id);
    if (!planet) return kInvalidId;
    return ReadOr<uint32_t>((uintptr_t)planet + sdk::ent::CPlanet::coordinate + sdk::ent::CCelestialCoordinate::origin,
                            kInvalidId);
}

std::vector<uint32_t> GalaxyManager::StarbaseIds(void* system) {
    return RefArray((uintptr_t)system + sdk::ent::CGalacticObject::starbases);
}

std::vector<uint32_t> GalaxyManager::FleetIds(void* system) {
    return RefArray((uintptr_t)system + sdk::ent::CGalacticObject::fleet_presence);
}

// CStarbaseLevelType is script data (no serializer, so no SDK layout): its ship_size is the one
// pointer field that, in every starbase level type, points at an entry of the ship size database.
std::ptrdiff_t GalaxyManager::LevelShipSizeField() {
    if (level_ship_size_ != -2) return level_ship_size_;
    auto items = [&](uintptr_t glob, std::vector<void*>& out) {
        void* db = nullptr;
        void* arr = nullptr;
        int32_t n = 0;
        if (!Read((const void*)(base_address_ + glob), &db) || !db || !Read((const void*)((uintptr_t)db + 0x50), &arr) ||
            !arr || !Read((const void*)((uintptr_t)db + 0x5C), &n) || n <= 0 || n > 5000) {
            return;
        }
        for (int32_t i = 0; i < n; ++i) {
            void* p = nullptr;
            if (Read((const void*)((uintptr_t)arr + i * 8), &p) && p) out.push_back(p);
        }
    };
    std::vector<void*> levels, sizes;
    items(sdk::glob::TGameDatabase_CStarbaseLevelTypeDatabase_pInstance, levels);
    items(sdk::glob::TGameDatabase_CShipSizeDatabase_pInstance, sizes);
    std::unordered_set<void*> size_set(sizes.begin(), sizes.end());
    std::map<std::ptrdiff_t, size_t> hits;
    for (void* lv : levels) {
        for (std::ptrdiff_t off = 0x28; off < 0x800; off += 8) {
            void* p = nullptr;
            if (Read((const void*)((uintptr_t)lv + off), &p) && size_set.count(p)) hits[off]++;
        }
    }
    level_ship_size_ = -1;
    for (const auto& [off, n] : hits) {
        if (!levels.empty() && n == levels.size()) {
            level_ship_size_ = level_ship_size_ == -1 ? off : -1;  // must be unique
            if (level_ship_size_ == -1) break;
        }
    }
    LOGF("[GALAXY] starbase level ship_size field: %lld (%zu levels)", (long long)level_ship_size_, levels.size());
    return level_ship_size_;
}

std::string GalaxyManager::CountryName(uint32_t country_id) {
    return country_id == kInvalidId ? "" : OutlinerManager::Get().CountryDisplayName(country_id);
}

nlohmann::json GalaxyManager::GetOverviewJson() {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };

    int intel_counts[5] = { 0, 0, 0, 0, 0 };
    int surveyed = 0, owned = 0, lanes = 0, frontier = 0;
    std::map<uint32_t, int> empires;
    std::unordered_set<uint32_t> player_systems;
    for (const auto& sys : s.systems) {
        int intel = Intel(s, sys.obj);
        intel_counts[intel]++;
        lanes += (int)sys.lanes.size();
        uint32_t owner = Owner(sys.obj);
        if (owner == s.player_id) {
            owned++;
            player_systems.insert(sys.id);
        } else if (owner != kInvalidId && intel >= 1) {
            empires[owner]++;
        }
        if (SystemSurveyed(s, sys.id, sys.obj)) surveyed++;
    }
    for (const auto& sys : s.systems) {
        if (Owner(sys.obj) != kInvalidId) continue;
        for (const auto& l : sys.lanes) {
            if (player_systems.count(l.first)) {
                frontier++;
                break;
            }
        }
    }
    std::vector<std::pair<uint32_t, int>> known(empires.begin(), empires.end());
    std::sort(known.begin(), known.end(), [](auto& a, auto& b) { return a.second > b.second; });
    nlohmann::json empire_arr = nlohmann::json::array();
    for (const auto& [id, n] : known) {
        empire_arr.push_back({ {"country_id", id}, {"name", CountryName(id)}, {"known_systems", n} });
    }
    uint32_t capital = CapitalSystem(s);
    auto cap_it = s.index.find(capital);
    nlohmann::json intel = nlohmann::json::object();
    for (int i = 0; i < 5; ++i) intel[kIntelNames[i]] = intel_counts[i];

    return {
        {"systems_total", s.systems.size()},
        {"hyperlanes_total", lanes / 2},
        // changes only when systems or hyperlanes do: a cached get_galaxy_map topology stays valid
        {"topology_version", std::to_string(s.systems.size()) + "-" + std::to_string(lanes)},
        {"player", {
            {"country_id", s.player_id},
            {"capital_system_id", capital},
            {"capital_system", cap_it != s.index.end() ? SystemName(capital, s.systems[cap_it->second].obj) : ""},
            {"owned_systems", owned}
        }},
        {"intel", intel},
        {"surveyed_systems", surveyed},
        {"unsurveyed_systems", (int)s.systems.size() - surveyed},
        {"unclaimed_frontier_systems", frontier},
        {"known_empires", empire_arr},
        {"unresearched_anomalies", SituationLogManager::Get().ReadAnomalies(s.player).size()}
    };
}

nlohmann::json GalaxyManager::GetMapJson(uint32_t center, int jumps) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (center == kInvalidId) center = CapitalSystem(s);
    if (!s.index.count(center)) return { {"error", "Unknown system id: " + std::to_string(center)} };
    jumps = std::clamp(jumps, 0, kMaxJumps);

    // breadth-first over hyperlanes
    std::unordered_map<uint32_t, int> dist{ {center, 0} };
    std::deque<uint32_t> q{ center };
    std::vector<uint32_t> order;
    while (!q.empty()) {
        uint32_t id = q.front();
        q.pop_front();
        order.push_back(id);
        int d = dist[id];
        if (d >= jumps) continue;
        for (const auto& l : s.systems[s.index[id]].lanes) {
            if (s.index.count(l.first) && !dist.count(l.first)) {
                dist[l.first] = d + 1;
                q.push_back(l.first);
            }
        }
    }

    std::unordered_set<uint32_t> anomaly_planets;
    for (const auto& a : SituationLogManager::Get().ReadAnomalies(s.player)) anomaly_planets.insert(a.planet_id);
    std::unordered_set<uint32_t> own_fleets;
    for (uint32_t f : fleets::Owned(s.player)) own_fleets.insert(f);

    nlohmann::json rows = nlohmann::json::array();
    nlohmann::json owners = nlohmann::json::object();
    for (uint32_t id : order) {
        const System& sys = s.systems[s.index[id]];
        int intel = Intel(s, sys.obj);
        uint32_t owner = Owner(sys.obj);
        bool owner_known = owner != kInvalidId && (owner == s.player_id || intel >= 1);
        std::string flags;
        if (SystemSurveyed(s, id, sys.obj)) flags += 'S';
        auto pit = s.planets.find(id);
        bool colony = false, anomaly = false;
        if (pit != s.planets.end()) {
            for (const auto& p : pit->second) {
                uint32_t col = ReadOr<uint32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::colony, kInvalidId);
                uint32_t pown = ReadOr<uint32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::owner, kInvalidId);
                if (col != kInvalidId && (intel >= 2 || pown == s.player_id)) colony = true;
                if (anomaly_planets.count(p.id)) anomaly = true;
            }
        }
        if (colony) flags += 'C';
        bool starbase = false;
        for (uint32_t sb : StarbaseIds(sys.obj)) starbase = starbase || RefLookup(base_address_, sdk::db::CStarbase, sb);
        if (starbase && (intel >= 1 || owner == s.player_id)) flags += 'B';
        for (uint32_t f : FleetIds(sys.obj)) {
            if (own_fleets.count(f)) {
                flags += 'F';
                break;
            }
        }
        if (anomaly) flags += 'A';
        if (owner_known && !owners.contains(std::to_string(owner))) owners[std::to_string(owner)] = CountryName(owner);
        rows.push_back({ id, SystemName(id, sys.obj), sys.x, sys.y,
                         owner_known ? nlohmann::json(owner) : nlohmann::json(nullptr), intel, dist[id], flags });
    }
    nlohmann::json lanes = nlohmann::json::array();
    for (uint32_t id : order) {
        for (const auto& l : s.systems[s.index[id]].lanes) {
            if (id < l.first && dist.count(l.first)) lanes.push_back({ id, l.first, l.second });
        }
    }
    return {
        {"center_system_id", center},
        {"jumps", jumps},
        {"columns", {"id", "name", "x", "y", "owner_id", "intel", "jumps", "flags"}},
        {"rows", rows},
        {"hyperlanes", lanes},  // [a, b, length]
        {"owners", owners},
        {"legend", {
            {"intel", "0 none, 1 low, 2 medium, 3 high, 4 full"},
            {"flags", "S surveyed, C colonized, B starbase, F own fleet present, A unresearched anomaly"},
            {"survey", "S means every planet is surveyed by the player (CCountry::HasFullySurveyedSystem); "
                       "planets inside another empire's borders cannot be surveyed without access"},
            {"owner_id", "null: unowned or not known to the player"}
        }}
    };
}

nlohmann::json GalaxyManager::GetSystemJson(uint32_t system_id) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    auto it = s.index.find(system_id);
    if (it == s.index.end()) return { {"error", "Unknown system id: " + std::to_string(system_id)} };
    const System& sys = s.systems[it->second];
    int intel = Intel(s, sys.obj);
    uint32_t owner = Owner(sys.obj);
    bool owner_known = owner != kInvalidId && (owner == s.player_id || intel >= 1);

    nlohmann::json lanes = nlohmann::json::array();
    for (const auto& l : sys.lanes) {
        auto lt = s.index.find(l.first);
        lanes.push_back({ {"to", l.first},
                          {"name", lt != s.index.end() ? SystemName(l.first, s.systems[lt->second].obj) : ""},
                          {"length", l.second} });
    }

    // starbase (visible with the owner)
    nlohmann::json starbase = nullptr;
    if (owner_known) {
        for (uint32_t sb_id : StarbaseIds(sys.obj)) {
            void* sb = RefLookup(base_address_, sdk::db::CStarbase, sb_id);
            if (!sb) continue;
            std::string level = KeyOf((uintptr_t)sb + sdk::ent::CStarbase::level, kTypeKey);
            // the level's display name is its ship size's (starbase_level_outpost -> starbase_outpost)
            std::string size_key;
            void* level_type = nullptr;
            std::ptrdiff_t ship_size = LevelShipSizeField();
            if (ship_size >= 0 && Read((const void*)((uintptr_t)sb + sdk::ent::CStarbase::level), &level_type) && level_type) {
                size_key = KeyOf((uintptr_t)level_type + ship_size, kTypeKey);
            }
            starbase = { {"id", sb_id}, {"level", level},
                         {"level_name", size_key.empty() ? "" : SafeLocalize(base_address_, size_key)} };
            break;
        }
    }

    // planets: intel >= 2, a survey, or our own
    std::unordered_map<uint32_t, std::string> anomalies;
    for (const auto& a : SituationLogManager::Get().ReadAnomalies(s.player)) anomalies[a.planet_id] = a.key;
    std::unordered_map<uint32_t, std::vector<std::string>> deposits;  // planet id -> deposit keys
    ForEachRef(base_address_, sdk::db::CDeposit, [&](uint32_t, void* d) {
        uintptr_t holder = (uintptr_t)d + sdk::ent::CDeposit::deposit_holder;
        if (ReadOr<uint32_t>(holder + kMetaRefType, kHolderNone) != kHolderPlanet) return;
        uint32_t pid = ReadOr<uint32_t>(holder + kMetaRefId, kInvalidId);
        std::string key = KeyOf((uintptr_t)d + sdk::ent::CDeposit::type, kTypeKey);
        if (pid != kInvalidId && !key.empty()) deposits[pid].push_back(key);
    });
    nlohmann::json planets = nlohmann::json::array();
    bool all_surveyed = true;
    int hidden = 0;
    auto pit = s.planets.find(system_id);
    if (pit != s.planets.end()) {
        for (const auto& p : pit->second) {
            bool surveyed = PlanetSurveyed(s, sys.obj, p.obj);
            all_surveyed = all_surveyed && surveyed;
            uint32_t pown = ReadOr<uint32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::owner, kInvalidId);
            if (intel < 2 && !surveyed && pown != s.player_id) {
                hidden++;
                continue;
            }
            std::string cls = KeyOf((uintptr_t)p.obj + sdk::ent::CPlanet::planet_class, kPlanetClassKey);
            nlohmann::json pj = {
                {"id", p.id},
                {"name", PersistentNameText((const void*)((uintptr_t)p.obj + sdk::ent::CPlanet::name))},
                {"class", cls},
                {"class_name", cls.empty() ? "" : SafeLocalize(base_address_, cls)},
                {"size", ReadOr<int32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::planet_size, 0)},
                {"surveyed", surveyed}
            };
            uint32_t col = ReadOr<uint32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::colony, kInvalidId);
            if (pown != kInvalidId) pj["owner_id"] = pown;
            if (col != kInvalidId) pj["colony_id"] = col;
            if (surveyed) {
                nlohmann::json dj = nlohmann::json::array();
                for (const auto& k : deposits[p.id]) dj.push_back({ {"key", k}, {"name", SafeLocalize(base_address_, k)} });
                pj["deposits"] = dj;
            }
            auto an = anomalies.find(p.id);
            if (an != anomalies.end()) pj["anomaly"] = { {"key", an->second}, {"name", SafeLocalize(base_address_, an->second)} };
            planets.push_back(pj);
        }
    }

    // fleets: our own always, others from intel >= 3
    std::unordered_set<uint32_t> own_fleets;
    for (uint32_t f : fleets::Owned(s.player)) own_fleets.insert(f);
    nlohmann::json fleet_arr = nlohmann::json::array();
    for (uint32_t fid : FleetIds(sys.obj)) {
        bool own = own_fleets.count(fid) > 0;
        if (!own && intel < 3) continue;
        void* f = fleets::Find(base_address_, fid);
        if (!f) continue;
        fleet_arr.push_back({ {"id", fid}, {"name", fleets::Name(f)}, {"own", own},
                              {"ship_class", fleets::ShipClassKey(fleets::ClassOf(f))},
                              {"military_power", fleets::MilitaryPower(f)} });
    }

    nlohmann::json out = {
        {"id", system_id},
        {"name", SystemName(system_id, sys.obj)},
        {"x", sys.x},
        {"y", sys.y},
        {"intel", intel},
        {"intel_name", kIntelNames[intel]},
        {"owner_id", owner_known ? nlohmann::json(owner) : nlohmann::json(nullptr)},
        {"owner_name", owner_known ? CountryName(owner) : ""},
        {"surveyed", all_surveyed},
        {"hyperlanes", lanes},
        {"starbase", starbase},
        {"planets", planets},
        {"fleets", fleet_arr}
    };
    if (hidden) out["unknown_planets"] = hidden;  // present but not known to the player
    return out;
}

nlohmann::json GalaxyManager::MoveFleet(uint32_t fleet_id, uint32_t system_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    auto owned = fleets::Owned(s.player);
    if (std::find(owned.begin(), owned.end(), fleet_id) == owned.end()) {
        return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    }
    if (!s.index.count(system_id)) return { {"success", false}, {"error", "Unknown system id: " + std::to_string(system_id)} };

    // CSendFleetToLocationCommand: a CMoveToSystemPointFleetOrder to the system's centre
    // (a coordinate with origin = the system, local x = y = 0)
    namespace cmd_ns = sdk::cmd::fleet_send_to_location;
    namespace cc = sdk::ent::CCelestialCoordinate;
    auto cmd = CommandBuilder::Get().Create(cmd_ns::kSpec);
    cmd.Set<uint32_t>(cmd_ns::fleet, fleet_id)
        .Set<int64_t>(cmd_ns::coordinate + cc::x, 0)
        .Set<int64_t>(cmd_ns::coordinate + cc::y, 0)
        .Set<uint32_t>(cmd_ns::coordinate + cc::origin, system_id)
        .Set<uint8_t>(cmd_ns::queue, queue ? 1 : 0)
        .Set<uint8_t>(cmd_ns::queue_to_front, 0);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return { {"success", false}, {"error", why.empty() ? "The fleet cannot move (a station or immobile)" : why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"target_system_id", system_id},
             {"target_system", SystemName(system_id, s.systems[s.index[system_id]].obj)}, {"queued", queue},
             {"message", "Move order posted; read the fleet's orders on a later call"} };
}

nlohmann::json GalaxyManager::Survey(uint32_t fleet_id, uint32_t system_id, uint32_t planet_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    auto owned = fleets::Owned(s.player);
    if (std::find(owned.begin(), owned.end(), fleet_id) == owned.end()) {
        return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    }
    if (planet_id != kInvalidId) {
        void* planet = RefLookup(base_address_, sdk::db::CPlanet, planet_id);
        if (!planet) return { {"success", false}, {"error", "Unknown planet id: " + std::to_string(planet_id)} };
        system_id = ReadOr<uint32_t>((uintptr_t)planet + sdk::ent::CPlanet::coordinate + sdk::ent::CCelestialCoordinate::origin,
                                     kInvalidId);
    }
    if (!s.index.count(system_id)) return { {"success", false}, {"error", "Unknown system id: " + std::to_string(system_id)} };

    // CFleetSurveyDepositHolderCommand: galactic_object -1 surveys the one holder, else the system
    namespace sv = sdk::cmd::survey_planet_order;
    auto cmd = CommandBuilder::Get().Create(sv::kSpec);
    cmd.Set<uint32_t>(sv::fleet, fleet_id)
        .Set<uint32_t>(sv::deposit_holder + kMetaRefType, planet_id != kInvalidId ? kHolderPlanet : kHolderNone)
        .Set<uint32_t>(sv::deposit_holder + kMetaRefId, planet_id)
        .Set<uint32_t>(sv::galactic_object, planet_id != kInvalidId ? kInvalidId : system_id)
        .Set<uint8_t>(sv::queue, queue ? 1 : 0)
        .Set<uint8_t>(sv::queue_to_front, 0);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return { {"success", false}, {"error", why.empty() ? "Survey order rejected by the engine" : why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    nlohmann::json out = { {"success", true}, {"fleet_id", fleet_id}, {"system_id", system_id},
                           {"system", SystemName(system_id, s.systems[s.index[system_id]].obj)}, {"queued", queue},
                           {"message", "Survey order posted"} };
    if (planet_id != kInvalidId) out["planet_id"] = planet_id;
    return out;
}

std::unordered_map<uint32_t, int> GalaxyManager::JumpsFrom(const Snapshot& s, uint32_t from) {
    std::unordered_map<uint32_t, int> dist;
    if (!s.index.count(from)) return dist;
    std::deque<uint32_t> q{ from };
    dist[from] = 0;
    while (!q.empty()) {
        uint32_t id = q.front();
        q.pop_front();
        for (const auto& l : s.systems[s.index.at(id)].lanes) {
            if (s.index.count(l.first) && !dist.count(l.first)) {
                dist[l.first] = dist[id] + 1;
                q.push_back(l.first);
            }
        }
    }
    return dist;
}

bool GalaxyManager::OwnFleet(const Snapshot& s, uint32_t fleet_id) {
    auto owned = fleets::Owned(s.player);
    return std::find(owned.begin(), owned.end(), fleet_id) != owned.end();
}

nlohmann::json GalaxyManager::FindPath(uint32_t from, uint32_t to) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (from == kInvalidId) from = CapitalSystem(s);
    if (!s.index.count(from) || !s.index.count(to)) return { {"error", "Unknown system id"} };
    // Dijkstra over hyperlane lengths
    std::unordered_map<uint32_t, double> dist{ {from, 0.0} };
    std::unordered_map<uint32_t, uint32_t> prev;
    using Item = std::pair<double, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    pq.push({ 0.0, from });
    while (!pq.empty()) {
        auto [d, id] = pq.top();
        pq.pop();
        if (d > dist[id]) continue;
        if (id == to) break;
        for (const auto& l : s.systems[s.index[id]].lanes) {
            if (!s.index.count(l.first)) continue;
            double nd = d + l.second;
            auto it = dist.find(l.first);
            if (it == dist.end() || nd < it->second) {
                dist[l.first] = nd;
                prev[l.first] = id;
                pq.push({ nd, l.first });
            }
        }
    }
    if (!dist.count(to)) return { {"from_system_id", from}, {"to_system_id", to}, {"reachable", false} };
    std::vector<uint32_t> path{ to };
    while (path.back() != from) path.push_back(prev[path.back()]);
    std::reverse(path.begin(), path.end());
    nlohmann::json systems = nlohmann::json::array();
    for (uint32_t id : path) {
        const System& sys = s.systems[s.index[id]];
        uint32_t owner = Owner(sys.obj);
        bool known = owner != kInvalidId && (owner == s.player_id || Intel(s, sys.obj) >= 1);
        systems.push_back({ {"id", id}, {"name", SystemName(id, sys.obj)},
                            {"owner_id", known ? nlohmann::json(owner) : nlohmann::json(nullptr)} });
    }
    return {
        {"from_system_id", from}, {"to_system_id", to}, {"reachable", true},
        {"jumps", (int)path.size() - 1}, {"length", std::round(dist[to] * 100.0) / 100.0},
        {"systems", systems},
        {"note", "Shortest route by hyperlane length. Closed borders, gateways, wormholes and jump drives are not "
                 "considered; the engine plans the actual route when an order is given."}
    };
}

bool GalaxyManager::OutpostCommand(uint32_t fleet_id, uint32_t system_id, bool queue, bool post, std::string* why) {
    // CFleetBuildOrbitalStationCommand: an outpost is a starbase (EShipClass 10) built on the
    // system (galactic_object set; the deposit holder stays the factory's "none")
    namespace ob = sdk::cmd::build_orbital_station_order;
    auto cmd = CommandBuilder::Get().Create(ob::kSpec);
    cmd.Set<uint32_t>(ob::fleet, fleet_id)
        .Set<uint32_t>(ob::galactic_object, system_id)
        .Set<uint8_t>(ob::class_, (uint8_t)fleets::ShipClass::Starbase)
        .Set<uint8_t>(ob::queue, queue ? 1 : 0)
        .Set<uint8_t>(ob::queue_to_front, 0);
    if (!cmd) {
        if (why) *why = cmd.error();
        return false;
    }
    if (!cmd.IsValid(why)) return false;
    if (post && !cmd.Post(NativeCommand::Check::EngineGate)) {
        if (why) *why = cmd.error();
        return false;
    }
    return true;
}

nlohmann::json GalaxyManager::BuildOutpost(uint32_t fleet_id, uint32_t system_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    if (!s.index.count(system_id)) return { {"success", false}, {"error", "Unknown system id: " + std::to_string(system_id)} };
    std::string why;
    if (!OutpostCommand(fleet_id, system_id, queue, true, &why)) {
        return { {"success", false}, {"error", why.empty() ? "The game refused the outpost" : why} };
    }
    return { {"success", true}, {"fleet_id", fleet_id}, {"system_id", system_id},
             {"system", SystemName(system_id, s.systems[s.index[system_id]].obj)}, {"queued", queue},
             {"message", "Outpost order posted"} };
}

nlohmann::json GalaxyManager::Colonize(uint32_t fleet_id, uint32_t planet_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    void* planet = RefLookup(base_address_, sdk::db::CPlanet, planet_id);
    if (!planet) return { {"success", false}, {"error", "Unknown planet id: " + std::to_string(planet_id)} };
    namespace co = sdk::cmd::colonize_planet_order;
    auto cmd = CommandBuilder::Get().Create(co::kSpec);
    cmd.Set<uint32_t>(co::fleet, fleet_id)
        .Set<uint32_t>(co::planet, planet_id)
        .Set<uint8_t>(co::queue, queue ? 1 : 0)
        .Set<uint8_t>(co::queue_to_front, 0);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The game refused the colonization" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"planet_id", planet_id},
             {"planet", PersistentNameText((const void*)((uintptr_t)planet + sdk::ent::CPlanet::name))},
             {"queued", queue}, {"message", "Colonization order posted"} };
}

nlohmann::json GalaxyManager::FindSystems(const std::string& purpose, uint32_t from, int limit, uint32_t fleet_id,
                                          const std::string& resource, uint32_t species_id) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (from == kInvalidId) from = CapitalSystem(s);
    if (!s.index.count(from)) return { {"error", "Unknown system id: " + std::to_string(from)} };
    limit = std::clamp(limit, 1, 50);
    auto jumps = JumpsFrom(s, from);
    std::vector<std::pair<int, uint32_t>> order;
    for (const auto& [id, d] : jumps) order.push_back({ d, id });
    std::sort(order.begin(), order.end());

    std::unordered_set<uint32_t> own_systems;
    for (const auto& sys : s.systems) {
        if (Owner(sys.obj) == s.player_id) own_systems.insert(sys.id);
    }
    auto base_row = [&](uint32_t id, int d) {
        const System& sys = s.systems[s.index[id]];
        int intel = Intel(s, sys.obj);
        uint32_t owner = Owner(sys.obj);
        bool known = owner != kInvalidId && (owner == s.player_id || intel >= 1);
        return nlohmann::json{ {"id", id}, {"name", SystemName(id, sys.obj)}, {"jumps", d}, {"intel", intel},
                               {"owner_id", known ? nlohmann::json(owner) : nlohmann::json(nullptr)} };
    };

    nlohmann::json out = nlohmann::json::array();
    if (purpose == "unsurveyed") {
        for (const auto& [d, id] : order) {
            if ((int)out.size() >= limit) break;
            const System& sys = s.systems[s.index[id]];
            if (SystemSurveyed(s, id, sys.obj)) continue;
            out.push_back(base_row(id, d));
        }
    } else if (purpose == "outpost") {
        // an outpost goes into an unowned system; the engine's own check (the build order's
        // IsValid with a construction ship) decides, else only the basic facts are listed
        if (fleet_id == kInvalidId) {
            for (uint32_t f : fleets::Owned(s.player)) {
                if (fleets::ClassOf(fleets::Find(base_address_, f)) == fleets::ShipClass::Constructor) {
                    fleet_id = f;
                    break;
                }
            }
        }
        for (const auto& [d, id] : order) {
            if ((int)out.size() >= limit) break;
            const System& sys = s.systems[s.index[id]];
            if (Owner(sys.obj) != kInvalidId) continue;
            bool borders = false;
            for (const auto& l : sys.lanes) borders = borders || own_systems.count(l.first);
            nlohmann::json row = base_row(id, d);
            row["surveyed"] = SystemSurveyed(s, id, sys.obj);
            row["borders_own_space"] = borders;
            if (fleet_id != kInvalidId) {
                std::string why;
                bool ok = OutpostCommand(fleet_id, id, false, false, &why);
                row["can_build"] = ok;
                if (!ok) row["reason"] = why;
            }
            out.push_back(row);
        }
    } else if (purpose == "deposit") {
        // deposits on planets the player has surveyed, matching `resource` in the deposit key
        std::unordered_map<uint32_t, std::vector<std::string>> deposits;
        ForEachRef(base_address_, sdk::db::CDeposit, [&](uint32_t, void* dp) {
            uintptr_t holder = (uintptr_t)dp + sdk::ent::CDeposit::deposit_holder;
            if (ReadOr<uint32_t>(holder + kMetaRefType, kHolderNone) != kHolderPlanet) return;
            std::string key = KeyOf((uintptr_t)dp + sdk::ent::CDeposit::type, kTypeKey);
            if (key.empty() || (!resource.empty() && key.find(resource) == std::string::npos)) return;
            deposits[ReadOr<uint32_t>(holder + kMetaRefId, kInvalidId)].push_back(key);
        });
        for (const auto& [d, id] : order) {
            if ((int)out.size() >= limit) break;
            const System& sys = s.systems[s.index[id]];
            auto pit = s.planets.find(id);
            if (pit == s.planets.end()) continue;
            nlohmann::json found = nlohmann::json::array();
            for (const auto& p : pit->second) {
                auto dit = deposits.find(p.id);
                if (dit == deposits.end() || !PlanetSurveyed(s, sys.obj, p.obj)) continue;
                for (const auto& k : dit->second) {
                    found.push_back({ {"planet_id", p.id}, {"key", k}, {"name", SafeLocalize(base_address_, k)} });
                }
            }
            if (found.empty()) continue;
            nlohmann::json row = base_row(id, d);
            row["deposits"] = found;
            out.push_back(row);
        }
    } else if (purpose == "colonizable") {
        // the expansion planner's rule (ValidatePlanetFilters): systems with intel above low, planets
        // the player surveyed, not owned, with habitability for the species from
        // NHabitability::CalcHabitability(species, planet, player) above zero; best first
        if (species_id == kInvalidId) {
            species_id = ReadOr<uint32_t>((uintptr_t)s.player + sdk::ent::CCountry::founder_species_ref, kInvalidId);
        }
        void* species = SpeciesManager::Get().FindSpeciesPtr(species_id);
        if (!species) return { {"error", "Unknown species id: " + std::to_string(species_id)} };
        struct Cand {
            double hab;
            int jumps;
            nlohmann::json row;
        };
        std::vector<Cand> cands;
        for (const auto& [d, id] : order) {
            const System& sys = s.systems[s.index[id]];
            if (Intel(s, sys.obj) < 2) continue;
            auto pit = s.planets.find(id);
            if (pit == s.planets.end()) continue;
            for (const auto& p : pit->second) {
                if (ReadOr<uint32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::owner, kInvalidId) != kInvalidId) continue;
                if (ReadOr<uint32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::colony, kInvalidId) != kInvalidId) continue;
                if (!PlanetSurveyed(s, sys.obj, p.obj)) continue;
                double hab = Habitability(base_address_, species, p.obj, s.player);
                if (hab <= 0) continue;
                std::string cls = KeyOf((uintptr_t)p.obj + sdk::ent::CPlanet::planet_class, kPlanetClassKey);
                cands.push_back({ hab, d, {
                    {"planet_id", p.id},
                    {"planet", PersistentNameText((const void*)((uintptr_t)p.obj + sdk::ent::CPlanet::name))},
                    {"class", cls}, {"class_name", SafeLocalize(base_address_, cls)},
                    {"size", ReadOr<int32_t>((uintptr_t)p.obj + sdk::ent::CPlanet::planet_size, 0)},
                    {"habitability_percent", std::round(hab * 1000.0) / 10.0},
                    {"system_id", id}, {"system", SystemName(id, sys.obj)}, {"jumps", d} } });
            }
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.hab != b.hab ? a.hab > b.hab : a.jumps < b.jumps;
        });
        nlohmann::json planets = nlohmann::json::array();
        for (size_t i = 0; i < cands.size() && (int)i < limit; ++i) planets.push_back(cands[i].row);
        return { {"purpose", purpose}, {"from_system_id", from}, {"species_id", species_id}, {"planets", planets},
                 {"note", "Habitability for the species with the player's modifiers; colonizing still needs a colony ship "
                          "and the planet inside or next to the player's borders (the colonize order reports why not)."} };
    } else {
        return { {"error", "purpose must be one of: unsurveyed, outpost, deposit, colonizable"} };
    }
    nlohmann::json res = { {"purpose", purpose}, {"from_system_id", from}, {"systems", out} };
    if (purpose == "outpost") res["checked_with_fleet_id"] = fleet_id == kInvalidId ? nlohmann::json(nullptr) : nlohmann::json(fleet_id);
    return res;
}

}  // namespace bridge
