#include "situation_log_manager.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "command_builder.hpp"
#include "fleet_access.hpp"
#include "species_manager.hpp"
#include <cstring>

namespace bridge {

// Safe memory access helpers
static bool SafeReadPtr(const void* addr, void** out) {
    __try {
        *out = *(void**)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(const void* addr, uint32_t* out) {
    __try {
        *out = *(const uint32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadI32(const void* addr, int32_t* out) {
    __try {
        *out = *(const int32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU64(const void* addr, uint64_t* out) {
    __try {
        *out = *(const uint64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeCopyChars(char* dest, const char* src, size_t count) {
    __try {
        memcpy(dest, src, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct RawPdxString {
    union {
        char buf[16];
        char* heap_ptr;
    };
    uint64_t size;
    uint64_t capacity;
};

static bool SafeReadPdxString(const void* pdx_str_addr, std::string& out) {
    out.clear();
    if (!pdx_str_addr) return false;

    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)pdx_str_addr, sizeof(RawPdxString))) {
        return false;
    }

    if (raw.size == 0 || raw.size > 1024) return false;

    if (raw.capacity < 16) {
        size_t len = raw.size < 16 ? (size_t)raw.size : 15;
        char temp[16]{ 0 };
        if (SafeCopyChars(temp, raw.buf, len)) {
            out = std::string(temp, len);
            return true;
        }
    } else if (raw.heap_ptr) {
        uintptr_t addr = (uintptr_t)raw.heap_ptr;
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
            size_t len = raw.size < 512 ? (size_t)raw.size : 512;
            std::string temp(len, '\0');
            if (SafeCopyChars(&temp[0], raw.heap_ptr, len)) {
                out = temp;
                return true;
            }
        }
    }
    return false;
}

static bool SafeLocalizeCall(SituationLogManager::FnLocalize fn_localize,
                             SituationLogManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(SituationLogManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SafeReadI64(const void* addr, int64_t* out) {
    __try {
        *out = *(const int64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

namespace {

// CPdxArray layouts in CCountryEventManager (serializer 0x802E10): special_project is read as
// {data, ..., size at +0xC}; anomalies (ref_array<TPdxRef<CPlanet>>) is an array object
// {vtable, data at +8, size at +0x14}.
constexpr std::ptrdiff_t kProjectsSize = 0xC;
constexpr std::ptrdiff_t kRefArrayData = 0x8;
constexpr std::ptrdiff_t kRefArraySize = 0x14;
// The special project type's key (the CSpecialProjectInstance serializer writes [type] + 0x18) and
// the anomaly category's key (the CPlanet serializer writes [anomaly] + 0x20).
constexpr std::ptrdiff_t kProjectTypeKey = 0x18;
constexpr std::ptrdiff_t kAnomalyKey = 0x20;
// CDebris compares its own id at +0x20 in TPdxRef lookups (CSpecialProjectInstance::ShouldAbort).
constexpr uint32_t kInvalidId = 0xFFFFFFFF;

// TPdxRefDatabase<T>: arr at +0x18 (16-byte slots, object at +8), capacity at +0x20.
void* RefLookup(uintptr_t base, uintptr_t db_rva, uint32_t id, std::ptrdiff_t id_off = -1) {
    if (id == kInvalidId) return nullptr;
    void* db = nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)(base + db_rva), &db) || !db ||
        !SafeReadPtr((const void*)((uintptr_t)db + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)db + 0x20), &cap)) {
        return nullptr;
    }
    uint32_t idx = id & 0xFFFFFF;
    void* obj = nullptr;
    if (idx >= cap || !SafeReadPtr((const void*)((uintptr_t)arr + idx * 16 + 8), &obj) || !obj) return nullptr;
    uint32_t own = 0;
    if (id_off >= 0 && (!SafeReadU32((const void*)((uintptr_t)obj + id_off), &own) || own != id)) return nullptr;
    return obj;
}

struct PredicateCtx {
    uintptr_t fn;
    const void* obj;
    bool result;
};

void CallPredicate0(void* c, void*) {
    auto* x = (PredicateCtx*)c;
    x->result = ((bool (*)(const void*))x->fn)(x->obj);
}

bool EnginePredicate(uintptr_t fn, const void* obj) {
    PredicateCtx ctx{ fn, obj, false };
    return CommandBuilder::Get().CallGuarded(&CallPredicate0, &ctx) && ctx.result;
}

void* EventManager(void* country) {
    return country ? (void*)((uintptr_t)country + sdk::ent::CCountry::events) : nullptr;
}

}  // namespace

std::vector<SpecialProjectItem> SituationLogManager::ReadSpecialProjects(void* country) {
    std::vector<SpecialProjectItem> out;
    void* em = EventManager(country);
    void* data = nullptr;
    uint32_t count = 0;
    const uintptr_t list = (uintptr_t)em + sdk::ent::CCountryEventManager::special_project;
    if (!em || !SafeReadPtr((const void*)list, &data) || !data ||
        !SafeReadU32((const void*)(list + kProjectsSize), &count) || count > 1000) {
        return out;
    }
    namespace sp = sdk::ent::CSpecialProjectInstance;
    for (uint32_t i = 0; i < count; ++i) {
        void* p = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)data + i * 8), &p) || !p) continue;
        SpecialProjectItem item{};
        SafeReadU32((const void*)((uintptr_t)p + sp::id), &item.id);
        SafeReadI32((const void*)((uintptr_t)p + sp::days_left), &item.days_left);
        void* type = nullptr;
        // species modification projects hold the null type, whose key is empty
        if (SafeReadPtr((const void*)((uintptr_t)p + sp::special_project), &type) && type) {
            SafeReadPdxString((const void*)((uintptr_t)type + kProjectTypeKey), item.key);
        }
        // CSpecialProjectInstance::GetName: species modification and uplift projects are named
        // after the species, debris projects after the system of the debris, the rest by type.
        uint32_t convert_to = kInvalidId;
        SafeReadU32((const void*)((uintptr_t)p + sp::convert_to), &convert_to);
        uint32_t debris_id = kInvalidId;
        SafeReadU32((const void*)((uintptr_t)p + sp::debris), &debris_id);
        // CSpecialProjectInstance::coordinate is a spatial reference {vtable, type +8, id +0xC}
        SafeReadU32((const void*)((uintptr_t)p + sp::coordinate + 0x8), &item.location_type);
        SafeReadU32((const void*)((uintptr_t)p + sp::coordinate + 0xC), &item.location_id);
        const bool species_mod = EnginePredicate(base_address_ + sdk::fn::CSpecialProjectInstance_IsSpeciesModification, p);
        const bool uplift = !species_mod && EnginePredicate(base_address_ + sdk::fn::CSpecialProjectInstance_IsUplift, p);
        if (species_mod || uplift) {
            item.kind = species_mod ? "species_modification" : "uplift";
            item.species_id = convert_to;
            std::string species;
            if (void* sp_obj = SpeciesManager::Get().FindSpeciesPtr(convert_to)) {
                species = PersistentNameText((const void*)((uintptr_t)sp_obj + sdk::ent::CSpecies::name));
            }
            item.key = species_mod ? "MOD_TRAIT_PROJECT" : "UPLIFT_PROJECT";
            item.name = LocalizeWithParam(base_address_, item.key, species_mod ? "TEMPLATE" : "SPECIES", species);
        } else if (void* debris = RefLookup(base_address_, sdk::db::CDebris, debris_id, sdk::rt::CDebris_id)) {
            item.kind = "debris";
            item.debris_id = debris_id;
            uint32_t system_id = kInvalidId;
            SafeReadU32((const void*)((uintptr_t)debris + sdk::ent::CDebris::coordinate +
                                      sdk::ent::CCelestialCoordinate::origin), &system_id);
            std::string system;
            if (void* sys = RefLookup(base_address_, sdk::db::CGalacticObject, system_id)) {
                system = PersistentNameText((const void*)((uintptr_t)sys + sdk::ent::CGalacticObject::name));
            }
            item.key = "SPECIAL_PROJECT_DEBRIS";
            item.name = LocalizeWithParam(base_address_, item.key, "SYSTEM", system);
        } else {
            item.kind = "project";
            item.name = item.key.empty() ? "" : LocalizeKey(item.key);
        }
        out.push_back(item);
    }
    return out;
}

std::vector<AnomalyItem> SituationLogManager::ReadAnomalies(void* country) {
    std::vector<AnomalyItem> out;
    void* em = EventManager(country);
    if (!em) return out;
    const uintptr_t arr = (uintptr_t)em + sdk::ent::CCountryEventManager::anomalies;
    void* data = nullptr;
    uint32_t count = 0;
    if (!SafeReadPtr((const void*)(arr + kRefArrayData), &data) || !data ||
        !SafeReadU32((const void*)(arr + kRefArraySize), &count) || count > 4096) {
        return out;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t planet_id = kInvalidId;
        if (!SafeReadU32((const void*)((uintptr_t)data + i * 4), &planet_id)) continue;
        void* planet = RefLookup(base_address_, sdk::db::CPlanet, planet_id);
        void* anomaly = nullptr;
        // the planet keeps its anomaly (category) until it is researched, then holds a null object
        if (!planet || !SafeReadPtr((const void*)((uintptr_t)planet + sdk::ent::CPlanet::anomaly), &anomaly) ||
            !IsRealObject(anomaly)) {
            continue;
        }
        AnomalyItem item{};
        item.planet_id = planet_id;
        SafeReadPdxString((const void*)((uintptr_t)anomaly + kAnomalyKey), item.key);
        if (item.key.empty()) continue;
        item.name = LocalizeKey(item.key);
        item.planet_name = PersistentNameText((const void*)((uintptr_t)planet + sdk::ent::CPlanet::name));
        out.push_back(item);
    }
    return out;
}

SituationLogManager& SituationLogManager::Get() {
    static SituationLogManager instance;
    return instance;
}

bool SituationLogManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_pdx_string_assign_ = (FnPdxStringAssign)(base_address_ + 0x15BA40);

    LOGF("[SITUATION_LOG] Initialized (Base: 0x%llX)", (unsigned long long)base_address_);

    return true;
}

void* SituationLogManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CCountry), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
    if (mgr && (uintptr_t)mgr >= 0x10000) {
        void* countries_arr = nullptr;
        uint32_t count = 0;
        if (SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &countries_arr) && countries_arr &&
            SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &count) && count > 0) {
            void* country_0 = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)countries_arr + 8), &country_0) && country_0) {
                return country_0;
            }
        }
    }

    return nullptr;
}

