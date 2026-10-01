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
constexpr std::ptrdiff_t kArrCapacity = 0x10;
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
// CBypass::type -> CBypassType, key ("gateway", "wormhole", "relay_bypass", ...) at +0x28
constexpr std::ptrdiff_t kBypassTypeKey = 0x28;
// ESpatialObjectType (CSpatialObjectRefCaster::PointerFromTypeAndID): the kinds a spatial
// reference {vtable, type +8, id +0xC} (CArchaeologicalSite::location) points at
constexpr uint32_t kSpatialPlanet = 2;
constexpr uint32_t kSpatialSystem = 4;
constexpr uint32_t kSpatialMegastructure = 6;
constexpr uint32_t kSpatialFleet = 3;
constexpr uint32_t kSpatialDebris = 5;
constexpr uint32_t kSpatialNaturalWormhole = 7;
constexpr uint32_t kSpatialAstralRift = 9;
// CRefObjectOrbitableRef<CFleetOrbitableEnumType> (fleet_orbit_planet::orbitable): id +0, kind
// byte +4 (1 = planet, as CFleetOrbitPlanetCommand(fleet, TPdxRef<CPlanet>, ...) builds it)
constexpr std::ptrdiff_t kOrbitableId = 0x0;
constexpr std::ptrdiff_t kOrbitableKind = 0x4;
constexpr uint8_t kOrbitablePlanet = 1;
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

// CCelestialCoordinate {vtable, x, y, .., origin system, .., randomized}: the engine copies it
// member-wise up to `randomized`
struct Coord {
    alignas(8) uint8_t raw[(sdk::ent::CCelestialCoordinate::randomized + 1 + 7) & ~7];
};

// A system's centre as CCelestialCoordinate(system, 0, 0) builds it: x = y = 0, origin = system
Coord SystemCentre(uintptr_t base, uint32_t system_id) {
    Coord c{};
    *(uintptr_t*)c.raw = base + sdk::vt::CCelestialCoordinate;
    *(uint32_t*)(c.raw + sdk::ent::CCelestialCoordinate::origin) = system_id;
    return c;
}

// CFleetPath as DrawMovementDebugLines builds it on the stack: vtable, CPdxArray<SNode> (vtable at
// +8, nodes / count per sdk::rt), CGameDate; Create fills the nodes, CalcEstimatedDays reads them
struct PathCtx {
    uintptr_t base;
    void* fleet;
    Coord from, to;
    alignas(16) uint8_t path[0x40];
    void* nodes;
    int32_t count;
    int64_t* per_node;
    int64_t days;
};

void CallFleetCoordinate(void* c, void*) {
    auto* x = (PathCtx*)c;
    // the fleet's position: a virtual on its coordinate interface (secondary base)
    auto iface = (uintptr_t)x->fleet + sdk::rt::CFleet_coordinate_base;
    auto fn = (*(const void* (***)(uintptr_t))iface)[sdk::vt::CFleet_GetCoordinate];
    x->nodes = (void*)fn(iface);
}

const void* FleetCoordinate(uintptr_t base, void* fleet) {
    PathCtx ctx{};
    ctx.base = base;
    ctx.fleet = fleet;
    if (!CommandBuilder::Get().CallGuarded(&CallFleetCoordinate, &ctx)) return nullptr;
    return ctx.nodes;
}

void CallCreatePath(void* c, void*) {
    auto* x = (PathCtx*)c;
    // CFleet::CalcMovementPathFindSettings, as the engine inlines it before Create
    bool flag = ((bool (*)(const void*))(x->base + sdk::fn::CFleet_PathFindSettingsFlag))(x->fleet);
    const void* avoid = *(const void* const*)(x->base + sdk::glob::TPdxNullObject_CGalacticObject_pInstance);
    ((void (*)(void*, const void*, const void*, const void*, const void*, uint32_t))(x->base + sdk::fn::CFleetPath_Create))(
        x->path, x->from.raw, x->to.raw, avoid, x->fleet, flag ? 3u : 2u);
}

bool EnginePath(PathCtx* ctx) {
    memset(ctx->path, 0, sizeof(ctx->path));
    *(uintptr_t*)ctx->path = ctx->base + sdk::vt::CFleetPath;
    *(uintptr_t*)(ctx->path + 8) = ctx->base + sdk::vt::CPdxArray_CFleetPath_SNode;
    if (!CommandBuilder::Get().CallGuarded(&CallCreatePath, ctx)) return false;
    ctx->nodes = *(void**)(ctx->path + sdk::rt::CFleetPath_nodes);
    ctx->count = *(int32_t*)(ctx->path + sdk::rt::CFleetPath_node_count);
    if (!ctx->nodes || ctx->count < 0 || ctx->count > 100000) ctx->count = 0;
    return true;
}

void CallEstimatedDays(void* c, void*) {
    auto* x = (PathCtx*)c;
    int64_t out = 0;
    ((int64_t * (*)(const void*, int64_t*, const void*, int64_t*))(x->base + sdk::fn::CFleetPath_CalcEstimatedDays))(
        x->path, &out, x->fleet, x->per_node);
    x->days = out;
}

// total and per-node days (CFixedPoint); per_node has one slot per node
bool EngineDays(PathCtx* ctx, int64_t* per_node, int64_t* total) {
    ctx->per_node = per_node;
    if (!CommandBuilder::Get().CallGuarded(&CallEstimatedDays, ctx)) return false;
    *total = ctx->days;
    return true;
}

void CallFreeNodes(void* c, void*) {
    auto* x = (PathCtx*)c;
    ((void (*)(void*))(x->base + sdk::fn::CRT_operator_delete))(x->nodes);
}

// the path's destructor: the nodes are plain data, the engine just frees the array
void FreePath(PathCtx* ctx) {
    ctx->nodes = *(void**)(ctx->path + sdk::rt::CFleetPath_nodes);
    if (ctx->nodes) CommandBuilder::Get().CallGuarded(&CallFreeNodes, ctx);
    *(void**)(ctx->path + sdk::rt::CFleetPath_nodes) = nullptr;
}

struct CanColonizeCtx {
    uintptr_t fn;
    const void* planet;
    const void* country;
    bool result;
};

