#include "game_state.hpp"
#include "command_builder.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "situation_log_manager.hpp"
#include "government_manager.hpp"
#include "society_manager.hpp"
#include "leader_manager.hpp"
#include "species_manager.hpp"
#include "fleet_manager.hpp"
#include "market_manager.hpp"
#include "discoveries_manager.hpp"
#include "contacts_manager.hpp"
#include "outliner_manager.hpp"
#include <cmath>

namespace bridge {

// Safe wrappers to prevent SEH / C2712 unwinding issues
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

static bool SafeReadU64(const void* addr, uint64_t* out) {
    __try {
        *out = *(const uint64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU8(const void* addr, uint8_t* out) {
    __try {
        *out = *(const uint8_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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

static bool SafeCopyChars(char* dest, const char* src, size_t count) {
    __try {
        memcpy(dest, src, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadPdxString(const void* pdx_str_addr, char* out, size_t max_len) {
    if (!pdx_str_addr || !out || max_len == 0) return false;
    out[0] = '\0';

    uint64_t str_sz = 0;
    uint64_t str_cap = 0;
    if (!SafeReadU64((const void*)((uintptr_t)pdx_str_addr + 16), &str_sz) ||
        !SafeReadU64((const void*)((uintptr_t)pdx_str_addr + 24), &str_cap)) {
        return false;
    }
    if (str_sz == 0 || str_sz > 1024) return false;

    size_t copy_len = str_sz < (max_len - 1) ? (size_t)str_sz : (max_len - 1);

    if (str_cap < 16) {
        if (SafeCopyChars(out, (const char*)pdx_str_addr, copy_len)) {
            out[copy_len] = '\0';
            return true;
        }
    } else {
        void* heap_ptr = nullptr;
        if (SafeReadPtr(pdx_str_addr, &heap_ptr) && heap_ptr) {
            if (SafeCopyChars(out, (const char*)heap_ptr, copy_len)) {
                out[copy_len] = '\0';
                return true;
            }
        }
    }
    return false;
}

static uint32_t ParseU32(const char* str) {
    if (!str) return 0;
    while (*str) {
        if (*str == '\x11') {
            str += (*(str + 1) ? 2 : 1);
            continue;
        }
        if (*str >= '0' && *str <= '9') {
            return (uint32_t)strtoul(str, nullptr, 10);
        }
        str++;
    }
    return 0;
}

static CapacityInfo ParseCapacity(const char* str) {
    CapacityInfo info{};
    if (!str) return info;

    const char* p = str;
    while (*p && (*p < '0' || *p > '9') && *p != '/') {
        if (*p == '\x11') {
            p += (*(p + 1) ? 2 : 1);
            continue;
        }
        p++;
    }
    if (*p >= '0' && *p <= '9') {
        char* end = nullptr;
        info.used = (uint32_t)strtoul(p, &end, 10);
        p = end;
    }

    while (*p && *p != '/') p++;
    if (*p == '/') {
        p++;
        while (*p && (*p < '0' || *p > '9')) {
            if (*p == '\x11') {
                p += (*(p + 1) ? 2 : 1);
                continue;
            }
            p++;
        }
        if (*p >= '0' && *p <= '9') {
            info.capacity = (uint32_t)strtoul(p, nullptr, 10);
        }
    }
    return info;
}

GameState& GameState::Get() {
    static GameState instance;
    return instance;
}

void GameState::Init(uintptr_t base_address) {
    base_address_ = base_address;
    cached_resource_names_.clear();
    LOGF("[GAME_STATE] Initialized with base address: 0x%llX", (unsigned long long)base_address_);
}

void* GameState::GetInGameIdler() {
    if (!base_address_) {
        return nullptr;
    }

    void* idler = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + in_game_idler_rva_), &idler)) {
        return idler;
    }
    return nullptr;
}

void* GameState::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    // Global CCountryManager at base + sdk::db::CCountry (4.5.1 Cygnus)
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
            // First entry in array (country ID 0 is player)
            if (SafeReadPtr((const void*)((uintptr_t)countries_arr + 8), &country_0) && country_0) {
                return country_0;
            }
        }
    }

    return nullptr;
}

uint32_t GameState::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;
    uint32_t cid = 0;
    if (SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid)) {
        return cid;
    }
    return 0;
}

GameDate GameState::ReadDate() {
    GameDate date{};
    if (!base_address_) return date;

    void* global_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &global_mgr) || !global_mgr || (uintptr_t)global_mgr < 0x10000) {
        return date;
    }

    uint32_t raw_hours = 0;
    if (!SafeReadU32((const void*)((uintptr_t)global_mgr + 0xC0), &raw_hours) || raw_hours < 0x29C55C0) {
        return date;
    }

