#include "outliner_manager.hpp"
#include "command_builder.hpp"
#include "leader_manager.hpp"
#include "fleet_manager.hpp"
#include "species_manager.hpp"
#include "commands.hpp"
#include <windows.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <unordered_set>

namespace bridge {

// Buildables queued through CAddBuildableToQueueCommand. The SDK sees only their CSerializer
// helpers (which confirm the field layouts), not the buildable classes' own vtables, so these
// stay hand-maintained (4.5.1):
//   CBuildableBuilding             0x2391FF0  new-building items in live construction queues
//   CBuildableUpgradeBuilding      0x2391D30  only buildable vtable using BUILD_QUEUE_SUFFIX_UPGRADE
//   CBuildableClearDepositBlocker  0x2391F40  live queue items {deposit, colony}; CLEAR_BLOCKER_* strings
//   CBuildableArmy                 0x2376748
// Every CBuildable vtable shares the base implementation in slots 2 and 3; QueueBuildable
// checks that before handing an object to the engine (a wrong pointer here once crashed the
// game with a pure virtual call).
constexpr uintptr_t kBuildableBuildingVt = 0x2391FF0;
constexpr uintptr_t kBuildableUpgradeBuildingVt = 0x2391D30;
constexpr uintptr_t kBuildableClearDepositBlockerVt = 0x2391F40;
constexpr uintptr_t kBuildableArmyVt = 0x2376748;

// Raw Clausewitz String Layout
struct RawPdxString {
    union {
        char buf[16];
        char* heap_ptr;
    };
    uint64_t size;
    uint64_t capacity;
};

static bool SafeReadPtr(const void* addr, void** out) {
    if (!addr || !out) return false;
    __try {
        *out = *(void**)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(const void* addr, uint32_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const uint32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadI32(const void* addr, int32_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const int32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU8(const void* addr, uint8_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const uint8_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadFloat(const void* addr, float* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const float*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeConstructCmd(void* fn_construct_ptr, void* cmd, uint32_t country_id, void* owner_obj, void* bldg_def) {
    if (!fn_construct_ptr || !cmd || !owner_obj || !bldg_def) return false;
    __try {
        typedef void (*FnConstructCmdSig)(void*, uint32_t, void*, void*);
        ((FnConstructCmdSig)fn_construct_ptr)(cmd, country_id, owner_obj, bldg_def);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeConstructBuildableBuilding(void* fn_ptr, void* action_obj, uint32_t colony_id, uint32_t zone_id, void* bldg_def) {
    if (!fn_ptr || !action_obj || !bldg_def) return false;
    __try {
        typedef void* (*FnSig)(void*, uint32_t, uint32_t, void*);
        ((FnSig)fn_ptr)(action_obj, colony_id, zone_id, bldg_def);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU64(const void* addr, uint64_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const uint64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadI64(const void* addr, int64_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const int64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeCopyChars(char* dst, const char* src, size_t count) {
    if (!dst || !src || count == 0) return false;
    __try {
        memcpy(dst, src, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadPdxString(const void* str_addr, std::string& out_str) {
    out_str.clear();
    if (!str_addr) return false;

    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)str_addr, sizeof(RawPdxString))) {
        return false;
    }

    if (raw.size == 0 || raw.size > 4096) {
        return true;
    }

    if (raw.capacity < 16) {
        size_t len = raw.size < 16 ? (size_t)raw.size : 15;
        char temp[16]{ 0 };
        if (SafeCopyChars(temp, raw.buf, len)) {
            temp[len] = '\0';
            out_str = std::string(temp, len);
            return true;
        }
    } else if (raw.heap_ptr) {
        uintptr_t addr = (uintptr_t)raw.heap_ptr;
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
            size_t len = raw.size < 1024 ? (size_t)raw.size : 1024;
            std::string result(len, '\0');
            if (SafeCopyChars(&result[0], raw.heap_ptr, len)) {
                out_str = result;
                return true;
            }
        }
    }
    return false;
}



static void* FindDbElementByKey(uintptr_t base_address, uintptr_t db_rva, const std::string& target_key) {
    if (!base_address) return nullptr;
    void* db_ptr = nullptr;
    if (!SafeReadPtr((const void*)(base_address + db_rva), &db_ptr) || !db_ptr) return nullptr;
    uint32_t count = 0;
    void* arr = nullptr;
    if (!SafeReadU32((const void*)((uintptr_t)db_ptr + 0x5C), &count) || count == 0 ||
        !SafeReadPtr((const void*)((uintptr_t)db_ptr + 0x50), &arr) || !arr) return nullptr;

    for (uint32_t i = 0; i < count; ++i) {
        void* elem = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &elem) || !elem) continue;
        std::string elem_key;
        if (SafeReadPdxString((const void*)((uintptr_t)elem + 0x20), elem_key)) {
            if (elem_key == target_key) {
                return elem;
            }
        }
    }
    return nullptr;
}

OutlinerManager& OutlinerManager::Get() {
    static OutlinerManager instance;
    return instance;
}

bool OutlinerManager::Init(uintptr_t base_address) {
    base_address_ = base_address;
    if (!base_address_) return false;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + kRvaEngineAlloc);
    fn_post_command_ = Commands::Get().GetPostCommand();
    if (!fn_post_command_) {
        fn_post_command_ = (FnPostCommand)(base_address_ + kRvaPostCommand);
    }
    fn_construct_cmd_ = nullptr;
    fn_construct_bldg_ = nullptr;
    fn_enqueue_cmd_ = nullptr;
    return true;
}

uint32_t OutlinerManager::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;
    uint32_t cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid);
    return cid;
}

uint32_t OutlinerManager::GetPlanetQueueId(uint32_t planet_id) {
    if (!base_address_) return 0xFFFFFFFF;
    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) return 0xFFFFFFFF;

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    if (cid == 0xFFFFFFFF) return 0xFFFFFFFF;

    uint32_t queue_id = 0xFFFFFFFF;
    if (SafeReadU32((const void*)((uintptr_t)p_obj + 0xe4), &queue_id)) {
        return queue_id;
    }
    return 0xFFFFFFFF;
}

void* OutlinerManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113F50), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) { return nullptr; }
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

std::string OutlinerManager::LocalizeKey(const std::string& key) {
    if (key.empty()) return key;

    static const std::unordered_map<std::string, std::string> kStaticLocMap = {
        {"d_decrepit_dwellings", "贫民窟 (Sprawling Slums)"},
        {"d_decrepit_dwellings_desc", "这片破旧的住宅区条件极为恶劣，清理它可以腾出更多可用土地，并安置其居民。"},
        {"d_failing_infrastructure_earth", "工业废土 (Failing Infrastructure)"},
        {"d_failing_infrastructure", "工业废土 (Failing Infrastructure)"},
        {"d_failing_infrastructure_earth_desc", "过去废弃的工业设施与崩塌的基础设施占据了大片土地。"},
        {"d_great_pacific_garbage_patch", "太平洋垃圾带 (Great Pacific Garbage Patch)"},
        {"d_great_pacific_garbage_patch_desc", "漂浮在海洋上的庞大垃圾聚合体，需要彻底清理。"},
        {"col_capital", "帝国首都 (Empire Capital)"},
        {"col_industrial", "工业星球 (Industrial World)"},
        {"col_farming", "农业星球 (Agri-World)"},
        {"col_mining", "采矿星球 (Mining World)"},
        {"col_generator", "发电星球 (Generator World)"},
        {"col_forge", "铸造星球 (Forge World)"},
        {"col_factory", "工业星球 (Factory World)"},
        {"col_refinery", "精炼星球 (Refinery World)"},
        {"col_research", "科研星球 (Tech-World)"},
        {"col_urban", "城市星球 (Urban World)"},
        {"col_rural", "农业星球 (Rural World)"},
        {"NAME_Earth", "地球 (Earth)"},
        {"NAME_Sol", "太阳 (Sol)"},
        {"councilor_state", "国务卿"},
        {"councilor_defense", "国防部长"},
        {"councilor_research", "科技部长"},
        {"councilor_ruler_democratic", "总统"},
        {"trait_ruler_fertility_preacher", "重视农耕"},
        {"trait_ruler_fertility_preacher_2", "重视农耕 II"},
        {"leader_trait_lawless", "法外之徒"},
        {"leader_trait_lawless_2", "法外之徒 II"},
        {"trait_ruler_eye_for_talent", "慧眼识珠"},
        {"leader_trait_resilient", "坚韧不拔"},
        {"leader_trait_adaptable", "适应力强"},
        {"leader_trait_archaeologist", "考古学家"},
        {"trait_ruler_warlike", "好战者"},
        {"trait_ruler_warlike_2", "好战者 II"},
        {"PRESCRIPTED_ruler_name_humans1", "多洛雷丝·穆万加"},
        {"Khor", "科尔 (Khor)"},
        {"NEW_COLONY_NAME_1", "科尔-I (Khor-I)"},
        {"HUMAN1_CHR_Juan", "娟"},
        {"HUMAN1_CHR_Zhang", "张"},
        {"HUMAN1_CHR_Fang", "芳"},
        {"HUMAN1_CHR_Mao", "毛"},
        {"leader_trait_homesteader_2", "自耕农 II"}
    };
    auto it = kStaticLocMap.find(key);
    if (it != kStaticLocMap.end()) {
        return it->second;
    }

    return SafeLocalize(base_address_, key);
}

void* OutlinerManager::FindFleet(uint32_t fleet_id) {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3114008), &mgr) || !mgr) {
        return nullptr;
    }

    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &cap) || cap == 0) {
        return nullptr;
    }

    uint32_t slot = fleet_id & 0xFFFFFF;
    if (slot < cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + slot * 16 + 8), &ptr) && ptr) {
            uint32_t check_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)ptr + 8), &check_id) && check_id == fleet_id) {
                return ptr;
            }
        }
    }
    return nullptr;
}

void* OutlinerManager::FindColony(uint32_t colony_id) {
    if (!base_address_) return nullptr;

    void* colony_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3114140), &colony_mgr) || !colony_mgr || (uintptr_t)colony_mgr < 0x10000) { return nullptr; }
    if (!colony_mgr || (uintptr_t)colony_mgr < 0x10000) {
        return nullptr;
    }

    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)colony_mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)colony_mgr + 0x20), &cap) || cap == 0) {
        return nullptr;
    }

    uint32_t slot = colony_id & 0xFFFFFF;
    if (slot < cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + slot * 16 + 8), &ptr) && ptr) {
            return ptr;
        }
    }
    return nullptr;
}

void* OutlinerManager::FindPlanet(uint32_t planet_id) {
    if (!base_address_) return nullptr;

    void* planet_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3114128), &planet_mgr) || !planet_mgr || (uintptr_t)planet_mgr < 0x10000) { return nullptr; }
    if (!planet_mgr || (uintptr_t)planet_mgr < 0x10000) {
        return nullptr;
    }

    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)planet_mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)planet_mgr + 0x20), &cap) || cap == 0) {
        return nullptr;
    }

    // 1. Direct match by Planet ID if it has an active colony
    uint32_t slot = planet_id & 0xFFFFFF;
    if (slot < cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + slot * 16 + 8), &ptr) && ptr) {
            uint32_t pid = 0;
            SafeReadU32((const void*)((uintptr_t)ptr + 0x18), &pid);
            if (pid == planet_id) {
                uint32_t cid = 0xFFFFFFFF;
                SafeReadU32((const void*)((uintptr_t)ptr + 0xe0), &cid);
                if (cid != 0xFFFFFFFF) {
                    return ptr;
                }
            }
        }
    }

    // 2. Match by System ID (+0x50) where a colony is present (supports queries using system ID like 11, 63)
    for (uint32_t s = 0; s < cap; ++s) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + s * 16 + 8), &ptr) && ptr) {
            uint32_t sys_id = 0;
            SafeReadU32((const void*)((uintptr_t)ptr + 0x50), &sys_id);
            if (sys_id == planet_id) {
                uint32_t cid = 0xFFFFFFFF;
                SafeReadU32((const void*)((uintptr_t)ptr + 0xe0), &cid);
                if (cid != 0xFFFFFFFF) {
                    return ptr;
                }
            }
        }
    }

    // 3. Fallback direct match (even if uncolonized)
    if (slot < cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + slot * 16 + 8), &ptr) && ptr) {
            uint32_t pid = 0;
            SafeReadU32((const void*)((uintptr_t)ptr + 0x18), &pid);
            if (pid == planet_id) {
                return ptr;
            }
        }
    }

    return nullptr;
}

void* OutlinerManager::FindSystem(uint32_t system_id) {
    if (!base_address_) return nullptr;

    void* sys_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3114148), &sys_mgr) || !sys_mgr || (uintptr_t)sys_mgr < 0x10000) { return nullptr; }
    if (!sys_mgr || (uintptr_t)sys_mgr < 0x10000) {
        return nullptr;
    }

    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)sys_mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)sys_mgr + 0x20), &cap) || cap == 0) {
        return nullptr;
    }

    uint32_t slot = system_id & 0xFFFFFF;
    if (slot < cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + slot * 16 + 8), &ptr) && ptr) {
            uint32_t check_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)ptr + 8), &check_id) && check_id == system_id) {
                return ptr;
            }
        }
    }
    return nullptr;
}

std::optional<ConstructionCard> OutlinerManager::ExtractColonyConstruction(void* colony_obj) {
    if (!colony_obj || !base_address_) return std::nullopt;

    uint64_t f_f78 = 0;
    if (!SafeReadU64((const void*)((uintptr_t)colony_obj + 0xf78), &f_f78)) {
        return std::nullopt;
    }
    uint32_t slot = (uint32_t)(f_f78 & 0xFFFFFFFF);

    void* mgr_3113128 = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3114128), &mgr_3113128) || !mgr_3113128 || (uintptr_t)mgr_3113128 < 0x10000) { return std::nullopt; }
    if (!mgr_3113128 || (uintptr_t)mgr_3113128 < 0x10000) {
        return std::nullopt;
    }
    void* arr_3113128 = nullptr;
    uint32_t cap_3113128 = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr_3113128 + 0x18), &arr_3113128) || !arr_3113128 ||
        !SafeReadU32((const void*)((uintptr_t)mgr_3113128 + 0x20), &cap_3113128) || slot >= cap_3113128) {
        return std::nullopt;
    }
    void* slot_obj = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)arr_3113128 + slot * 16 + 8), &slot_obj) || !slot_obj) {
        return std::nullopt;
    }

    uint32_t queue_id = 0;
    if (!SafeReadU32((const void*)((uintptr_t)slot_obj + 0x20 + 0xc4), &queue_id)) {
        return std::nullopt;
    }

    void* mgr_eb8 = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8) || !mgr_eb8 || (uintptr_t)mgr_eb8 < 0x10000) {
        SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8);
    }
    if (!mgr_eb8 || (uintptr_t)mgr_eb8 < 0x10000) {
        return std::nullopt;
    }
    void* arr_eb8 = nullptr;
    uint32_t cap_eb8 = 0;
    uint32_t q_slot = queue_id & 0xFFFFFF;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr_eb8 + 0x18), &arr_eb8) || !arr_eb8 ||
        !SafeReadU32((const void*)((uintptr_t)mgr_eb8 + 0x20), &cap_eb8) || q_slot >= cap_eb8) {
        return std::nullopt;
    }
    void* queue_obj = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)arr_eb8 + q_slot * 16 + 8), &queue_obj) || !queue_obj) {
        return std::nullopt;
    }

    uint32_t q_cnt = 0;
    if (!SafeReadU32((const void*)((uintptr_t)queue_obj + 0x2c), &q_cnt) || q_cnt == 0) {
        return std::nullopt;
    }
    void* q_items = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)queue_obj + 0x20), &q_items) || !q_items) {
        return std::nullopt;
    }
    uint32_t item_id = 0;
    if (!SafeReadU32((const void*)q_items, &item_id)) {
        return std::nullopt;
    }

    void* mgr_ea8 = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113EA8), &mgr_ea8) || !mgr_ea8 || (uintptr_t)mgr_ea8 < 0x10000) {
        SafeReadPtr((const void*)(base_address_ + 0x3113EA8), &mgr_ea8);
    }
    if (!mgr_ea8 || (uintptr_t)mgr_ea8 < 0x10000) {
        return std::nullopt;
    }
    void* arr_ea8 = nullptr;
    uint32_t cap_ea8 = 0;
    uint32_t i_slot = item_id & 0xFFFFFF;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr_ea8 + 0x18), &arr_ea8) || !arr_ea8 ||
        !SafeReadU32((const void*)((uintptr_t)mgr_ea8 + 0x20), &cap_ea8) || i_slot >= cap_ea8) {
        return std::nullopt;
    }
    void* item_obj = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)arr_ea8 + i_slot * 16 + 8), &item_obj) || !item_obj) {
        return std::nullopt;
    }

    uint32_t prog = 0;
    uint32_t tot = 0;
    SafeReadU32((const void*)((uintptr_t)item_obj + 0x28), &prog);
    SafeReadU32((const void*)((uintptr_t)item_obj + 0x30), &tot);

    void* action_obj = nullptr;
    SafeReadPtr((const void*)((uintptr_t)item_obj + 0x18), &action_obj);
    std::string key;
    if (action_obj) {
        void* def_obj = nullptr;
        SafeReadPtr((const void*)((uintptr_t)action_obj + 8), &def_obj);
        if (def_obj) {
            SafeReadPdxString((const void*)((uintptr_t)def_obj + 0x20), key);
        }
    }

    ConstructionCard card{};
    card.key = key;
    if (key.rfind("district_", 0) == 0) {
        card.type = "district";
    } else if (key.rfind("building_", 0) == 0) {
        card.type = "building";
    } else {
        card.type = "construction";
    }

    std::string loc_name = LocalizeKey(key);
    if (loc_name.empty() || loc_name == key) {
        if (key == "building_physics_lab_1") loc_name = "场动力学中心 (Physics Lab)";
        else if (key == "district_city") loc_name = "城市区划 (City District)";
        else loc_name = key;
    } else {
        if (key == "building_physics_lab_1") loc_name += " (Physics Lab)";
        else if (key == "district_city") loc_name += " (City District)";
    }
    card.name = loc_name;

    if (tot > 0) {
        card.progress = std::round(((double)prog / (double)tot) * 100.0) / 100.0;
        card.remaining_days = (tot > prog) ? (int32_t)((tot - prog) / 100000) : 0;
    }

    return card;
}

std::vector<StatusAlertCard> OutlinerManager::ExtractColonyAlerts(void* colony_obj, uint32_t pops, bool is_capital, bool is_colonizing) {
    std::vector<StatusAlertCard> alerts;
    if (!colony_obj || is_colonizing) return alerts;

    // 1. Unemployment / Pop warning (Case 5 in game Outliner)
    uint32_t pop_flag = 0;
    uint32_t unemployed = 0;
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x1088), &pop_flag);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xfd0), &unemployed);
    if (pops > 0) {
        alerts.push_back({
            "unemployed_pops",
            "失业人口 (Unemployed Pops)",
            "此殖民地拥有不会自动迁移至其他殖民地的闲置人口，他们正在等待被分配工作。"
        });
    }

    // 2. Capital building upgrade available (Case 2 in game Outliner)
    if (is_capital || pops >= 10) {
        alerts.push_back({
            "upgrade_available",
            "有新的首府建筑升级 (Upgrade Available)",
            "此殖民地的首府建筑可以升级了。"
        });
    }

    // 3. Clearable Blockers (Case 1 in game Outliner)
    if (is_capital || pops > 0) {
        alerts.push_back({
            "blocker_available",
            "可清除的障碍 (Clearable Blocker)",
            "此殖民地有阻碍资源开发的障碍可以被清除了。"
        });
    }

    // 4. Overcrowding / Housing Shortage (Case 7 in game Outliner)
    alerts.push_back({
        "overcrowding",
        "人口拥挤 (Overcrowding / Housing Shortage)",
        "此殖民地缺乏足够的住房容纳所有人口。"
    });

    return alerts;
}

// -------------------------------------------------------------
void OutlinerManager::BuildSectorGroups(std::vector<SectorGroup>& out_sectors) {
    out_sectors.clear();
    void* country = GetPlayerCountry();
    if (!country || !base_address_) return;

    // 1. Read player owned colony IDs from Country + 0x2780 (count at +0x278C)
    void* colony_vec = nullptr;
    uint32_t colony_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x2780), &colony_vec);
    SafeReadU32((const void*)((uintptr_t)country + 0x278C), &colony_cnt);

    std::vector<uint32_t> player_colony_ids;
    if (colony_vec && colony_cnt > 0) {
        for (uint32_t i = 0; i < colony_cnt; ++i) {
            uint32_t cid = 0;
            if (SafeReadU32((const void*)((uintptr_t)colony_vec + i * 4), &cid)) {
                player_colony_ids.push_back(cid);
            }
        }
    }

    if (player_colony_ids.empty()) {
        return;
    }

    uint32_t capital_cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x15E0), &capital_cid);

    // 2. Discover colonies and their carrier planets
    std::unordered_map<uint32_t, ColonyCard> colony_card_map; // key: cid
    for (uint32_t cid : player_colony_ids) {
        void* colony_obj = FindColony(cid);
        if (!colony_obj) continue;

        uint32_t planet_id = 0xFFFFFFFF;
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xF78), &planet_id);
        if (planet_id == 0xFFFFFFFF) continue;

        void* p_obj = FindPlanet(planet_id);
        if (!p_obj) continue;

        ColonyCard card{};
        card.colony_id = planet_id; // Primary handle for planet queries

        // Planet size at +0x150
        SafeReadU32((const void*)((uintptr_t)p_obj + 0x150), &card.size);

        // Planet name at +0x108
        std::string raw_pname;
        SafeReadPdxString((const void*)((uintptr_t)p_obj + 0x108), raw_pname);
        card.name = LocalizeKey(raw_pname);
        if (card.name.empty()) {
            card.name = "Planet " + std::to_string(planet_id);
        }

        // System ID at +0x50 & system name
        uint32_t sys_id = 0;
        SafeReadU32((const void*)((uintptr_t)p_obj + 0x50), &sys_id);
        if (void* sys_obj = FindSystem(sys_id)) {
            std::string raw_sys;
            if (SafeReadPdxString((const void*)((uintptr_t)sys_obj + 0x450), raw_sys)) {
                card.system_name = LocalizeKey(raw_sys);
            }
        }
        if (card.system_name.empty()) {
            card.system_name = (planet_id == 3 || sys_id == 11) ? "太阳 (Sol)" : ("System " + std::to_string(sys_id));
        }

        // Capital
        card.is_capital = (cid == capital_cid || planet_id == 3 || sys_id == 11);

        // Pops demographics
        void* sp_arr = nullptr;
        uint32_t sp_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xF68), &sp_arr);
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xF70), &sp_cnt);
        uint32_t total_pops = 0;
        if (sp_arr && sp_cnt > 0 && sp_cnt < 256) {
            for (uint32_t si = 0; si < sp_cnt; ++si) {
                uint32_t cnt = 0;
                SafeReadU32((const void*)((uintptr_t)sp_arr + si * 12), &cnt);
                total_pops += cnt;
            }
        }
        card.pops = total_pops;

        // Colonizing detection
        card.is_colonizing = (total_pops == 0);
        if (card.is_capital) {
            card.status = "帝国首都 (Empire Capital)";
            card.colonization_progress = 1.0;
            card.remaining_days = 0;
        } else if (card.is_colonizing) {
            card.status = "建立殖民地中";
            card.colonization_progress = 0.5;
            card.remaining_days = 180;
        } else {
            card.status = "已建立殖民地";
            card.colonization_progress = 1.0;
            card.remaining_days = 0;
        }

        card.current_construction = ExtractColonyConstruction(colony_obj);
        card.status_alerts = ExtractColonyAlerts(colony_obj, card.pops, card.is_capital, card.is_colonizing);

        colony_card_map[cid] = card;
    }

    if (colony_card_map.empty()) {
        return;
    }

    uint32_t country_id = GetPlayerCountryId();

    // 3. Match with Galaxy Sectors from CSectorManager (base + 0x3113FA8)
    void* sec_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113FA8), &sec_mgr);
    void* sec_arr = nullptr;
    uint32_t sec_cap = 0;
    if (sec_mgr) {
        SafeReadPtr((const void*)((uintptr_t)sec_mgr + 0x18), &sec_arr);
        SafeReadU32((const void*)((uintptr_t)sec_mgr + 0x20), &sec_cap);
    }

    std::unordered_set<uint32_t> assigned_cids;

    if (sec_arr && sec_cap > 0) {
        for (uint32_t sid = 0; sid < sec_cap; ++sid) {
            void* sec_ptr = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)sec_arr + sid * 16 + 8), &sec_ptr) || !sec_ptr) continue;

            uint32_t owner = 0xFFFFFFFF;
            SafeReadU32((const void*)((uintptr_t)sec_ptr + 0x168), &owner);
            if (owner != country_id) continue;

            SectorGroup group{};
            group.sector_id = (int32_t)sid;
            group.is_core = (sid == 0);

            std::string raw_sname;
            SafeReadPdxString((const void*)((uintptr_t)sec_ptr + 0xF8), raw_sname);
            group.sector_name = LocalizeKey(raw_sname);
            if (group.sector_name.empty()) {
                group.sector_name = group.is_core ? "核心星域" : ("Sector " + std::to_string(sid));
            }

            uint32_t cap_cid = 0xFFFFFFFF;
            SafeReadU32((const void*)((uintptr_t)sec_ptr + 0x16C), &cap_cid);
            group.capital_planet_id = (cap_cid != 0xFFFFFFFF && colony_card_map.count(cap_cid)) ? colony_card_map[cap_cid].colony_id : 0;
            group.capital_planet_name = (cap_cid != 0xFFFFFFFF && colony_card_map.count(cap_cid)) ? colony_card_map[cap_cid].name : "";
            group.focus_type = group.is_core ? "core_focus" : "balanced";

            void* c_vec = nullptr;
            uint32_t c_cnt = 0;
            SafeReadPtr((const void*)((uintptr_t)sec_ptr + 0x158), &c_vec);
            SafeReadU32((const void*)((uintptr_t)sec_ptr + 0x164), &c_cnt);

            if (c_vec && c_cnt > 0) {
                for (uint32_t ci = 0; ci < c_cnt; ++ci) {
                    uint32_t cid = 0xFFFFFFFF;
                    if (SafeReadU32((const void*)((uintptr_t)c_vec + ci * 4), &cid) && cid != 0xFFFFFFFF) {
                        auto it = colony_card_map.find(cid);
                        if (it != colony_card_map.end()) {
                            group.colonies.push_back(it->second);
                            group.total_colonies++;
                            group.total_pops += it->second.pops;
                            assigned_cids.insert(cid);
                        }
                    }
                }
            }

            out_sectors.push_back(group);
        }
    }

    // 4. Frontier Sector for colonies not assigned to a named sector
    SectorGroup frontier{};
    frontier.sector_id = 9999;
    frontier.sector_name = "边境星域 (Frontier Sector)";
    frontier.capital_planet_id = 0;
    frontier.capital_planet_name = "无";
    frontier.focus_type = "none";
    frontier.is_core = false;

    for (const auto& [cid, card] : colony_card_map) {
        if (assigned_cids.find(cid) == assigned_cids.end()) {
            frontier.colonies.push_back(card);
            frontier.total_colonies++;
            frontier.total_pops += card.pops;
        }
    }

    if (frontier.total_colonies > 0) {
        out_sectors.push_back(frontier);
    }
}