void CallCanColonize(void* c, void* out) {
    auto* x = (CanColonizeCtx*)c;
    x->result = ((bool (*)(const void*, const void*, void*))x->fn)(x->planet, x->country, out);
}

// CPlanet::CanColonize, the check CFleetColonizePlanetCommand::IsValid makes (with no reason);
// `why` gets the game's reason when it says no
bool CanColonize(uintptr_t base, const void* planet, const void* country, std::string* why) {
    CanColonizeCtx ctx{ base + sdk::fn::CPlanet_CanColonize, planet, country, false };
    std::string text;
    if (!CommandBuilder::Get().CallForText(&CallCanColonize, &ctx, &text)) return false;
    if (!ctx.result && why) *why = text;
    return ctx.result;
}

// CPdxArray<TPdxRef<T>> embedded in a command {vtable (factory's), data +8, capacity +0x10,
// size +0x14}; the command frees the data, so it comes from the engine heap
bool SetRefArray(NativeCommand& cmd, std::ptrdiff_t arr, const std::vector<uint32_t>& ids, std::string* why) {
    if (!cmd) {
        if (why) *why = cmd.error();
        return false;
    }
    void* data = CommandBuilder::Get().EngineAlloc(ids.size() * sizeof(uint32_t));
    if (!data) {
        if (why) *why = "engine allocation for the fleet list failed";
        return false;
    }
    memcpy(data, ids.data(), ids.size() * sizeof(uint32_t));
    cmd.Set<void*>(arr + kArrData, data)
        .Set<uint32_t>(arr + kArrCapacity, (uint32_t)ids.size())
        .Set<uint32_t>(arr + kArrSize, (uint32_t)ids.size());
    return true;
}

struct ClaimCtx {
    uintptr_t fn;
    const void* system;
    const void* country;
    alignas(8) uint8_t out[0x18];  // CClaim {vtable, owner +8, date +0xC, claims +0x10}
};

void CallGetClaims(void* c, void*) {
    auto* x = (ClaimCtx*)c;
    ((void* (*)(const void*, void*, const void*))x->fn)(x->system, x->out, x->country);
}

// CGalacticObject::GetClaimsBy(country).claims; 0 when none (or the call failed)
int ClaimsBy(uintptr_t base, const void* system, const void* country) {
    ClaimCtx ctx{ base + sdk::fn::CGalacticObject_GetClaimsBy, system, country, {} };
    if (!CommandBuilder::Get().CallGuarded(&CallGetClaims, &ctx)) return 0;
    return *(const int32_t*)(ctx.out + sdk::ent::CClaim::claims);
}

double Fixed(uintptr_t addr) { return std::round(ReadOr<int64_t>(addr, 0) / kFixed * 100.0) / 100.0; }

