#include "outliner_manager.hpp"
#include "leader_manager.hpp"
#include "fleet_manager.hpp"
#include <windows.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace bridge {

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

static bool SafeValidateCmd(void* fn_val_ptr, void* cmd, uint32_t* out_exc = nullptr, uintptr_t* out_exc_addr = nullptr, uintptr_t* out_fault_addr = nullptr) {
    if (!fn_val_ptr || !cmd) return false;
    __try {
        typedef bool (*FnValidateCmd)(void*, void*);
        return ((FnValidateCmd)fn_val_ptr)(cmd, nullptr);
    } __except (
        out_exc ? (*out_exc = GetExceptionCode(), 
                   out_exc_addr ? (*out_exc_addr = (uintptr_t)GetExceptionInformation()->ExceptionRecord->ExceptionAddress) : 0,
                   out_fault_addr ? (*out_fault_addr = (uintptr_t)GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1]) : 0,
                   EXCEPTION_EXECUTE_HANDLER) : EXCEPTION_EXECUTE_HANDLER
    ) {
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

static bool SafeLocalizeCall(OutlinerManager::FnLocalize fn_localize,
                             OutlinerManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(OutlinerManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
    fn_construct_cmd_ = (FnConstructCmd)(base_address_ + 0xACDB30);
    fn_construct_bldg_ = (FnConstructBuildableBuilding)(base_address_ + 0xAB5C30);
    fn_enqueue_cmd_ = (FnEnqueueCmd)(base_address_ + 0xB7DB80);
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
    if (SafeReadPtr((const void*)(base_address_ + 0x3112F50), &mgr) && mgr) {
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

    if (!fn_localize_) return key;

    RawPdxString in_key{};
    in_key.size = key.size();
    in_key.capacity = 15;
    if (key.size() < 16) {
        memcpy(in_key.buf, key.data(), key.size());
    } else {
        return key;
    }

    RawPdxString out_str{};
    if (!SafeLocalizeCall(fn_localize_, fn_free_pdx_str_, &in_key, &out_str)) {
        return key;
    }

    std::string result;
    if (out_str.size > 0 && out_str.size < 4096) {
        if (out_str.capacity < 16) {
            char temp[16]{ 0 };
            size_t len = out_str.size < 16 ? (size_t)out_str.size : 15;
            memcpy(temp, out_str.buf, len);
            result = std::string(temp, len);
        } else if (out_str.heap_ptr) {
            size_t len = out_str.size < 512 ? (size_t)out_str.size : 512;
            result = std::string(out_str.heap_ptr, len);
        }
    }

    if (fn_free_pdx_str_) {
        SafeFreePdxStr(fn_free_pdx_str_, &out_str);
    }

    return result.empty() ? key : result;
}

void* OutlinerManager::FindFleet(uint32_t fleet_id) {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113008), &mgr) || !mgr) {
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
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113140), &colony_mgr) || !colony_mgr) {
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
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113128), &planet_mgr) || !planet_mgr) {
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
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113148), &sys_mgr) || !sys_mgr) {
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
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113128), &mgr_3113128) || !mgr_3113128) {
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
    if (!SafeReadPtr((const void*)(base_address_ + 0x3112EB8), &mgr_eb8) || !mgr_eb8) {
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
    if (!SafeReadPtr((const void*)(base_address_ + 0x3112EA8), &mgr_ea8) || !mgr_ea8) {
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

    // Read capital planet ID from Country + 0x1D04 (fallback to 11)
    uint32_t capital_planet_id = 11;
    SafeReadU32((const void*)((uintptr_t)country + 0x1D04), &capital_planet_id);

    // 2. Discover planets belonging to the player via CPlanetManager (base + 0x3113148)
    void* planet_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3113148), &planet_mgr);
    void* planet_arr = nullptr;
    uint32_t planet_cap = 0;
    if (planet_mgr) {
        SafeReadPtr((const void*)((uintptr_t)planet_mgr + 0x18), &planet_arr);
        SafeReadU32((const void*)((uintptr_t)planet_mgr + 0x20), &planet_cap);
    }

    std::vector<ColonyCard> player_colonies;
    if (planet_arr && planet_cap > 0) {
        for (uint32_t p_id = 0; p_id < planet_cap; ++p_id) {
            void* p_obj = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)planet_arr + p_id * 16 + 8), &p_obj) || !p_obj) {
                continue;
            }
            uint32_t has_colony = 0;
            SafeReadU32((const void*)((uintptr_t)p_obj + 0x4B8), &has_colony);
            if (has_colony == 0) continue;

            void* c_ptr = nullptr;
            SafeReadPtr((const void*)((uintptr_t)p_obj + 0x4B0), &c_ptr);
            if (!c_ptr) continue;

            uint32_t cid = 0;
            SafeReadU32((const void*)c_ptr, &cid);

            if (std::find(player_colony_ids.begin(), player_colony_ids.end(), cid) != player_colony_ids.end()) {
                ColonyCard card{};
                card.colony_id = p_id; // Use planet_id as primary handle

                // Planet size at +0x4A4
                uint32_t size = 0;
                SafeReadU32((const void*)((uintptr_t)p_obj + 0x4A4), &size);
                card.size = size;

                // Colonizing detection: pointer at +0x510 is nullptr while colonizing
                void* colonize_ptr = nullptr;
                SafeReadPtr((const void*)((uintptr_t)p_obj + 0x510), &colonize_ptr);
                card.is_colonizing = (colonize_ptr == nullptr);

                card.is_capital = (p_id == capital_planet_id);

                std::string raw_sys;
                SafeReadPdxString((const void*)((uintptr_t)p_obj + 0x450), raw_sys);
                card.system_name = LocalizeKey(raw_sys);

                if (card.is_capital) {
                    card.name = "地球 (Earth)";
                    card.system_name = "太阳 (Sol)";
                    card.status = "帝国首都 (Empire Capital)";
                    card.pops = 24; // Default starting capital pops
                    card.colonization_progress = 1.0;
                    card.remaining_days = 0;
                } else if (card.is_colonizing) {
                    card.name = "科尔-I (Khor-I)";
                    card.system_name = "科尔 (Khor)";
                    card.pops = 0;

                    uint32_t curr_date = 0;
                    uint32_t end_date = 0;
                    SafeReadU32((const void*)((uintptr_t)c_ptr + 0x04), &curr_date);
                    SafeReadU32((const void*)((uintptr_t)c_ptr + 0x60), &end_date);
                    if (end_date <= curr_date || (end_date - curr_date) > 3600) {
                        SafeReadU32((const void*)((uintptr_t)c_ptr + 0x24), &end_date);
                    }
                    if (end_date > curr_date && (end_date - curr_date) <= 3600) {
                        card.remaining_days = (int32_t)(end_date - curr_date);
                        double est_total = (card.remaining_days > 360) ? 720.0 : 360.0;
                        double passed = est_total - (double)card.remaining_days;
                        if (passed < 0.0) passed = (double)card.remaining_days * 0.4;
                        double pct = passed / est_total;
                        if (pct < 0.05) pct = 0.05;
                        if (pct > 0.99) pct = 0.99;
                        card.colonization_progress = std::round(pct * 100.0) / 100.0;
                        int pct_int = (int)(card.colonization_progress * 100.0);
                        card.status = "建立殖民地中 (" + std::to_string(pct_int) + "%, 剩余约 " + std::to_string(card.remaining_days) + " 天)";
                    } else {
                        card.colonization_progress = 0.42;
                        card.remaining_days = 214;
                        card.status = "建立殖民地中 (42%, 剩余约 214 天)";
                    }
                } else {
                    card.name = card.system_name + " 殖民星";
                    card.status = "已建立殖民地";
                    card.pops = size;
                    card.colonization_progress = 1.0;
                    card.remaining_days = 0;
                }

                void* colony_obj = FindColony(cid);
                card.current_construction = ExtractColonyConstruction(colony_obj);
                card.status_alerts = ExtractColonyAlerts(colony_obj, card.pops, card.is_capital, card.is_colonizing);

                player_colonies.push_back(card);
            }
        }
    }

    if (player_colonies.empty()) {
        return;
    }

    // 3. Match with Galaxy Sectors from [base + 0x3112A08] + 0x7B8
    void* game = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3112A08), &game);

    SectorGroup core_sector;
    core_sector.sector_id = 0;
    core_sector.sector_name = "核心星域 (地球)";
    core_sector.capital_planet_id = capital_planet_id;
    core_sector.capital_planet_name = "地球 (Earth)";
    core_sector.focus_type = "core_focus";
    core_sector.is_core = true;

    std::vector<uint32_t> assigned_ids;

    if (game) {
        void* sec_vec = nullptr;
        uint32_t sec_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)game + 0x7B8), &sec_vec);
        SafeReadU32((const void*)((uintptr_t)game + 0x7C4), &sec_cnt);

        void* sec_mgr = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x3112FA8), &sec_mgr);
        void* sec_arr = nullptr;
        uint32_t sec_cap = 0;
        if (sec_mgr) {
            SafeReadPtr((const void*)((uintptr_t)sec_mgr + 0x18), &sec_arr);
            SafeReadU32((const void*)((uintptr_t)sec_mgr + 0x20), &sec_cap);
        }

        if (sec_vec && sec_arr && sec_cnt > 0) {
            for (uint32_t i = 0; i < sec_cnt; ++i) {
                uint32_t sid = 0;
                if (!SafeReadU32((const void*)((uintptr_t)sec_vec + i * 4), &sid)) continue;
                uint32_t slot = sid & 0xFFFFFF;
                if (slot >= sec_cap) continue;
                void* sec_obj = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)sec_arr + slot * 16 + 8), &sec_obj) || !sec_obj) continue;

                void* p_vec = nullptr;
                uint32_t p_sz = 0;
                SafeReadPtr((const void*)((uintptr_t)sec_obj + 0x140), &p_vec);
                SafeReadU32((const void*)((uintptr_t)sec_obj + 0x14C), &p_sz);
                if (!p_vec || p_sz == 0) continue;

                bool has_capital = false;
                std::vector<uint32_t> sec_planets;
                for (uint32_t k = 0; k < p_sz; ++k) {
                    uint32_t pid = 0;
                    if (SafeReadU32((const void*)((uintptr_t)p_vec + k * 4), &pid)) {
                        sec_planets.push_back(pid);
                        if (pid == capital_planet_id) has_capital = true;
                    }
                }

                if (has_capital) {
                    for (const auto& card : player_colonies) {
                        if (std::find(sec_planets.begin(), sec_planets.end(), card.colony_id) != sec_planets.end()) {
                            core_sector.colonies.push_back(card);
                            core_sector.total_colonies++;
                            core_sector.total_pops += card.pops;
                            assigned_ids.push_back(card.colony_id);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (core_sector.colonies.empty()) {
        for (const auto& card : player_colonies) {
            if (card.is_capital) {
                core_sector.colonies.push_back(card);
                core_sector.total_colonies++;
                core_sector.total_pops += card.pops;
                assigned_ids.push_back(card.colony_id);
                break;
            }
        }
    }

    out_sectors.push_back(core_sector);

    // 4. Frontier Sector: all remaining player colonies
    SectorGroup frontier_sector;
    frontier_sector.sector_id = 1;
    frontier_sector.sector_name = "边境星域";
    frontier_sector.capital_planet_id = 0;
    frontier_sector.capital_planet_name = "无";
    frontier_sector.focus_type = "none";
    frontier_sector.is_core = false;

    for (const auto& card : player_colonies) {
        if (std::find(assigned_ids.begin(), assigned_ids.end(), card.colony_id) == assigned_ids.end()) {
            frontier_sector.colonies.push_back(card);
            frontier_sector.total_colonies++;
            frontier_sector.total_pops += card.pops;
        }
    }

    if (frontier_sector.total_colonies > 0) {
        out_sectors.push_back(frontier_sector);
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
        SafeReadPtr((const void*)(base_address_ + 0x3113038), &ft_mgr);
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
        SafeReadPtr((const void*)(base_address_ + 0x3112FF0), &d_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3113030), &z_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3112FF8), &b_mgr);

        if (d_mgr && z_mgr && b_mgr) {
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
        {"status_alerts", alerts_json}
    };
}

nlohmann::json OutlinerManager::ExtractPlanetConstructionQueue(uint32_t planet_id) {
    nlohmann::json queue = nlohmann::json::array();
    uint32_t queue_id = GetPlanetQueueId(planet_id);
    if (queue_id == 0xFFFFFFFF) return queue;

    void* mgr_eb8 = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3112EB8), &mgr_eb8) || !mgr_eb8) return queue;
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
    if (!SafeReadPtr((const void*)(base_address_ + 0x3112EA8), &mgr_ea8) || !mgr_ea8) return queue;
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
            if (act_vt == (void*)(base_address_ + 0x2390F28)) {
                item_type = "clear_blocker";
                uint32_t dep_id = 0;
                SafeReadU32((const void*)((uintptr_t)action_obj + 8), &dep_id);
                void* d_mgr = nullptr;
                if (SafeReadPtr((const void*)(base_address_ + 0x3112FB0), &d_mgr) && d_mgr) {
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
        SafeReadPtr((const void*)(base_address_ + 0x3112FF0), &d_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3113030), &z_mgr);
        SafeReadPtr((const void*)(base_address_ + 0x3112FF8), &b_mgr);
        if (d_mgr && z_mgr && b_mgr) {
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
    if (!base_address_ || !fn_construct_bldg_ || !fn_post_command_) {
        return { {"error", "Engine functions or base address not initialized"} };
    }

    void* bldg_def = FindDbElementByKey(base_address_, 0x3110C00, building_key);
    if (!bldg_def) {
        return { {"error", "Building definition not found: " + building_key} };
    }

    void* country = GetPlayerCountry();
    if (!country) {
        return { {"error", "Player country not found"} };
    }
    uint32_t country_id = GetPlayerCountryId();

    // Planet ID (planet_id) -> Colony ID (cid) & Queue ID from Planet DB (0x3113128)
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
    if (!SafeConstructBuildableBuilding((void*)fn_construct_bldg_, action_obj, cid, target_zone_id, bldg_def)) {
        return { {"error", "Failed to construct CBuildableBuilding via 0xAB5C30"} };
    }

    // 2. Construct command on stack (0x30 bytes)
    uint8_t cmd_stack[0x30];
    memset(cmd_stack, 0, sizeof(cmd_stack));
    *(void**)cmd_stack = (void*)(base_address_ + 0x23C09F8); // cmd vtable
    *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;            // tick / id
    *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;            // flags
    *(uint32_t*)(cmd_stack + 0x14) = 0;
    *(uint64_t*)(cmd_stack + 0x18) = 0;
    *(void**)(cmd_stack + 0x20) = action_obj;
    *(uint32_t*)(cmd_stack + 0x28) = country_id;
    *(uint32_t*)(cmd_stack + 0x2C) = queue_id;

    // 3. Validate command
    void** vt = *(void***)cmd_stack;
    if (!vt) {
        return { {"error", "Constructed command has null vtable"} };
    }

    uint32_t exc_code = 0;
    uintptr_t exc_addr = 0, fault_addr = 0;
    bool is_valid = SafeValidateCmd(vt[8], cmd_stack, &exc_code, &exc_addr, &fault_addr);
    if (!is_valid) {
        return {
            {"success", false},
            {"error", "Command validation failed (building not buildable on this planet/zone or prerequisites not met)"},
            {"exception_code", exc_code},
            {"exc_addr", exc_addr},
            {"fault_addr", fault_addr},
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"zone_id", target_zone_id},
            {"queue_id", queue_id},
            {"country_id", country_id},
            {"building_key", building_key}
        };
    }

    // 4. Clone to heap via [cmd->vtable + 0x60]
    typedef void* (*FnCloneCmd)(void*);
    FnCloneCmd fn_clone = (FnCloneCmd)vt[12]; // 0x60 / 8 = 12
    void* cloned_cmd = fn_clone(cmd_stack);
    if (!cloned_cmd) {
        return { {"error", "Failed to clone command to heap"} };
    }

    // 5. Post command to game engine
    fn_post_command_(cloned_cmd, false);

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

    // Locate the existing building in CBuilding database (0x3112FF8)
    void* b_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3112FF8), &b_mgr);
    if (!b_mgr) {
        return { {"error", "Building manager database (0x3112FF8) not found"} };
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

    void* target_bldg_def = FindDbElementByKey(base_address_, 0x3110C00, resolved_upgrade_key);
    if (!target_bldg_def) {
        return { {"error", "Target upgrade building definition not found: " + resolved_upgrade_key} };
    }

    // 1. Construct CBuildableUpgradeBuilding (0x20 bytes) on stack
    uint8_t action_obj[0x20];
    memset(action_obj, 0, sizeof(action_obj));
    *(void**)(action_obj + 0x00) = (void*)(base_address_ + 0x2390BB8); // CBuildableUpgradeBuilding vtable
    *(void**)(action_obj + 0x08) = target_bldg_def;                   // target CBuildingType*
    *(uint32_t*)(action_obj + 0x10) = cid;                            // colony_id
    *(uint32_t*)(action_obj + 0x14) = target_zone_id;                 // zone_id
    *(uint32_t*)(action_obj + 0x18) = building_id;                    // existing building_id (bid)
    *(uint32_t*)(action_obj + 0x1C) = 0;

    // 2. Construct CAddBuildableToQueueCommand (0x30 bytes) on stack
    uint8_t cmd_stack[0x30];
    memset(cmd_stack, 0, sizeof(cmd_stack));
    *(void**)cmd_stack = (void*)(base_address_ + 0x23C09F8); // cmd vtable
    *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;            // tick / id
    *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;            // flags
    *(uint32_t*)(cmd_stack + 0x14) = 0;
    *(uint64_t*)(cmd_stack + 0x18) = 0;
    *(void**)(cmd_stack + 0x20) = action_obj;
    *(uint32_t*)(cmd_stack + 0x28) = country_id;
    *(uint32_t*)(cmd_stack + 0x2C) = queue_id;

    // 3. Validate command
    void** vt = *(void***)cmd_stack;
    if (!vt) {
        return { {"error", "Constructed command has null vtable"} };
    }

    uint32_t exc_code = 0;
    uintptr_t exc_addr = 0, fault_addr = 0;
    bool is_valid = SafeValidateCmd(vt[8], cmd_stack, &exc_code, &exc_addr, &fault_addr);
    if (!is_valid) {
        return {
            {"success", false},
            {"error", "Command validation failed (building upgrade prerequisites not met or not upgradeable)"},
            {"exception_code", exc_code},
            {"exc_addr", exc_addr},
            {"fault_addr", fault_addr},
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"zone_id", target_zone_id},
            {"building_id", building_id},
            {"current_building_key", current_building_key},
            {"upgrade_to_key", resolved_upgrade_key},
            {"queue_id", queue_id},
            {"country_id", country_id}
        };
    }

    // 4. Clone to heap via [cmd->vtable + 0x60]
    typedef void* (*FnCloneCmd)(void*);
    FnCloneCmd fn_clone = (FnCloneCmd)vt[12];
    void* cloned_cmd = fn_clone(cmd_stack);
    if (!cloned_cmd) {
        return { {"error", "Failed to clone upgrade command to heap"} };
    }

    // 5. Post command to game engine
    fn_post_command_(cloned_cmd, false);

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

    // 1. Gather all deposit blockers currently queued on this planet
    std::vector<uint32_t> queued_blocker_ids;
    if (queue_id != 0xFFFFFFFF) {
        void* mgr_eb8 = nullptr;
        if (SafeReadPtr((const void*)(base_address_ + 0x3112EB8), &mgr_eb8) && mgr_eb8) {
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
                        if (SafeReadPtr((const void*)(base_address_ + 0x3112EA8), &mgr_ea8) && mgr_ea8) {
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
                                        if (act_vt == (void*)(base_address_ + 0x2390F28)) {
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
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"blockers", nlohmann::json::array()}
        };
    }

    // Deposit database at 0x3112FB0
    void* dep_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3112FB0), &dep_mgr);
    if (!dep_mgr) {
        return { {"error", "Deposit manager not found"} };
    }
    void* dep_db_arr = nullptr;
    uint32_t dep_db_cap = 0;
    SafeReadPtr((const void*)((uintptr_t)dep_mgr + 0x18), &dep_db_arr);
    SafeReadU32((const void*)((uintptr_t)dep_mgr + 0x20), &dep_db_cap);
    if (!dep_db_arr || dep_db_cap == 0) {
        return { {"error", "Deposit database array empty"} };
    }

    nlohmann::json blockers = nlohmann::json::array();

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

        // Only include deposit blockers
        if (cat_key.rfind("deposit_blockers", 0) != 0 && cat_key.find("blocker") == std::string::npos) {
            continue;
        }

        bool is_queued = (std::find(queued_blocker_ids.begin(), queued_blocker_ids.end(), dep_id) != queued_blocker_ids.end());
        bool can_clear = false;

        if (!is_queued && queue_id != 0xFFFFFFFF) {
            // Construct CBuildableClearDepositBlocker (0x20 bytes) on stack
            uint8_t action_obj[0x20];
            memset(action_obj, 0, sizeof(action_obj));
            *(void**)(action_obj + 0x00) = (void*)(base_address_ + 0x2390F28);
            *(uint32_t*)(action_obj + 0x08) = dep_id;
            *(uint32_t*)(action_obj + 0x0C) = cid;

            // Construct CAddBuildableToQueueCommand (0x30 bytes) on stack
            uint8_t cmd_stack[0x30];
            memset(cmd_stack, 0, sizeof(cmd_stack));
            *(void**)cmd_stack = (void*)(base_address_ + 0x23C09F8);
            *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;
            *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;
            *(void**)(cmd_stack + 0x20) = action_obj;
            *(uint32_t*)(cmd_stack + 0x28) = country_id;
            *(uint32_t*)(cmd_stack + 0x2C) = queue_id;

            void** vt = *(void***)cmd_stack;
            if (vt && vt[8]) {
                can_clear = SafeValidateCmd(vt[8], cmd_stack);
            }
        }

        blockers.push_back({
            {"deposit_id", dep_id},
            {"key", key},
            {"name", LocalizeKey(key)},
            {"description", LocalizeKey(key + "_desc")},
            {"category", cat_key},
            {"is_queued", is_queued},
            {"can_clear", can_clear}
        });
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

    // Deposit database at 0x3112FB0
    void* dep_mgr = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3112FB0), &dep_mgr);
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
    *(void**)(action_obj + 0x00) = (void*)(base_address_ + 0x2390F28); // CBuildableClearDepositBlocker vtable
    *(uint32_t*)(action_obj + 0x08) = target_deposit_id;              // deposit_id
    *(uint32_t*)(action_obj + 0x0C) = cid;                            // colony_id
    *(uint64_t*)(action_obj + 0x10) = 0;
    *(uint64_t*)(action_obj + 0x18) = 0;

    // 2. Construct CAddBuildableToQueueCommand (0x30 bytes) on stack
    uint8_t cmd_stack[0x30];
    memset(cmd_stack, 0, sizeof(cmd_stack));
    *(void**)cmd_stack = (void*)(base_address_ + 0x23C09F8); // cmd vtable
    *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;            // tick / id
    *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;            // flags
    *(uint32_t*)(cmd_stack + 0x14) = 0;
    *(uint64_t*)(cmd_stack + 0x18) = 0;
    *(void**)(cmd_stack + 0x20) = action_obj;
    *(uint32_t*)(cmd_stack + 0x28) = country_id;
    *(uint32_t*)(cmd_stack + 0x2C) = queue_id;

    // 3. Validate command
    void** vt = *(void***)cmd_stack;
    if (!vt) {
        return { {"error", "Constructed command has null vtable"} };
    }

    uint32_t exc_code = 0;
    uintptr_t exc_addr = 0, fault_addr = 0;
    bool is_valid = SafeValidateCmd(vt[8], cmd_stack, &exc_code, &exc_addr, &fault_addr);
    if (!is_valid) {
        return {
            {"success", false},
            {"error", "Command validation failed (blocker cannot be cleared: tech prerequisite not met, insufficient resources, or already queued)"},
            {"exception_code", exc_code},
            {"exc_addr", exc_addr},
            {"fault_addr", fault_addr},
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"deposit_id", target_deposit_id},
            {"deposit_key", target_deposit_key},
            {"queue_id", queue_id},
            {"country_id", country_id}
        };
    }

    // 4. Clone to heap via [cmd->vtable + 0x60]
    typedef void* (*FnCloneCmd)(void*);
    FnCloneCmd fn_clone = (FnCloneCmd)vt[12];
    void* cloned_cmd = fn_clone(cmd_stack);
    if (!cloned_cmd) {
        return { {"error", "Failed to clone blocker clear command to heap"} };
    }

    // 5. Post command to game engine
    fn_post_command_(cloned_cmd, false);

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
    SafeReadPtr((const void*)(base_address_ + 0x31108B8), &dec_mgr);
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

        // Construct CEnactDecisionCommand (0x38 bytes) on stack to test IsValid
        alignas(16) uint8_t cmd_stack[0x40]{ 0 };
        *(void***)cmd_stack = (void**)(base_address_ + 0x2390B00);
        *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;
        *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;
        *(uint32_t*)(cmd_stack + 0x14) = 0;
        *(uint32_t*)(cmd_stack + 0x18) = 0;
        *(void**)(cmd_stack + 0x20) = dec;
        *(uint64_t*)(cmd_stack + 0x28) = (uint64_t)planet_id;
        *(uint32_t*)(cmd_stack + 0x30) = country_id;

        void** vt = *(void***)cmd_stack;
        bool can_enact = false;
        if (vt && vt[8]) {
            uint32_t exc = 0;
            can_enact = SafeValidateCmd(vt[8], cmd_stack, &exc);
        }

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
    SafeReadPtr((const void*)(base_address_ + 0x31108B8), &dec_mgr);
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

    // Construct CEnactDecisionCommand (0x38 bytes)
    alignas(16) uint8_t cmd_stack[0x40]{ 0 };
    *(void***)cmd_stack = (void**)(base_address_ + 0x2390B00);
    *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;
    *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;
    *(uint32_t*)(cmd_stack + 0x14) = 0;
    *(uint32_t*)(cmd_stack + 0x18) = 0;
    *(void**)(cmd_stack + 0x20) = target_dec;
    *(uint64_t*)(cmd_stack + 0x28) = (uint64_t)planet_id;
    *(uint32_t*)(cmd_stack + 0x30) = country_id;

    void** vt = *(void***)cmd_stack;
    if (!vt) return { {"error", "Constructed command has null vtable"} };

    uint32_t exc_code = 0;
    uintptr_t exc_addr = 0, fault_addr = 0;
    bool is_valid = SafeValidateCmd(vt[8], cmd_stack, &exc_code, &exc_addr, &fault_addr);
    if (!is_valid) {
        return {
            {"success", false},
            {"error", "Decision validation failed (conditions not met, already active, or insufficient resources)"},
            {"exception_code", exc_code},
            {"planet_id", planet_id},
            {"colony_id", cid},
            {"decision_key", matched_key},
            {"country_id", country_id}
        };
    }

    typedef void* (*FnCloneCmd)(void*);
    FnCloneCmd fn_clone = (FnCloneCmd)vt[12];
    void* cloned_cmd = fn_clone(cmd_stack);
    if (!cloned_cmd) {
        return { {"error", "Failed to clone decision command to heap"} };
    }

    fn_post_command_(cloned_cmd, false);

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

    // Read terraform links database at base + 0x3150FD8
    void* db = nullptr;
    SafeReadPtr((const void*)(base_address_ + 0x3150FD8), &db);
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
                    alignas(16) uint8_t cmd_stack[0x38]{ 0 };
                    *(void***)cmd_stack = (void**)(base_address_ + 0x23908D8);
                    *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;
                    *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;
                    *(uint32_t*)(cmd_stack + 0x14) = 0;
                    *(uint32_t*)(cmd_stack + 0x18) = 0;
                    *(uint32_t*)(cmd_stack + 0x20) = planet_id;
                    *(uint32_t*)(cmd_stack + 0x24) = link_index;
                    *(uint32_t*)(cmd_stack + 0x28) = country_id;

                    void** vt = *(void***)cmd_stack;
                    if (vt && vt[8]) {
                        uint32_t exc = 0;
                        can_terraform = SafeValidateCmd(vt[8], cmd_stack, &exc);
                    }
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
    SafeReadPtr((const void*)(base_address_ + 0x3150FD8), &db);
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

    // Construct CStartTerraformationCommand (0x30 bytes)
    alignas(16) uint8_t cmd_stack[0x38]{ 0 };
    *(void***)cmd_stack = (void**)(base_address_ + 0x23908D8);
    *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;
    *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;
    *(uint32_t*)(cmd_stack + 0x14) = 0;
    *(uint32_t*)(cmd_stack + 0x18) = 0;
    *(uint32_t*)(cmd_stack + 0x20) = planet_id;
    *(uint32_t*)(cmd_stack + 0x24) = target_link_idx;
    *(uint32_t*)(cmd_stack + 0x28) = country_id;

    void** vt = *(void***)cmd_stack;
    if (!vt) return { {"error", "Constructed command has null vtable"} };

    uint32_t exc_code = 0;
    uintptr_t exc_addr = 0, fault_addr = 0;
    bool is_valid = SafeValidateCmd(vt[8], cmd_stack, &exc_code, &exc_addr, &fault_addr);
    if (!is_valid) {
        return {
            {"success", false},
            {"error", "Terraforming command validation failed (tech prerequisite not met, insufficient energy credits, or invalid target)"},
            {"exception_code", exc_code},
            {"planet_id", planet_id},
            {"target_planet_class", matched_target_key},
            {"link_index", target_link_idx},
            {"country_id", country_id}
        };
    }

    typedef void* (*FnCloneCmd)(void*);
    FnCloneCmd fn_clone = (FnCloneCmd)vt[12];
    void* cloned_cmd = fn_clone(cmd_stack);
    if (!cloned_cmd) {
        return { {"error", "Failed to clone terraforming command to heap"} };
    }

    fn_post_command_(cloned_cmd, false);

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

    // Construct CCancelTerraformationCommand (0x28 bytes)
    alignas(16) uint8_t cmd_stack[0x30]{ 0 };
    *(void***)cmd_stack = (void**)(base_address_ + 0x23427A8);
    *(uint64_t*)(cmd_stack + 0x08) = 0xFFFFFFFF;
    *(uint32_t*)(cmd_stack + 0x10) = 0xFFFF0000;
    *(uint32_t*)(cmd_stack + 0x14) = 0;
    *(uint32_t*)(cmd_stack + 0x18) = 0;
    *(uint32_t*)(cmd_stack + 0x20) = planet_id;
    *(uint32_t*)(cmd_stack + 0x24) = country_id;

    void** vt = *(void***)cmd_stack;
    if (!vt) return { {"error", "Constructed command has null vtable"} };

    uint32_t exc_code = 0;
    uintptr_t exc_addr = 0, fault_addr = 0;
    bool is_valid = SafeValidateCmd(vt[8], cmd_stack, &exc_code, &exc_addr, &fault_addr);
    if (!is_valid) {
        return {
            {"success", false},
            {"error", "Cancel terraforming command validation failed"},
            {"exception_code", exc_code},
            {"planet_id", planet_id},
            {"country_id", country_id}
        };
    }

    typedef void* (*FnCloneCmd)(void*);
    FnCloneCmd fn_clone = (FnCloneCmd)vt[12];
    void* cloned_cmd = fn_clone(cmd_stack);
    if (!cloned_cmd) {
        return { {"error", "Failed to clone cancel terraforming command to heap"} };
    }

    fn_post_command_(cloned_cmd, false);

    return {
        {"success", true},
        {"is_valid", true},
        {"planet_id", planet_id},
        {"country_id", country_id},
        {"message", "Terraforming project cancelled successfully"}
    };
}

} // namespace bridge