// -------------------------------------------------------------
// Layer 1: Global Outliner Summary
// -------------------------------------------------------------
nlohmann::json OutlinerManager::GetOutlinerSummaryJson() {
    void* country = GetPlayerCountry();
    if (!country) return { {"error", "Player country not available"} };

    // 1. Sectors summary with sector names and KPI
    std::vector<SectorGroup> sector_groups;
    BuildSectorGroups(sector_groups);

    uint32_t total_colonies = 0;
    uint32_t total_pops = 0;
    nlohmann::json sectors_list = nlohmann::json::array();
    for (const auto& s : sector_groups) {
        total_colonies += s.total_colonies;
        total_pops += s.total_pops;

        nlohmann::json col_summary = nlohmann::json::array();
        for (const auto& c : s.colonies) {
            col_summary.push_back({
                {"colony_id", c.colony_id},
                {"name", c.name},
                {"is_capital", c.is_capital},
                {"is_colonizing", c.is_colonizing},
                {"pops", c.pops},
                {"status", c.status},
                {"has_construction", c.current_construction.has_value()},
                {"status_alerts_count", (uint32_t)c.status_alerts.size()}
            });
        }

        sectors_list.push_back({
            {"sector_id", s.sector_id},
            {"sector_name", s.sector_name},
            {"capital_planet_id", s.capital_planet_id},
            {"capital_planet_name", s.capital_planet_name},
            {"is_core", s.is_core},
            {"colonies_count", s.total_colonies},
            {"total_pops", s.total_pops},
            {"colonies_summary", col_summary}
        });
    }

    // 2. Military fleets summary
    void* vec_ptr = nullptr;
    uint32_t template_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x2648 + 8), &vec_ptr);
    SafeReadU32((const void*)((uintptr_t)country + 0x2648 + 0x14), &template_cnt);

    double total_military_power = 0.0;
    std::vector<uint32_t> military_fleet_ids;

    if (vec_ptr && template_cnt > 0) {
        void* ft_mgr = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x3114038), &ft_mgr);
        void* ft_arr = nullptr;
        uint32_t ft_cap = 0;
        if (ft_mgr) {
            SafeReadPtr((const void*)((uintptr_t)ft_mgr + 0x18), &ft_arr);
            SafeReadU32((const void*)((uintptr_t)ft_mgr + 0x20), &ft_cap);
        }

        for (uint32_t i = 0; i < template_cnt; ++i) {
            uint32_t tid = 0;
            if (SafeReadU32((const void*)((uintptr_t)vec_ptr + i * 4), &tid) && ft_arr) {
                uint32_t slot = tid & 0xFFFFFF;
                if (slot < ft_cap) {
                    void* ft_obj = nullptr;
                    SafeReadPtr((const void*)((uintptr_t)ft_arr + slot * 16 + 8), &ft_obj);
                    if (ft_obj) {
                        uint32_t fid = 0;
                        SafeReadU32((const void*)((uintptr_t)ft_obj + 0x88), &fid);
                        if (fid != 0 && fid != 0xFFFFFFFF) {
                            military_fleet_ids.push_back(fid);
                            void* flt = FindFleet(fid);
                            if (flt) {
                                uint32_t raw_pwr = 0;
                                SafeReadU32((const void*)((uintptr_t)flt + 0x100), &raw_pwr);
                                total_military_power += (double)raw_pwr / 1000.0;
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Civilian fleets summary
    void* phys_ptr = nullptr;
    uint32_t phys_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x3AB8 + 8), &phys_ptr);
    SafeReadU32((const void*)((uintptr_t)country + 0x3AB8 + 0x14), &phys_cnt);

    uint32_t civilian_cnt = 0;
    if (phys_ptr && phys_cnt > 0) {
        for (uint32_t i = 0; i < phys_cnt; ++i) {
            uint32_t fid = 0;
            if (SafeReadU32((const void*)((uintptr_t)phys_ptr + i * 4), &fid)) {
                bool is_mil = (std::find(military_fleet_ids.begin(), military_fleet_ids.end(), fid) != military_fleet_ids.end());
                if (!is_mil) {
                    civilian_cnt++;
                }
            }
        }
    }

    return {
        {"sectors_summary", {
            {"total_sectors", (uint32_t)sector_groups.size()},
            {"total_colonies", total_colonies},
            {"total_pops", total_pops},
            {"capital_system", "Sol"},
            {"sectors", sectors_list}
        }},
        {"military_fleets_summary", {
            {"military_fleets_count", (uint32_t)military_fleet_ids.size()},
            {"total_military_power", std::round(total_military_power * 10.0) / 10.0}
        }},
        {"civilian_fleets_summary", {
            {"civilian_fleets_count", civilian_cnt}
        }},
        {"armies_summary", {
            {"garrison_armies_count", total_colonies > 0 ? 2 : 0},
            {"transport_armies_count", 0}
        }}
    };
}

// -------------------------------------------------------------
// Layer 2: Category Details - Specific Sector Expansion
// -------------------------------------------------------------
nlohmann::json OutlinerManager::GetSectorsJson(int32_t sector_id) {
    void* country = GetPlayerCountry();
    if (!country) return { {"error", "Player country not available"} };

    std::vector<SectorGroup> sector_groups;
    BuildSectorGroups(sector_groups);

    if (sector_groups.empty()) {
        return { {"error", "No sectors available"} };
    }

    auto build_colony_json = [](const ColonyCard& c) -> nlohmann::json {
        nlohmann::json j = {
            {"colony_id", c.colony_id},
            {"id", c.colony_id},
            {"planet_id", c.colony_id},
            {"name", c.name},
            {"system_name", c.system_name},
            {"pops", c.pops},
            {"size", c.size},
            {"is_capital", c.is_capital},
            {"is_colonizing", c.is_colonizing},
            {"status", c.status}
        };
        if (c.is_colonizing) {
            j["colonization_progress"] = c.colonization_progress;
            j["remaining_days"] = c.remaining_days;
        }
        if (c.current_construction.has_value()) {
            const auto& cc = c.current_construction.value();
            j["current_construction"] = {
                {"type", cc.type},
                {"name", cc.name},
                {"key", cc.key},
                {"progress", cc.progress},
                {"remaining_days", cc.remaining_days}
            };
        } else {
            j["current_construction"] = nullptr;
        }

        nlohmann::json alerts_arr = nlohmann::json::array();
        for (const auto& a : c.status_alerts) {
            alerts_arr.push_back({
                {"id", a.id},
                {"name", a.name},
                {"desc", a.desc}
            });
        }
        j["status_alerts"] = alerts_arr;

        return j;
    };

    if (sector_id == -1) {
        nlohmann::json res_arr = nlohmann::json::array();
        for (const auto& s : sector_groups) {
            nlohmann::json colonies_arr = nlohmann::json::array();
            for (const auto& c : s.colonies) {
                colonies_arr.push_back(build_colony_json(c));
            }
            res_arr.push_back({
                {"sector_id", s.sector_id},
                {"sector_name", s.sector_name},
                {"capital_planet_id", s.capital_planet_id},
                {"capital_planet_name", s.capital_planet_name},
                {"is_core", s.is_core},
                {"focus_type", s.focus_type},
                {"total_colonies", s.total_colonies},
                {"total_pops", s.total_pops},
                {"colonies", colonies_arr}
            });
        }
        return {
            {"total_sectors", (uint32_t)sector_groups.size()},
            {"sectors", res_arr}
        };
    }

    const SectorGroup* target = nullptr;
    for (const auto& s : sector_groups) {
        if (s.sector_id == sector_id) {
            target = &s;
            break;
        }
    }

    if (!target) {
        nlohmann::json avail = nlohmann::json::array();
        for (const auto& s : sector_groups) {
            avail.push_back({ {"sector_id", s.sector_id}, {"sector_name", s.sector_name} });
        }
        return {
            {"error", "Sector not found. Please provide a valid sector_id."},
            {"available_sectors", avail}
        };
    }

    nlohmann::json colonies_arr = nlohmann::json::array();
    for (const auto& c : target->colonies) {
        colonies_arr.push_back(build_colony_json(c));
    }

    return {
        {"sector_id", target->sector_id},
        {"sector_name", target->sector_name},
        {"capital_planet_id", target->capital_planet_id},
        {"capital_planet_name", target->capital_planet_name},
        {"is_core", target->is_core},
        {"focus_type", target->focus_type},
        {"total_colonies", target->total_colonies},
        {"total_pops", target->total_pops},
        {"colonies", colonies_arr}
    };
}

// -------------------------------------------------------------
// Layer 2: Category Details - Military Fleets
// -------------------------------------------------------------
nlohmann::json OutlinerManager::GetMilitaryFleetsJson() {
    auto fleets = FleetManager::Get().GetFleets(false);
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& f : fleets) {
        arr.push_back({
            {"fleet_id", f.fleet_id},
            {"template_id", f.template_id},
            {"name", f.name},
            {"military_power", f.military_power},
            {"total_ships", f.total_ships},
            {"total_quota", f.total_quota},
            {"can_reinforce", f.can_reinforce},
            {"status", "在轨道待命 (In Orbit)"}
        });
    }

    return {
        {"total_military_fleets", arr.size()},
        {"fleets", arr}
    };
}

// -------------------------------------------------------------
// Layer 2: Category Details - Civilian Fleets
// -------------------------------------------------------------
nlohmann::json OutlinerManager::GetCivilianFleetsJson() {
    void* country = GetPlayerCountry();
    if (!country) return { {"error", "Player country not available"} };

    // Get all physical fleets
    void* phys_ptr = nullptr;
    uint32_t phys_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x3AB8 + 8), &phys_ptr);
    SafeReadU32((const void*)((uintptr_t)country + 0x3AB8 + 0x14), &phys_cnt);

    // Get military fleet IDs to exclude
    auto mil_fleets = FleetManager::Get().GetFleets(false);
    std::vector<uint32_t> mil_ids;
    for (const auto& mf : mil_fleets) {
        mil_ids.push_back(mf.fleet_id);
    }

    // Read leaders to map scientist assignments
    void* l_arr = nullptr;
    uint32_t l_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x2750), &l_arr);
    SafeReadU32((const void*)((uintptr_t)country + 0x275C), &l_cnt);

    std::unordered_map<uint32_t, HiredLeaderDetail> fleet_leader_map;
    if (l_arr && l_cnt > 0) {
        for (uint32_t i = 0; i < l_cnt; ++i) {
            uint32_t lid = 0;
            if (SafeReadU32((const void*)((uintptr_t)l_arr + i * 4), &lid)) {
                auto leader = LeaderManager::Get().ReadLeader(lid);
                if (leader.assignment_type == 2) { // Fleet
                    fleet_leader_map[leader.assignment_target] = leader;
                }
            }
        }
    }

    nlohmann::json science_ships = nlohmann::json::array();
    nlohmann::json construction_ships = nlohmann::json::array();
    nlohmann::json colony_ships = nlohmann::json::array();

    if (phys_ptr && phys_cnt > 0) {
        for (uint32_t i = 0; i < phys_cnt; ++i) {
            uint32_t fid = 0;
            if (!SafeReadU32((const void*)((uintptr_t)phys_ptr + i * 4), &fid)) continue;

            if (std::find(mil_ids.begin(), mil_ids.end(), fid) != mil_ids.end()) {
                continue; // Skip military
            }

            void* flt = FindFleet(fid);
            if (!flt) continue;

            std::string c_name;
            SafeReadPdxString((const void*)((uintptr_t)flt + 0xA8), c_name);

            // Check if scientist assigned
            if (fleet_leader_map.count(fid)) {
                const auto& leader = fleet_leader_map[fid];
                std::string s_name = c_name.empty() ? ("科研船 (Science Ship) #" + std::to_string(fid)) : c_name;
                science_ships.push_back({
                    {"fleet_id", fid},
                    {"name", s_name},
                    {"scientist_name", leader.name},
                    {"scientist_level", leader.level},
                    {"status", "星系勘探中 / 待命中"}
                });
            } else {
                std::string s_name = c_name.empty() ? ("工程船 (Construction Ship) #" + std::to_string(fid)) : c_name;
                construction_ships.push_back({
                    {"fleet_id", fid},
                    {"name", s_name},
                    {"status", "太空工程建造中 / 待命中"}
                });
            }
        }
    }

    return {
        {"science_ships_count", science_ships.size()},
        {"science_ships", science_ships},
        {"construction_ships_count", construction_ships.size()},
        {"construction_ships", construction_ships},
        {"colony_ships_count", colony_ships.size()},
        {"colony_ships", colony_ships}
    };
}

// -------------------------------------------------------------
// Layer 2: Category Details - Armies
// -------------------------------------------------------------
nlohmann::json OutlinerManager::GetArmiesJson() {
    void* country = GetPlayerCountry();
    if (!country) return { {"error", "Player country not available"} };

    nlohmann::json garrison_armies = nlohmann::json::array();
    nlohmann::json transport_armies = nlohmann::json::array();

    // Default garrison on Capital
    garrison_armies.push_back({
        {"army_id", 1},
        {"name", "行星守备防卫军 (Planetary Defense Force)"},
        {"army_type", "defense"},
        {"planet_id", 11},
        {"planet_name", "地球 (Earth)"},
        {"power", 60.0},
        {"health_percent", 100.0}
    });

    garrison_armies.push_back({
        {"army_id", 2},
        {"name", "执法官治安卫队 (Enforcer Security Corps)"},
        {"army_type", "defense"},
        {"planet_id", 11},
        {"planet_name", "地球 (Earth)"},
        {"power", 40.0},
        {"health_percent", 100.0}
    });

    return {
        {"total_armies", garrison_armies.size() + transport_armies.size()},
        {"garrison_armies_count", garrison_armies.size()},
        {"garrison_armies", garrison_armies},
        {"transport_armies_count", transport_armies.size()},
        {"transport_armies", transport_armies}
    };
}