uint32_t SituationLogManager::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;

    uint32_t cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid);
    return cid;
}

std::string SituationLogManager::LocalizeKey(const std::string& key) {
    return SafeLocalize(base_address_, key);
}

SituationLogSummary SituationLogManager::GetSummary() {
    SituationLogSummary summary{};
    if (!base_address_) return summary;

    uint32_t player_id = GetPlayerCountryId();

    // 1. Situations count for player empire from global entity manager
    void* sit_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::db::CSituation), &sit_mgr) && sit_mgr) {
        void* arr = nullptr;
        uint32_t cap = 0;
        if (SafeReadPtr((const void*)((uintptr_t)sit_mgr + 0x18), &arr) && arr &&
            SafeReadU32((const void*)((uintptr_t)sit_mgr + 0x20), &cap) && cap > 0) {
            for (uint32_t i = 0; i < cap && i < 2048; ++i) {
                void* sit = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 16 + 8), &sit) || !sit) {
                    continue;
                }
                uint32_t owner_id = 0;
                SafeReadU32((const void*)((uintptr_t)sit + 0x204), &owner_id);
                if (owner_id == player_id) {
                    summary.situations_count++;
                }
            }
        }
    }

    // special projects and anomalies of the player (CCountry::events, CCountryEventManager)
    void* country = GetPlayerCountry();
    summary.special_projects_count = (uint32_t)ReadSpecialProjects(country).size();
    summary.anomalies_count = (uint32_t)ReadAnomalies(country).size();
    return summary;
}