const char* kIntelNames[] = { "none", "low", "medium", "high", "full" };
// EPathJumpMethod (save tokens jump_hyperlane, jump_bypass): a hyperlane, or a gateway / wormhole / L-gate
const char* kJumpMethods[] = { "hyperlane", "bypass" };
// EFleetStance (save tokens passive, aggressive, evasive; names FLEET_STANCE_*)
const char* kStances[] = { "passive", "aggressive", "evasive" };
const char* kStanceNames[] = { "FLEET_STANCE_PASSIVE", "FLEET_STANCE_AGGRESSIVE", "FLEET_STANCE_EVASIVE" };
// EMiaType: mia_emergency_ftl 0, mia_return_home 1 (the ones a player orders)
constexpr uint32_t kMiaEmergencyFtl = 0;
constexpr uint32_t kMiaReturnHome = 1;

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
    if (s.player) s.player_id = ReadOr<uint32_t>((uintptr_t)s.player + sdk::rt::CCountry_id, kInvalidId);

    namespace go = sdk::ent::CGalacticObject;
    namespace cc = sdk::ent::CCelestialCoordinate;
    namespace hl = sdk::ent::CHyperlane;
    ForEachRef(base_address_, sdk::db::CGalacticObject, [&](uint32_t, void* obj) {
        System sys{};
        sys.id = ReadOr<uint32_t>((uintptr_t)obj + sdk::rt::CGalacticObject_id, kInvalidId);
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
        uint32_t pid = ReadOr<uint32_t>((uintptr_t)obj + sdk::rt::CPlanet_id, kInvalidId);
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
        if (ClaimsBy(base_address_, sys.obj, s.player) > 0) flags += 'K';
        if (intel >= 2 || owner == s.player_id) {
            if (!RefArray((uintptr_t)sys.obj + sdk::ent::CGalacticObject::megastructures).empty()) flags += 'M';
            if (!RefArray((uintptr_t)sys.obj + sdk::ent::CGalacticObject::bypasses).empty()) flags += 'G';
        }
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
            {"flags", "S surveyed, C colonized, B starbase, F own fleet present, A unresearched anomaly, "
                      "K claimed by the player, M megastructure, G gateway / wormhole / relay (bypass)"},
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
        nlohmann::json fj = { {"id", fid}, {"name", fleets::Name(f)}, {"own", own},
                              {"ship_class", fleets::ShipClassKey(fleets::ClassOf(f))},
                              {"military_power", fleets::MilitaryPower(f)} };
        if (own) {
            uint32_t st = ReadOr<uint32_t>((uintptr_t)f + sdk::ent::CFleet::fleet_stance, 0);
            if (st < std::size(kStances)) fj["stance"] = kStances[st];
        }
        fleet_arr.push_back(fj);
    }

    // other objects in the system: megastructures, bypasses (gateways, wormholes, relays, L-gates),
    // astral rifts and debris from intel >= 2 or in our own systems; archaeological sites when
    // the site lists the player in visible_to
    bool see_objects = intel >= 2 || owner == s.player_id;
    namespace go = sdk::ent::CGalacticObject;
    nlohmann::json megas = nlohmann::json::array();
    nlohmann::json bypasses = nlohmann::json::array();
    nlohmann::json rifts = nlohmann::json::array();
    nlohmann::json debris = nlohmann::json::array();
    if (see_objects) {
        namespace ms = sdk::ent::CMegaStructure;
        for (uint32_t id : RefArray((uintptr_t)sys.obj + go::megastructures)) {
            void* m = RefLookup(base_address_, sdk::db::CMegaStructure, id);
            if (!m) continue;
            std::string type = KeyOf((uintptr_t)m + ms::type, kTypeKey);
            nlohmann::json mj = { {"id", id}, {"type", type}, {"type_name", type.empty() ? "" : SafeLocalize(base_address_, type)} };
            std::string mname = PersistentNameText((const void*)((uintptr_t)m + ms::name));
            if (!mname.empty()) mj["name"] = mname;  // relays and gates carry no name of their own
            uint32_t mown = ReadOr<uint32_t>((uintptr_t)m + ms::owner, kInvalidId);
            if (mown != kInvalidId) mj["owner_id"] = mown;
            uint32_t mplanet = ReadOr<uint32_t>((uintptr_t)m + ms::planet, kInvalidId);
            if (mplanet != kInvalidId) mj["planet_id"] = mplanet;
            if (ReadOr<uint8_t>((uintptr_t)m + ms::is_dismantling, 0)) mj["dismantling"] = true;
            megas.push_back(mj);
        }

        // where each bypass is: every system's bypass list
        std::unordered_map<uint32_t, uint32_t> bypass_system;
        for (const auto& other : s.systems) {
            for (uint32_t b : RefArray((uintptr_t)other.obj + go::bypasses)) bypass_system[b] = other.id;
        }
        namespace bp = sdk::ent::CBypass;
        for (uint32_t id : RefArray((uintptr_t)sys.obj + go::bypasses)) {
            void* b = RefLookup(base_address_, sdk::db::CBypass, id);
            if (!b) continue;
            std::string type = KeyOf((uintptr_t)b + bp::type, kBypassTypeKey);
            std::string type_name = type.empty() ? "" : SafeLocalize(base_address_, type);
            // the systems it leads to: its linked bypass plus its active network connections
            std::vector<uint32_t> to;
            auto add_to = [&](uint32_t other_bypass) {
                auto it2 = bypass_system.find(other_bypass);
                if (it2 != bypass_system.end() && it2->second != system_id &&
                    std::find(to.begin(), to.end(), it2->second) == to.end()) {
                    to.push_back(it2->second);
                }
            };
            add_to(ReadOr<uint32_t>((uintptr_t)b + bp::linked_to, kInvalidId));
            for (uint32_t c : RefArray((uintptr_t)b + bp::active_connections)) add_to(c);
            nlohmann::json leads = nlohmann::json::array();
            for (size_t i = 0; i < to.size() && i < 12; ++i) {
                auto ti = s.index.find(to[i]);
                leads.push_back({ {"id", to[i]}, {"name", ti != s.index.end() ? SystemName(to[i], s.systems[ti->second].obj) : ""} });
            }
            nlohmann::json bj = { {"id", id}, {"type", type}, {"active", ReadOr<uint8_t>((uintptr_t)b + bp::active, 0) != 0},
                                  {"leads_to", leads} };
            if (!type_name.empty() && type_name != type) bj["type_name"] = type_name;
            if (to.size() > 12) bj["leads_to_total"] = to.size();
            uint32_t lock = ReadOr<uint32_t>((uintptr_t)b + bp::lock_country, kInvalidId);
            if (lock != kInvalidId) bj["locked_by"] = lock;
            bypasses.push_back(bj);
        }

        namespace ar = sdk::ent::CAstralRift;
        for (uint32_t id : RefArray((uintptr_t)sys.obj + go::astral_rifts)) {
            void* r = RefLookup(base_address_, sdk::db::CAstralRift, id);
            if (!r) continue;
            std::string type = KeyOf((uintptr_t)r + ar::type, kTypeKey);
            nlohmann::json rj = { {"id", id}, {"type", type}, {"type_name", type.empty() ? "" : SafeLocalize(base_address_, type)},
                                  {"name", PersistentNameText((const void*)((uintptr_t)r + ar::name))},
                                  {"clues", ReadOr<int32_t>((uintptr_t)r + ar::clues, 0)},
                                  {"difficulty", ReadOr<int32_t>((uintptr_t)r + ar::difficulty, 0)} };
            uint32_t ex = ReadOr<uint32_t>((uintptr_t)r + ar::explorer_fleet, kInvalidId);
            if (ex != kInvalidId) rj["explorer_fleet_id"] = ex;
            auto explorable = RefArray((uintptr_t)r + ar::explorable_by);
            rj["explorable_by_player"] = std::find(explorable.begin(), explorable.end(), s.player_id) != explorable.end();
            rifts.push_back(rj);
        }

        namespace db_ = sdk::ent::CDebris;
        std::unordered_map<uint32_t, uint32_t> debris_project;
        for (const auto& pr : SituationLogManager::Get().ReadSpecialProjects(s.player)) {
            if (pr.debris_id != kInvalidId) debris_project[pr.debris_id] = pr.id;
        }
        ForEachRef(base_address_, sdk::db::CDebris, [&](uint32_t, void* d) {
            if (ReadOr<uint32_t>((uintptr_t)d + db_::coordinate + sdk::ent::CCelestialCoordinate::origin, kInvalidId) != system_id) return;
            if (ReadOr<uint8_t>((uintptr_t)d + db_::killed, 0)) return;
            uint32_t did = ReadOr<uint32_t>((uintptr_t)d + sdk::rt::CDebris_id, kInvalidId);
            nlohmann::json dj = { {"id", did} };
            auto pr = debris_project.find(did);
            if (pr != debris_project.end()) dj["project_id"] = pr->second;  // research it with collect_data
            uint32_t from = ReadOr<uint32_t>((uintptr_t)d + db_::from_country, kInvalidId);
            if (from != kInvalidId) dj["from_country_id"] = from;
            uint32_t cty = ReadOr<uint32_t>((uintptr_t)d + db_::country, kInvalidId);
            if (cty != kInvalidId) dj["country_id"] = cty;
            debris.push_back(dj);
        });
    }

    nlohmann::json sites = nlohmann::json::array();
    namespace as = sdk::ent::CArchaeologicalSite;
    ForEachRef(base_address_, sdk::db::CArchaeologicalSite, [&](uint32_t, void* a) {
        uintptr_t loc = (uintptr_t)a + as::location;
        uint32_t ltype = ReadOr<uint32_t>(loc + kMetaRefType, kInvalidId), lid = ReadOr<uint32_t>(loc + kMetaRefId, kInvalidId);
        if (SpatialSystem(ltype, lid) != system_id) return;
        auto visible = RefArray((uintptr_t)a + as::visible_to);
        if (std::find(visible.begin(), visible.end(), s.player_id) == visible.end()) return;
        std::string type = KeyOf((uintptr_t)a + as::type, kTypeKey);
        nlohmann::json sj = { {"id", ReadOr<uint32_t>((uintptr_t)a + sdk::rt::CArchaeologicalSite_id, kInvalidId)}, {"type", type},
                              {"type_name", type.empty() ? "" : SafeLocalize(base_address_, type)},
                              {"chapter", ReadOr<int32_t>((uintptr_t)a + as::index, 0)},
                              {"clues", ReadOr<int32_t>((uintptr_t)a + as::clues, 0)},
                              {"difficulty", ReadOr<int32_t>((uintptr_t)a + as::difficulty, 0)},
                              {"locked", ReadOr<uint8_t>((uintptr_t)a + as::locked, 0) != 0} };
        if (ltype == kSpatialPlanet) sj["planet_id"] = lid;
        uint32_t ex = ReadOr<uint32_t>((uintptr_t)a + as::excavator_fleet, kInvalidId);
        if (ex != kInvalidId) sj["excavator_fleet_id"] = ex;
        sites.push_back(sj);
    });

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
        {"fleets", fleet_arr},
        {"player_claims", ClaimsBy(base_address_, sys.obj, s.player)}
    };
    if (!megas.empty()) out["megastructures"] = megas;
    if (!bypasses.empty()) out["bypasses"] = bypasses;
    if (!rifts.empty()) out["astral_rifts"] = rifts;
    if (!debris.empty()) out["debris"] = debris;
    if (!sites.empty()) out["archaeological_sites"] = sites;
    // the player's special projects researched here (debris analysis, located event projects)
    nlohmann::json projects = nlohmann::json::array();
    for (const auto& pr : SituationLogManager::Get().ReadSpecialProjects(s.player)) {
        if (pr.location_type == kInvalidId || SpatialSystem(pr.location_type, pr.location_id) != system_id) continue;
        nlohmann::json pj = { {"id", pr.id}, {"name", pr.name}, {"kind", pr.kind} };
        if (pr.days_left >= 0) pj["days_left"] = pr.days_left;
        projects.push_back(pj);
    }
    if (!projects.empty()) out["special_projects"] = projects;
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