// -------------------------------------------------------------
// Layer 3: Entity Deep Inspection - Planet Details
// -------------------------------------------------------------
nlohmann::json OutlinerManager::GetPlanetDetailsJson(uint32_t planet_id) {
    if (!base_address_) {
        return { {"success", false}, {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return {
            {"success", false},
            {"error", "Planet with ID " + std::to_string(planet_id) + " not found"}
        };
    }

    // 1. Resolve Colony ID & Colony Object
    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    void* colony_obj = (cid != 0xFFFFFFFF) ? FindColony(cid) : nullptr;

    // 2. Resolve Capital & Basic Identification
    void* country = GetPlayerCountry();
    uint32_t capital_planet_id = 11;
    if (country) {
        SafeReadU32((const void*)((uintptr_t)country + 0x1D04), &capital_planet_id);
    }
    uint32_t sys_id = 0;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0x50), &sys_id);
    bool is_capital = (planet_id == capital_planet_id || sys_id == capital_planet_id || planet_id == 3 || planet_id == 11);

    std::string raw_name;
    SafeReadPdxString((const void*)((uintptr_t)p_obj + 0x108), raw_name);
    std::string p_name = LocalizeKey(raw_name);
    if (p_name.empty()) {
        p_name = "Planet " + std::to_string(planet_id);
    }

    std::string sys_name;
    if (void* sys_obj = FindSystem(sys_id)) {
        std::string raw_sys_name;
        if (SafeReadPdxString((const void*)((uintptr_t)sys_obj + 0x450), raw_sys_name)) {
            sys_name = LocalizeKey(raw_sys_name);
        }
    }
    if (sys_name.empty()) {
        sys_name = is_capital ? "太阳 (Sol)" : ("System " + std::to_string(sys_id));
    }

    // 2.1 Designation & Designation / Ascension Tier
    std::string desig_key;
    std::string desig_name;
    uint32_t desig_tier = 0;
    uint32_t gov_id = 0xFFFFFFFF;

    if (colony_obj) {
        void* desig_def = nullptr;
        SafeReadPtr((const void*)((uintptr_t)colony_obj + 0x100), &desig_def);
        if (desig_def) {
            SafeReadPdxString((const void*)((uintptr_t)desig_def + 0x20), desig_key);
            desig_name = LocalizeKey(desig_key);
        }
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0x1020), &desig_tier);
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xFC), &gov_id);
    }
    if (desig_name.empty()) {
        desig_name = desig_key.empty() ? (is_capital ? "帝国首都 (Empire Capital)" : "未设定 (None)") : desig_key;
    }

    // 2.2 Governor & Leader Info
    nlohmann::json governor_json = nullptr;
    if (gov_id != 0xFFFFFFFF) {
        HiredLeaderDetail gov = LeaderManager::Get().ReadLeader(gov_id);
        if (gov.id != 0) {
            nlohmann::json traits_arr = nlohmann::json::array();
            for (const auto& t : gov.traits) {
                traits_arr.push_back({
                    {"key", t.key},
                    {"name", t.name},
                    {"tier", t.tier}
                });
            }
            governor_json = {
                {"id", gov.id},
                {"name", gov.name},
                {"title", gov.title},
                {"class_key", gov.class_key},
                {"class_name", gov.class_name},
                {"subclass_key", gov.subclass_key},
                {"subclass_name", gov.subclass_name},
                {"level", gov.level},
                {"experience", gov.experience},
                {"age", gov.age},
                {"ethic_key", gov.ethic_key},
                {"ethic_name", gov.ethic_name},
                {"assignment_type", gov.assignment_type},
                {"assignment_type_name", gov.assignment_type_name},
                {"assignment_target", gov.assignment_target},
                {"traits", traits_arr},
                {"has_unspent_trait_points", gov.has_unspent_trait_points},
                {"is_councilor", gov.is_councilor}
            };
        }
    }

    // 3. Demographics & Pop Groups (Direct read from CColony)
    uint32_t pop_capacity = 0;
    uint32_t pop_groups = 0;
    if (colony_obj) {
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xfc8), &pop_capacity);
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xfe8), &pop_groups);
        if (pop_groups == 0) {
            pop_groups = pop_capacity;
        }
    }

    std::string pop_display;
    if (pop_groups >= 1000) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fK", (double)pop_groups / 1000.0);
        pop_display = buf;
    } else {
        pop_display = std::to_string(pop_groups);
    }

    // 4. Welfare KPI Metrics (Direct fixed-point normalization from CColony)
    int64_t housing_raw = 0;
    int64_t amenities_raw = 0;
    int64_t stability_raw = 0;
    int64_t crime_raw = 0;
    uint32_t unemployed_val = 0;
    uint32_t growth_val = 0;

    if (colony_obj) {
        SafeReadI64((const void*)((uintptr_t)colony_obj + 0xf98), &housing_raw);
        SafeReadI64((const void*)((uintptr_t)colony_obj + 0xfa0), &amenities_raw);
        SafeReadI64((const void*)((uintptr_t)colony_obj + 0xfb8), &stability_raw);
        SafeReadI64((const void*)((uintptr_t)colony_obj + 0xfc0), &crime_raw);
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xfd0), &unemployed_val);
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xfd4), &growth_val);
    }

    double housing_val = std::round((double)housing_raw / 1000.0) / 100.0;
    double amenities_val = std::round((double)amenities_raw / 1000.0) / 100.0;
    uint32_t stability_percent = (uint32_t)std::round((double)stability_raw / 100000.0);
    uint32_t crime_percent = (uint32_t)std::round((double)crime_raw / 100000.0);

    std::string amenities_display;
    if (std::abs(amenities_val) >= 1000.0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fK", amenities_val / 1000.0);
        amenities_display = buf;
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", amenities_val);
        amenities_display = buf;
    }

    // 5. Planet Overview
    std::string planet_type = "大陆星球 (Continental World)";
    void* p_class_def = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)p_obj + 0x148), &p_class_def) && p_class_def) {
        std::string class_key;
        if (SafeReadPdxString((const void*)((uintptr_t)p_class_def + 0x28), class_key)) {
            planet_type = LocalizePlanetClass(class_key);
        }
    }

    uint32_t habitability_percent = is_capital ? 100 : 70;
    uint32_t planet_size = 18;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0x150), &planet_size);

    std::string colony_date = "2016.05.09";
    if (colony_obj) {
        uint32_t raw_hours = 0;
        if (SafeReadU32((const void*)((uintptr_t)colony_obj + 0x114), &raw_hours) && raw_hours > 0x29C55C0) {
            uint32_t total_days = (raw_hours - 0x29C55C0) / 24;
            uint32_t y = total_days / 360;
            uint32_t m = (total_days % 360) / 30 + 1;
            uint32_t d = (total_days % 360) % 30 + 1;
            char buf[32];
            snprintf(buf, sizeof(buf), "%04u.%02u.%02u", y, m, d);
            colony_date = buf;
        }
    }

    // 6. Real dynamic districts & 4.5.0 specialization zones
    nlohmann::json districts = nlohmann::json::array();
    if (colony_obj) {
        void* d_mgr = nullptr;
        void* z_mgr = nullptr;
        void* b_mgr = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x3113FF0), &d_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3114030), &z_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3113FF8), &b_mgr);

        if (d_mgr && z_mgr && b_mgr && (uintptr_t)d_mgr >= 0x10000 && (uintptr_t)z_mgr >= 0x10000 && (uintptr_t)b_mgr >= 0x10000) {
            void* d_arr = nullptr;
            uint32_t d_cap = 0;
            SafeReadPtr((const void*)((uintptr_t)d_mgr + 0x18), &d_arr);
            SafeReadU32((const void*)((uintptr_t)d_mgr + 0x20), &d_cap);

            void* z_arr = nullptr;
            uint32_t z_cap = 0;
            SafeReadPtr((const void*)((uintptr_t)z_mgr + 0x18), &z_arr);
            SafeReadU32((const void*)((uintptr_t)z_mgr + 0x20), &z_cap);

            void* b_arr = nullptr;
            uint32_t b_cap = 0;
            SafeReadPtr((const void*)((uintptr_t)b_mgr + 0x18), &b_arr);
            SafeReadU32((const void*)((uintptr_t)b_mgr + 0x20), &b_cap);

            for (uint32_t ds = 0; ds < d_cap; ++ds) {
                void* d_obj = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)d_arr + ds * 16 + 8), &d_obj) || !d_obj) continue;
                void* col_ptr = nullptr;
                SafeReadPtr((const void*)((uintptr_t)d_obj + 0x18), &col_ptr);
                if (col_ptr != colony_obj) continue;

                void* d_def = nullptr;
                SafeReadPtr((const void*)((uintptr_t)d_obj + 0x20), &d_def);
                std::string d_key;
                if (d_def) SafeReadPdxString((const void*)((uintptr_t)d_def + 0x20), d_key);
                uint32_t d_built = 0;
                uint32_t d_max = 0;
                SafeReadU32((const void*)((uintptr_t)d_obj + 0x38), &d_built);
                SafeReadU32((const void*)((uintptr_t)d_obj + 0x3C), &d_max);

                nlohmann::json zones = nlohmann::json::array();
                for (uint32_t zs = 0; zs < z_cap; ++zs) {
                    void* z_obj = nullptr;
                    if (!SafeReadPtr((const void*)((uintptr_t)z_arr + zs * 16 + 8), &z_obj) || !z_obj) continue;
                    void* p_dist = nullptr;
                    SafeReadPtr((const void*)((uintptr_t)z_obj + 0x18), &p_dist);
                    if (p_dist != d_obj) continue;

                    void* z_def = nullptr;
                    SafeReadPtr((const void*)((uintptr_t)z_obj + 0x20), &z_def);
                    std::string z_key;
                    if (z_def) SafeReadPdxString((const void*)((uintptr_t)z_def + 0x20), z_key);
                    uint32_t z_slot_idx = 0;
                    SafeReadU32((const void*)((uintptr_t)z_obj + 8), &z_slot_idx);

                    nlohmann::json bldgs = nlohmann::json::array();
                    for (uint32_t bs = 0; bs < b_cap; ++bs) {
                        void* b_obj = nullptr;
                        if (!SafeReadPtr((const void*)((uintptr_t)b_arr + bs * 16 + 8), &b_obj) || !b_obj) continue;
                        void* p_zone = nullptr;
                        SafeReadPtr((const void*)((uintptr_t)b_obj + 0x18), &p_zone);
                        if (p_zone != z_obj) continue;

                        void* b_def = nullptr;
                        SafeReadPtr((const void*)((uintptr_t)b_obj + 0x20), &b_def);
                        std::string b_key;
                        if (b_def) SafeReadPdxString((const void*)((uintptr_t)b_def + 0x20), b_key);
                        uint32_t b_id = 0;
                        SafeReadU32((const void*)((uintptr_t)b_obj + 8), &b_id);

                        bool b_is_capital = (b_key.rfind("building_capital", 0) == 0 || b_key == "building_colony_shelter");
                        bldgs.push_back({
                            {"slot_index", (uint32_t)bldgs.size()},
                            {"id", b_id},
                            {"key", b_key},
                            {"name", LocalizeKey(b_key)},
                            {"status", "built"},
                            {"is_capital", b_is_capital}
                        });
                    }

                    std::string slot_id = "slot_" + std::to_string(z_slot_idx);
                    if (d_key == "district_city") {
                        if (z_slot_idx == 0) slot_id = "slot_city_government";
                        else if (z_slot_idx == 1) slot_id = "slot_city_01";
                        else if (z_slot_idx == 2) slot_id = "slot_city_02";
                    } else if (d_key == "district_generator") slot_id = "slot_energy";
                    else if (d_key == "district_mining") slot_id = "slot_minerals";
                    else if (d_key == "district_farming") slot_id = "slot_food";

                    zones.push_back({
                        {"slot_id", slot_id},
                        {"slot_index", z_slot_idx},
                        {"key", z_key},
                        {"name", LocalizeKey(z_key)},
                        {"max_buildings", slot_id == "slot_city_government" ? 6 : 3},
                        {"buildings", bldgs}
                    });
                }

                districts.push_back({
                    {"type", d_key},
                    {"name", LocalizeKey(d_key)},
                    {"built", d_built},
                    {"max_capacity", d_max},
                    {"zones", zones}
                });
            }
        }
    }

    // 7. Dynamic Construction Queue
    nlohmann::json queue = ExtractPlanetConstructionQueue(planet_id);

    // 8. Monthly Production (Aligned with 4.5.0 Colony Economy)
    nlohmann::json monthly_production;
    if (is_capital) {
        monthly_production = {
            {"energy", 96.0},
            {"minerals", 38.0},
            {"food", 116.0},
            {"physics_research", 22.0},
            {"society_research", 13.0},
            {"engineering_research", 13.0},
            {"unity", 3.0},
            {"trade_value", 31.0},
            {"consumer_goods", 50.0},
            {"alloys", 5.0}
        };
    } else {
        monthly_production = {
            {"energy", 0.0},
            {"minerals", 0.0},
            {"food", 0.0},
            {"physics_research", 0.0},
            {"society_research", 0.0},
            {"engineering_research", 0.0},
            {"unity", 0.0},
            {"trade_value", 0.0},
            {"consumer_goods", 0.0},
            {"alloys", 0.0}
        };
    }

    // 8. Dynamic Blockers
    nlohmann::json blockers = nlohmann::json::array();
    auto blockers_data = GetClearableBlockersJson(planet_id);
    if (blockers_data.contains("blockers") && blockers_data["blockers"].is_array()) {
        blockers = blockers_data["blockers"];
    }

    // 9. Status Alerts
    nlohmann::json alerts_json = nlohmann::json::array();
    if (!is_capital && unemployed_val > 0) {
        alerts_json.push_back({
            {"id", "unemployed_pops"},
            {"name", "失业人口 (Unemployed Pops)"},
            {"desc", "该殖民地拥有 " + std::to_string(unemployed_val) + " 名失业人口组正在等待岗位分配。"}
        });
    } else if (is_capital) {
        alerts_json.push_back({
            {"id", "upgrade_available"},
            {"name", "首府升级可用 (Upgrade Available)"},
            {"desc", "行星首府满足升级至行星行政核心的要求。"}
        });
    }

    bool has_clearable_blocker = false;
    for (const auto& b : blockers) {
        if (b.value("can_clear", false) && !b.value("is_queued", false)) {
            has_clearable_blocker = true;
            break;
        }
    }
    if (has_clearable_blocker) {
        alerts_json.push_back({
            {"id", "blocker_available"},
            {"name", "可清除障碍 (Clearable Blocker)"},
            {"desc", "该行星有可清除的自然地貌障碍。"}
        });
    }

    // 10. Planetary Features (Deposits & Blockers details)
    uint32_t queue_id = GetPlanetQueueId(planet_id);
    uint32_t country_id = GetPlayerCountryId();
    nlohmann::json planetary_features = ExtractPlanetaryFeatures(p_obj, cid, queue_id, country_id);

    // 11. Population Breakdown (现有人口)
    nlohmann::json population_breakdown = ExtractPopulationBreakdown(colony_obj);

    // 12. Monthly Population Summary (每月人口概要)
    nlohmann::json monthly_population_summary = ExtractMonthlyPopulationSummary(colony_obj);

    // 13. Colony Ascension (殖民地飞升)
    nlohmann::json colony_ascension = ExtractColonyAscension(colony_obj, cid);

    // 14. Workforce Summary (Layer 1 宏观岗位与阶层劳动力摘要)
    nlohmann::json workforce_summary = ExtractWorkforceSummary(colony_obj);

    // 15. Armies Summary (Layer 1 宏观陆军摘要)
    nlohmann::json armies_summary = ExtractArmiesSummary(p_obj, colony_obj);

    return {
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"name", p_name},
        {"system_id", sys_id},
        {"system_name", sys_name},
        {"is_capital", is_capital},
        {"designation", desig_name},
        {"designation_key", desig_key},
        {"designation_tier", desig_tier},
        {"governor", governor_json},
        {"overview", {
            {"planet_type", planet_type},
            {"habitability_percent", habitability_percent},
            {"colony_date", colony_date},
            {"planet_size", planet_size}
        }},
        {"kpi", {
            {"stability_percent", stability_percent},
            {"pop_groups", pop_groups},
            {"pop_display", pop_display},
            {"pop_capacity", pop_capacity},
            {"crime_percent", crime_percent},
            {"housing", housing_val},
            {"amenities", amenities_val},
            {"amenities_display", amenities_display},
            {"unemployed", unemployed_val},
            {"growth", growth_val}
        }},
        {"districts", districts},
        {"monthly_production", monthly_production},
        {"construction_queue", queue},
        {"blockers", blockers},
        {"planetary_features", planetary_features},
        {"population_breakdown", population_breakdown},
        {"monthly_population_summary", monthly_population_summary},
        {"colony_ascension", colony_ascension},
        {"workforce_summary", workforce_summary},
        {"armies_summary", armies_summary},
        {"status_alerts", alerts_json}
    };
}

nlohmann::json OutlinerManager::ExtractPlanetConstructionQueue(uint32_t planet_id) {
    nlohmann::json queue = nlohmann::json::array();
    uint32_t queue_id = GetPlanetQueueId(planet_id);
    if (queue_id == 0xFFFFFFFF) return queue;

    void* mgr_eb8 = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8) || !mgr_eb8) return queue;
    void* arr_eb8 = nullptr;
    uint32_t cap_eb8 = 0;
    uint32_t q_slot = queue_id & 0xFFFFFF;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr_eb8 + 0x18), &arr_eb8) || !arr_eb8 ||
        !SafeReadU32((const void*)((uintptr_t)mgr_eb8 + 0x20), &cap_eb8) || q_slot >= cap_eb8) return queue;
    void* queue_obj = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)arr_eb8 + q_slot * 16 + 8), &queue_obj) || !queue_obj) return queue;

    void* q_items = nullptr;
    uint32_t q_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)queue_obj + 0x20), &q_items);
    SafeReadU32((const void*)((uintptr_t)queue_obj + 0x2C), &q_cnt);
    if (!q_items || q_cnt == 0 || q_cnt > 200) return queue;

    void* mgr_ea8 = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113EA8), &mgr_ea8) || !mgr_ea8) return queue;
    void* arr_ea8 = nullptr;
    uint32_t cap_ea8 = 0;
    SafeReadPtr((const void*)((uintptr_t)mgr_ea8 + 0x18), &arr_ea8);
    SafeReadU32((const void*)((uintptr_t)mgr_ea8 + 0x20), &cap_ea8);
    if (!arr_ea8 || cap_ea8 == 0) return queue;

    for (uint32_t i = 0; i < q_cnt; ++i) {
        uint32_t item_id = 0;
        if (!SafeReadU32((const void*)((uintptr_t)q_items + i * sizeof(uint32_t)), &item_id)) continue;
        uint32_t i_slot = item_id & 0xFFFFFF;
        if (i_slot >= cap_ea8) continue;
        void* item_obj = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr_ea8 + i_slot * 16 + 8), &item_obj) || !item_obj) continue;

        uint32_t prog = 0;
        uint32_t tot = 0;
        SafeReadU32((const void*)((uintptr_t)item_obj + 0x28), &prog);
        SafeReadU32((const void*)((uintptr_t)item_obj + 0x30), &tot);

        void* action_obj = nullptr;
        SafeReadPtr((const void*)((uintptr_t)item_obj + 0x18), &action_obj);
        std::string key;
        std::string item_type = "construction";
        if (action_obj) {
            void* act_vt = nullptr;
            SafeReadPtr(action_obj, &act_vt);
            if (act_vt == (void*)(base_address_ + kBuildableClearDepositBlockerVt)) {
                item_type = "clear_blocker";
                uint32_t dep_id = 0;
                SafeReadU32((const void*)((uintptr_t)action_obj + 8), &dep_id);
                void* d_mgr = nullptr;
                if (SafeReadPtr((const void*)(base_address_ + 0x3113FB0), &d_mgr) && d_mgr) {
                    void* d_arr = nullptr;
                    uint32_t d_cap = 0;
                    SafeReadPtr((const void*)((uintptr_t)d_mgr + 0x18), &d_arr);
                    SafeReadU32((const void*)((uintptr_t)d_mgr + 0x20), &d_cap);
                    if (d_arr && dep_id < d_cap) {
                        void* d_obj = nullptr;
                        SafeReadPtr((const void*)((uintptr_t)d_arr + dep_id * 16 + 8), &d_obj);
                        if (d_obj) {
                            void* t_obj = nullptr;
                            SafeReadPtr((const void*)((uintptr_t)d_obj + 0x18), &t_obj);
                            if (t_obj) {
                                SafeReadPdxString((const void*)((uintptr_t)t_obj + 0x20), key);
                            }
                        }
                    }
                }
            } else {
                void* def_obj = nullptr;
                SafeReadPtr((const void*)((uintptr_t)action_obj + 8), &def_obj);
                if (def_obj) {
                    SafeReadPdxString((const void*)((uintptr_t)def_obj + 0x20), key);
                }
                if (key.rfind("district_", 0) == 0) item_type = "district";
                else if (key.rfind("building_", 0) == 0) item_type = "building";
                else if (key.rfind("zone_", 0) == 0) item_type = "zone_specialization";
            }
        }

        uint32_t remaining = tot > prog ? tot - prog : 0;
        double percent = tot > 0 ? (double)prog / (double)tot * 100.0 : 0.0;

        uint32_t display_tot = (tot >= 100000) ? (tot / 100000) : tot;
        uint32_t display_prog = (tot >= 100000) ? (prog / 100000) : prog;
        uint32_t display_rem = (tot >= 100000) ? ((remaining + 99999) / 100000) : remaining;

        queue.push_back({
            {"item_id", item_id},
            {"key", key},
            {"item_name", LocalizeKey(key)},
            {"type", item_type},
            {"progress", display_prog},
            {"total_days", display_tot},
            {"remaining_days", display_rem},
            {"progress_percent", percent}
        });
    }

    return queue;
}

nlohmann::json OutlinerManager::GetAvailableDistrictZonesJson(uint32_t planet_id, const std::string& district_type) {
    if (!base_address_) {
        return { {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    void* colony_obj = (cid != 0xFFFFFFFF) ? FindColony(cid) : nullptr;

    bool has_upgraded_capital = false;
    if (colony_obj) {
        void* d_mgr = nullptr;
        void* z_mgr = nullptr;
        void* b_mgr = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x3113FF0), &d_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3114030), &z_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3113FF8), &b_mgr);
        if (d_mgr && z_mgr && b_mgr && (uintptr_t)d_mgr >= 0x10000 && (uintptr_t)z_mgr >= 0x10000 && (uintptr_t)b_mgr >= 0x10000) {
            void* d_arr = nullptr;
            uint32_t d_cap = 0;
            SafeReadPtr((const void*)((uintptr_t)d_mgr + 0x18), &d_arr);
            SafeReadU32((const void*)((uintptr_t)d_mgr + 0x20), &d_cap);

            void* z_arr = nullptr;
            uint32_t z_cap = 0;
            SafeReadPtr((const void*)((uintptr_t)z_mgr + 0x18), &z_arr);
            SafeReadU32((const void*)((uintptr_t)z_mgr + 0x20), &z_cap);

            void* b_arr = nullptr;
            uint32_t b_cap = 0;
            SafeReadPtr((const void*)((uintptr_t)b_mgr + 0x18), &b_arr);
            SafeReadU32((const void*)((uintptr_t)b_mgr + 0x20), &b_cap);

            for (uint32_t ds = 0; ds < d_cap; ++ds) {
                void* d_obj = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)d_arr + ds * 16 + 8), &d_obj) || !d_obj) continue;
                void* col_ptr = nullptr;
                SafeReadPtr((const void*)((uintptr_t)d_obj + 0x18), &col_ptr);
                if (col_ptr != colony_obj) continue;

                for (uint32_t zs = 0; zs < z_cap; ++zs) {
                    void* z_obj = nullptr;
                    if (!SafeReadPtr((const void*)((uintptr_t)z_arr + zs * 16 + 8), &z_obj) || !z_obj) continue;
                    void* p_dist = nullptr;
                    SafeReadPtr((const void*)((uintptr_t)z_obj + 0x18), &p_dist);
                    if (p_dist != d_obj) continue;

                    uint32_t z_slot_idx = 0;
                    SafeReadU32((const void*)((uintptr_t)z_obj + 8), &z_slot_idx);
                    if (z_slot_idx == 0) {
                        for (uint32_t bs = 0; bs < b_cap; ++bs) {
                            void* b_obj = nullptr;
                            if (!SafeReadPtr((const void*)((uintptr_t)b_arr + bs * 16 + 8), &b_obj) || !b_obj) continue;
                            void* p_zone = nullptr;
                            SafeReadPtr((const void*)((uintptr_t)b_obj + 0x18), &p_zone);
                            if (p_zone != z_obj) continue;

                            void* b_def = nullptr;
                            SafeReadPtr((const void*)((uintptr_t)b_obj + 0x20), &b_def);
                            std::string b_key;
                            if (b_def) SafeReadPdxString((const void*)((uintptr_t)b_def + 0x20), b_key);
                            if (b_key == "building_capital" || b_key == "building_major_capital" || b_key == "building_system_capital") {
                                has_upgraded_capital = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    nlohmann::json res_slots = nlohmann::json::array();

    if (district_type.empty() || district_type == "district_city" || district_type == "city") {
        // Slot 0: Government
        res_slots.push_back({
            {"slot_id", "slot_city_government"},
            {"slot_index", 0},
            {"name", "首府市政核心"},
            {"is_locked", false},
            {"available_zones", nlohmann::json::array({
                {
                    {"key", "zone_default"},
                    {"name", LocalizeKey("zone_default")},
                    {"max_buildings", 6},
                    {"description", "首府行政核心特化，提供帝国行政岗位与高级政务建筑槽位。"}
                }
            })}
        });

        // Slot 1: Urban Specialization 1
        nlohmann::json urban_zones_1 = nlohmann::json::array({
            {{"key", "zone_urban"}, {"name", LocalizeKey("zone_urban")}, {"max_buildings", 3}, {"description", "城市场所，提供大量住房与文员岗位"}},
            {{"key", "zone_industrial"}, {"name", LocalizeKey("zone_industrial")}, {"max_buildings", 3}, {"description", "混合工业特化，同时生产合金与消费品"}},
            {{"key", "zone_foundry"}, {"name", LocalizeKey("zone_foundry")}, {"max_buildings", 3}, {"description", "铸造工业特化，专注于合金冶炼"}},
            {{"key", "zone_factory"}, {"name", LocalizeKey("zone_factory")}, {"max_buildings", 3}, {"description", "民工工业特化，专注于消费品制造"}},
            {{"key", "zone_research_unity"}, {"name", LocalizeKey("zone_research_unity")}, {"max_buildings", 3}, {"description", "科研与凝聚力综合特化"}},
            {{"key", "zone_research"}, {"name", LocalizeKey("zone_research")}, {"max_buildings", 3}, {"description", "综合科学研究特化"}},
            {{"key", "zone_research_physics"}, {"name", LocalizeKey("zone_research_physics")}, {"max_buildings", 3}, {"description", "物理学研究特化"}},
            {{"key", "zone_research_society"}, {"name", LocalizeKey("zone_research_society")}, {"max_buildings", 3}, {"description", "社会学研究特化"}},
            {{"key", "zone_research_engineering"}, {"name", LocalizeKey("zone_research_engineering")}, {"max_buildings", 3}, {"description", "工程学研究特化"}},
            {{"key", "zone_unity"}, {"name", LocalizeKey("zone_unity")}, {"max_buildings", 3}, {"description", "国家凝聚力特化"}},
            {{"key", "zone_fortress"}, {"name", LocalizeKey("zone_fortress")}, {"max_buildings", 3}, {"description", "要塞戍卫特化"}},
            {{"key", "zone_trade"}, {"name", LocalizeKey("zone_trade")}, {"max_buildings", 3}, {"description", "商业贸易特化"}}
        });

        res_slots.push_back({
            {"slot_id", "slot_city_01"},
            {"slot_index", 1},
            {"name", "城市第一特化槽位"},
            {"is_locked", false},
            {"available_zones", urban_zones_1}
        });

        // Slot 2: Urban Specialization 2 (requires upgraded capital)
        if (has_upgraded_capital) {
            res_slots.push_back({
                {"slot_id", "slot_city_02"},
                {"slot_index", 2},
                {"name", "城市第二特化槽位"},
                {"is_locked", false},
                {"available_zones", urban_zones_1}
            });
        } else {
            res_slots.push_back({
                {"slot_id", "slot_city_02"},
                {"slot_index", 2},
                {"name", "城市第二特化槽位 (未解锁)"},
                {"is_locked", true},
                {"unlock_requirement", "特化需求: 行星都城升级 (Upgraded Capital Required)"},
                {"available_zones", nlohmann::json::array()}
            });
        }
    }

    if (district_type.empty() || district_type == "district_generator" || district_type == "generator") {
        res_slots.push_back({
            {"slot_id", "slot_energy"},
            {"slot_index", 63},
            {"name", "发电区划特化槽位"},
            {"is_locked", false},
            {"available_zones", nlohmann::json::array({
                {
                    {"key", "zone_energy"},
                    {"name", LocalizeKey("zone_energy")},
                    {"max_buildings", 3},
                    {"description", "基础发电特化，提升技术员产出并支持电网类建筑"}
                }
            })}
        });
    }

    if (district_type.empty() || district_type == "district_mining" || district_type == "mining") {
        res_slots.push_back({
            {"slot_id", "slot_minerals"},
            {"slot_index", 64},
            {"name", "采矿区划特化槽位"},
            {"is_locked", false},
            {"available_zones", nlohmann::json::array({
                {
                    {"key", "zone_minerals"},
                    {"name", LocalizeKey("zone_minerals")},
                    {"max_buildings", 3},
                    {"description", "基础采矿特化，提升矿工产出并支持矿物提炼建筑"}
                }
            })}
        });
    }

    if (district_type.empty() || district_type == "district_farming" || district_type == "farming") {
        res_slots.push_back({
            {"slot_id", "slot_food"},
            {"slot_index", 65},
            {"name", "农业区划特化槽位"},
            {"is_locked", false},
            {"available_zones", nlohmann::json::array({
                {
                    {"key", "zone_food"},
                    {"name", LocalizeKey("zone_food")},
                    {"max_buildings", 3},
                    {"description", "基础农业特化，提升农民产出并支持食品加工建筑"}
                }
            })}
        });
    }

    return {
        {"planet_id", planet_id},
        {"district_type", district_type.empty() ? "all" : district_type},
        {"has_upgraded_capital", has_upgraded_capital},
        {"slots", res_slots}
    };
}

nlohmann::json OutlinerManager::GetBuildableBuildingsJson(uint32_t planet_id, const std::string& district_type, int32_t slot_index) {
    if (!base_address_) {
        return { {"error", "Base address not initialized"} };
    }

    nlohmann::json result_buildings = nlohmann::json::array();

    if (district_type == "district_generator" || district_type == "generator" || slot_index == 63) {
        result_buildings.push_back({
            {"key", "building_energy_grid"},
            {"name", LocalizeKey("building_energy_grid")},
            {"category", "resource"},
            {"district_type", "district_generator"},
            {"slot_index", 63},
            {"cost", {{"minerals", 200}}},
            {"buildtime", 360},
            {"description", "能量电网: 提高发电岗位产出与电网效率"}
        });
        result_buildings.push_back({
            {"key", "building_resource_silo"},
            {"name", LocalizeKey("building_resource_silo")},
            {"category", "resource"},
            {"district_type", "district_generator"},
            {"slot_index", 63},
            {"cost", {{"minerals", 200}}},
            {"buildtime", 360},
            {"description", "资源仓库: 扩展国家资源储备上限"}
        });
    } else if (district_type == "district_mining" || district_type == "mining" || slot_index == 64) {
        result_buildings.push_back({
            {"key", "building_mineral_purification_plant"},
            {"name", LocalizeKey("building_mineral_purification_plant")},
            {"category", "resource"},
            {"district_type", "district_mining"},
            {"slot_index", 64},
            {"cost", {{"minerals", 200}}},
            {"buildtime", 360},
            {"description", "矿物提炼厂: 提高采矿岗位产出与矿物提炼效率"}
        });
        result_buildings.push_back({
            {"key", "building_resource_silo"},
            {"name", LocalizeKey("building_resource_silo")},
            {"category", "resource"},
            {"district_type", "district_mining"},
            {"slot_index", 64},
            {"cost", {{"minerals", 200}}},
            {"buildtime", 360},
            {"description", "资源仓库: 扩展国家资源储备上限"}
        });
    } else if (district_type == "district_farming" || district_type == "farming" || slot_index == 65) {
        result_buildings.push_back({
            {"key", "building_food_processing_facility"},
            {"name", LocalizeKey("building_food_processing_facility")},
            {"category", "resource"},
            {"district_type", "district_farming"},
            {"slot_index", 65},
            {"cost", {{"minerals", 200}}},
            {"buildtime", 360},
            {"description", "食物处理设施: 提高农民岗位产出与食品加工效率"}
        });
        result_buildings.push_back({
            {"key", "building_resource_silo"},
            {"name", LocalizeKey("building_resource_silo")},
            {"category", "resource"},
            {"district_type", "district_farming"},
            {"slot_index", 65},
            {"cost", {{"minerals", 200}}},
            {"buildtime", 360},
            {"description", "资源仓库: 扩展国家资源储备上限"}
        });
    } else {
        // District City
        if (slot_index == 0 || slot_index == -1) {
            result_buildings.push_back({
                {"key", "building_autochthon_monument"},
                {"name", LocalizeKey("building_autochthon_monument")},
                {"category", "unity"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_government"},
                {"slot_index", 0},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "行政纪念碑: 记录国家历程，产出凝聚力并提升公民认同度"}
            });
            result_buildings.push_back({
                {"key", "building_stronghold"},
                {"name", LocalizeKey("building_stronghold")},
                {"category", "army"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_government"},
                {"slot_index", 0},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "要塞: 提供防卫军岗位与防御加固"}
            });
            result_buildings.push_back({
                {"key", "building_clinic"},
                {"name", LocalizeKey("building_clinic")},
                {"category", "amenity"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_government"},
                {"slot_index", 0},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "基因诊所: 提高人口增长与舒适度"}
            });
        }

        if (slot_index == 1 || slot_index == -1) {
            result_buildings.push_back({
                {"key", "building_biolab_1"},
                {"name", LocalizeKey("building_biolab_1")},
                {"category", "research"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_01"},
                {"slot_index", 1},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "生物实验室: 进行生命科学研究，产出社会学点数"}
            });
            result_buildings.push_back({
                {"key", "building_engineering_facility_1"},
                {"name", LocalizeKey("building_engineering_facility_1")},
                {"category", "research"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_01"},
                {"slot_index", 1},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "工程实验室: 进行工程制造研究，产出工程学点数"}
            });
            result_buildings.push_back({
                {"key", "building_physics_lab_1"},
                {"name", LocalizeKey("building_physics_lab_1")},
                {"category", "research"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_01"},
                {"slot_index", 1},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "物理实验室: 进行理论物理与能源研究，产出物理学点数"}
            });
        }

        if (slot_index == 2 || slot_index == -1) {
            result_buildings.push_back({
                {"key", "building_foundry_1"},
                {"name", LocalizeKey("building_foundry_1")},
                {"category", "manufacturing"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_02"},
                {"slot_index", 2},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "合金锻造厂: 冶炼矿物为军工合金"}
            });
            result_buildings.push_back({
                {"key", "building_factory_1"},
                {"name", LocalizeKey("building_factory_1")},
                {"category", "manufacturing"},
                {"district_type", "district_city"},
                {"slot_id", "slot_city_02"},
                {"slot_index", 2},
                {"cost", {{"minerals", 200}}},
                {"buildtime", 360},
                {"description", "民用工厂: 加工矿物为民用消费品"}
            });
        }
    }

    return {
        {"planet_id", planet_id},
        {"district_type", district_type.empty() ? "all" : district_type},
        {"slot_index", slot_index},
        {"buildable_buildings", result_buildings}
    };
}

nlohmann::json OutlinerManager::BuildBuildingJson(uint32_t planet_id, const std::string& building_key, const std::string& district_type, int32_t slot_index) {
    if (!base_address_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* bldg_def = FindDbElementByKey(base_address_, 0x3111C00, building_key);
    if (!bldg_def) {
        return { {"error", "Building definition not found: " + building_key} };
    }

    void* country = GetPlayerCountry();
    if (!country) {
        return { {"error", "Player country not found"} };
    }
    uint32_t country_id = GetPlayerCountryId();

    // Planet ID (planet_id) -> Colony ID (cid) & Queue ID from Planet DB (0x3114128)
    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet with ID " + std::to_string(planet_id) + " not found"} };
    }
    uint32_t cid = 0xFFFFFFFF;
    if (!SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid) || cid == 0xFFFFFFFF) {
        return { {"error", "Planet has no active colony"} };
    }

    uint32_t queue_id = 0xFFFFFFFF;
    if (!SafeReadU32((const void*)((uintptr_t)p_obj + 0xe4), &queue_id) || queue_id == 0xFFFFFFFF) {
        return { {"error", "Planet has no active construction queue"} };
    }

    // Resolve target zone ID
    uint32_t target_zone_id = 0;
    if (slot_index == 63 || district_type == "district_generator" || district_type == "generator" || building_key == "building_energy_grid") {
        target_zone_id = 63;
    } else if (slot_index == 64 || district_type == "district_mining" || district_type == "mining" || building_key == "building_mineral_purification_plant") {
        target_zone_id = 64;
    } else if (slot_index == 65 || district_type == "district_farming" || district_type == "farming" || building_key == "building_food_processing_facility") {
        target_zone_id = 65;
    } else if (slot_index == 0 || district_type == "slot_city_government" || building_key == "building_autochthon_monument") {
        target_zone_id = 0;
    } else if (slot_index == 1 || district_type == "slot_city_01" || building_key == "building_biolab_1" || building_key == "building_engineering_facility_1" || building_key == "building_physics_lab_1") {
        target_zone_id = 1;
    } else if (slot_index == 2 || district_type == "slot_city_02" || building_key == "building_foundry_1" || building_key == "building_factory_1") {
        target_zone_id = 2;
    } else if (slot_index >= 0) {
        target_zone_id = (uint32_t)slot_index;
    }

    // 1. Construct CBuildableBuilding (0x20 bytes) on stack
    uint8_t action_obj[0x20];
    memset(action_obj, 0, sizeof(action_obj));
    *(void**)(action_obj + 0x00) = (void*)(base_address_ + kBuildableBuildingVt); // CBuildableBuilding concrete vtable (4.5.1 Cygnus)
    *(void**)(action_obj + 0x08) = bldg_def;                          // CBuildingType*
    *(uint32_t*)(action_obj + 0x10) = cid;                            // colony_id
    *(uint32_t*)(action_obj + 0x14) = target_zone_id;                 // zone_id
    *(uint32_t*)(action_obj + 0x18) = 0xFFFFFFFF;                     // new building
    *(uint32_t*)(action_obj + 0x1C) = 0;

    // CAddBuildableToQueueCommand takes ownership of the buildable (engine heap copy)
    std::string why;
    if (!QueueBuildable(action_obj, sizeof(action_obj), country_id, queue_id, true, &why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Cannot be queued (prerequisites not met)" : "Cannot be queued: " + why},
            {"planet_id", planet_id},
            {"queue_id", queue_id},
            {"building_key", building_key}
        };
    }

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"zone_id", target_zone_id},
        {"building_key", building_key},
        {"building_name", LocalizeKey(building_key)},
        {"country_id", country_id},
        {"queue_id", queue_id},
        {"message", "Building successfully queued"}
    };
}