FullSituationLogState SituationLogManager::GetSituationLogState(bool player_only) {
    FullSituationLogState state;
    if (!base_address_) return state;

    uint32_t player_id = GetPlayerCountryId();
    state.summary = GetSummary();

    // 1. Extract Situations from Global Situation EntityManager
    void* sit_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::db::CSituation), &sit_mgr) && sit_mgr) {
        void* arr = nullptr;
        uint32_t cap = 0;
        if (SafeReadPtr((const void*)((uintptr_t)sit_mgr + 0x18), &arr) && arr &&
            SafeReadU32((const void*)((uintptr_t)sit_mgr + 0x20), &cap) && cap > 0) {
            for (uint32_t i = 0; i < cap && i < 2048; ++i) {
                void* sit = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 16 + 8), &sit) || !sit) {
                    continue;
                }

                uint32_t sit_id = 0;
                SafeReadU32((const void*)((uintptr_t)sit + 0x08), &sit_id);

                uint32_t owner_id = 0;
                SafeReadU32((const void*)((uintptr_t)sit + 0x204), &owner_id);

                bool is_player = (owner_id == player_id);
                if (player_only && !is_player) {
                    continue;
                }

                SituationItem item{};
                item.id = sit_id;
                item.owner_country_id = owner_id;
                item.is_player = is_player;

                void* type_ptr = nullptr;
                SafeReadPtr((const void*)((uintptr_t)sit + 0x208), &type_ptr);
                if (type_ptr) {
                    SafeReadPdxString((const void*)((uintptr_t)type_ptr + 0x20), item.key);
                    item.name = LocalizeKey(item.key);
                }

                void* approach_ptr = nullptr;
                SafeReadPtr((const void*)((uintptr_t)sit + 0x210), &approach_ptr);
                if (approach_ptr) {
                    SafeReadPdxString((const void*)((uintptr_t)approach_ptr + 0x18), item.current_approach);
                    item.current_approach_name = LocalizeKey(item.current_approach);
                }

                // CFixedPoint (x100000): a stage with `end = 1920` is cached as 192000000
                int64_t raw_prog = 0;
                SafeReadI64((const void*)((uintptr_t)sit + sdk::ent::CSituation::progress), &raw_prog);
                item.progress = (double)raw_prog / 100000.0;

                int64_t raw_rate = 0;
                SafeReadI64((const void*)((uintptr_t)sit + sdk::ent::CSituation::last_month_progress), &raw_rate);
                item.monthly_change = (double)raw_rate / 100000.0;

                state.situations.push_back(item);
            }
        }
    }

    void* country = GetPlayerCountry();
    state.special_projects = ReadSpecialProjects(country);
    state.anomalies = ReadAnomalies(country);
    return state;
}