    uint32_t hours = raw_hours - 0x29C55C0;
    uint32_t total_days = hours / 24;
    date.year = total_days / 360;
    date.month = (total_days % 360) / 30 + 1;
    date.day = (total_days % 360) % 30 + 1;

    char buf[32];
    snprintf(buf, sizeof(buf), "%04u.%02u.%02u", date.year, date.month, date.day);
    date.formatted = buf;
    return date;
}

void GameState::EnsureResourceNamesLoaded() {
    if (!cached_resource_names_.empty() || !base_address_) return;

    void* res_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CStrategicResourceDatabase_pInstance), &res_db) || !res_db || (uintptr_t)res_db < 0x10000) {
        return;
    }

    uint32_t count = 0;
    void* res_arr = nullptr;
    if (!SafeReadU32((const void*)((uintptr_t)res_db + 0x14), &count) || count == 0 || count > 256) {
        return;
    }
    if (!SafeReadPtr((const void*)((uintptr_t)res_db + 0x08), &res_arr) || !res_arr) {
        return;
    }

    std::vector<std::string> names;
    std::vector<void*> ptrs;
    names.reserve(count);

    char name_buf[128];
    for (uint32_t i = 0; i < count; ++i) {
        void* res_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)res_arr + i * 8), &res_ptr) || !res_ptr) {
            names.push_back("");
            ptrs.push_back(nullptr);
            continue;
        }
        ptrs.push_back(res_ptr);
        if (SafeReadPdxString((const void*)((uintptr_t)res_ptr + 0x30), name_buf, sizeof(name_buf))) {
            names.push_back(name_buf);
        } else {
            names.push_back("");
        }
    }

    if (names.size() == count) {
        cached_resource_names_ = std::move(names);
        cached_resource_ptrs_ = std::move(ptrs);
        LOGF("[GAME_STATE] Loaded %u global resource names.", (unsigned int)cached_resource_names_.size());
    }
}

const std::vector<std::string>& GameState::ResourceNames() {
    EnsureResourceNamesLoaded();
    return cached_resource_names_;
}

nlohmann::json GameState::ResourceTableJson(const void* table_ptr_field) {
    nlohmann::json out = nlohmann::json::object();
    const auto& names = ResourceNames();
    void* table = nullptr;
    void* data = nullptr;
    uint32_t size = 0;
    if (!table_ptr_field || !SafeReadPtr(table_ptr_field, &table) || !table ||
        !SafeReadPtr(table, &data) || !data ||
        !SafeReadU32((const void*)((uintptr_t)table + 0xC), &size)) {
        return out;
    }
    for (uint32_t i = 0; i < size && i < names.size(); ++i) {
        int64_t raw = 0;
        if (!names[i].empty() && SafeReadI64((const void*)((uintptr_t)data + i * 8), &raw) && raw != 0) {
            out[names[i]] = std::round(raw / 1000.0) / 100.0;
        }
    }
    return out;
}

namespace {
struct ResourceMaxCtx {
    uintptr_t fn;
    void* resource;
    void* country;
    int64_t result;
};

void CallResourceMax(void* c, void*) {
    auto* x = (ResourceMaxCtx*)c;
    ((int64_t* (*)(const void*, int64_t*, const void*))x->fn)(x->resource, &x->result, x->country);
}
}  // namespace