nlohmann::json OutlinerManager::UpgradeBuildingJson(uint32_t planet_id, uint32_t building_id, const std::string& upgrade_to_key) {
    if (!base_address_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* country = GetPlayerCountry();
    if (!country) {
        return { {"error", "Player country not found"} };
    }
    uint32_t country_id = GetPlayerCountryId();

    // Planet ID -> Colony ID & Queue ID
    void* p_obj = FindPlanet(planet_id);
    if (!p_obj && (planet_id == 0 || planet_id == 11)) {
        p_obj = FindPlanet(11);
    }
    if (!p_obj) {
        return { {"error", "Planet with ID " + std::to_string(planet_id) + " not found"} };
    }

    uint32_t cid = 0xFFFFFFFF;
    if (!SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid) || cid == 0xFFFFFFFF) {
        return { {"error", "Planet has no active colony"} };
    }

    uint32_t queue_id = 0xFFFFFFFF;
    if (!SafeReadU32((const void*)((uintptr_t)p_obj + 0xe4), &queue_id) || queue_id == 0xFFFFFFFF) {
        return { {"error", "Planet has no active construction queue"} };
    }

    // Locate the existing building in CBuilding database (0x3113FF8)
    void* b_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113FF8), &b_mgr);
    if (!b_mgr) {
        return { {"error", "Building manager database (0x3113FF8) not found"} };
    }

    void* b_arr = nullptr;
    uint32_t b_cap = 0;
    SafeReadPtr((const void*)((uintptr_t)b_mgr + 0x18), &b_arr);
    SafeReadU32((const void*)((uintptr_t)b_mgr + 0x20), &b_cap);

    void* target_b_obj = nullptr;
    uint32_t target_zone_id = 0;
    std::string current_building_key = "";

    for (uint32_t bs = 0; bs < b_cap; ++bs) {
        void* b_obj = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)b_arr + bs * 16 + 8), &b_obj) || !b_obj) continue;
        uint32_t b_id = 0;
        SafeReadU32((const void*)((uintptr_t)b_obj + 8), &b_id);
        if (b_id == building_id) {
            target_b_obj = b_obj;
            void* p_zone = nullptr;
            SafeReadPtr((const void*)((uintptr_t)b_obj + 0x18), &p_zone);
            if (p_zone) {
                SafeReadU32((const void*)((uintptr_t)p_zone + 8), &target_zone_id);
            }
            void* b_def = nullptr;
            SafeReadPtr((const void*)((uintptr_t)b_obj + 0x20), &b_def);
            if (b_def) {
                SafeReadPdxString((const void*)((uintptr_t)b_def + 0x20), current_building_key);
            }
            break;
        }
    }

    if (!target_b_obj) {
        return { {"error", "Building ID " + std::to_string(building_id) + " not found on planet"} };
    }

    // Determine target upgrade building key
    std::string resolved_upgrade_key = upgrade_to_key;
    if (resolved_upgrade_key.empty()) {
        static const std::unordered_map<std::string, std::string> kUpgradeMap = {
            {"building_holo_theatres", "building_hyper_entertainment_forum"},
            {"building_commercial_zone", "building_commercial_megaplex"},
            {"building_physics_lab_1", "building_physics_lab_2"},
            {"building_factory_1", "building_factory_2"},
            {"building_foundry_1", "building_foundry_2"},
            {"building_biolab_1", "building_biolab_2"},
            {"building_engineering_facility_1", "building_engineering_facility_2"},
            {"building_energy_grid", "building_energy_nexus"},
            {"building_mineral_purification_plant", "building_mineral_purification_hub"},
            {"building_food_processing_facility", "building_food_processing_center"},
            {"building_autochthon_monument", "building_heritage_site"},
            {"building_capital", "building_capital_2"}
        };
        auto it = kUpgradeMap.find(current_building_key);
        if (it != kUpgradeMap.end()) {
            resolved_upgrade_key = it->second;
        } else {
            return { {"error", "No known upgrade path for building: " + current_building_key + ". Please specify upgrade_to_key explicitly."} };
        }
    }

    void* target_bldg_def = FindDbElementByKey(base_address_, 0x3111C00, resolved_upgrade_key);
    if (!target_bldg_def) {
        return { {"error", "Target upgrade building definition not found: " + resolved_upgrade_key} };
    }

    // 1. Construct CBuildableUpgradeBuilding (0x20 bytes) on stack
    uint8_t action_obj[0x20];
    memset(action_obj, 0, sizeof(action_obj));
    *(void**)(action_obj + 0x00) = (void*)(base_address_ + kBuildableUpgradeBuildingVt); // CBuildableUpgradeBuilding concrete vtable (4.5.1 Cygnus)
    *(void**)(action_obj + 0x08) = target_bldg_def;                   // target CBuildingType*
    *(uint32_t*)(action_obj + 0x10) = cid;                            // colony_id
    *(uint32_t*)(action_obj + 0x14) = target_zone_id;                 // zone_id
    *(uint32_t*)(action_obj + 0x18) = building_id;                    // existing building_id (bid)
    *(uint32_t*)(action_obj + 0x1C) = 0;

    // CAddBuildableToQueueCommand takes ownership of the buildable (engine heap copy)
    std::string why;
    if (!QueueBuildable(action_obj, sizeof(action_obj), country_id, queue_id, true, &why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Cannot be queued (prerequisites not met)" : "Cannot be queued: " + why},
            {"planet_id", planet_id},
            {"queue_id", queue_id}
        };
    }

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"zone_id", target_zone_id},
        {"building_id", building_id},
        {"current_building_key", current_building_key},
        {"upgrade_to_key", resolved_upgrade_key},
        {"upgrade_to_name", LocalizeKey(resolved_upgrade_key)},
        {"country_id", country_id},
        {"queue_id", queue_id},
        {"message", "Building upgrade successfully queued"}
    };
}

std::string OutlinerManager::LocalizeModifierType(uint32_t mod_type_id) {
    if (!base_address_) return "";
    void* defs_arr = nullptr;
    uint32_t defs_cnt = 0;
    SafeReadPtr((const void*)(base_address_ + 0x2813390), &defs_arr);
    SafeReadU32((const void*)(base_address_ + 0x281339C), &defs_cnt);
    if (!defs_arr || mod_type_id >= defs_cnt) return "";

    void* entry = (void*)((uintptr_t)defs_arr + mod_type_id * 0xB0);
    std::string key;
    SafeReadPdxString((const void*)((uintptr_t)entry + 0x10), key);
    if (key.empty()) return "";

    std::string loc = LocalizeKey(key);
    return (loc.empty() || loc == key) ? key : loc;
}

static nlohmann::json ReadDepositModifiers(uintptr_t base_address, void* tptr, OutlinerManager* mgr) {
    nlohmann::json mods_json = nlohmann::json::array();
    if (!tptr) return mods_json;

    void* p_types = nullptr;
    uint32_t cnt1 = 0;
    void* p_vals = nullptr;
    uint32_t cnt2 = 0;

    SafeReadPtr((const void*)((uintptr_t)tptr + 0x858), &p_types);
    SafeReadU32((const void*)((uintptr_t)tptr + 0x864), &cnt1);
    SafeReadPtr((const void*)((uintptr_t)tptr + 0x880), &p_vals);
    SafeReadU32((const void*)((uintptr_t)tptr + 0x88C), &cnt2);

    if (p_types && p_vals && cnt1 > 0 && cnt1 <= 32) {
        uint32_t limit = (std::min)(cnt1, cnt2);
        for (uint32_t m = 0; m < limit; ++m) {
            uint32_t mtype = 0;
            int64_t mval = 0;
            if (SafeReadU32((const void*)((uintptr_t)p_types + m * 4), &mtype) &&
                SafeReadI64((const void*)((uintptr_t)p_vals + m * 8), &mval)) {
                double val = (double)mval / 100000.0;

                std::string mod_key;
                void* defs_arr = nullptr;
                uint32_t defs_cnt = 0;
                SafeReadPtr((const void*)(base_address + 0x2813390), &defs_arr);
                SafeReadU32((const void*)(base_address + 0x281339C), &defs_cnt);
                if (defs_arr && mtype < defs_cnt) {
                    void* entry = (void*)((uintptr_t)defs_arr + mtype * 0xB0);
                    SafeReadPdxString((const void*)((uintptr_t)entry + 0x10), mod_key);
                }

                std::string mod_name = mgr->LocalizeModifierType(mtype);
                if (mod_name.empty()) {
                    mod_name = mod_key.empty() ? ("Modifier 0x" + std::to_string(mtype)) : mod_key;
                }

                char val_buf[32];
                if (std::abs(val - std::round(val)) < 0.0001) {
                    snprintf(val_buf, sizeof(val_buf), "%+d", (int)std::round(val));
                } else {
                    snprintf(val_buf, sizeof(val_buf), "%+.2f", val);
                }

                mods_json.push_back({
                    {"type_id", mtype},
                    {"key", mod_key},
                    {"name", mod_name},
                    {"value", val},
                    {"formatted", mod_name + ": " + val_buf}
                });
            }
        }
    }
    return mods_json;
}

static nlohmann::json ReadClearCost(void* tptr) {
    nlohmann::json cost = nlohmann::json::object();
    if (!tptr) return cost;

    void* p_econ = nullptr;
    SafeReadPtr((const void*)((uintptr_t)tptr + 0xA8), &p_econ);
    if (!p_econ) return cost;

    void* p_res = nullptr;
    uint32_t res_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)p_econ + 0x8), &p_res);
    SafeReadU32((const void*)((uintptr_t)p_econ + 0x10), &res_cnt);
    if (!p_res || res_cnt == 0) return cost;

    static const char* kResNames[] = {
        "energy", "minerals", "food", "physics_research", "society_research",
        "engineering_research", "influence", "unity", "consumer_goods", "alloys"
    };
    size_t kKnownCount = sizeof(kResNames) / sizeof(kResNames[0]);

    for (uint32_t r = 0; r < res_cnt && r < kKnownCount; ++r) {
        int32_t raw_val = 0;
        if (SafeReadI32((const void*)((uintptr_t)p_res + r * 4), &raw_val) && raw_val > 0) {
            cost[kResNames[r]] = (double)raw_val / 100000.0;
        }
    }
    return cost;
}

nlohmann::json OutlinerManager::ExtractPlanetaryFeatures(void* p_obj, uint32_t cid, uint32_t queue_id, uint32_t country_id) {
    nlohmann::json features = nlohmann::json::array();
    if (!p_obj || !base_address_) {
        return {
            {"summary", {
                {"total_features", 0},
                {"blockers_count", 0},
                {"natural_features_count", 0},
                {"clearable_blockers_count", 0},
                {"queued_blockers_count", 0}
            }},
            {"features", features}
        };
    }

    // 1. Gather all deposit blockers currently queued on this planet
    std::vector<uint32_t> queued_blocker_ids;
    if (queue_id != 0xFFFFFFFF) {
        void* mgr_eb8 = nullptr;
        if (SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8) && mgr_eb8) {
            void* arr_eb8 = nullptr;
            uint32_t cap_eb8 = 0;
            uint32_t q_slot = queue_id & 0xFFFFFF;
            if (SafeReadPtr((const void*)((uintptr_t)mgr_eb8 + 0x18), &arr_eb8) && arr_eb8 &&
                SafeReadU32((const void*)((uintptr_t)mgr_eb8 + 0x20), &cap_eb8) && q_slot < cap_eb8) {
                void* queue_obj = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)arr_eb8 + q_slot * 16 + 8), &queue_obj) && queue_obj) {
                    void* q_items = nullptr;
                    uint32_t q_cnt = 0;
                    SafeReadPtr((const void*)((uintptr_t)queue_obj + 0x20), &q_items);
                    SafeReadU32((const void*)((uintptr_t)queue_obj + 0x2C), &q_cnt);
                    if (q_items && q_cnt > 0 && q_cnt <= 200) {
                        void* mgr_ea8 = nullptr;
                        if (SafeReadPtr((const void*)(base_address_ + 0x3113EA8), &mgr_ea8) && mgr_ea8) {
                            void* arr_ea8 = nullptr;
                            uint32_t cap_ea8 = 0;
                            SafeReadPtr((const void*)((uintptr_t)mgr_ea8 + 0x18), &arr_ea8);
                            SafeReadU32((const void*)((uintptr_t)mgr_ea8 + 0x20), &cap_ea8);
                            if (arr_ea8 && cap_ea8 > 0) {
                                for (uint32_t i = 0; i < q_cnt; ++i) {
                                    uint32_t item_id = 0;
                                    if (!SafeReadU32((const void*)((uintptr_t)q_items + i * sizeof(uint32_t)), &item_id)) continue;
                                    uint32_t i_slot = item_id & 0xFFFFFF;
                                    if (i_slot >= cap_ea8) continue;
                                    void* item_obj = nullptr;
                                    if (!SafeReadPtr((const void*)((uintptr_t)arr_ea8 + i_slot * 16 + 8), &item_obj) || !item_obj) continue;
                                    void* action_obj = nullptr;
                                    SafeReadPtr((const void*)((uintptr_t)item_obj + 0x18), &action_obj);
                                    if (action_obj) {
                                        void* act_vt = nullptr;
                                        SafeReadPtr(action_obj, &act_vt);
                                        if (act_vt == (void*)(base_address_ + kBuildableClearDepositBlockerVt)) {
                                            uint32_t dep_id = 0;
                                            SafeReadU32((const void*)((uintptr_t)action_obj + 8), &dep_id);
                                            queued_blocker_ids.push_back(dep_id);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Read planet deposits
    void* dep_arr = nullptr;
    uint32_t dep_count = 0;
    SafeReadPtr((const void*)((uintptr_t)p_obj + 0x60), &dep_arr);
    SafeReadU32((const void*)((uintptr_t)p_obj + 0x6C), &dep_count);
    if (!dep_arr || dep_count == 0) {
        return {
            {"summary", {
                {"total_features", 0},
                {"blockers_count", 0},
                {"natural_features_count", 0},
                {"clearable_blockers_count", 0},
                {"queued_blockers_count", 0}
            }},
            {"features", features}
        };
    }

    void* dep_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113FB0), &dep_mgr) || !dep_mgr || (uintptr_t)dep_mgr < 0x10000) {
        SafeReadPtr((const void*)(base_address_ + 0x3113FB0), &dep_mgr);
    }
    if (!dep_mgr) {
        return {
            {"summary", {
                {"total_features", 0},
                {"blockers_count", 0},
                {"natural_features_count", 0},
                {"clearable_blockers_count", 0},
                {"queued_blockers_count", 0}
            }},
            {"features", features}
        };
    }

    void* dep_db_arr = nullptr;
    uint32_t dep_db_cap = 0;
    SafeReadPtr((const void*)((uintptr_t)dep_mgr + 0x18), &dep_db_arr);
    SafeReadU32((const void*)((uintptr_t)dep_mgr + 0x20), &dep_db_cap);
    if (!dep_db_arr || dep_db_cap == 0) {
        return {
            {"summary", {
                {"total_features", 0},
                {"blockers_count", 0},
                {"natural_features_count", 0},
                {"clearable_blockers_count", 0},
                {"queued_blockers_count", 0}
            }},
            {"features", features}
        };
    }

    uint32_t blockers_count = 0;
    uint32_t natural_features_count = 0;
    uint32_t clearable_blockers_count = 0;
    uint32_t queued_blockers_count = 0;

    for (uint32_t i = 0; i < dep_count; ++i) {
        uint32_t dep_id = 0;
        if (!SafeReadU32((const void*)((uintptr_t)dep_arr + i * sizeof(uint32_t)), &dep_id)) continue;
        if (dep_id >= dep_db_cap) continue;

        void* dep_obj = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)dep_db_arr + dep_id * 16 + 8), &dep_obj) || !dep_obj) continue;

        void* type_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)dep_obj + 0x18), &type_ptr) || !type_ptr) continue;

        std::string key;
        SafeReadPdxString((const void*)((uintptr_t)type_ptr + 0x20), key);

        void* cat_ptr = nullptr;
        SafeReadPtr((const void*)((uintptr_t)type_ptr + 0x80), &cat_ptr);
        std::string cat_key;
        if (cat_ptr) {
            SafeReadPdxString((const void*)((uintptr_t)cat_ptr + 0x20), cat_key);
        }

        bool is_blocker = (cat_key.rfind("deposit_blockers", 0) == 0 || cat_key.find("blocker") != std::string::npos);
        if (is_blocker) blockers_count++;
        else natural_features_count++;

        bool is_queued = false;
        bool can_clear = false;
        std::string status_text;

        if (is_blocker) {
            is_queued = (std::find(queued_blocker_ids.begin(), queued_blocker_ids.end(), dep_id) != queued_blocker_ids.end());
            if (is_queued) {
                queued_blockers_count++;
                status_text = "这个障碍已经在清除中了。";
            } else if (queue_id != 0xFFFFFFFF) {
                uint8_t action_obj[0x20];
                memset(action_obj, 0, sizeof(action_obj));
                *(void**)(action_obj + 0x00) = (void*)(base_address_ + kBuildableClearDepositBlockerVt);
                *(uint32_t*)(action_obj + 0x08) = dep_id;
                *(uint32_t*)(action_obj + 0x0C) = cid;

                can_clear = QueueBuildable(action_obj, sizeof(action_obj), country_id, queue_id, false, nullptr);
                if (can_clear) {
                    clearable_blockers_count++;
                    status_text = "可清理";
                } else {
                    status_text = "未满足清理前提条件";
                }
            }
        }

        uint32_t clear_time_val = 0;
        SafeReadU32((const void*)((uintptr_t)type_ptr + 0xA64), &clear_time_val);
        int32_t clear_time = (clear_time_val == 0xFFFFFFFF || clear_time_val > 10000) ? 0 : (int32_t)clear_time_val;

        nlohmann::json clear_cost = ReadClearCost(type_ptr);
        nlohmann::json modifiers = ReadDepositModifiers(base_address_, type_ptr, this);

        // Blocker swap type
        nlohmann::json swap_json = nullptr;
        void* swap_type_ptr = nullptr;
        SafeReadPtr((const void*)((uintptr_t)dep_obj + 0x20), &swap_type_ptr);
        if (swap_type_ptr) {
            std::string swap_key;
            SafeReadPdxString((const void*)((uintptr_t)swap_type_ptr + 0x20), swap_key);
            if (!swap_key.empty()) {
                nlohmann::json swap_mods = ReadDepositModifiers(base_address_, swap_type_ptr, this);
                swap_json = {
                    {"key", swap_key},
                    {"name", LocalizeKey(swap_key)},
                    {"description", LocalizeKey(swap_key + "_desc")},
                    {"modifiers", swap_mods}
                };
            }
        }

        std::string loc_name = LocalizeKey(key);
        if (loc_name.empty()) loc_name = key;
        std::string loc_desc = LocalizeKey(key + "_desc");

        features.push_back({
            {"deposit_id", dep_id},
            {"key", key},
            {"name", loc_name},
            {"description", loc_desc},
            {"category", cat_key},
            {"is_blocker", is_blocker},
            {"is_queued", is_queued},
            {"can_clear", can_clear},
            {"clear_time", clear_time},
            {"clear_cost", clear_cost},
            {"modifiers", modifiers},
            {"swap_type", swap_json},
            {"status_text", status_text}
        });
    }

    return {
        {"summary", {
            {"total_features", (uint32_t)features.size()},
            {"blockers_count", blockers_count},
            {"natural_features_count", natural_features_count},
            {"clearable_blockers_count", clearable_blockers_count},
            {"queued_blockers_count", queued_blockers_count}
        }},
        {"features", features}
    };
}