bool GalaxyManager::SurveyCommand(uint32_t fleet_id, uint32_t system_id, uint32_t planet_id, bool queue, bool post,
                                  std::string* why) {
    // CFleetSurveyDepositHolderCommand: galactic_object -1 surveys the one holder, else the system
    namespace sv = sdk::cmd::survey_planet_order;
    auto cmd = CommandBuilder::Get().Create(sv::kSpec);
    cmd.Set<uint32_t>(sv::fleet, fleet_id)
        .Set<uint32_t>(sv::deposit_holder + kMetaRefType, planet_id != kInvalidId ? kHolderPlanet : kHolderNone)
        .Set<uint32_t>(sv::deposit_holder + kMetaRefId, planet_id)
        .Set<uint32_t>(sv::galactic_object, planet_id != kInvalidId ? kInvalidId : system_id)
        .Set<uint8_t>(sv::queue, queue ? 1 : 0)
        .Set<uint8_t>(sv::queue_to_front, 0);
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

    std::string why;
    if (!SurveyCommand(fleet_id, system_id, planet_id, queue, true, &why)) {
        return { {"success", false}, {"error", why.empty() ? "Survey order rejected by the engine" : why} };
    }
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

nlohmann::json GalaxyManager::FindPath(uint32_t fleet_id, uint32_t to) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) {
        return { {"error", "fleet_id must be one of the player's fleets: the route and travel time depend on its "
                           "speed, FTL and border access"} };
    }
    void* fleet = RefLookup(base_address_, sdk::db::CFleet, fleet_id);
    if (!fleet) return { {"error", "Unknown fleet id: " + std::to_string(fleet_id)} };
    if (!s.index.count(to)) return { {"error", "Unknown system id: " + std::to_string(to)} };

    // from the fleet's own position: CalcEstimatedDays times the first leg from there
    PathCtx ctx{};
    ctx.base = base_address_;
    ctx.fleet = fleet;
    const void* here = FleetCoordinate(base_address_, fleet);
    if (!here || !Read(here, &ctx.from)) return { {"error", "Could not read the fleet's position"} };
    uint32_t from = ReadOr<uint32_t>((uintptr_t)here + sdk::ent::CCelestialCoordinate::origin, kInvalidId);
    ctx.to = SystemCentre(base_address_, to);
    if (!EnginePath(&ctx)) return { {"error", "The engine path finder raised"} };

    int n = ctx.count;
    std::vector<uint32_t> node_system(n, kInvalidId), node_jump(n, 0), node_bypass(n, kInvalidId);
    for (int i = 0; i < n; ++i) {
        uintptr_t node = (uintptr_t)ctx.nodes + i * sdk::rt::CFleetPath_node_size;
        node_system[i] = ReadOr<uint32_t>(node + sdk::ent::CCelestialCoordinate::origin, kInvalidId);
        node_jump[i] = ReadOr<uint32_t>(node + sdk::rt::CFleetPath_node_jump_method, 0);
        node_bypass[i] = ReadOr<uint32_t>(node + sdk::rt::CFleetPath_node_bypass, kInvalidId);
    }
    std::vector<int64_t> per_node(n > 0 ? n : 1, 0);
    int64_t total = 0;
    bool timed = n > 0 && EngineDays(&ctx, per_node.data(), &total);
    FreePath(&ctx);
    if (n == 0) {
        return { {"fleet_id", fleet_id}, {"from_system_id", from}, {"to_system_id", to}, {"reachable", false},
                 {"note", "The game finds no route for this fleet (no hyperlane connection it may use, or it cannot move)"} };
    }

    // nodes are the leave / enter points of each system (the last one is the destination's
    // centre); per_node[i] is the time spent before the leg into node i, so the arrival at
    // node i is per_node[i + 1] (the total for the last)
    auto days = [](int64_t v) { return std::round(v / kFixed * 10.0) / 10.0; };
    nlohmann::json systems = nlohmann::json::array();
    auto add_system = [&](uint32_t id, nlohmann::json arrival, const char* via, uint32_t bypass) {
        void* obj = s.index.count(id) ? s.systems[s.index[id]].obj : nullptr;
        uint32_t owner = obj ? Owner(obj) : kInvalidId;
        bool known = owner != kInvalidId && (owner == s.player_id || Intel(s, obj) >= 1);
        nlohmann::json row = { {"id", id}, {"name", obj ? SystemName(id, obj) : ""},
                               {"owner_id", known ? nlohmann::json(owner) : nlohmann::json(nullptr)},
                               {"arrival_days", arrival} };
        if (via) row["via"] = via;
        // an entry node's bypass is the one arrived at (a gateway, wormhole, relay, L-gate)
        if (void* b = bypass != kInvalidId ? RefLookup(base_address_, sdk::db::CBypass, bypass) : nullptr) {
            std::string key = KeyOf((uintptr_t)b + sdk::ent::CBypass::type, kBypassTypeKey);
            if (!key.empty()) {
                row["bypass"] = { {"key", key} };
                std::string name = SafeLocalize(base_address_, key);  // "lgate" is a loc key, "relay_bypass" not
                if (!name.empty() && name != key) row["bypass"]["name"] = name;
            }
        }
        systems.push_back(row);
    };
    add_system(from, 0.0, nullptr, kInvalidId);
    for (int i = 0; i < n; ++i) {
        uint32_t id = node_system[i];
        if (id == kInvalidId || id == systems.back()["id"].get<uint32_t>()) continue;
        nlohmann::json arrival = nullptr;
        if (timed) arrival = days(i + 1 < n ? per_node[i + 1] : total);
        // the entry node carries how the fleet got here (EPathJumpMethod)
        bool by_bypass = node_jump[i] == 1;
        add_system(id, arrival, node_jump[i] < std::size(kJumpMethods) ? kJumpMethods[node_jump[i]] : "unknown",
                   by_bypass ? node_bypass[i] : kInvalidId);
    }
    nlohmann::json out = { {"fleet_id", fleet_id}, {"from_system_id", from}, {"to_system_id", to}, {"reachable", true},
                           {"jumps", (int)systems.size() - 1}, {"systems", systems} };
    out["estimated_days"] = timed ? nlohmann::json(days(total)) : nlohmann::json(nullptr);
    return out;
}