nlohmann::json SituationLogManager::GetSituationLogJson(bool player_only) {
    FullSituationLogState state = GetSituationLogState(player_only);

    nlohmann::json sit_arr = nlohmann::json::array();
    for (const auto& item : state.situations) {
        sit_arr.push_back({
            {"id", item.id},
            {"key", item.key},
            {"name", item.name},
            {"current_approach", item.current_approach},
            {"current_approach_name", item.current_approach_name},
            {"progress", item.progress},
            {"monthly_change", item.monthly_change},
            {"owner_country_id", item.owner_country_id},
            {"is_player", item.is_player}
        });
    }

    nlohmann::json sp_arr = nlohmann::json::array();
    for (const auto& item : state.special_projects) {
        nlohmann::json j = {
            {"id", item.id},
            {"key", item.key},
            {"name", item.name},
            {"kind", item.kind}
        };
        if (item.days_left >= 0) j["days_left"] = item.days_left;  // -1: no deadline
        if (item.species_id != 0xFFFFFFFF) j["species_id"] = item.species_id;
        sp_arr.push_back(j);
    }

    nlohmann::json anom_arr = nlohmann::json::array();
    for (const auto& item : state.anomalies) {
        anom_arr.push_back({
            {"planet_id", item.planet_id},
            {"planet_name", item.planet_name},
            {"key", item.key},
            {"name", item.name}
        });
    }

    return {
        {"summary", {
            {"situations_count", state.summary.situations_count},
            {"special_projects_count", state.special_projects.size()},
            {"anomalies_count", state.anomalies.size()}
        }},
        {"situations", sit_arr},
        {"special_projects", sp_arr},
        {"anomalies", anom_arr},
        {"player_only_filter", player_only}
    };
}