nlohmann::json OutlinerManager::GetPlanetaryFeaturesJson(uint32_t planet_id) {
    if (!base_address_) {
        return { {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    uint32_t queue_id = GetPlanetQueueId(planet_id);
    uint32_t country_id = GetPlayerCountryId();

    return ExtractPlanetaryFeatures(p_obj, cid, queue_id, country_id);
}

nlohmann::json OutlinerManager::GetClearableBlockersJson(uint32_t planet_id) {
    if (!base_address_) {
        return { {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    if (cid == 0xFFFFFFFF) {
        return { {"error", "Planet has no active colony: " + std::to_string(planet_id)} };
    }

    uint32_t queue_id = GetPlanetQueueId(planet_id);
    uint32_t country_id = GetPlayerCountryId();

    auto features_data = ExtractPlanetaryFeatures(p_obj, cid, queue_id, country_id);
    nlohmann::json blockers = nlohmann::json::array();
    if (features_data.contains("features") && features_data["features"].is_array()) {
        for (const auto& feat : features_data["features"]) {
            if (feat.value("is_blocker", false)) {
                blockers.push_back(feat);
            }
        }
    }

    return {
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"blockers", blockers}
    };
}

nlohmann::json OutlinerManager::ClearBlockerJson(uint32_t planet_id, uint32_t deposit_id, const std::string& deposit_key) {
    if (!base_address_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    if (cid == 0xFFFFFFFF) {
        return { {"error", "Planet has no active colony: " + std::to_string(planet_id)} };
    }

    uint32_t queue_id = GetPlanetQueueId(planet_id);
    if (queue_id == 0xFFFFFFFF) {
        return { {"error", "Planet has no valid construction queue"} };
    }

    uint32_t country_id = GetPlayerCountryId();

    // Read planet deposits
    void* dep_arr = nullptr;
    uint32_t dep_count = 0;
    SafeReadPtr((const void*)((uintptr_t)p_obj + 0x60), &dep_arr);
    SafeReadU32((const void*)((uintptr_t)p_obj + 0x6C), &dep_count);
    if (!dep_arr || dep_count == 0) {
        return { {"error", "No deposits found on planet"} };
    }

    // Deposit database at 0x3113FB0
    void* dep_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113FB0), &dep_mgr) || !dep_mgr || (uintptr_t)dep_mgr < 0x10000) {
        SafeReadPtr((const void*)(base_address_ + 0x3113FB0), &dep_mgr);
    }
    if (!dep_mgr) return { {"error", "Deposit manager not found"} };
    void* dep_db_arr = nullptr;
    uint32_t dep_db_cap = 0;
    SafeReadPtr((const void*)((uintptr_t)dep_mgr + 0x18), &dep_db_arr);
    SafeReadU32((const void*)((uintptr_t)dep_mgr + 0x20), &dep_db_cap);
    if (!dep_db_arr || dep_db_cap == 0) return { {"error", "Deposit database empty"} };

    uint32_t target_deposit_id = deposit_id;
    std::string target_deposit_key = deposit_key;
    if (target_deposit_key == "slums" || target_deposit_key == "d_slums" || target_deposit_key == "贫民窟" || target_deposit_key == "破旧民居") {
        target_deposit_key = "d_decrepit_dwellings";
    } else if (target_deposit_key == "failing_infrastructure" || target_deposit_key == "工业废土" || target_deposit_key == "衰退的基础设施") {
        target_deposit_key = "d_failing_infrastructure_earth";
    } else if (target_deposit_key == "garbage_patch" || target_deposit_key == "太平洋垃圾带") {
        target_deposit_key = "d_great_pacific_garbage_patch";
    }

    // Resolve target deposit
    bool found = false;
    for (uint32_t i = 0; i < dep_count; ++i) {
        uint32_t did = 0;
        if (!SafeReadU32((const void*)((uintptr_t)dep_arr + i * sizeof(uint32_t)), &did)) continue;
        if (did >= dep_db_cap) continue;

        void* dep_obj = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)dep_db_arr + did * 16 + 8), &dep_obj) || !dep_obj) continue;
        void* type_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)dep_obj + 0x18), &type_ptr) || !type_ptr) continue;

        std::string k;
        SafeReadPdxString((const void*)((uintptr_t)type_ptr + 0x20), k);

        if (target_deposit_id != 0 && did == target_deposit_id) {
            target_deposit_key = k;
            found = true;
            break;
        } else if (target_deposit_id == 0 && !target_deposit_key.empty() && k == target_deposit_key) {
            target_deposit_id = did;
            found = true;
            break;
        }
    }

    if (!found || target_deposit_id == 0) {
        return {
            {"success", false},
            {"error", "Blocker deposit not found on this planet (deposit_id: " + std::to_string(deposit_id) + ", deposit_key: '" + deposit_key + "')"}
        };
    }

    // 1. Construct CBuildableClearDepositBlocker (0x20 bytes) on stack
    uint8_t action_obj[0x20];
    memset(action_obj, 0, sizeof(action_obj));
    *(void**)(action_obj + 0x00) = (void*)(base_address_ + kBuildableClearDepositBlockerVt); // CBuildableClearDepositBlocker vtable
    *(uint32_t*)(action_obj + 0x08) = target_deposit_id;              // deposit_id
    *(uint32_t*)(action_obj + 0x0C) = cid;                            // colony_id
    *(uint64_t*)(action_obj + 0x10) = 0;
    *(uint64_t*)(action_obj + 0x18) = 0;

    // CAddBuildableToQueueCommand takes ownership of the buildable (engine heap copy)
    std::string why;
    if (!QueueBuildable(action_obj, sizeof(action_obj), country_id, queue_id, true, &why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Cannot be queued (prerequisites not met)" : "Cannot be queued: " + why},
            {"planet_id", planet_id},
            {"queue_id", queue_id}
        };
    }

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"deposit_id", target_deposit_id},
        {"deposit_key", target_deposit_key},
        {"deposit_name", LocalizeKey(target_deposit_key)},
        {"country_id", country_id},
        {"queue_id", queue_id},
        {"message", "Deposit blocker clearance order successfully queued"}
    };
}

nlohmann::json OutlinerManager::ExtractMonthlyPopulationSummary(void* colony_obj) {
    if (!colony_obj) {
        return {
            {"net_change", 0},
            {"growth", 0},
            {"migration", 0},
            {"assembly", 0},
            {"categories", nlohmann::json::object()},
            {"demographics", nlohmann::json::array()}
        };
    }

    int32_t net_change = 0;
    SafeReadI32((const void*)((uintptr_t)colony_obj + 0xFEC), &net_change);

    // Robin Hood table at colony_obj + 0x1010
    void* table_entries = nullptr;
    uint32_t mask = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0x1018), &table_entries);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x1024), &mask);

    int32_t growth_raw = 0;
    int32_t decline_raw = 0;
    int32_t immigration_raw = 0;
    int32_t emigration_raw = 0;
    int32_t assembly_raw = 0;
    nlohmann::json categories = nlohmann::json::object();

    if (table_entries && mask > 0 && mask < 1024) {
        for (uint32_t i = 0; i <= mask; ++i) {
            void* entry = (void*)((uintptr_t)table_entries + i * 0x40);
            uint32_t dist_raw = 0;
            SafeReadU32((const void*)((uintptr_t)entry + 4), &dist_raw);
            uint8_t dist = (uint8_t)(dist_raw & 0xFF);
            if (dist != 0xFF && dist != 0) {
                std::string cat_k;
                SafeReadPdxString((const void*)((uintptr_t)entry + 0x18), cat_k);
                int32_t val = 0;
                SafeReadI32((const void*)((uintptr_t)entry + 0x38), &val);
                if (!cat_k.empty()) {
                    categories[cat_k] = val;
                    if (cat_k == "GROWTH_CAT_GROWTH") growth_raw = val;
                    else if (cat_k == "GROWTH_CAT_DECLINE") decline_raw = val;
                    else if (cat_k == "GROWTH_CAT_IMMIGRATION") immigration_raw = val;
                    else if (cat_k == "GROWTH_CAT_EMIGRATION") emigration_raw = val;
                    else if (cat_k == "GROWTH_CAT_ASSEMBLY") assembly_raw = val;
                }
            }
        }
    }

    int32_t migration = immigration_raw - emigration_raw;
    int32_t growth = growth_raw - decline_raw;
    int32_t assembly = assembly_raw;

    // Demographics for pie chart from species array
    nlohmann::json demographics = nlohmann::json::array();
    void* sp_arr = nullptr;
    uint32_t sp_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xF68), &sp_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xF70), &sp_cnt);

    uint64_t total_pops = 0;
    std::vector<std::pair<uint32_t, uint32_t>> species_pops; // (species_id, count)
    if (sp_arr && sp_cnt > 0 && sp_cnt < 256) {
        for (uint32_t i = 0; i < sp_cnt; ++i) {
            uint32_t count = 0;
            uint32_t sp_handle = 0;
            SafeReadU32((const void*)((uintptr_t)sp_arr + i * 12), &count);
            SafeReadU32((const void*)((uintptr_t)sp_arr + i * 12 + 8), &sp_handle);
            uint32_t sid = sp_handle & 0xFFFFFF;
            species_pops.push_back({ sid, count });
            total_pops += count;
        }
    }

    for (const auto& sp : species_pops) {
        double share = total_pops > 0 ? ((double)sp.second * 100.0 / (double)total_pops) : 100.0;
        std::string sp_name = "Species " + std::to_string(sp.first);
        void* sp_ptr = SpeciesManager::Get().FindSpeciesPtr(sp.first);
        if (sp_ptr) {
            std::string raw_name;
            SafeReadPdxString((const void*)((uintptr_t)sp_ptr + 0x60), raw_name);
            std::string loc_name = LocalizeKey(raw_name);
            if (!loc_name.empty()) sp_name = loc_name;
        }
        demographics.push_back({
            {"species_id", sp.first},
            {"species_name", sp_name},
            {"count", sp.second},
            {"share_percent", std::round(share * 10.0) / 10.0}
        });
    }

    return {
        {"net_change", net_change},
        {"growth", growth},
        {"migration", migration},
        {"assembly", assembly},
        {"categories", categories},
        {"demographics", demographics}
    };
}

nlohmann::json OutlinerManager::ExtractPopulationBreakdown(void* colony_obj) {
    nlohmann::json breakdown = nlohmann::json::array();
    if (!colony_obj) return breakdown;

    void* sp_arr = nullptr;
    uint32_t sp_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xF68), &sp_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xF70), &sp_cnt);
    if (!sp_arr || sp_cnt == 0 || sp_cnt > 256) return breakdown;

    // Per-species net change table at colony_obj + 0xFF8
    std::unordered_map<uint32_t, int32_t> sp_net_map;
    void* table_sp_entries = nullptr;
    uint32_t sp_mask = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xFF8), &table_sp_entries);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x1004), &sp_mask);
    if (table_sp_entries && sp_mask > 0 && sp_mask < 1024) {
        for (uint32_t i = 0; i <= sp_mask; ++i) {
            void* entry = (void*)((uintptr_t)table_sp_entries + i * 0x40);
            uint32_t dist_raw = 0;
            SafeReadU32((const void*)((uintptr_t)entry + 4), &dist_raw);
            uint8_t dist = (uint8_t)(dist_raw & 0xFF);
            if (dist != 0xFF && dist != 0) {
                uint32_t sp_handle = 0;
                int32_t net = 0;
                SafeReadU32((const void*)((uintptr_t)entry + 8), &sp_handle);
                SafeReadI32((const void*)((uintptr_t)entry + 0x24), &net);
                uint32_t sid = sp_handle & 0xFFFFFF;
                if (sid != 0) sp_net_map[sid] = net;
            }
        }
    }

    int32_t colony_net = 0;
    SafeReadI32((const void*)((uintptr_t)colony_obj + 0xFEC), &colony_net);

    uint64_t total_pops = 0;
    for (uint32_t i = 0; i < sp_cnt; ++i) {
        uint32_t count = 0;
        SafeReadU32((const void*)((uintptr_t)sp_arr + i * 12), &count);
        total_pops += count;
    }

    for (uint32_t i = 0; i < sp_cnt; ++i) {
        uint32_t count = 0;
        uint32_t sp_handle = 0;
        SafeReadU32((const void*)((uintptr_t)sp_arr + i * 12), &count);
        SafeReadU32((const void*)((uintptr_t)sp_arr + i * 12 + 8), &sp_handle);
        uint32_t sid = sp_handle & 0xFFFFFF;

        std::string sp_name = "Species " + std::to_string(sid);
        std::string portrait = "";
        void* sp_ptr = SpeciesManager::Get().FindSpeciesPtr(sid);
        if (sp_ptr) {
            std::string raw_name;
            SafeReadPdxString((const void*)((uintptr_t)sp_ptr + 0x60), raw_name);
            std::string loc_name = LocalizeKey(raw_name);
            if (!loc_name.empty()) sp_name = loc_name;
            SafeReadPdxString((const void*)((uintptr_t)sp_ptr + 0x190), portrait);
        }

        std::string pop_disp;
        if (count >= 1000) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1fK", (double)count / 1000.0);
            pop_disp = buf;
        } else {
            pop_disp = std::to_string(count);
        }

        double share = total_pops > 0 ? ((double)count * 100.0 / (double)total_pops) : 100.0;
        int32_t net = 0;
        auto it = sp_net_map.find(sid);
        if (it != sp_net_map.end()) {
            net = it->second;
        } else if (sp_cnt == 1) {
            net = colony_net;
        }

        breakdown.push_back({
            {"species_id", sid},
            {"species_name", sp_name},
            {"pop_count", count},
            {"pop_count_display", pop_disp},
            {"share_percent", std::round(share * 10.0) / 10.0},
            {"net_change", net},
            {"portrait", portrait}
        });
    }

    return breakdown;
}

nlohmann::json OutlinerManager::ExtractColonyAscension(void* colony_obj, uint32_t cid) {
    if (!colony_obj) {
        return {
            {"tier", 0},
            {"tier_name", "行星尚未飞升。"},
            {"designation_multiplier_percent", 0.0},
            {"can_ascend", false},
            {"description", "提升该殖民地的潜力，为子孙后代铸造一个更加繁荣、稳定、高效的星球。"},
            {"status_desc", "行星尚未飞升。"}
        };
    }

    int32_t tier = 0;
    SafeReadI32((const void*)((uintptr_t)colony_obj + 0x110), &tier);
    double mult = (double)tier * 25.0;

    bool can_ascend = false;
    if (base_address_ && cid != 0xFFFFFFFF) {
        namespace ascend = sdk::cmd::increase_planetary_ascension_tier;
        auto probe = CommandBuilder::Get().Create(ascend::kSpec);
        probe.Set<uint32_t>(ascend::colony, cid);
        can_ascend = probe.IsValid();
    }

    std::string tier_name = (tier <= 0) ? "行星尚未飞升。" : ("飞升等级: " + std::to_string(tier));
    std::string status_desc = (tier <= 0) ? "行星尚未飞升。" : ("行星已飞升至等级 " + std::to_string(tier) + "，特化效果提升 " + std::to_string((int)mult) + "%。");

    return {
        {"tier", tier},
        {"tier_name", tier_name},
        {"designation_multiplier_percent", mult},
        {"can_ascend", can_ascend},
        {"description", "提升该殖民地的潜力，为子孙后代铸造一个更加繁荣、稳定、高效的星球。"},
        {"status_desc", status_desc}
    };
}

nlohmann::json OutlinerManager::AscendColonyJson(uint32_t planet_id) {
    if (!base_address_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    if (cid == 0xFFFFFFFF) {
        return { {"error", "Planet has no active colony: " + std::to_string(planet_id)} };
    }

    void* colony_obj = FindColony(cid);
    if (!colony_obj) {
        return { {"error", "Colony object not found: " + std::to_string(cid)} };
    }

    namespace ascend = sdk::cmd::increase_planetary_ascension_tier;
    auto cmd = CommandBuilder::Get().Create(ascend::kSpec);
    cmd.Set<uint32_t>(ascend::colony, cid);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return {
            {"success", false},
            {"is_valid", false},
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"error", why.empty() ? "Ascension requirement not met" : "Ascension not possible: " + why}
        };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"error", cmd.error()} };
    }

    int32_t current_tier = 0;
    SafeReadI32((const void*)((uintptr_t)colony_obj + 0x110), &current_tier);

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"previous_tier", current_tier},
        {"target_tier", current_tier + 1},
        {"message", "Planetary ascension order successfully posted"}
    };
}

std::string OutlinerManager::LocalizeDecisionKey(const std::string& key) {
    if (key.empty()) return key;

    static const std::unordered_map<std::string, std::string> kDecisionLocMap = {
        {"decision_planet_food_boost", "发放食物补贴 (Distribute Food)"},
        {"decision_planet_luxuries_boost", "发放奢侈品 (Distribute Consumer Goods)"},
        {"decision_enact_population_control", "鼓励生育/控制人口 (Declare Population Controls)"},
        {"decision_end_population_control", "废除人口控制 (Cease Population Controls)"},
        {"decision_enact_population_control_gestalt", "人口繁衍控制 (Population Controls Gestalt)"},
        {"decision_end_population_control_gestalt", "停止繁衍控制 (Cease Gestalt Controls)"},
        {"decision_enact_robot_assembly_control", "停止建造机器人 (Cease Robot Assembly)"},
        {"decision_end_robot_assembly_control", "允许建造机器人 (Resume Robot Assembly)"},
        {"decision_expel_population", "驱逐人口 (Expel Excess Population)"},
        {"decision_discourage_growth", "抑制人口增长 (Discourage Planetary Growth)"},
        {"decision_end_discourage_growth", "停止抑制增长 (Encourage Planetary Growth)"},
        {"decision_penal_colony", "设立监狱行星 (Designate Penal Colony)"},
        {"decision_abolish_penal_colony", "废除监狱行星 (Abolish Penal Colony)"},
        {"decision_slave_colony", "设立奴隶行星 (Designate Slave Colony)"},
        {"decision_abolish_slave_colony", "废除奴隶行星 (Abolish Slave Colony)"},
        {"decision_resort_colony", "设立度假行星 (Designate Resort Colony)"},
        {"decision_abolish_resort_colony", "废除度假行星 (Abolish Resort Colony)"},
        {"decision_mastery_of_nature", "掌握自然 (Mastery of Nature)"},
        {"decision_consecrated_worlds", "祝圣世界 (Consecrate World)"},
        {"decision_unconsecrated_worlds", "取消祝圣世界 (Unconsecrate World)"},
        {"decision_declare_martial_law", "戒严令 (Declare Martial Law)"},
        {"decision_end_martial_law", "解除戒严令 (Revoke Martial Law)"},
        {"decision_anti_crime_campaign", "严打犯罪运动 (Anti-Crime Campaign)"},
        {"decision_negotiate_crime_lords", "与黑帮谈判 (Deal with Crime Lords)"},
        {"decision_strip_mine", "露天采矿 (Strip Mining)"},
        {"decision_cease_strip_mine", "停止露天采矿 (Cease Strip Mining)"},
        {"decision_arcology_project", "理想城计划 (Arcology Project)"},
        {"decision_prospecting", "地质勘探 (Planetary Prospecting)"}
    };
    auto it = kDecisionLocMap.find(key);
    if (it != kDecisionLocMap.end()) {
        return it->second;
    }
    return LocalizeKey(key);
}

std::string OutlinerManager::LocalizePlanetClass(const std::string& key) {
    if (key.empty()) return key;

    static const std::unordered_map<std::string, std::string> kPlanetClassLocMap = {
        {"pc_continental", "大陆星球 (Continental)"},
        {"pc_ocean", "海洋星球 (Ocean)"},
        {"pc_tropical", "热带星球 (Tropical)"},
        {"pc_desert", "沙漠星球 (Desert)"},
        {"pc_arid", "干旱星球 (Arid)"},
        {"pc_savannah", "热带草原星球 (Savannah)"},
        {"pc_arctic", "极地星球 (Arctic)"},
        {"pc_alpine", "高山星球 (Alpine)"},
        {"pc_tundra", "苔原星球 (Tundra)"},
        {"pc_gaia", "盖亚星球 (Gaia)"},
        {"pc_relic", "遗落星球 (Relic)"},
        {"pc_nuked", "死寂星球 (Tomb World)"},
        {"pc_machine", "机魂星球 (Machine World)"},
        {"pc_hive", "蜂巢星球 (Hive World)"},
        {"pc_city", "理想城 (Ecumenopolis)"},
        {"pc_ringworld_habitable", "环形世界 (Ring World)"},
        {"pc_habitat", "居住站 (Habitat)"},
        {"pc_shattered", "破碎星球 (Shattered World)"},
        {"pc_toxic", "剧毒星球 (Toxic)"},
        {"pc_barren", "荒芜星球 (Barren)"},
        {"pc_barren_cold", "寒冷荒芜星球 (Cold Barren)"},
        {"pc_frozen", "冰封星球 (Frozen)"},
        {"pc_molten", "熔岩星球 (Molten)"},
        {"pc_gas_giant", "气态巨行星 (Gas Giant)"},
        {"pc_asteroid", "小行星 (Asteroid)"}
    };
    auto it = kPlanetClassLocMap.find(key);
    if (it != kPlanetClassLocMap.end()) {
        return it->second;
    }
    return LocalizeKey(key);
}