nlohmann::json GalaxyManager::CancelFleetOrders(const std::vector<uint32_t>& fleet_ids) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (fleet_ids.empty()) return { {"success", false}, {"error", "fleet_ids is empty"} };
    for (uint32_t f : fleet_ids) {
        if (!OwnFleet(s, f)) return { {"success", false}, {"error", "Fleet " + std::to_string(f) + " is not one of the player's fleets"} };
    }
    // CFleetCancelOrdersCommand: ClearOrders + CancelMovement + ClearAutoMoveTarget for each
    // fleet the country controls that has an order
    namespace co = sdk::cmd::fleet_cancel_orders;
    auto cmd = CommandBuilder::Get().Create(co::kSpec);
    std::string why;
    cmd.Set<uint32_t>(co::country, s.player_id);
    if (!SetRefArray(cmd, co::fleets, fleet_ids, &why)) return { {"success", false}, {"error", why} };
    if (!cmd.IsValid(&why)) {
        return { {"success", false}, {"error", why.empty() ? "None of these fleets has an order to cancel" : why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_ids", fleet_ids}, {"message", "Orders cancelled; the fleets stop where they are"} };
}

nlohmann::json GalaxyManager::FollowFleet(uint32_t fleet_id, uint32_t target_fleet_id, bool attack, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    void* target = fleets::Find(base_address_, target_fleet_id);
    if (!target) return { {"success", false}, {"error", "Unknown fleet id: " + std::to_string(target_fleet_id)} };
    // CFollowFleetCommand: IsValid asks the CFollowFleetOrder it would add (CanDo)
    namespace fo = sdk::cmd::follow_command;
    auto cmd = CommandBuilder::Get().Create(fo::kSpec);
    cmd.Set<uint32_t>(fo::fleet, fleet_id)
        .Set<uint32_t>(fo::target_fleet, target_fleet_id)
        .Set<uint8_t>(fo::attack, attack ? 1 : 0)
        .Set<uint8_t>(fo::cancelled, 0)
        .Set<uint8_t>(fo::queue, queue ? 1 : 0)
        .Set<uint8_t>(fo::queue_to_front, 0);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The fleet cannot follow that fleet" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"target_fleet_id", target_fleet_id},
             {"target_fleet", fleets::Name(target)}, {"attack", attack}, {"queued", queue},
             {"message", "Follow order posted"} };
}

nlohmann::json GalaxyManager::SetFleetStance(uint32_t fleet_id, const std::string& stance) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    auto it = std::find(std::begin(kStances), std::end(kStances), stance);
    if (it == std::end(kStances)) return { {"success", false}, {"error", "stance must be one of: passive, aggressive, evasive"} };
    uint32_t value = (uint32_t)(it - std::begin(kStances));
    void* fleet = fleets::Find(base_address_, fleet_id);
    uint32_t before = fleet ? ReadOr<uint32_t>((uintptr_t)fleet + sdk::ent::CFleet::fleet_stance, 0) : 0;
    // CSwitchFleetStanceCommand -> CFleet::SetFleetStance; IsValid: the fleet supports stances
    namespace st = sdk::cmd::switch_fleet_stance_command;
    auto cmd = CommandBuilder::Get().Create(st::kSpec);
    cmd.Set<uint32_t>(st::fleet, fleet_id).Set<uint32_t>(st::stance, value);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "This fleet has no stances (civilian or a station)" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id},
             {"previous_stance", before < std::size(kStances) ? kStances[before] : "unknown"},
             {"stance", stance}, {"stance_name", SafeLocalize(base_address_, kStanceNames[value])} };
}