std::unordered_map<std::string, ResourceDetail> GameState::ReadResources(void* country) {
    std::unordered_map<std::string, ResourceDetail> result;
    if (!country) {
        country = GetPlayerCountry();
    }
    if (!country) return result;

    EnsureResourceNamesLoaded();
    if (cached_resource_names_.empty()) return result;

    size_t count = cached_resource_names_.size();

    // 1. Stockpile array from [Country + 0x2B40] + 0x30
    void* bal_ptr = nullptr;
    void* stockpile_arr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x2B40), &bal_ptr) && bal_ptr) {
        SafeReadPtr((const void*)((uintptr_t)bal_ptr + 0x30), &stockpile_arr);
    }

    // 2. Vector arrays:
    // Game native total monthly income at Country + 0x2018 (includes market/all sources)
    // Game native total monthly expense at Country + 0x2038 (includes trades/maintenance)
    // Game native true monthly net balance at Country + 0x2058
    void* income_arr = nullptr;
    void* expense_arr = nullptr;
    void* net_arr = nullptr;

    SafeReadPtr((const void*)((uintptr_t)country + 0x2018), &income_arr);
    SafeReadPtr((const void*)((uintptr_t)country + 0x2038), &expense_arr);
    SafeReadPtr((const void*)((uintptr_t)country + 0x2058), &net_arr);

    auto round2 = [](double val) -> double {
        return std::round(val * 100.0) / 100.0;
    };

    for (size_t i = 0; i < count; ++i) {
        const std::string& key = cached_resource_names_[i];
        if (key.empty()) continue;

        ResourceDetail det{};
        if (stockpile_arr) {
            int64_t raw_val = 0;
            if (SafeReadI64((const void*)((uintptr_t)stockpile_arr + i * 8), &raw_val)) {
                det.stockpile = round2(raw_val / 100000.0);
            }
        }

        if (income_arr) {
            int64_t raw_val = 0;
            if (SafeReadI64((const void*)((uintptr_t)income_arr + i * 8), &raw_val)) {
                det.income = round2(raw_val / 100000.0);
            }
        }

        if (expense_arr) {
            int64_t raw_val = 0;
            if (SafeReadI64((const void*)((uintptr_t)expense_arr + i * 8), &raw_val)) {
                det.expense = round2(raw_val / 100000.0);
            }
        }

        if (net_arr) {
            int64_t raw_val = 0;
            if (SafeReadI64((const void*)((uintptr_t)net_arr + i * 8), &raw_val)) {
                det.net = round2(raw_val / 100000.0);
            }
        }

        // Storage cap: CStrategicResource +0x110 is the base maximum (< 0: uncapped); the
        // country's cap comes from CStrategicResource::GetMaximumForCountry, as the top bar shows.
        void* res_ptr = i < cached_resource_ptrs_.size() ? cached_resource_ptrs_[i] : nullptr;
        int64_t base_max = -1;
        if (res_ptr && SafeReadI64((const void*)((uintptr_t)res_ptr + 0x110), &base_max) && base_max >= 0) {
            ResourceMaxCtx ctx{ base_address_ + sdk::fn::CStrategicResource_GetMaximumForCountry, res_ptr, country, 0 };
            if (CommandBuilder::Get().CallGuarded(&CallResourceMax, &ctx)) {
                det.max = round2(ctx.result / 100000.0);
            }
        }

        result[key] = det;
    }

    return result;
}

EmpireStats GameState::ReadEmpireStats(void* country) {
    EmpireStats stats{};
    if (!country) {
        country = GetPlayerCountry();
    }
    if (country) {
        uint32_t colonies = 0;
        if (SafeReadU32((const void*)((uintptr_t)country + 0x2CC8), &colonies)) {
            stats.colonies = colonies;
        }
    }

    void* idler = GetInGameIdler();
    if (!idler) return stats;

    void* topbar = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)idler + 0xAC8), &topbar) || !topbar) {
        return stats;
    }

    void* ui_win = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)topbar + 0x78), &ui_win) || !ui_win) {
        return stats;
    }

    void* c_arr = nullptr;
    uint32_t c_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)ui_win + 0x878), &c_arr) || !c_arr ||
        !SafeReadU32((const void*)((uintptr_t)ui_win + 0x884), &c_cnt) || c_cnt == 0 || c_cnt > 20) {
        return stats;
    }

    // Locate shared_top_bar: child container whose first named entry is "tb_energy_group"
    void* shared_tb = nullptr;
    char first_name[64];
    for (uint32_t j = 0; j < c_cnt; ++j) {
        void* child = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)c_arr + j * 8), &child) || !child) {
            continue;
        }
        void* vec = nullptr;
        uint32_t vec_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)child + 0x890), &vec) && vec &&
            SafeReadU32((const void*)((uintptr_t)child + 0x89C), &vec_cnt) && vec_cnt >= 12) {
            if (SafeReadPdxString((const void*)((uintptr_t)vec + 16), first_name, sizeof(first_name)) &&
                strcmp(first_name, "tb_energy_group") == 0) {
                shared_tb = child;
                break;
            }
        }
    }

    if (!shared_tb) return stats;

    void* stb_c_arr = nullptr;
    uint32_t stb_cnt = 0;
    void* vec_names = nullptr;
    uint32_t names_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)shared_tb + 0x878), &stb_c_arr) || !stb_c_arr ||
        !SafeReadU32((const void*)((uintptr_t)shared_tb + 0x884), &stb_cnt) ||
        !SafeReadPtr((const void*)((uintptr_t)shared_tb + 0x890), &vec_names) || !vec_names ||
        !SafeReadU32((const void*)((uintptr_t)shared_tb + 0x89C), &names_cnt)) {
        return stats;
    }

    uint32_t limit = (stb_cnt < names_cnt) ? stb_cnt : names_cnt;
    char gname[64];
    char kname[64];
    char amt_str[64];

    for (uint32_t i = 0; i < limit; ++i) {
        if (!SafeReadPdxString((const void*)((uintptr_t)vec_names + i * 48 + 16), gname, sizeof(gname))) {
            continue;
        }

        if (strcmp(gname, "empire_size_group") != 0 &&
            strcmp(gname, "starbase_group") != 0 &&
            strcmp(gname, "navy_group") != 0) {
            continue;
        }

        void* group_child = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)stb_c_arr + i * 8), &group_child) || !group_child) {
            continue;
        }

        void* keys = nullptr;
        uint32_t k_cnt = 0;
        void* vals = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)group_child + 0x710), &keys) || !keys ||
            !SafeReadU32((const void*)((uintptr_t)group_child + 0x71C), &k_cnt) || k_cnt == 0 || k_cnt > 20 ||
            !SafeReadPtr((const void*)((uintptr_t)group_child + 0x6F8), &vals) || !vals) {
            continue;
        }

        amt_str[0] = '\0';
        for (uint32_t k = 0; k < k_cnt; ++k) {
            if (SafeReadPdxString((const void*)((uintptr_t)keys + k * 48 + 16), kname, sizeof(kname)) &&
                strcmp(kname, "amount") == 0) {
                void* val_elem = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)vals + k * 8), &val_elem) && val_elem) {
                    SafeReadPdxString((const void*)((uintptr_t)val_elem + 0x168), amt_str, sizeof(amt_str));
                }
                break;
            }
        }

        if (strcmp(gname, "empire_size_group") == 0) {
            stats.empire_size = ParseU32(amt_str);
        } else if (strcmp(gname, "starbase_group") == 0) {
            stats.starbases = ParseCapacity(amt_str);
        } else if (strcmp(gname, "navy_group") == 0) {
            stats.naval_capacity = ParseCapacity(amt_str);
        }
    }

    return stats;
}