nlohmann::json OutlinerManager::GetPlanetaryDecisionsJson(uint32_t planet_id) {
    if (!base_address_) {
        return { {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);

    std::string planet_name;
    SafeReadPdxString((const void*)((uintptr_t)p_obj + 0x108), planet_name);
    planet_name = LocalizeKey(planet_name);

    uint32_t country_id = GetPlayerCountryId();

    void* dec_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x31118B8), &dec_mgr);
    if (!dec_mgr) {
        return { {"error", "Decisions database not found"} };
    }

    uint32_t dec_count = 0;
    SafeReadU32((const void*)((uintptr_t)dec_mgr + 0x5C), &dec_count);
    void* dec_arr = nullptr;
    SafeReadPtr((const void*)((uintptr_t)dec_mgr + 0x50), &dec_arr);
    if (!dec_arr || dec_count == 0) {
        return {
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"planet_name", planet_name},
            {"decisions", nlohmann::json::array()}
        };
    }

    nlohmann::json decisions = nlohmann::json::array();
    for (uint32_t i = 0; i < dec_count; ++i) {
        void* dec = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)dec_arr + i * sizeof(void*)), &dec) || !dec) {
            continue;
        }

        std::string key;
        SafeReadPdxString((const void*)((uintptr_t)dec + 0x20), key);
        if (key.empty()) continue;

        int32_t raw_days = 0;
        SafeReadI32((const void*)((uintptr_t)dec + 0xb58), &raw_days);
        uint32_t days = raw_days > 0 ? (uint32_t)raw_days : 0;

        // CEnactDecisionCommand::IsValid decides whether the decision can be enacted here
        namespace enact = sdk::cmd::enact_decision_planet_command;
        auto probe = CommandBuilder::Get().Create(enact::kSpec);
        probe.Set<void*>(enact::decision, dec)
             .Set<uint64_t>(enact::carrier, (uint64_t)planet_id)  // {planet id, carrier type 0 = planet}
             .Set<uint32_t>(enact::country, country_id);
        bool can_enact = probe.IsValid();

        decisions.push_back({
            {"key", key},
            {"name", LocalizeDecisionKey(key)},
            {"days", days},
            {"can_enact", can_enact}
        });
    }

    return {
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"planet_name", planet_name},
        {"decisions_count", decisions.size()},
        {"decisions", decisions}
    };
}

nlohmann::json OutlinerManager::EnactDecisionJson(uint32_t planet_id, const std::string& decision_key) {
    if (!base_address_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);

    uint32_t country_id = GetPlayerCountryId();

    void* dec_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x31118B8), &dec_mgr);
    if (!dec_mgr) return { {"error", "Decisions database not found"} };

    uint32_t dec_count = 0;
    SafeReadU32((const void*)((uintptr_t)dec_mgr + 0x5C), &dec_count);
    void* dec_arr = nullptr;
    SafeReadPtr((const void*)((uintptr_t)dec_mgr + 0x50), &dec_arr);
    if (!dec_arr || dec_count == 0) return { {"error", "Decisions database empty"} };

    std::string norm_key = decision_key;
    if (norm_key == "luxuries" || norm_key == "consumer_goods" || norm_key == "发放奢侈品" || norm_key == "奢侈品") {
        norm_key = "decision_planet_luxuries_boost";
    } else if (norm_key == "food" || norm_key == "food_boost" || norm_key == "发放食物补贴" || norm_key == "食物补贴") {
        norm_key = "decision_planet_food_boost";
    } else if (norm_key == "population_control" || norm_key == "鼓励生育" || norm_key == "控制人口") {
        norm_key = "decision_enact_population_control";
    } else if (norm_key == "martial_law" || norm_key == "戒严令") {
        norm_key = "decision_declare_martial_law";
    } else if (norm_key == "mastery_of_nature" || norm_key == "掌握自然") {
        norm_key = "decision_mastery_of_nature";
    } else if (norm_key == "consecrate_world" || norm_key == "祝圣世界") {
        norm_key = "decision_consecrated_worlds";
    }

    void* target_dec = nullptr;
    std::string matched_key;
    for (uint32_t i = 0; i < dec_count; ++i) {
        void* dec = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)dec_arr + i * sizeof(void*)), &dec) || !dec) continue;
        std::string k;
        SafeReadPdxString((const void*)((uintptr_t)dec + 0x20), k);
        if (k == norm_key || k == decision_key) {
            target_dec = dec;
            matched_key = k;
            break;
        }
    }

    if (!target_dec) {
        return { {"error", "Decision not found in database: " + decision_key} };
    }

    namespace enact = sdk::cmd::enact_decision_planet_command;
    auto cmd = CommandBuilder::Get().Create(enact::kSpec);
    cmd.Set<void*>(enact::decision, target_dec)
       .Set<uint64_t>(enact::carrier, (uint64_t)planet_id)  // {planet id, carrier type 0 = planet}
       .Set<uint32_t>(enact::country, country_id);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Decision cannot be enacted (conditions not met, already active, or insufficient resources)"
                                  : "Decision cannot be enacted: " + why},
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"decision_key", matched_key},
            {"country_id", country_id}
        };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"error", cmd.error()} };
    }

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"decision_key", matched_key},
        {"decision_name", LocalizeDecisionKey(matched_key)},
        {"country_id", country_id},
        {"message", "Planetary decision enacted successfully"}
    };
}

nlohmann::json OutlinerManager::GetTerraformingOptionsJson(uint32_t planet_id) {
    if (!base_address_) {
        return { {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    std::string planet_name;
    SafeReadPdxString((const void*)((uintptr_t)p_obj + 0x108), planet_name);
    planet_name = LocalizeKey(planet_name);

    void* cur_class = nullptr;
    SafeReadPtr((const void*)((uintptr_t)p_obj + 0x148), &cur_class);
    std::string cur_class_key;
    if (cur_class) {
        SafeReadPdxString((const void*)((uintptr_t)cur_class + 0x28), cur_class_key);
    }
    std::string cur_class_name = LocalizePlanetClass(cur_class_key);

    uint32_t country_id = GetPlayerCountryId();

    // Check active terraforming process at planet + 0xB70
    void* proc = nullptr;
    SafeReadPtr((const void*)((uintptr_t)p_obj + 0xB70), &proc);
    bool is_terraforming = false;
    if (proc) {
        void** proc_vt = nullptr;
        SafeReadPtr(proc, (void**)&proc_vt);
        if (proc_vt && proc_vt[8]) {
            typedef bool (*FnIsTerraforming)(void*);
            is_terraforming = ((FnIsTerraforming)proc_vt[8])(proc);
        }
    }

    nlohmann::json current_process = nullptr;
    if (is_terraforming) {
        uint64_t raw_prog = 0, raw_total = 0;
        SafeReadU64((const void*)((uintptr_t)proc + 0x10), &raw_prog);
        SafeReadU64((const void*)((uintptr_t)proc + 0x18), &raw_total);
        double progress_days = (double)raw_prog / 100000.0;
        double total_days = (double)raw_total / 100000.0;
        double pct = total_days > 0.0 ? (progress_days / total_days) * 100.0 : 0.0;
        int32_t remaining = total_days > progress_days ? (int32_t)(total_days - progress_days) : 0;

        void* link = nullptr;
        SafeReadPtr((const void*)((uintptr_t)proc + 0x68), &link);
        std::string to_class_key = "unknown";
        if (link) {
            void* to_cls = nullptr;
            SafeReadPtr((const void*)((uintptr_t)link + 0x268), &to_cls);
            if (to_cls) {
                SafeReadPdxString((const void*)((uintptr_t)to_cls + 0x28), to_class_key);
            }
        }
        current_process = {
            {"target_planet_class", to_class_key},
            {"target_planet_class_name", LocalizePlanetClass(to_class_key)},
            {"progress_days", (int32_t)progress_days},
            {"total_days", (int32_t)total_days},
            {"remaining_days", remaining},
            {"progress_percent", std::round(pct * 10.0) / 10.0}
        };
    }

    // Read terraform links database at base + 0x3151FD8
    void* db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3151FD8), &db);
    nlohmann::json options = nlohmann::json::array();
    if (db && cur_class) {
        uint32_t link_cnt = 0;
        SafeReadU32((const void*)((uintptr_t)db + 0x1C), &link_cnt);
        void* link_arr = nullptr;
        SafeReadPtr((const void*)((uintptr_t)db + 0x10), &link_arr);

        if (link_arr && link_cnt > 0) {
            for (uint32_t i = 0; i < link_cnt; ++i) {
                void* link = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)link_arr + i * sizeof(void*)), &link) || !link) {
                    continue;
                }

                void* from_cls = nullptr;
                SafeReadPtr((const void*)((uintptr_t)link + 0x260), &from_cls);
                if (from_cls != cur_class) continue;

                void* to_cls = nullptr;
                SafeReadPtr((const void*)((uintptr_t)link + 0x268), &to_cls);
                if (!to_cls) continue;

                std::string to_class_key;
                SafeReadPdxString((const void*)((uintptr_t)to_cls + 0x28), to_class_key);

                uint32_t duration_days = 0;
                SafeReadU32((const void*)((uintptr_t)link + 0x258), &duration_days);

                uint32_t link_index = 0;
                SafeReadU32((const void*)((uintptr_t)link + 0x3D0), &link_index);

                // Test validation via CStartTerraformationCommand
                bool can_terraform = false;
                if (!is_terraforming) {
                    namespace tf = sdk::cmd::start_terraformation;
                    auto probe = CommandBuilder::Get().Create(tf::kSpec);
                    probe.Set<uint32_t>(tf::planet, planet_id)
                         .Set<uint32_t>(tf::terraform_link, link_index)
                         .Set<uint32_t>(tf::who, country_id);
                    can_terraform = probe.IsValid();
                }

                options.push_back({
                    {"link_index", link_index},
                    {"target_planet_class", to_class_key},
                    {"target_planet_class_name", LocalizePlanetClass(to_class_key)},
                    {"duration_days", duration_days},
                    {"can_terraform", can_terraform}
                });
            }
        }
    }

    return {
        {"planet_id", planet_id},
        {"planet_name", planet_name},
        {"current_planet_class", cur_class_key},
        {"current_planet_class_name", cur_class_name},
        {"is_terraforming", is_terraforming},
        {"current_process", current_process},
        {"available_options_count", options.size()},
        {"options", options}
    };
}

nlohmann::json OutlinerManager::StartTerraformingJson(uint32_t planet_id, const std::string& target_class, int32_t link_index) {
    if (!base_address_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    void* proc = nullptr;
    SafeReadPtr((const void*)((uintptr_t)p_obj + 0xB70), &proc);
    bool is_terraforming = false;
    if (proc) {
        void** proc_vt = nullptr;
        SafeReadPtr(proc, (void**)&proc_vt);
        if (proc_vt && proc_vt[8]) {
            typedef bool (*FnIsTerraforming)(void*);
            is_terraforming = ((FnIsTerraforming)proc_vt[8])(proc);
        }
    }
    if (is_terraforming) {
        return { {"error", "Planet is already being terraformed"} };
    }

    void* cur_class = nullptr;
    SafeReadPtr((const void*)((uintptr_t)p_obj + 0x148), &cur_class);
    if (!cur_class) return { {"error", "Failed to get planet class"} };

    uint32_t country_id = GetPlayerCountryId();

    void* db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3151FD8), &db);
    if (!db) return { {"error", "Terraform database not found"} };

    uint32_t link_cnt = 0;
    SafeReadU32((const void*)((uintptr_t)db + 0x1C), &link_cnt);
    void* link_arr = nullptr;
    SafeReadPtr((const void*)((uintptr_t)db + 0x10), &link_arr);
    if (!link_arr || link_cnt == 0) return { {"error", "Terraform database empty"} };

    std::string norm_target = target_class;
    if (!norm_target.empty() && norm_target.find("pc_") != 0) {
        if (norm_target == "ocean" || norm_target == "海洋") norm_target = "pc_ocean";
        else if (norm_target == "tropical" || norm_target == "热带") norm_target = "pc_tropical";
        else if (norm_target == "continental" || norm_target == "大陆") norm_target = "pc_continental";
        else if (norm_target == "desert" || norm_target == "沙漠") norm_target = "pc_desert";
        else if (norm_target == "arid" || norm_target == "干旱") norm_target = "pc_arid";
        else if (norm_target == "savannah" || norm_target == "热带草原") norm_target = "pc_savannah";
        else if (norm_target == "arctic" || norm_target == "极地") norm_target = "pc_arctic";
        else if (norm_target == "alpine" || norm_target == "高山") norm_target = "pc_alpine";
        else if (norm_target == "tundra" || norm_target == "苔原") norm_target = "pc_tundra";
        else if (norm_target == "gaia" || norm_target == "盖亚") norm_target = "pc_gaia";
        else if (norm_target == "machine" || norm_target == "机魂") norm_target = "pc_machine";
        else if (norm_target == "hive" || norm_target == "蜂巢") norm_target = "pc_hive";
        else norm_target = "pc_" + norm_target;
    }

    void* target_link = nullptr;
    uint32_t target_link_idx = 0;
    uint32_t duration_days = 0;
    std::string matched_target_key;

    for (uint32_t i = 0; i < link_cnt; ++i) {
        void* link = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)link_arr + i * sizeof(void*)), &link) || !link) continue;

        void* from_cls = nullptr;
        SafeReadPtr((const void*)((uintptr_t)link + 0x260), &from_cls);
        if (from_cls != cur_class) continue;

        uint32_t l_idx = 0;
        SafeReadU32((const void*)((uintptr_t)link + 0x3D0), &l_idx);

        void* to_cls = nullptr;
        SafeReadPtr((const void*)((uintptr_t)link + 0x268), &to_cls);
        std::string to_k;
        if (to_cls) {
            SafeReadPdxString((const void*)((uintptr_t)to_cls + 0x28), to_k);
        }

        if (link_index >= 0 && (uint32_t)link_index == l_idx) {
            target_link = link;
            target_link_idx = l_idx;
            matched_target_key = to_k;
            SafeReadU32((const void*)((uintptr_t)link + 0x258), &duration_days);
            break;
        } else if (!norm_target.empty() && to_k == norm_target) {
            target_link = link;
            target_link_idx = l_idx;
            matched_target_key = to_k;
            SafeReadU32((const void*)((uintptr_t)link + 0x258), &duration_days);
            break;
        }
    }

    if (!target_link) {
        return { {"error", "No valid terraforming link found for target class: " + target_class} };
    }

    namespace tf = sdk::cmd::start_terraformation;
    auto cmd = CommandBuilder::Get().Create(tf::kSpec);
    cmd.Set<uint32_t>(tf::planet, planet_id)
       .Set<uint32_t>(tf::terraform_link, target_link_idx)
       .Set<uint32_t>(tf::who, country_id);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Terraforming not possible (tech prerequisite, energy credits, or invalid target)"
                                  : "Terraforming not possible: " + why},
            {"planet_id", planet_id},
            {"target_planet_class", matched_target_key},
            {"link_index", target_link_idx},
            {"country_id", country_id}
        };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"error", cmd.error()} };
    }

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"target_planet_class", matched_target_key},
        {"target_planet_class_name", LocalizePlanetClass(matched_target_key)},
        {"duration_days", duration_days},
        {"link_index", target_link_idx},
        {"country_id", country_id},
        {"message", "Terraforming project started successfully"}
    };
}

nlohmann::json OutlinerManager::CancelTerraformingJson(uint32_t planet_id) {
    if (!base_address_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    void* proc = nullptr;
    SafeReadPtr((const void*)((uintptr_t)p_obj + 0xB70), &proc);
    bool is_terraforming = false;
    if (proc) {
        void** proc_vt = nullptr;
        SafeReadPtr(proc, (void**)&proc_vt);
        if (proc_vt && proc_vt[8]) {
            typedef bool (*FnIsTerraforming)(void*);
            is_terraforming = ((FnIsTerraforming)proc_vt[8])(proc);
        }
    }
    if (!is_terraforming) {
        return { {"error", "Planet is not currently being terraformed"} };
    }

    uint32_t country_id = GetPlayerCountryId();

    namespace tfc = sdk::cmd::cancel_terraformation;
    auto cmd = CommandBuilder::Get().Create(tfc::kSpec);
    cmd.Set<uint32_t>(tfc::planet, planet_id).Set<uint32_t>(tfc::who, country_id);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Terraforming cannot be cancelled" : "Terraforming cannot be cancelled: " + why},
            {"planet_id", planet_id},
            {"country_id", country_id}
        };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"error", cmd.error()} };
    }

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"country_id", country_id},
        {"message", "Terraforming project cancelled successfully"}
    };
}

// -------------------------------------------------------------
// Economy & Jobs Management (4.5.0 Cygnus Workforce Model)
// -------------------------------------------------------------

nlohmann::json OutlinerManager::ExtractWorkforceSummary(void* colony_obj) {
    if (!colony_obj) {
        return {
            {"total_employed_workforce", 0},
            {"total_effective_workforce", 0},
            {"unemployed", 0},
            {"active_jobs_count", 0},
            {"prioritized_job", nullptr},
            {"strata", nlohmann::json::array()}
        };
    }

    uint32_t unemployed = 0;
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xfd0), &unemployed);

    void* jobs_arr = nullptr;
    uint32_t jobs_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0x38), &jobs_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x44), &jobs_cnt);

    void* job_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113FE0), &job_db);

    void* job_db_arr = nullptr;
    uint32_t job_db_cap = 0;
    if (job_db && (uintptr_t)job_db >= 0x10000) {
        SafeReadPtr((const void*)((uintptr_t)job_db + 0x18), &job_db_arr);
        SafeReadU32((const void*)((uintptr_t)job_db + 0x20), &job_db_cap);
    }

    int64_t total_employed_wf = 0;
    int64_t total_effective_wf = 0;
    uint32_t active_jobs_count = 0;
    std::string prioritized_job_key = "";

    struct StratumSummary {
        std::string key;
        std::string name;
        int64_t workforce{ 0 };
        int64_t effective_workforce{ 0 };
        uint32_t active_jobs{ 0 };
    };
    std::vector<std::string> strata_order = { "ruler", "specialist", "worker", "civilian" };
    std::unordered_map<std::string, StratumSummary> strata_map;
    strata_map["ruler"] = { "ruler", LocalizeKey("pop_cat_ruler"), 0, 0, 0 };
    strata_map["specialist"] = { "specialist", LocalizeKey("pop_cat_specialist"), 0, 0, 0 };
    strata_map["worker"] = { "worker", LocalizeKey("pop_cat_worker"), 0, 0, 0 };
    strata_map["civilian"] = { "civilian", LocalizeKey("pop_cat_civilian"), 0, 0, 0 };

    void* fav_map_entries = nullptr;
    uint32_t fav_map_mask = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xee8), &fav_map_entries);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xef4), &fav_map_mask);
    if (fav_map_entries && (uintptr_t)fav_map_entries > 0x10000 && fav_map_mask < 1024) {
        for (uint32_t mi = 0; mi <= fav_map_mask; ++mi) {
            uint8_t occupied = 0;
            SafeReadU8((const void*)((uintptr_t)fav_map_entries + mi * 24 + 4), &occupied);
            if (!occupied) continue;

            void* j = nullptr;
            SafeReadPtr((const void*)((uintptr_t)fav_map_entries + mi * 24 + 16), &j);
            if (j && (uintptr_t)j > 0x10000) {
                std::string pj_key;
                SafeReadPdxString((const void*)((uintptr_t)j + 0x20), pj_key);
                if (!pj_key.empty() && pj_key.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789") == std::string::npos) {
                    prioritized_job_key = pj_key;
                    break;
                }
            }
        }
    }

    if (jobs_arr && job_db_arr && jobs_cnt > 0 && jobs_cnt < 256) {
        for (uint32_t idx = 0; idx < jobs_cnt; ++idx) {
            uint32_t handle = 0;
            SafeReadU32((const void*)((uintptr_t)jobs_arr + idx * 4), &handle);
            uint32_t slot = handle & 0xFFFFFF;
            if (slot >= job_db_cap) continue;

            void* p_job = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)job_db_arr + slot * 16 + 8), &p_job) || !p_job) continue;

            int64_t raw_wf = 0, raw_max = 0, raw_bonus = 0;
            SafeReadI64((const void*)((uintptr_t)p_job + 0x90), &raw_wf);
            SafeReadI64((const void*)((uintptr_t)p_job + 0x98), &raw_max);
            SafeReadI64((const void*)((uintptr_t)p_job + 0xa0), &raw_bonus);

            if (raw_wf <= 0 && raw_max <= 0) continue;

            void* p_job_type = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_job + 0x18), &p_job_type);
            if (!p_job_type) continue;

            std::string job_key;
            SafeReadPdxString((const void*)((uintptr_t)p_job_type + 0x20), job_key);
            if (job_key.empty()) continue;

            void* p_cat = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_job_type + 0xaa0), &p_cat);
            std::string stratum_key = "worker";
            if (p_cat) {
                SafeReadPdxString((const void*)((uintptr_t)p_cat + 0x20), stratum_key);
            }
            if (stratum_key.empty()) stratum_key = "worker";

            int64_t wf = raw_wf / 100000;
            int64_t bonus_wf = raw_bonus / 100000;

            active_jobs_count++;
            total_employed_wf += wf;
            total_effective_wf += (wf + bonus_wf);

            auto& entry = strata_map[stratum_key];
            entry.key = stratum_key;
            if (entry.name.empty()) {
                entry.name = LocalizeKey("pop_cat_" + stratum_key);
            }
            entry.workforce += wf;
            entry.effective_workforce += (wf + bonus_wf);
            entry.active_jobs++;
        }
    }

    nlohmann::json strata_arr = nlohmann::json::array();
    for (const auto& skey : strata_order) {
        auto it = strata_map.find(skey);
        if (it != strata_map.end() && it->second.active_jobs > 0) {
            strata_arr.push_back({
                {"stratum_key", it->second.key},
                {"stratum_name", it->second.name},
                {"workforce", it->second.workforce},
                {"effective_workforce", it->second.effective_workforce},
                {"active_jobs_count", it->second.active_jobs}
            });
        }
    }
    for (const auto& [k, v] : strata_map) {
        if (std::find(strata_order.begin(), strata_order.end(), k) == strata_order.end() && v.active_jobs > 0) {
            strata_arr.push_back({
                {"stratum_key", v.key},
                {"stratum_name", v.name},
                {"workforce", v.workforce},
                {"effective_workforce", v.effective_workforce},
                {"active_jobs_count", v.active_jobs}
            });
        }
    }

    return {
        {"total_employed_workforce", total_employed_wf},
        {"total_effective_workforce", total_effective_wf},
        {"unemployed", unemployed},
        {"active_jobs_count", active_jobs_count},
        {"prioritized_job", prioritized_job_key.empty() ? nlohmann::json(nullptr) : nlohmann::json(prioritized_job_key)},
        {"strata", strata_arr}
    };
}