nlohmann::json GalaxyManager::FleetMia(const std::vector<uint32_t>& fleet_ids, const std::string& type) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (fleet_ids.empty()) return { {"success", false}, {"error", "fleet_ids is empty"} };
    for (uint32_t f : fleet_ids) {
        if (!OwnFleet(s, f)) return { {"success", false}, {"error", "Fleet " + std::to_string(f) + " is not one of the player's fleets"} };
    }
    uint32_t mia;
    if (type == "return_home") mia = kMiaReturnHome;
    else if (type == "emergency_ftl") mia = kMiaEmergencyFtl;
    else return { {"success", false}, {"error", "type must be return_home or emergency_ftl"} };
    // CGoMIACommand: CFleet::GoMIA(type) for every fleet with CFleet::CanGoMIA
    namespace mi = sdk::cmd::mia_command;
    auto cmd = CommandBuilder::Get().Create(mi::kSpec);
    std::string why;
    if (!SetRefArray(cmd, mi::fleets, fleet_ids, &why)) return { {"success", false}, {"error", why} };
    cmd.Set<uint32_t>(sdk::rt::CGoMIACommand_mia_type, mia);
    if (!cmd.IsValid(&why)) {
        return { {"success", false}, {"error", why.empty() ? "None of these fleets can go missing in action now" : why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_ids", fleet_ids}, {"type", type},
             {"message", type == "return_home" ? "The fleets jump out and return home (MIA until they arrive)"
                                               : "Emergency FTL: the fleets retreat and are MIA for a while"} };
}

nlohmann::json GalaxyManager::ClaimSystem(uint32_t system_id, bool remove, int count) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    auto it = s.index.find(system_id);
    if (it == s.index.end()) return { {"success", false}, {"error", "Unknown system id: " + std::to_string(system_id)} };
    void* sys = s.systems[it->second].obj;
    int before = ClaimsBy(base_address_, sys, s.player);
    std::string why;
    if (remove) {
        // CRemoveSystemClaimCommand: at most the claims we hold; not while at war with the owner
        if (count <= 0) count = before;
        namespace rc = sdk::cmd::remove_system_claim_command;
        auto cmd = CommandBuilder::Get().Create(rc::kSpec);
        cmd.Set<uint32_t>(rc::country, s.player_id).Set<uint32_t>(rc::system, system_id).Set<int32_t>(rc::claims, count);
        if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "No claims to remove here" : why}, {"player_claims", before} };
        if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    } else {
        // CAddSystemClaimCommand, as the UI builds it: `count` claims dated today; IsValid is
        // CGalacticObject::IsClaimableBy plus the influence cost (CCountry::CalcClaimCost)
        if (count <= 0) count = 1;
        void* state = nullptr;
        Read((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &state);
        uint32_t today = state ? ReadOr<uint32_t>((uintptr_t)state + sdk::rt::CGameState_date_hours, 0) : 0;
        namespace ac = sdk::cmd::add_system_claim_command;
        auto cmd = CommandBuilder::Get().Create(ac::kSpec);
        cmd.Set<uint32_t>(ac::country, s.player_id)
            .Set<uint32_t>(ac::system, system_id)
            .Set<int32_t>(ac::claims, count)
            .Set<uint32_t>(ac::date, today);
        if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The system cannot be claimed" : why}, {"player_claims", before} };
        if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    }
    return { {"success", true}, {"system_id", system_id}, {"system", SystemName(system_id, sys)},
             {"action", remove ? "remove" : "add"}, {"count", count}, {"player_claims_before", before},
             {"message", "Claim order posted; read player_claims from get_system on a later call"} };
}

uint32_t GalaxyManager::SpatialSystem(uint32_t type, uint32_t id) {
    namespace cc = sdk::ent::CCelestialCoordinate;
    auto origin = [](void* obj, std::ptrdiff_t coord) {
        return obj ? ReadOr<uint32_t>((uintptr_t)obj + coord + cc::origin, kInvalidId) : kInvalidId;
    };
    switch (type) {
    case kSpatialSystem:
        return id;
    case kSpatialPlanet:
        return origin(RefLookup(base_address_, sdk::db::CPlanet, id), sdk::ent::CPlanet::coordinate);
    case kSpatialDebris:
        return origin(RefLookup(base_address_, sdk::db::CDebris, id), sdk::ent::CDebris::coordinate);
    case kSpatialMegastructure:
        return origin(RefLookup(base_address_, sdk::db::CMegaStructure, id), sdk::ent::CMegaStructure::coordinate);
    case kSpatialNaturalWormhole:
        return origin(RefLookup(base_address_, sdk::db::CNaturalWormhole, id), sdk::ent::CNaturalWormhole::coordinate);
    case kSpatialAstralRift:
        return origin(RefLookup(base_address_, sdk::db::CAstralRift, id), sdk::ent::CAstralRift::coordinate);
    case kSpatialFleet:
        if (void* f = fleets::Find(base_address_, id)) {
            const void* c = FleetCoordinate(base_address_, f);
            return c ? ReadOr<uint32_t>((uintptr_t)c + cc::origin, kInvalidId) : kInvalidId;
        }
        return kInvalidId;
    default:
        return kInvalidId;
    }
}