GameStatus GameState::ReadStatus() {
    GameStatus status;
    void* idler = GetInGameIdler();
    if (!idler) {
        status.in_game = false;
        return status;
    }

    uint32_t raw_speed = 0;
    uint8_t raw_paused = 0;
    if (SafeReadU32((const void*)((uintptr_t)idler + 0x590), &raw_speed) &&
        SafeReadU8((const void*)((uintptr_t)idler + 0x594), &raw_paused)) {
        status.in_game = true;
        status.speed = raw_speed;
        status.is_paused = (raw_paused != 0); // Clausewitz engine: 1 = paused, 0 = running
    } else {
        status.in_game = false;
        return status;
    }

    void* country = GetPlayerCountry();
    if (country) {
        status.date = ReadDate();
        status.stats = ReadEmpireStats(country);
        status.resources = ReadResources(country);

        auto sit_summary = SituationLogManager::Get().GetSummary();
        status.situations_count = sit_summary.situations_count;
        status.special_projects_count = sit_summary.special_projects_count;
        status.anomalies_count = sit_summary.anomalies_count;
    }

    return status;
}

nlohmann::json GameState::GetStatusJson() {
    GameStatus status = ReadStatus();
    nlohmann::json res_json = nlohmann::json::object();

    for (const auto& [key, detail] : status.resources) {
        res_json[key] = {
            {"stockpile", detail.stockpile},
            {"income", detail.income},
            {"expense", detail.expense},
            {"net", detail.net}
        };
        if (detail.max >= 0) {
            res_json[key]["max"] = detail.max;
            res_json[key]["capped"] = detail.stockpile >= detail.max;
        }
    }

    nlohmann::json root = {
        {"in_game", status.in_game},
        {"is_paused", status.is_paused},
        {"speed", status.speed}
    };

    if (status.in_game) {
        if (!status.date.formatted.empty()) {
            root["date"] = {
                {"year", status.date.year},
                {"month", status.date.month},
                {"day", status.date.day},
                {"formatted", status.date.formatted}
            };
        }
        root["stats"] = {
            {"empire_size", status.stats.empire_size},
            {"colonies", status.stats.colonies},
            {"starbases", {
                {"used", status.stats.starbases.used},
                {"capacity", status.stats.starbases.capacity}
            }},
            {"naval_capacity", {
                {"used", status.stats.naval_capacity.used},
                {"capacity", status.stats.naval_capacity.capacity}
            }}
        };
        root["situation_log"] = {
            {"situations_count", status.situations_count},
            {"special_projects_count", status.special_projects_count},
            {"anomalies_count", status.anomalies_count}
        };
        root["council"] = GovernmentManager::Get().GetSummaryJson();
        root["society"] = SocietyManager::Get().GetSummaryJson();
        root["leaders"] = LeaderManager::Get().GetSummaryJson();
        root["species"] = SpeciesManager::Get().GetSummaryJson();
        root["fleets"] = FleetManager::Get().GetSummaryJson();
        root["market"] = MarketManager::Get().GetSummaryJson();
        root["discoveries"] = DiscoveriesManager::Get().GetSummaryJson();
        root["contacts"] = ContactsManager::Get().GetSummaryJson();
        root["outliner"] = OutlinerManager::Get().GetOutlinerSummaryJson();
        root["resources"] = res_json;
    }

    return root;
}

} // namespace bridge