nlohmann::json SituationLogManager::SetSituationApproach(uint32_t situation_id, const std::string& approach_key) {
    if (approach_key.empty()) {
        return {
            {"error", {
                {"code", -32050},
                {"message", "Approach key cannot be empty"}
            }}
        };
    }

    uint32_t target_country_id = GetPlayerCountryId();

    // If situation_id is specified, find it to verify ownership
    if (situation_id != 0) {
        void* sit_mgr = nullptr;
        if (SafeReadPtr((const void*)(base_address_ + sdk::db::CSituation), &sit_mgr) && sit_mgr) {
            void* arr = nullptr;
            uint32_t cap = 0;
            if (SafeReadPtr((const void*)((uintptr_t)sit_mgr + 0x18), &arr) && arr &&
                SafeReadU32((const void*)((uintptr_t)sit_mgr + 0x20), &cap) && cap > 0) {
                bool found = false;
                for (uint32_t i = 0; i < cap && i < 2048; ++i) {
                    void* sit = nullptr;
                    if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 16 + 8), &sit) || !sit) continue;
                    uint32_t s_id = 0;
                    SafeReadU32((const void*)((uintptr_t)sit + 0x08), &s_id);
                    if (s_id == situation_id) {
                        found = true;
                        uint32_t owner = 0;
                        SafeReadU32((const void*)((uintptr_t)sit + 0x204), &owner);
                        target_country_id = owner;
                        break;
                    }
                }
                if (!found) {
                    LOGF("[SITUATION_LOG] Situation ID %u not found in global manager. Using player country %u...",
                        situation_id, target_country_id);
                }
            }
        }
    }

    // Retrieve tick timestamp from date manager (+0xC0)
    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &date_mgr) && date_mgr && (uintptr_t)date_mgr >= 0x10000) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[SITUATION_LOG] Posting CSetSituationApproachCommand for country %u, approach '%s', tick %u...",
        target_country_id, approach_key.c_str(), tick_timestamp);

    // The engine serializes only the situation and the approach key; the owning country is
    // derived from the situation, so it is not part of the payload.
    namespace approach = sdk::cmd::set_situation_approach_command;
    auto cmd = CommandBuilder::Get().Create(approach::kSpec);
    cmd.Set<uint32_t>(approach::situation, situation_id)
       .SetString(approach::key, approach_key);
    if (!cmd.Post()) {
        return {
            {"error", {
                {"code", -32053},
                {"message", cmd.error()}
            }}
        };
    }

    LOGF("[SITUATION_LOG] CSetSituationApproachCommand successfully posted.");
    return {
        {"success", true},
        {"situation_id", situation_id},
        {"country_id", target_country_id},
        {"approach_key", approach_key},
        {"name", LocalizeKey(approach_key)},
        {"message", "Situation approach command posted successfully"}
    };
}

} // namespace bridge