nlohmann::json GalaxyManager::CollectData(uint32_t fleet_id, uint32_t project_id, uint32_t system_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    if (project_id == kInvalidId && !s.index.count(system_id)) {
        return { {"success", false}, {"error", "Give project_id, or system_id of a system with a located special project"} };
    }
    // CCollectDataFleetOrderCommand: galactic_object set = every located project of the country in
    // that system; galactic_object -1 = the project with this id (as the two engine ctors build it)
    namespace cd = sdk::cmd::collect_data_fleet_order_command;
    auto cmd = CommandBuilder::Get().Create(cd::kSpec);
    cmd.Set<uint32_t>(cd::fleet, fleet_id)
        .Set<uint32_t>(cd::galactic_object, project_id != kInvalidId ? kInvalidId : system_id)
        .Set<uint32_t>(cd::id, project_id != kInvalidId ? project_id : 0)
        .Set<uint8_t>(cd::queue, queue ? 1 : 0);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return { {"success", false}, {"error", why.empty() ? "The game refused: no special project there that this fleet can research" : why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    nlohmann::json out = { {"success", true}, {"fleet_id", fleet_id}, {"queued", queue}, {"message", "Research order posted"} };
    if (project_id != kInvalidId) out["project_id"] = project_id;
    else out["system_id"] = system_id;
    return out;
}

nlohmann::json GalaxyManager::LandArmies(uint32_t fleet_id, uint32_t planet_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    void* planet = RefLookup(base_address_, sdk::db::CPlanet, planet_id);
    if (!planet) return { {"success", false}, {"error", "Unknown planet id: " + std::to_string(planet_id)} };
    uint32_t colony = ReadOr<uint32_t>((uintptr_t)planet + sdk::ent::CPlanet::colony, kInvalidId);
    if (colony == kInvalidId) return { {"success", false}, {"error", "The planet has no colony to invade"} };
    // CFleetLandArmiesCommand: IsValid is CLandArmiesFleetOrder::IsPossible (war, armies aboard,
    // planetary shields and defences ...), with the game's reason
    namespace la = sdk::cmd::fleet_land_armies_command;
    auto cmd = CommandBuilder::Get().Create(la::kSpec);
    cmd.Set<uint32_t>(la::fleet, fleet_id).Set<uint32_t>(la::colony, colony)
        .Set<uint8_t>(la::queue, queue ? 1 : 0).Set<uint8_t>(la::queue_to_front, 0);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The game refused the landing" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"planet_id", planet_id}, {"colony_id", colony},
             {"planet", PersistentNameText((const void*)((uintptr_t)planet + sdk::ent::CPlanet::name))}, {"queued", queue},
             {"message", "Landing order posted: the armies fly there and invade"} };
}

nlohmann::json GalaxyManager::OrbitPlanet(uint32_t fleet_id, uint32_t planet_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    void* planet = RefLookup(base_address_, sdk::db::CPlanet, planet_id);
    if (!planet) return { {"success", false}, {"error", "Unknown planet id: " + std::to_string(planet_id)} };
    namespace ob = sdk::cmd::fleet_orbit_planet;
    auto cmd = CommandBuilder::Get().Create(ob::kSpec);
    cmd.Set<uint32_t>(ob::fleet, fleet_id)
        .Set<uint32_t>(ob::orbitable + kOrbitableId, planet_id)
        .Set<uint8_t>(ob::orbitable + kOrbitableKind, kOrbitablePlanet)
        .Set<uint8_t>(ob::queue, queue ? 1 : 0)
        .Set<uint8_t>(ob::queue_to_front, 0);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The fleet cannot move (a station or immobile)" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"planet_id", planet_id},
             {"planet", PersistentNameText((const void*)((uintptr_t)planet + sdk::ent::CPlanet::name))}, {"queued", queue},
             {"message", "Orbit order posted"} };
}

nlohmann::json GalaxyManager::ResearchAnomalies(uint32_t fleet_id, uint32_t system_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    if (!s.index.count(system_id)) return { {"success", false}, {"error", "Unknown system id: " + std::to_string(system_id)} };
    // CFleetResearchAnomaliesCommand: a science ship researches the system's discovered anomalies
    namespace ra = sdk::cmd::research_anomalies;
    auto cmd = CommandBuilder::Get().Create(ra::kSpec);
    cmd.Set<uint32_t>(ra::fleet, fleet_id).Set<uint32_t>(ra::system, system_id).Set<uint8_t>(ra::queue, queue ? 1 : 0);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The game refused the anomaly research" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"system_id", system_id},
             {"system", SystemName(system_id, s.systems[s.index[system_id]].obj)}, {"queued", queue},
             {"message", "Anomaly research order posted"} };
}

nlohmann::json GalaxyManager::ExcavateSite(uint32_t fleet_id, uint32_t site_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    if (!RefLookup(base_address_, sdk::db::CArchaeologicalSite, site_id)) {
        return { {"success", false}, {"error", "Unknown archaeological site id: " + std::to_string(site_id)} };
    }
    // CExcavateArchaeologicalSiteFleetOrderCommand: a science ship with a scientist digs the site
    namespace ex = sdk::cmd::excavate_archaeological_site_fleet_order_command;
    auto cmd = CommandBuilder::Get().Create(ex::kSpec);
    cmd.Set<uint32_t>(ex::fleet, fleet_id).Set<uint32_t>(ex::archaeological_site, site_id).Set<uint8_t>(ex::queue, queue ? 1 : 0);
    std::string why;
    if (!cmd.IsValid(&why)) {
        // CArchaeologicalSite::IsPotentialExcavator: chapters left and the site type's potential
        // trigger for this fleet; neither gives a reason text
        return { {"success", false}, {"error", why.empty() ? "The game refused the excavation: the site has no chapters left, "
                                                             "or its potential trigger rejects this fleet" : why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"site_id", site_id}, {"queued", queue},
             {"message", "Excavation order posted"} };
}

nlohmann::json GalaxyManager::UseBypass(uint32_t fleet_id, uint32_t bypass_id, uint32_t to_system, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    void* b = RefLookup(base_address_, sdk::db::CBypass, bypass_id);
    if (!b) return { {"success", false}, {"error", "Unknown bypass id: " + std::to_string(bypass_id)} };
    // the destination is the bypass in to_system that this one connects to (its linked bypass or
    // one of its active network connections)
    namespace bp = sdk::ent::CBypass;
    uint32_t dest = kInvalidId;
    auto it = s.index.find(to_system);
    if (it == s.index.end()) return { {"success", false}, {"error", "Unknown system id: " + std::to_string(to_system)} };
    auto there = RefArray((uintptr_t)s.systems[it->second].obj + sdk::ent::CGalacticObject::bypasses);
    std::vector<uint32_t> links = RefArray((uintptr_t)b + bp::active_connections);
    links.push_back(ReadOr<uint32_t>((uintptr_t)b + bp::linked_to, kInvalidId));
    for (uint32_t l : links) {
        if (l != kInvalidId && std::find(there.begin(), there.end(), l) != there.end()) {
            dest = l;
            break;
        }
    }
    if (dest == kInvalidId) {
        return { {"success", false}, {"error", "This bypass does not lead to that system (see leads_to in stellaris_get_system)"} };
    }
    namespace ub = sdk::cmd::use_bypass_command;
    auto cmd = CommandBuilder::Get().Create(ub::kSpec);
    cmd.Set<uint32_t>(ub::fleet, fleet_id).Set<uint32_t>(ub::bypass, bypass_id).Set<uint32_t>(ub::destination, dest)
        .Set<uint8_t>(ub::queue, queue ? 1 : 0);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The game refused the jump" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"bypass_id", bypass_id}, {"destination_bypass_id", dest},
             {"to_system_id", to_system}, {"to_system", SystemName(to_system, s.systems[it->second].obj)}, {"queued", queue},
             {"message", "Bypass jump order posted (the fleet flies to the bypass first)"} };
}