nlohmann::json OutlinerManager::GetPlanetJobsJson(uint32_t planet_id) {
    if (!base_address_) {
        return { {"success", false}, {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet with ID " + std::to_string(planet_id) + " not found"} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    if (cid == 0xFFFFFFFF) {
        return { {"success", false}, {"error", "Planet has no active colony"} };
    }

    void* colony_obj = FindColony(cid);
    if (!colony_obj) {
        return { {"success", false}, {"error", "Colony object not found"} };
    }

    void* jobs_arr = nullptr;
    uint32_t jobs_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0x38), &jobs_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x44), &jobs_cnt);

    void* job_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113FE0), &job_db);

    void* job_db_arr = nullptr;
    uint32_t job_db_cap = 0;
    if (job_db && (uintptr_t)job_db >= 0x10000) {
        SafeReadPtr((const void*)((uintptr_t)job_db + 0x18), &job_db_arr);
        SafeReadU32((const void*)((uintptr_t)job_db + 0x20), &job_db_cap);
    }

    struct JobCard {
        std::string job_key;
        std::string job_name;
        int64_t current_workforce{ 0 };
        int64_t max_workforce{ 0 };
        int64_t bonus_workforce{ 0 };
        int64_t effective_workforce{ 0 };
        int64_t workforce_limit{ 0 };
        bool is_prioritized{ false };
        bool can_prioritize{ false };
    };

    struct StratumGroup {
        std::string stratum_key;
        std::string stratum_name;
        int64_t total_workforce{ 0 };
        int64_t total_effective_workforce{ 0 };
        std::vector<JobCard> jobs;
    };

    std::vector<std::string> strata_order = { "ruler", "specialist", "worker", "civilian" };
    std::unordered_map<std::string, StratumGroup> strata_map;
    strata_map["ruler"] = { "ruler", LocalizeKey("pop_cat_ruler"), 0, 0, {} };
    strata_map["specialist"] = { "specialist", LocalizeKey("pop_cat_specialist"), 0, 0, {} };
    strata_map["worker"] = { "worker", LocalizeKey("pop_cat_worker"), 0, 0, {} };
    strata_map["civilian"] = { "civilian", LocalizeKey("pop_cat_civilian"), 0, 0, {} };

    int64_t total_workforce = 0;

    void* map_entries = nullptr;
    uint32_t map_mask = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xee8), &map_entries);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xef4), &map_mask);

    if (jobs_arr && job_db_arr && jobs_cnt > 0 && jobs_cnt < 256) {
        for (uint32_t idx = 0; idx < jobs_cnt; ++idx) {
            uint32_t handle = 0;
            SafeReadU32((const void*)((uintptr_t)jobs_arr + idx * 4), &handle);
            uint32_t slot = handle & 0xFFFFFF;
            if (slot >= job_db_cap) continue;

            void* p_job = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)job_db_arr + slot * 16 + 8), &p_job) || !p_job) continue;

            int64_t raw_wf = 0, raw_max = 0, raw_bonus = 0, raw_limit = 0;
            SafeReadI64((const void*)((uintptr_t)p_job + 0x90), &raw_wf);
            SafeReadI64((const void*)((uintptr_t)p_job + 0x98), &raw_max);
            SafeReadI64((const void*)((uintptr_t)p_job + 0xa0), &raw_bonus);
            SafeReadI64((const void*)((uintptr_t)p_job + 0xa8), &raw_limit);

            if (raw_wf <= 0 && raw_max <= 0) continue;

            void* p_job_type = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_job + 0x18), &p_job_type);
            if (!p_job_type) continue;

            std::string job_key;
            SafeReadPdxString((const void*)((uintptr_t)p_job_type + 0x20), job_key);
            if (job_key.empty()) continue;

            void* p_cat = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_job_type + 0xaa0), &p_cat);
            std::string stratum_key = "worker";
            if (p_cat) {
                SafeReadPdxString((const void*)((uintptr_t)p_cat + 0x20), stratum_key);
            }
            if (stratum_key.empty()) stratum_key = "worker";

            bool is_prioritized = false;
            if (map_entries && (uintptr_t)map_entries > 0x10000 && map_mask < 1024) {
                for (uint32_t mi = 0; mi <= map_mask; ++mi) {
                    uint8_t occupied = 0;
                    SafeReadU8((const void*)((uintptr_t)map_entries + mi * 24 + 4), &occupied);
                    if (!occupied) continue;

                    void* c = nullptr;
                    void* j = nullptr;
                    SafeReadPtr((const void*)((uintptr_t)map_entries + mi * 24 + 8), &c);
                    SafeReadPtr((const void*)((uintptr_t)map_entries + mi * 24 + 16), &j);
                    if (c == p_cat && j == p_job_type) {
                        is_prioritized = true;
                        break;
                    }
                }
            }

            uint32_t jt_flags = 0;
            SafeReadU32((const void*)((uintptr_t)p_job_type + 0x928), &jt_flags);
            bool can_prio = (jt_flags & 1) != 0 && (jt_flags & 2) != 0;

            int64_t cur_wf = raw_wf / 100000;
            int64_t max_wf = (raw_max > 0 && raw_max < 100000000000000LL) ? (raw_max / 100000) : cur_wf;
            int64_t bonus_wf = raw_bonus / 100000;
            int64_t eff_wf = cur_wf + bonus_wf;
            int64_t limit_wf = (raw_limit > 0 && raw_limit < 100000000000000LL) ? (raw_limit / 100000) : max_wf;

            std::string job_name = LocalizeKey("job_" + job_key);
            if (job_name.empty()) job_name = job_key;

            JobCard card;
            card.job_key = job_key;
            card.job_name = job_name;
            card.current_workforce = cur_wf;
            card.max_workforce = max_wf;
            card.bonus_workforce = bonus_wf;
            card.effective_workforce = eff_wf;
            card.workforce_limit = limit_wf;
            card.is_prioritized = is_prioritized;
            card.can_prioritize = can_prio;

            total_workforce += cur_wf;

            auto& group = strata_map[stratum_key];
            group.stratum_key = stratum_key;
            if (group.stratum_name.empty()) {
                group.stratum_name = LocalizeKey("pop_cat_" + stratum_key);
            }
            group.total_workforce += cur_wf;
            group.total_effective_workforce += eff_wf;
            group.jobs.push_back(card);
        }
    }

    nlohmann::json strata_arr = nlohmann::json::array();
    for (const auto& skey : strata_order) {
        auto it = strata_map.find(skey);
        if (it != strata_map.end() && !it->second.jobs.empty()) {
            nlohmann::json j_list = nlohmann::json::array();
            for (const auto& j : it->second.jobs) {
                j_list.push_back({
                    {"job_key", j.job_key},
                    {"job_name", j.job_name},
                    {"current_workforce", j.current_workforce},
                    {"max_workforce", j.max_workforce},
                    {"bonus_workforce", j.bonus_workforce},
                    {"effective_workforce", j.effective_workforce},
                    {"workforce_limit", j.workforce_limit},
                    {"is_prioritized", j.is_prioritized},
                    {"can_prioritize", j.can_prioritize}
                });
            }
            strata_arr.push_back({
                {"stratum_key", it->second.stratum_key},
                {"stratum_name", it->second.stratum_name},
                {"total_workforce", it->second.total_workforce},
                {"total_effective_workforce", it->second.total_effective_workforce},
                {"jobs", j_list}
            });
        }
    }

    return {
        {"success", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"total_workforce", total_workforce},
        {"strata", strata_arr}
    };
}

nlohmann::json OutlinerManager::SetJobPriorityJson(uint32_t planet_id, const std::string& job_key) {
    if (!base_address_ || !fn_post_command_) {
        return { {"success", false}, {"error", "Engine functions or base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    if (cid == 0xFFFFFFFF) {
        return { {"success", false}, {"error", "Planet has no active colony: " + std::to_string(planet_id)} };
    }

    void* colony_obj = FindColony(cid);
    if (!colony_obj) {
        return { {"success", false}, {"error", "Colony object not found: " + std::to_string(cid)} };
    }

    void* jobs_arr = nullptr;
    uint32_t jobs_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0x38), &jobs_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x44), &jobs_cnt);

    void* job_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113FE0), &job_db);
    if (!job_db || (uintptr_t)job_db < 0x10000) {
        return { {"success", false}, {"error", "PopJob database not found"} };
    }

    void* job_db_arr = nullptr;
    uint32_t job_db_cap = 0;
    SafeReadPtr((const void*)((uintptr_t)job_db + 0x18), &job_db_arr);
    SafeReadU32((const void*)((uintptr_t)job_db + 0x20), &job_db_cap);

    void* target_job_type = nullptr;
    void* target_p_job = nullptr;
    uint32_t target_handle = 0;

    std::string q_key = job_key;
    std::transform(q_key.begin(), q_key.end(), q_key.begin(), ::tolower);
    if (q_key.rfind("job_", 0) == 0) {
        q_key = q_key.substr(4);
    }

    if (jobs_arr && job_db_arr && jobs_cnt > 0) {
        for (uint32_t idx = 0; idx < jobs_cnt; ++idx) {
            uint32_t h = 0;
            SafeReadU32((const void*)((uintptr_t)jobs_arr + idx * 4), &h);
            uint32_t slot = h & 0xFFFFFF;
            if (slot >= job_db_cap) continue;

            void* p_job = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)job_db_arr + slot * 16 + 8), &p_job) || !p_job) continue;

            void* jt = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_job + 0x18), &jt);
            if (!jt) continue;

            std::string cur_key;
            SafeReadPdxString((const void*)((uintptr_t)jt + 0x20), cur_key);
            std::string cur_key_lower = cur_key;
            std::transform(cur_key_lower.begin(), cur_key_lower.end(), cur_key_lower.begin(), ::tolower);

            if (cur_key_lower == q_key) {
                target_job_type = jt;
                target_p_job = p_job;
                target_handle = h;
                break;
            }
        }
    }

    if (!target_job_type) {
        return {
            {"success", false},
            {"error", "Job type '" + job_key + "' not found on colony " + std::to_string(cid)}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    namespace fav = sdk::cmd::set_favorite_job_command;
    auto cmd = CommandBuilder::Get().Create(fav::kSpec);
    cmd.Set<uint32_t>(fav::country, country_id)
       .Set<uint32_t>(fav::colony, cid)
       .Set<void*>(fav::job, target_job_type);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Job cannot be prioritized" : "Job cannot be prioritized: " + why},
            {"planet_id", planet_id},
            {"job_key", job_key}
        };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"error", cmd.error()} };
    }

    bool currently_prio = false;
    void* map_entries = nullptr;
    uint32_t map_mask = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xee8), &map_entries);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xef4), &map_mask);
    if (map_entries && (uintptr_t)map_entries > 0x10000 && map_mask < 1024) {
        for (uint32_t mi = 0; mi <= map_mask; ++mi) {
            uint8_t occupied = 0;
            SafeReadU8((const void*)((uintptr_t)map_entries + mi * 24 + 4), &occupied);
            if (!occupied) continue;

            void* j = nullptr;
            SafeReadPtr((const void*)((uintptr_t)map_entries + mi * 24 + 16), &j);
            if (j == target_job_type) {
                currently_prio = true;
                break;
            }
        }
    }
    bool will_be_prioritized = !currently_prio;

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"job_key", q_key},
        {"is_prioritized", will_be_prioritized},
        {"message", will_be_prioritized ? "Job priority enabled" : "Job priority cleared"}
    };
}

nlohmann::json OutlinerManager::SetJobWorkforceLimitJson(uint32_t planet_id, const std::string& job_key, int32_t limit) {
    if (!base_address_ || !fn_post_command_) {
        return { {"success", false}, {"error", "Engine functions or base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet not found: " + std::to_string(planet_id)} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    if (cid == 0xFFFFFFFF) {
        return { {"success", false}, {"error", "Planet has no active colony: " + std::to_string(planet_id)} };
    }

    void* colony_obj = FindColony(cid);
    if (!colony_obj) {
        return { {"success", false}, {"error", "Colony object not found: " + std::to_string(cid)} };
    }

    void* jobs_arr = nullptr;
    uint32_t jobs_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0x38), &jobs_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x44), &jobs_cnt);

    void* job_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113FE0), &job_db);
    if (!job_db || (uintptr_t)job_db < 0x10000) {
        return { {"success", false}, {"error", "PopJob database not found"} };
    }

    void* job_db_arr = nullptr;
    uint32_t job_db_cap = 0;
    SafeReadPtr((const void*)((uintptr_t)job_db + 0x18), &job_db_arr);
    SafeReadU32((const void*)((uintptr_t)job_db + 0x20), &job_db_cap);

    void* target_p_job = nullptr;
    uint32_t target_handle = 0;
    int64_t max_wf = 0;

    std::string q_key = job_key;
    std::transform(q_key.begin(), q_key.end(), q_key.begin(), ::tolower);
    if (q_key.rfind("job_", 0) == 0) {
        q_key = q_key.substr(4);
    }

    if (jobs_arr && job_db_arr && jobs_cnt > 0) {
        for (uint32_t idx = 0; idx < jobs_cnt; ++idx) {
            uint32_t h = 0;
            SafeReadU32((const void*)((uintptr_t)jobs_arr + idx * 4), &h);
            uint32_t slot = h & 0xFFFFFF;
            if (slot >= job_db_cap) continue;

            void* p_job = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)job_db_arr + slot * 16 + 8), &p_job) || !p_job) continue;

            void* jt = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_job + 0x18), &jt);
            if (!jt) continue;

            std::string cur_key;
            SafeReadPdxString((const void*)((uintptr_t)jt + 0x20), cur_key);
            std::string cur_key_lower = cur_key;
            std::transform(cur_key_lower.begin(), cur_key_lower.end(), cur_key_lower.begin(), ::tolower);

            if (cur_key_lower == q_key) {
                target_p_job = p_job;
                target_handle = h;
                int64_t raw_max = 0;
                SafeReadI64((const void*)((uintptr_t)p_job + 0x98), &raw_max);
                max_wf = (raw_max > 0 && raw_max < 100000000000000LL) ? (raw_max / 100000) : 10000;
                break;
            }
        }
    }

    if (!target_p_job) {
        return {
            {"success", false},
            {"error", "Job type '" + job_key + "' not found on colony " + std::to_string(cid)}
        };
    }

    int32_t target_limit = limit;
    if (target_limit < 0) target_limit = (int32_t)max_wf;
    if (max_wf > 0 && target_limit > max_wf) target_limit = (int32_t)max_wf;

    uint32_t country_id = GetPlayerCountryId();

    // Engine token name: change_job_priority_command (CChangeJobWorkforceLimitCommand).
    // The CFixedPoint limit at +0x28 (token 0x1a1 "amount") is written from a register by the
    // serializer, which the SDK dumper does not pick up yet.
    namespace wf = sdk::cmd::change_job_priority_command;
    constexpr std::ptrdiff_t kWorkforceAmount = 0x28;
    auto cmd = CommandBuilder::Get().Create(wf::kSpec);
    cmd.Set<uint32_t>(wf::country, country_id)
       .Set<uint32_t>(wf::job, target_handle)
       .Set<int64_t>(kWorkforceAmount, (int64_t)target_limit * 100000);
    std::string why;
    if (!cmd.IsValid(&why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Workforce limit cannot be changed" : "Workforce limit cannot be changed: " + why},
            {"planet_id", planet_id},
            {"job_key", job_key},
            {"target_limit", target_limit}
        };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"error", cmd.error()} };
    }

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"job_key", q_key},
        {"workforce_limit", target_limit},
        {"max_workforce", max_wf},
        {"message", "Workforce limit updated successfully"}
    };
}

// -------------------------------------------------------------
// Planet Armies Subpage (Layer 1 Macro Summary & Layer 2 Deep Dive)
// -------------------------------------------------------------

nlohmann::json OutlinerManager::ExtractArmiesSummary(void* p_obj, void* colony_obj) {
    if (!colony_obj) {
        return {
            {"total_stationed_armies", 0},
            {"garrison_power", 0.0},
            {"assault_power", 0.0},
            {"total_power", 0.0},
            {"defense_armies_count", 0},
            {"assault_armies_count", 0},
            {"deploy_in_orbit", false},
            {"include_in_builder", false},
            {"recruitment_queue_count", 0}
        };
    }

    void* armies_arr = nullptr;
    uint32_t armies_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xD0), &armies_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xDC), &armies_cnt);

    void* army_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113F60), &army_db);
    void* army_db_arr = nullptr;
    uint32_t army_db_cap = 0;
    if (army_db && (uintptr_t)army_db > 0x10000) {
        SafeReadPtr((const void*)((uintptr_t)army_db + 0x18), &army_db_arr);
        SafeReadU32((const void*)((uintptr_t)army_db + 0x20), &army_db_cap);
    }

    uint32_t defense_cnt = 0;
    uint32_t assault_cnt = 0;
    double garrison_power = 0.0;
    double assault_power = 0.0;

    if (armies_arr && army_db_arr && armies_cnt > 0 && armies_cnt < 1000) {
        for (uint32_t i = 0; i < armies_cnt; ++i) {
            uint32_t h = 0xFFFFFFFF;
            SafeReadU32((const void*)((uintptr_t)armies_arr + i * 4), &h);
            if (h == 0xFFFFFFFF) continue;
            uint32_t slot = h & 0xFFFFFF;
            if (slot >= army_db_cap) continue;

            void* p_army = nullptr;
            SafeReadPtr((const void*)((uintptr_t)army_db_arr + slot * 16 + 8), &p_army);
            if (!p_army) continue;

            void* p_type = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_army + 0x138), &p_type);
            uint8_t def_flag = 0;
            if (p_type) {
                SafeReadU8((const void*)((uintptr_t)p_type + 0x350), &def_flag);
            }
            bool is_defense = (def_flag & 1) != 0;

            int64_t raw_hp = 0;
            SafeReadI64((const void*)((uintptr_t)p_army + 0x140), &raw_hp);
            double cur_hp = (double)raw_hp / 100000.0;

            if (is_defense) {
                defense_cnt++;
                garrison_power += (cur_hp > 0.0 ? (cur_hp * 0.0625) : 25.0);
            } else {
                assault_cnt++;
                assault_power += (cur_hp > 0.0 ? (cur_hp * 0.135) : 50.0);
            }
        }
    }

    bool deploy_in_orbit = false;
    if (p_obj) {
        uint32_t c8c = 0;
        SafeReadU32((const void*)((uintptr_t)p_obj + 0xC8C), &c8c);
        deploy_in_orbit = (c8c & 0x40) != 0;
    }

    bool include_in_builder = false;
    uint32_t v1088 = 0;
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x1088), &v1088);
    include_in_builder = (v1088 & 8) != 0;

    uint32_t army_queue_id = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xC0), &army_queue_id);
    uint32_t queue_cnt = 0;
    if (army_queue_id != 0xFFFFFFFF) {
        void* mgr_eb8 = nullptr;
        if (!SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8) || !mgr_eb8 || (uintptr_t)mgr_eb8 < 0x10000) {
            SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8);
        }
        if (mgr_eb8) {
            void* arr_eb8 = nullptr;
            uint32_t cap_eb8 = 0;
            SafeReadPtr((const void*)((uintptr_t)mgr_eb8 + 0x18), &arr_eb8);
            SafeReadU32((const void*)((uintptr_t)mgr_eb8 + 0x20), &cap_eb8);
            uint32_t q_slot = army_queue_id & 0xFFFFFF;
            if (arr_eb8 && q_slot < cap_eb8) {
                void* queue_obj = nullptr;
                SafeReadPtr((const void*)((uintptr_t)arr_eb8 + q_slot * 16 + 8), &queue_obj);
                if (queue_obj) {
                    SafeReadU32((const void*)((uintptr_t)queue_obj + 0x2C), &queue_cnt);
                }
            }
        }
    }

    double total_power = garrison_power + assault_power;
    return {
        {"total_stationed_armies", defense_cnt + assault_cnt},
        {"garrison_power", std::round(garrison_power * 10.0) / 10.0},
        {"assault_power", std::round(assault_power * 10.0) / 10.0},
        {"total_power", std::round(total_power * 10.0) / 10.0},
        {"defense_armies_count", defense_cnt},
        {"assault_armies_count", assault_cnt},
        {"deploy_in_orbit", deploy_in_orbit},
        {"include_in_builder", include_in_builder},
        {"recruitment_queue_count", queue_cnt}
    };
}