nlohmann::json GalaxyManager::ExploreBypass(uint32_t fleet_id, uint32_t bypass_id, bool queue) {
    Snapshot s = Take();
    if (!s.player) return { {"error", "No player country (not in game?)"} };
    if (!OwnFleet(s, fleet_id)) return { {"success", false}, {"error", "Fleet " + std::to_string(fleet_id) + " is not one of the player's fleets"} };
    if (!RefLookup(base_address_, sdk::db::CBypass, bypass_id)) return { {"success", false}, {"error", "Unknown bypass id: " + std::to_string(bypass_id)} };
    // CExploreBypassCommand: a science ship explores an unexplored wormhole / gateway
    namespace eb = sdk::cmd::explore_bypass_command;
    auto cmd = CommandBuilder::Get().Create(eb::kSpec);
    cmd.Set<uint32_t>(eb::fleet, fleet_id).Set<uint32_t>(eb::bypass, bypass_id).Set<uint8_t>(eb::queue, queue ? 1 : 0);
    std::string why;
    if (!cmd.IsValid(&why)) return { {"success", false}, {"error", why.empty() ? "The game refused the exploration" : why} };
    if (!cmd.Post(NativeCommand::Check::EngineGate)) return { {"success", false}, {"error", cmd.error()} };
    return { {"success", true}, {"fleet_id", fleet_id}, {"bypass_id", bypass_id}, {"queued", queue},
             {"message", "Bypass exploration order posted"} };
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
    // IsValid asks CanColonize without a reason; ask it first so a refusal says why
    std::string why;
    if (!CanColonize(base_address_, planet, s.player, &why)) {
        return { {"success", false}, {"error", why.empty() ? "The game refused the colonization" : why} };
    }
    namespace co = sdk::cmd::colonize_planet_order;
    auto cmd = CommandBuilder::Get().Create(co::kSpec);
    cmd.Set<uint32_t>(co::fleet, fleet_id)
        .Set<uint32_t>(co::planet, planet_id)
        .Set<uint8_t>(co::queue, queue ? 1 : 0)
        .Set<uint8_t>(co::queue_to_front, 0);
    if (!cmd.IsValid(&why)) {
        // CanColonize passed: CColonizePlanetFleetOrder::CanQueue refused this fleet
        return { {"success", false}, {"error", why.empty() ? "This fleet cannot take the colonization order (not a colony ship?)" : why} };
    }
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
    std::map<std::string, int> skipped;  // unsurveyed systems the fleet cannot survey, by the game's reason
    if (purpose == "unsurveyed") {
        // the game decides what a science ship may survey (route, border access, nothing left to
        // survey there for it): the survey order's IsValid, as the fleet's order menu checks it
        if (fleet_id == kInvalidId) {
            for (uint32_t f : fleets::Owned(s.player)) {
                if (fleets::ClassOf(fleets::Find(base_address_, f)) == fleets::ShipClass::ScienceShip) {
                    fleet_id = f;
                    break;
                }
            }
        }
        for (const auto& [d, id] : order) {
            if ((int)out.size() >= limit) break;
            const System& sys = s.systems[s.index[id]];
            if (SystemSurveyed(s, id, sys.obj)) continue;
            if (fleet_id != kInvalidId) {
                std::string why;
                if (!SurveyCommand(fleet_id, id, kInvalidId, false, false, &why)) {
                    skipped[why.empty() ? "rejected by the game" : RenderPdxMarkup(why)]++;
                    continue;
                }
            }
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
        // NHabitability::CalcHabitability(species, planet, player) above zero; best first. The
        // planner hides systems of empires the player has communications with; a system another
        // country owns cannot be colonized at all, so every foreign-owned system is left out
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
            uint32_t sys_owner = Owner(sys.obj);
            if (sys_owner != kInvalidId && sys_owner != s.player_id) continue;
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
                    {"system_id", id}, {"system", SystemName(id, sys.obj)}, {"jumps", d},
                    {"system_owned", sys_owner == s.player_id} } });
            }
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.hab != b.hab ? a.hab > b.hab : a.jumps < b.jumps;
        });
        nlohmann::json planets = nlohmann::json::array();
        for (size_t i = 0; i < cands.size() && (int)i < limit; ++i) {
            // the game's own verdict for the player now (borders, hostile fleets, blockers ...)
            nlohmann::json row = cands[i].row;
            void* planet = RefLookup(base_address_, sdk::db::CPlanet, row["planet_id"].get<uint32_t>());
            std::string why;
            bool can = planet && CanColonize(base_address_, planet, s.player, &why);
            row["can_colonize"] = can;
            if (!can && !why.empty()) row["reason"] = why;
            planets.push_back(row);
        }
        return { {"purpose", purpose}, {"from_system_id", from}, {"species_id", species_id}, {"planets", planets},
                 {"note", "Habitability for the species with the player's modifiers; systems owned by other empires are "
                          "left out. can_colonize is the game's CPlanet::CanColonize for the player now (reason when not); "
                          "colonizing also needs a colony ship."} };
    } else {
        return { {"error", "purpose must be one of: unsurveyed, outpost, deposit, colonizable"} };
    }
    nlohmann::json res = { {"purpose", purpose}, {"from_system_id", from}, {"systems", out} };
    if (purpose == "outpost" || purpose == "unsurveyed") {
        res["checked_with_fleet_id"] = fleet_id == kInvalidId ? nlohmann::json(nullptr) : nlohmann::json(fleet_id);
    }
    if (purpose == "unsurveyed" && !skipped.empty()) {
        nlohmann::json sk = nlohmann::json::object();
        for (const auto& [why, n] : skipped) sk[why] = n;
        res["not_surveyable"] = sk;  // unsurveyed but refused for this fleet, counted by the game's reason
    }
    return res;
}

}  // namespace bridge