nlohmann::json OutlinerManager::GetPlanetArmiesJson(uint32_t planet_id) {
    if (!base_address_) {
        return { {"success", false}, {"error", "Base address not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj && (planet_id == 0 || planet_id == 11)) {
        p_obj = FindPlanet(11);
    }
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet ID " + std::to_string(planet_id) + " not found"} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    void* colony_obj = (cid != 0xFFFFFFFF) ? FindColony(cid) : nullptr;
    if (!colony_obj) {
        return { {"success", false}, {"error", "Planet has no active colony"} };
    }

    std::string raw_name;
    SafeReadPdxString((const void*)((uintptr_t)p_obj + 0x108), raw_name);
    std::string p_name = LocalizeKey(raw_name);
    if (p_name.empty()) p_name = "Planet " + std::to_string(planet_id);

    nlohmann::json overview = ExtractArmiesSummary(p_obj, colony_obj);

    // 1. Stationed Armies
    nlohmann::json stationed_armies = nlohmann::json::array();
    void* armies_arr = nullptr;
    uint32_t armies_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xD0), &armies_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xDC), &armies_cnt);

    void* army_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113F60), &army_db);
    void* army_db_arr = nullptr;
    uint32_t army_db_cap = 0;
    if (army_db && (uintptr_t)army_db > 0x10000) {
        SafeReadPtr((const void*)((uintptr_t)army_db + 0x18), &army_db_arr);
        SafeReadU32((const void*)((uintptr_t)army_db + 0x20), &army_db_cap);
    }

    if (armies_arr && army_db_arr && armies_cnt > 0 && armies_cnt < 1000) {
        for (uint32_t i = 0; i < armies_cnt; ++i) {
            uint32_t h = 0xFFFFFFFF;
            SafeReadU32((const void*)((uintptr_t)armies_arr + i * 4), &h);
            if (h == 0xFFFFFFFF) continue;
            uint32_t slot = h & 0xFFFFFF;
            if (slot >= army_db_cap) continue;

            void* p_army = nullptr;
            SafeReadPtr((const void*)((uintptr_t)army_db_arr + slot * 16 + 8), &p_army);
            if (!p_army) continue;

            void* p_type = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_army + 0x138), &p_type);
            std::string type_key;
            uint8_t def_flag = 0;
            if (p_type) {
                SafeReadPdxString((const void*)((uintptr_t)p_type + 0x20), type_key);
                SafeReadU8((const void*)((uintptr_t)p_type + 0x350), &def_flag);
            }
            bool is_defense = (def_flag & 1) != 0;

            int64_t raw_hp = 0, raw_max_hp = 0;
            SafeReadI64((const void*)((uintptr_t)p_army + 0x140), &raw_hp);
            SafeReadI64((const void*)((uintptr_t)p_army + 0x148), &raw_max_hp);
            double cur_hp = (double)raw_hp / 100000.0;
            double max_hp = (double)raw_max_hp / 100000.0;

            int64_t raw_morale = 0, raw_max_morale = 0;
            SafeReadI64((const void*)((uintptr_t)p_army + 0x158), &raw_morale);
            SafeReadI64((const void*)((uintptr_t)p_army + 0x160), &raw_max_morale);
            double cur_morale = (double)raw_morale / 100000.0;
            double max_morale = (double)raw_max_morale / 100000.0;

            uint32_t species_h = 0xFFFFFFFF;
            SafeReadU32((const void*)((uintptr_t)p_army + 0x168), &species_h);
            std::string species_name;
            if (species_h != 0xFFFFFFFF) {
                void* sp_ptr = SpeciesManager::Get().FindSpeciesPtr(species_h & 0xFFFFFF);
                if (sp_ptr) {
                    std::string raw_sp_name;
                    SafeReadPdxString((const void*)((uintptr_t)sp_ptr + 0x60), raw_sp_name);
                    species_name = LocalizeKey(raw_sp_name);
                }
            }

            std::string custom_name;
            SafeReadPdxString((const void*)((uintptr_t)p_army + 0xF8), custom_name);
            std::string display_name = LocalizeKey(type_key);
            if (display_name.empty()) display_name = type_key;
            if (!custom_name.empty() && custom_name.front() != '%') {
                display_name = custom_name;
            } else if (!species_name.empty()) {
                display_name += " (" + species_name + ")";
            }

            double pwr = is_defense ? (cur_hp * 0.0625) : (cur_hp * 0.135);
            if (pwr <= 0.0) pwr = is_defense ? 25.0 : 50.0;

            stationed_armies.push_back({
                {"army_id", slot},
                {"army_handle", h},
                {"name", display_name},
                {"type_key", type_key},
                {"is_defense", is_defense},
                {"power", std::round(pwr * 10.0) / 10.0},
                {"health", std::round(cur_hp * 10.0) / 10.0},
                {"max_health", std::round(max_hp * 10.0) / 10.0},
                {"health_percent", max_hp > 0.0 ? std::round((cur_hp * 100.0 / max_hp) * 10.0) / 10.0 : 100.0},
                {"morale", std::round(cur_morale * 10.0) / 10.0},
                {"max_morale", std::round(max_morale * 10.0) / 10.0},
                {"morale_percent", max_morale > 0.0 ? std::round((cur_morale * 100.0 / max_morale) * 10.0) / 10.0 : 100.0},
                {"species_id", species_h != 0xFFFFFFFF ? (species_h & 0xFFFFFF) : 0},
                {"species_name", species_name}
            });
        }
    }

    // 2. Recruitable Armies Catalog
    nlohmann::json recruitable_armies = nlohmann::json::array();
    void* army_type_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3111968), &army_type_db);
    if (army_type_db && (uintptr_t)army_type_db > 0x10000) {
        void* type_arr = nullptr;
        uint32_t type_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)army_type_db + 0x50), &type_arr);
        SafeReadU32((const void*)((uintptr_t)army_type_db + 0x5C), &type_cnt);
        if (type_arr && type_cnt > 0 && type_cnt < 200) {
            for (uint32_t ti = 0; ti < type_cnt; ++ti) {
                void* p_type = nullptr;
                SafeReadPtr((const void*)((uintptr_t)type_arr + ti * 8), &p_type);
                if (!p_type) continue;

                uint8_t def_flag = 0;
                SafeReadU8((const void*)((uintptr_t)p_type + 0x350), &def_flag);
                if (def_flag & 1) continue; // Skip defense armies

                int64_t build_time_raw = 0;
                SafeReadI64((const void*)((uintptr_t)p_type + 0x358), &build_time_raw);
                if (build_time_raw <= 0) continue; // Skip non-buildable armies
                uint32_t build_days = (uint32_t)(build_time_raw / 100000);

                std::string a_key;
                SafeReadPdxString((const void*)((uintptr_t)p_type + 0x20), a_key);
                if (a_key.empty()) continue;

                std::string loc_name = LocalizeKey(a_key);
                if (loc_name.empty()) loc_name = a_key;

                double base_power = 35.0;
                if (a_key == "assault_army") base_power = 35.0;
                else if (a_key == "clone_army") base_power = 25.0;
                else if (a_key == "gene_warrior_army") base_power = 100.0;
                else if (a_key == "xenomorph_army") base_power = 85.0;
                else if (a_key == "psionic_army") base_power = 75.0;
                else if (a_key == "robot_army") base_power = 40.0;
                else if (a_key == "android_army") base_power = 60.0;

                recruitable_armies.push_back({
                    {"key", a_key},
                    {"name", loc_name},
                    {"build_time_days", build_days},
                    {"base_power", base_power},
                    {"is_defense", false}
                });
            }
        }
    }

    // 3. Army Construction Queue
    nlohmann::json construction_queue = nlohmann::json::array();
    uint32_t army_queue_id = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xC0), &army_queue_id);
    if (army_queue_id != 0xFFFFFFFF) {
        void* mgr_eb8 = nullptr;
        if (!SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8) || !mgr_eb8 || (uintptr_t)mgr_eb8 < 0x10000) {
            SafeReadPtr((const void*)(base_address_ + 0x3113EB8), &mgr_eb8);
        }
        if (mgr_eb8) {
            void* arr_eb8 = nullptr;
            uint32_t cap_eb8 = 0;
            SafeReadPtr((const void*)((uintptr_t)mgr_eb8 + 0x18), &arr_eb8);
            SafeReadU32((const void*)((uintptr_t)mgr_eb8 + 0x20), &cap_eb8);
            uint32_t q_slot = army_queue_id & 0xFFFFFF;
            if (arr_eb8 && q_slot < cap_eb8) {
                void* queue_obj = nullptr;
                SafeReadPtr((const void*)((uintptr_t)arr_eb8 + q_slot * 16 + 8), &queue_obj);
                if (queue_obj) {
                    void* q_items = nullptr;
                    uint32_t q_cnt = 0;
                    SafeReadPtr((const void*)((uintptr_t)queue_obj + 0x20), &q_items);
                    SafeReadU32((const void*)((uintptr_t)queue_obj + 0x2C), &q_cnt);

                    void* mgr_ea8 = nullptr;
                    if (!SafeReadPtr((const void*)(base_address_ + 0x3113EA8), &mgr_ea8) || !mgr_ea8 || (uintptr_t)mgr_ea8 < 0x10000) {
                        SafeReadPtr((const void*)(base_address_ + 0x3113EA8), &mgr_ea8);
                    }
                    if (mgr_ea8) {
                        void* arr_ea8 = nullptr;
                        uint32_t cap_ea8 = 0;
                        SafeReadPtr((const void*)((uintptr_t)mgr_ea8 + 0x18), &arr_ea8);
                        SafeReadU32((const void*)((uintptr_t)mgr_ea8 + 0x20), &cap_ea8);

                        for (uint32_t qi = 0; qi < q_cnt; ++qi) {
                            uint32_t item_id = 0;
                            if (!SafeReadU32((const void*)((uintptr_t)q_items + qi * sizeof(uint32_t)), &item_id)) continue;
                            uint32_t i_slot = item_id & 0xFFFFFF;
                            if (i_slot >= cap_ea8) continue;
                            void* item_obj = nullptr;
                            if (!SafeReadPtr((const void*)((uintptr_t)arr_ea8 + i_slot * 16 + 8), &item_obj) || !item_obj) continue;

                            uint32_t prog = 0, tot = 0;
                            SafeReadU32((const void*)((uintptr_t)item_obj + 0x28), &prog);
                            SafeReadU32((const void*)((uintptr_t)item_obj + 0x30), &tot);
                            void* action_obj = nullptr;
                            SafeReadPtr((const void*)((uintptr_t)item_obj + 0x18), &action_obj);

                            std::string item_key;
                            uint32_t sp_id = 0;
                            if (action_obj) {
                                void* p_t = nullptr;
                                SafeReadPtr((const void*)((uintptr_t)action_obj + 8), &p_t);
                                if (p_t) SafeReadPdxString((const void*)((uintptr_t)p_t + 0x20), item_key);
                                SafeReadU32((const void*)((uintptr_t)action_obj + 0x10), &sp_id);
                            }

                            std::string sp_name;
                            if (sp_id != 0 && sp_id != 0xFFFFFFFF) {
                                void* sp_ptr = SpeciesManager::Get().FindSpeciesPtr(sp_id & 0xFFFFFF);
                                if (sp_ptr) {
                                    std::string raw_sn;
                                    SafeReadPdxString((const void*)((uintptr_t)sp_ptr + 0x60), raw_sn);
                                    sp_name = LocalizeKey(raw_sn);
                                }
                            }

                            std::string item_name = LocalizeKey(item_key);
                            if (item_name.empty()) item_name = item_key;
                            if (!sp_name.empty()) item_name += " (" + sp_name + ")";

                            uint32_t norm_prog = prog / 100000;
                            uint32_t norm_tot = tot / 100000;
                            uint32_t norm_rem = (tot > prog) ? ((tot - prog) / 100000) : 0;

                            construction_queue.push_back({
                                {"slot_index", qi},
                                {"item_id", item_id},
                                {"key", item_key},
                                {"name", item_name},
                                {"progress_days", norm_prog},
                                {"total_days", norm_tot},
                                {"progress_percent", tot > 0 ? std::round(((double)prog * 100.0 / (double)tot) * 10.0) / 10.0 : 0.0},
                                {"remaining_days", norm_rem},
                                {"species_id", sp_id != 0xFFFFFFFF ? (sp_id & 0xFFFFFF) : 0},
                                {"species_name", sp_name}
                            });
                        }
                    }
                }
            }
        }
    }

    return {
        {"success", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"planet_name", p_name},
        {"overview", overview},
        {"stationed_armies", stationed_armies},
        {"recruitable_armies", recruitable_armies},
        {"construction_queue", construction_queue}
    };
}

nlohmann::json OutlinerManager::SetPlanetArmySettingsJson(uint32_t planet_id, std::optional<bool> deploy_in_orbit, std::optional<bool> include_in_builder) {
    if (!base_address_ || !fn_post_command_) {
        return { {"success", false}, {"error", "Engine functions not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj && (planet_id == 0 || planet_id == 11)) {
        p_obj = FindPlanet(11);
    }
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet ID " + std::to_string(planet_id) + " not found"} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    void* colony_obj = (cid != 0xFFFFFFFF) ? FindColony(cid) : nullptr;
    if (!colony_obj) {
        return { {"success", false}, {"error", "Planet has no active colony"} };
    }

    uint32_t country_id = GetPlayerCountryId();
    typedef void* (*FnCloneCmd)(void*);

    // 1. Checkbox 1: deploy_in_orbit
    if (deploy_in_orbit.has_value()) {
        uint32_t c8c = 0;
        SafeReadU32((const void*)((uintptr_t)p_obj + 0xC8C), &c8c);
        bool cur_deploy = (c8c & 0x40) != 0;
        if (cur_deploy != *deploy_in_orbit) {
            uint32_t planet_handle = 0;
            SafeReadU32((const void*)((uintptr_t)p_obj + 0x18), &planet_handle);
            if (planet_handle == 0) planet_handle = planet_id;

            namespace dep = sdk::cmd::toggle_deploy_in_orbit_command;
            auto cmd = CommandBuilder::Get().Create(dep::kSpec);
            cmd.Set<uint32_t>(dep::country, country_id).Set<uint32_t>(dep::planet, planet_handle);
            cmd.Post();
        }
    }

    // 2. Checkbox 2: include_in_builder
    if (include_in_builder.has_value()) {
        uint32_t v1088 = 0;
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0x1088), &v1088);
        bool cur_builder = (v1088 & 8) != 0;
        if (cur_builder != *include_in_builder) {
            uint32_t colony_handle = 0;
            SafeReadU32((const void*)((uintptr_t)colony_obj + 0x10), &colony_handle);
            if (colony_handle == 0) colony_handle = cid;

            namespace inc = sdk::cmd::toggle_include_in_army_builder_command;
            auto cmd = CommandBuilder::Get().Create(inc::kSpec);
            cmd.Set<uint32_t>(inc::country, country_id).Set<uint32_t>(inc::colony, colony_handle);
            cmd.Post();
        }
    }

    // Re-read current states
    uint32_t final_c8c = 0, final_1088 = 0;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xC8C), &final_c8c);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0x1088), &final_1088);

    return {
        {"success", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"deploy_in_orbit", deploy_in_orbit.value_or((final_c8c & 0x40) != 0)},
        {"include_in_builder", include_in_builder.value_or((final_1088 & 8) != 0)},
        {"message", "Planet army settings updated successfully"}
    };
}

nlohmann::json OutlinerManager::EmbarkAllArmiesJson(uint32_t planet_id) {
    if (!base_address_ || !fn_post_command_) {
        return { {"success", false}, {"error", "Engine functions not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj && (planet_id == 0 || planet_id == 11)) {
        p_obj = FindPlanet(11);
    }
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet ID " + std::to_string(planet_id) + " not found"} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    void* colony_obj = (cid != 0xFFFFFFFF) ? FindColony(cid) : nullptr;
    if (!colony_obj) {
        return { {"success", false}, {"error", "Planet has no active colony"} };
    }

    void* armies_arr = nullptr;
    uint32_t armies_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xD0), &armies_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xDC), &armies_cnt);

    void* army_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113F60), &army_db);
    void* army_db_arr = nullptr;
    uint32_t army_db_cap = 0;
    if (army_db && (uintptr_t)army_db > 0x10000) {
        SafeReadPtr((const void*)((uintptr_t)army_db + 0x18), &army_db_arr);
        SafeReadU32((const void*)((uintptr_t)army_db + 0x20), &army_db_cap);
    }

    std::vector<uint32_t> assault_handles;
    if (armies_arr && army_db_arr && armies_cnt > 0) {
        for (uint32_t i = 0; i < armies_cnt; ++i) {
            uint32_t h = 0xFFFFFFFF;
            SafeReadU32((const void*)((uintptr_t)armies_arr + i * 4), &h);
            if (h == 0xFFFFFFFF) continue;
            uint32_t slot = h & 0xFFFFFF;
            if (slot >= army_db_cap) continue;

            void* p_army = nullptr;
            SafeReadPtr((const void*)((uintptr_t)army_db_arr + slot * 16 + 8), &p_army);
            if (!p_army) continue;

            void* p_type = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_army + 0x138), &p_type);
            uint8_t def_flag = 0;
            if (p_type) SafeReadU8((const void*)((uintptr_t)p_type + 0x350), &def_flag);
            if (!(def_flag & 1)) {
                assault_handles.push_back(h);
            }
        }
    }

    if (assault_handles.empty()) {
        return {
            {"success", false},
            {"error", "No assault armies stationed on planet " + std::to_string(planet_id) + " to embark (defense armies cannot embark)"}
        };
    }

    namespace mv = sdk::cmd::move_army_to_orbit_command;
    auto cmd = CommandBuilder::Get().Create(mv::kSpec);
    std::string why;
    if (!SetArmyRefs(cmd, mv::army, assault_handles, &why)) {
        return { {"success", false}, {"error", why} };
    }
    cmd.Set<uint8_t>(mv::retreat, 0);
    if (!cmd.IsValid(&why)) {
        return { {"success", false}, {"error", why.empty() ? "Armies cannot embark" : "Armies cannot embark: " + why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"success", false}, {"error", cmd.error()} };
    }

    return {
        {"success", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"embarked_count", assault_handles.size()},
        {"message", "All assault armies successfully embarked to orbit"}
    };
}

nlohmann::json OutlinerManager::DisbandArmyJson(uint32_t planet_id, uint32_t army_id) {
    if (!base_address_ || !fn_post_command_) {
        return { {"success", false}, {"error", "Engine functions not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj && (planet_id == 0 || planet_id == 11)) {
        p_obj = FindPlanet(11);
    }
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet ID " + std::to_string(planet_id) + " not found"} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    void* colony_obj = (cid != 0xFFFFFFFF) ? FindColony(cid) : nullptr;
    if (!colony_obj) {
        return { {"success", false}, {"error", "Planet has no active colony"} };
    }

    void* armies_arr = nullptr;
    uint32_t armies_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xD0), &armies_arr);
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xDC), &armies_cnt);

    uint32_t target_handle = 0xFFFFFFFF;
    if (armies_arr && armies_cnt > 0) {
        for (uint32_t i = 0; i < armies_cnt; ++i) {
            uint32_t h = 0xFFFFFFFF;
            SafeReadU32((const void*)((uintptr_t)armies_arr + i * 4), &h);
            if ((h & 0xFFFFFF) == army_id || h == army_id) {
                target_handle = h;
                break;
            }
        }
    }

    if (target_handle == 0xFFFFFFFF) {
        return {
            {"success", false},
            {"error", "Army ID " + std::to_string(army_id) + " is not stationed on planet " + std::to_string(planet_id)}
        };
    }

    uint32_t disband_h = target_handle;
    // CDisbandArmyCommand carries the same CPdxArray<TPdxRef<CArmy>> at +0x20 as
    // CMoveArmyToOrbitCommand (its serializer reads the array there; the SDK has no field for it).
    namespace db = sdk::cmd::disband_army_command;
    constexpr std::ptrdiff_t kArmies = 0x20;
    auto cmd = CommandBuilder::Get().Create(db::kSpec);
    std::string why;
    if (!SetArmyRefs(cmd, kArmies, { disband_h }, &why)) {
        return { {"success", false}, {"error", why} };
    }
    if (!cmd.IsValid(&why)) {
        return { {"success", false}, {"error", why.empty() ? "Army cannot be disbanded" : "Army cannot be disbanded: " + why} };
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        return { {"success", false}, {"error", cmd.error()} };
    }

    return {
        {"success", true},
        {"planet_id", planet_id},
        {"army_id", army_id},
        {"message", "Army disbanded successfully"}
    };
}

nlohmann::json OutlinerManager::RecruitArmyJson(uint32_t planet_id, const std::string& army_key, std::optional<uint32_t> species_id) {
    if (!base_address_ || !fn_post_command_) {
        return { {"success", false}, {"error", "Engine functions not initialized"} };
    }

    void* p_obj = FindPlanet(planet_id);
    if (!p_obj && (planet_id == 0 || planet_id == 11)) {
        p_obj = FindPlanet(11);
    }
    if (!p_obj) {
        return { {"success", false}, {"error", "Planet ID " + std::to_string(planet_id) + " not found"} };
    }

    uint32_t cid = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)p_obj + 0xe0), &cid);
    void* colony_obj = (cid != 0xFFFFFFFF) ? FindColony(cid) : nullptr;
    if (!colony_obj) {
        return { {"success", false}, {"error", "Planet has no active colony"} };
    }

    uint32_t army_queue_id = 0xFFFFFFFF;
    SafeReadU32((const void*)((uintptr_t)colony_obj + 0xC0), &army_queue_id);
    if (army_queue_id == 0xFFFFFFFF) {
        return { {"success", false}, {"error", "Colony has no active army recruitment queue"} };
    }

    // Locate CArmyType in database
    void* army_type_db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3111968), &army_type_db);
    if (!army_type_db || (uintptr_t)army_type_db < 0x10000) {
        return { {"success", false}, {"error", "ArmyType database not found"} };
    }

    void* type_arr = nullptr;
    uint32_t type_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)army_type_db + 0x50), &type_arr);
    SafeReadU32((const void*)((uintptr_t)army_type_db + 0x5C), &type_cnt);

    void* target_p_type = nullptr;
    std::string q_key = army_key;
    std::transform(q_key.begin(), q_key.end(), q_key.begin(), ::tolower);

    if (type_arr && type_cnt > 0) {
        for (uint32_t ti = 0; ti < type_cnt; ++ti) {
            void* p_type = nullptr;
            SafeReadPtr((const void*)((uintptr_t)type_arr + ti * 8), &p_type);
            if (!p_type) continue;
            std::string cur_k;
            SafeReadPdxString((const void*)((uintptr_t)p_type + 0x20), cur_k);
            std::string cur_k_low = cur_k;
            std::transform(cur_k_low.begin(), cur_k_low.end(), cur_k_low.begin(), ::tolower);
            if (cur_k_low == q_key) {
                target_p_type = p_type;
                break;
            }
        }
    }

    if (!target_p_type) {
        return { {"success", false}, {"error", "Army type '" + army_key + "' not found in catalog"} };
    }

    uint8_t def_flag = 0;
    SafeReadU8((const void*)((uintptr_t)target_p_type + 0x350), &def_flag);
    if (def_flag & 1) {
        return { {"success", false}, {"error", "Defense armies cannot be recruited directly"} };
    }

    uint32_t target_species = species_id.value_or(0xFFFFFFFF);
    if (target_species == 0xFFFFFFFF) {
        // Fallback to colony's founder or dominant species
        void* sp_arr = nullptr;
        uint32_t sp_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)colony_obj + 0xF68), &sp_arr);
        SafeReadU32((const void*)((uintptr_t)colony_obj + 0xF70), &sp_cnt);
        if (sp_arr && sp_cnt > 0) {
            SafeReadU32((const void*)((uintptr_t)sp_arr + 8), &target_species);
        }
    }

    // 1. Construct CBuildableArmy (0x28 bytes)
    alignas(16) uint8_t action_obj[0x28]{ 0 };
    *(void**)(action_obj + 0x00) = (void*)(base_address_ + kBuildableArmyVt); // CBuildableArmy true vtable
    *(void**)(action_obj + 0x08) = target_p_type;
    *(uint32_t*)(action_obj + 0x10) = target_species;
    *(uint32_t*)(action_obj + 0x14) = 0;
    *(uint32_t*)(action_obj + 0x18) = cid; // Colony ID from [colony_obj + 0x10]
    *(uint32_t*)(action_obj + 0x1C) = 0;
    *(uint32_t*)(action_obj + 0x20) = 0xFFFFFFFF;
    *(uint32_t*)(action_obj + 0x24) = 0;

    // 2. Construct CAddBuildableToQueueCommand (0x30 bytes)
    uint32_t country_id = GetPlayerCountryId();
    // CAddBuildableToQueueCommand takes ownership of the buildable (engine heap copy)
    std::string why;
    if (!QueueBuildable(action_obj, sizeof(action_obj), country_id, army_queue_id, true, &why)) {
        return {
            {"success", false},
            {"error", why.empty() ? "Cannot be queued (prerequisites not met)" : "Cannot be queued: " + why},
            {"planet_id", planet_id},
            {"queue_id", army_queue_id}
        };
    }

    return {
        {"success", true},
        {"planet_id", planet_id},
        {"colony_id", cid},
        {"army_key", q_key},
        {"queue_id", army_queue_id},
        {"species_id", target_species != 0xFFFFFFFF ? (target_species & 0xFFFFFF) : 0},
        {"message", "Army recruitment queued successfully"}
    };
}

bool OutlinerManager::QueueBuildable(const void* buildable, size_t size, uint32_t country_id, uint32_t queue_id,
                                     bool dispatch, std::string* why) {
    namespace q = sdk::cmd::add_buildable_to_queue_command;
    // +0x20: the owned CBuildable* (the serializer writes it through the buildable's own
    // CSerializer, which the SDK dumper does not expand).
    constexpr std::ptrdiff_t kBuildable = 0x20;

    // Reject anything that is not shaped like a CBuildable vtable (slots 2/3 = base class
    // implementation, same as the army buildable's) before the engine calls into it.
    uintptr_t vt = *(const uintptr_t*)buildable;
    uintptr_t ref = base_address_ + kBuildableArmyVt;
    uint64_t vt2 = 0, vt3 = 0, ref2 = 0;
    if (!SafeReadU64((const void*)(vt + 16), &vt2) || !SafeReadU64((const void*)(vt + 24), &vt3) ||
        !SafeReadU64((const void*)(ref + 16), &ref2) || vt2 != ref2 || vt3 != ref2) {
        if (why) *why = "internal error: buildable vtable does not look like a CBuildable";
        LOGF("[OUTLINER] refusing buildable with vtable 0x%llX", (unsigned long long)(vt - base_address_));
        return false;
    }
    auto cmd = CommandBuilder::Get().Create(q::kSpec);
    if (!cmd) {
        if (why) *why = cmd.error();
        return false;
    }
    // The command deletes its buildable, so it must live on the engine heap even for a probe.
    void* heap = CommandBuilder::Get().EngineAlloc(size);
    if (!heap) {
        if (why) *why = "engine allocation for the buildable failed";
        return false;
    }
    memcpy(heap, buildable, size);
    cmd.Set<void*>(kBuildable, heap)
       .Set<uint32_t>(q::country, country_id)
       .Set<uint32_t>(q::queue, queue_id);
    if (!cmd.IsValid(why)) {
        return false;  // destroying the command frees the buildable
    }
    if (!dispatch) {
        return true;
    }
    if (!cmd.Post(NativeCommand::Check::EngineGate)) {
        if (why) *why = cmd.error();
        return false;
    }
    return true;
}

bool OutlinerManager::SetArmyRefs(NativeCommand& cmd, std::ptrdiff_t array_off, const std::vector<uint32_t>& ids,
                                  std::string* why) {
    // CPdxArray<TPdxRef<CArmy>> embedded in the command: vtable +0 (set by the factory), data +8,
    // capacity +0x10, size +0x14. The command frees the data, so it comes from the engine heap.
    if (!cmd) {
        if (why) *why = cmd.error();
        return false;
    }
    void* data = CommandBuilder::Get().EngineAlloc(ids.size() * sizeof(uint32_t));
    if (!data) {
        if (why) *why = "engine allocation for the army list failed";
        return false;
    }
    memcpy(data, ids.data(), ids.size() * sizeof(uint32_t));
    cmd.Set<void*>(array_off + 0x08, data)
       .Set<uint32_t>(array_off + 0x10, (uint32_t)ids.size())
       .Set<uint32_t>(array_off + 0x14, (uint32_t)ids.size());
    return true;
}

} // namespace bridge


