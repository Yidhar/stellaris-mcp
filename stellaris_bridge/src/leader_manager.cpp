#include "leader_manager.hpp"
#include "alert_manager.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <sstream>

namespace bridge {

#pragma pack(push, 1)
struct RawPdxString {
    union {
        char buf[16];
        char* heap_ptr;
    };
    uint64_t size;
    uint64_t capacity;
};
#pragma pack(pop)

// Safe Memory Access Helpers with pure C / SEH protection
static bool SafeReadPtr(const void* src, void** dest) {
    __try {
        *dest = *(void**)src;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *dest = nullptr;
        return false;
    }
}

static bool SafeReadU32(const void* src, uint32_t* dest) {
    __try {
        *dest = *(const uint32_t*)src;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *dest = 0;
        return false;
    }
}

static bool SafeReadU8(const void* src, uint8_t* dest) {
    __try {
        *dest = *(const uint8_t*)src;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *dest = 0;
        return false;
    }
}

static bool SafeReadI64(const void* src, int64_t* dest) {
    if (!src || !dest) return false;
    __try {
        *dest = *(const int64_t*)src;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *dest = 0;
        return false;
    }
}

static bool SafeGetLocalizedLeaderNameCall(LeaderManager::FnGetLocalizedLeaderName fn, RawPdxString* out_name, void* name_obj) {
    if (!fn || !out_name || !name_obj) return false;
    __try {
        fn(out_name, name_obj, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadPdxStringRaw(const void* src, char* out_buf, size_t max_len) {
    __try {
        const RawPdxString* pdx = (const RawPdxString*)src;
        if (pdx->size == 0 || pdx->size > 1024) {
            out_buf[0] = '\0';
            return false;
        }
        if (pdx->capacity < 16) {
            size_t len = pdx->size < max_len ? (size_t)pdx->size : (max_len - 1);
            memcpy(out_buf, pdx->buf, len);
            out_buf[len] = '\0';
        } else if (pdx->heap_ptr) {
            size_t len = pdx->size < max_len ? (size_t)pdx->size : (max_len - 1);
            memcpy(out_buf, pdx->heap_ptr, len);
            out_buf[len] = '\0';
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out_buf[0] = '\0';
        return false;
    }
}

static bool SafeReadPdxString(const void* src, std::string& out) {
    char buf[512] = { 0 };
    if (SafeReadPdxStringRaw(src, buf, sizeof(buf))) {
        out = buf;
        return true;
    }
    out = "";
    return false;
}

static bool SafeLocalizeCall(LeaderManager::FnLocalize fn_localize, LeaderManager::FnFreePdxStr fn_free,
    const RawPdxString* in_key, RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(LeaderManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SafePostCommand(LeaderManager::FnPostCommand fn_post, void* cmd) {
    __try {
        fn_post(cmd, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

LeaderManager& LeaderManager::Get() {
    static LeaderManager instance;
    return instance;
}

bool LeaderManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_get_localized_leader_name_ = (FnGetLocalizedLeaderName)(base_address_ + 0x3E8E20);

    // Native Command Vtables
    hire_leader_cmd_vtable_ = base_address_ + 0x2393840;
    fire_leader_cmd_vtable_ = base_address_ + 0x23938F8;
    assign_leader_cmd_vtable_ = base_address_ + 0x23C9A68;

    LOGF("[LEADER] Initialized (Base: 0x%llX, HireVT: 0x%llX, FireVT: 0x%llX, AssignVT: 0x%llX)",
        (unsigned long long)base_address_,
        (unsigned long long)hire_leader_cmd_vtable_,
        (unsigned long long)fire_leader_cmd_vtable_,
        (unsigned long long)assign_leader_cmd_vtable_);

    return fn_engine_alloc_ != nullptr && fn_post_command_ != nullptr;
}

void* LeaderManager::GetPlayerCountry() {
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

uint32_t LeaderManager::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;

    uint32_t cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid);
    return cid;
}

std::string LeaderManager::LocalizeKey(const std::string& key) {
    if (key.empty()) return key;

    static const std::unordered_map<std::string, std::string> kStaticLocMap = {
        {"trait_ruler_fertility_preacher", "重视农耕"},
        {"trait_ruler_fertility_preacher_2", "重视农耕 II"},
        {"leader_trait_lawless", "法外之徒"},
        {"leader_trait_lawless_2", "法外之徒 II"},
        {"trait_ruler_warlike", "好战者"},
        {"trait_ruler_warlike_2", "好战者 II"},
        {"trait_ruler_eye_for_talent", "慧眼识珠"},
        {"leader_trait_resilient", "坚韧不拔"},
        {"leader_trait_adaptable", "适应力强"},
        {"leader_trait_archaeologist", "考古学家"},
        {"leader_trait_homesteader", "自耕农"},
        {"leader_trait_homesteader_2", "自耕农 II"},
        {"PRESCRIPTED_ruler_name_humans1", "多洛雷丝·穆万加"},
        {"councilor_state", "国务卿"},
        {"councilor_defense", "国防部长"},
        {"councilor_research", "科技部长"},
        {"councilor_ruler_democratic", "总统"},
        {"HUMAN1_CHR_Juan", "娟"},
        {"HUMAN1_CHR_Zhang", "张"},
        {"HUMAN1_CHR_Fang", "芳"},
        {"HUMAN1_CHR_Mao", "毛"}
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

void* LeaderManager::FindLeaderPtr(uint32_t leader_id) {
    if (!base_address_ || leader_id == 0 || leader_id == 0xFFFFFFFF) return nullptr;

    void* leader_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113120), &leader_mgr) || !leader_mgr) {
        return nullptr;
    }

    void* leader_tbl = nullptr;
    uint32_t leader_cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)leader_mgr + 0x18), &leader_tbl) || !leader_tbl ||
        !SafeReadU32((const void*)((uintptr_t)leader_mgr + 0x20), &leader_cap) || leader_cap == 0) {
        return nullptr;
    }

    // Direct slot index check (low 16 bits)
    uint32_t direct_slot = leader_id & 0xFFFF;
    if (direct_slot < leader_cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)leader_tbl + direct_slot * 16 + 8), &ptr) && ptr) {
            uint32_t check_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)ptr + 0x20), &check_id) && check_id == leader_id) {
                return ptr;
            }
        }
    }

    // Linear scan fallback
    for (uint32_t i = 0; i < leader_cap && i < 2048; ++i) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)leader_tbl + i * 16 + 8), &ptr) && ptr) {
            uint32_t check_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)ptr + 0x20), &check_id) && check_id == leader_id) {
                return ptr;
            }
        }
    }

    return nullptr;
}

static bool HasNonAscii(const std::string& str) {
    for (unsigned char c : str) {
        if (c >= 0x80) return true;
    }
    return false;
}

HiredLeaderDetail LeaderManager::ReadLeader(uint32_t leader_id) {
    HiredLeaderDetail detail{};
    detail.id = leader_id;

    void* leader = FindLeaderPtr(leader_id);
    if (!leader) return detail;

    SafeReadPdxString((const void*)((uintptr_t)leader + 0x50), detail.key);

    // 1. Resolve Localized Leader Name
    void* name_obj = (void*)((uintptr_t)leader + 0x38);
    if (fn_get_localized_leader_name_) {
        RawPdxString out_name{};
        if (SafeGetLocalizedLeaderNameCall(fn_get_localized_leader_name_, &out_name, name_obj)) {
            if (out_name.size > 0 && out_name.size < 256) {
                if (out_name.capacity < 16) {
                    char tmp[16]{ 0 };
                    memcpy(tmp, out_name.buf, out_name.size);
                    detail.name = std::string(tmp, out_name.size);
                } else if (out_name.heap_ptr) {
                    detail.name = std::string(out_name.heap_ptr, out_name.size);
                }
            }
            if (fn_free_pdx_str_) {
                SafeFreePdxStr(fn_free_pdx_str_, &out_name);
            }
        }
    }

    // Fallback: decode CPersistentName variable array
    if (detail.name.empty() || detail.name == detail.key) {
        void* var_arr = nullptr;
        uint32_t var_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)name_obj + 0x48), &var_arr) && var_arr &&
            SafeReadU32((const void*)((uintptr_t)name_obj + 0x54), &var_cnt) && var_cnt > 0) {
            std::string var1, var2;
            for (uint32_t v = 0; v < var_cnt && v < 4; ++v) {
                void* v_item = (void*)((uintptr_t)var_arr + v * 0x40);
                void* val_ptr = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)v_item + 0x38), &val_ptr) && val_ptr) {
                    std::string v_key;
                    if (SafeReadPdxString((const void*)((uintptr_t)val_ptr + 0x18), v_key)) {
                        std::string part = LocalizeKey(v_key);
                        if (part.empty() || part == v_key) {
                            if (v_key.rfind("HUMAN1_CHR_", 0) == 0) part = v_key.substr(11);
                            else if (v_key.rfind("HUMAN2_CHR_", 0) == 0) part = v_key.substr(11);
                            else if (v_key.rfind("NAME_", 0) == 0) part = v_key.substr(5);
                            else part = v_key;
                        }
                        if (v == 0) var1 = part;
                        else if (v == 1) var2 = part;
                    }
                }
            }
            if (!var1.empty() && !var2.empty()) {
                if (HasNonAscii(var1) || HasNonAscii(var2)) {
                    detail.name = var2 + var1;
                } else {
                    detail.name = var1 + " " + var2;
                }
            } else if (!var1.empty()) {
                detail.name = var1;
            }
        }
    }
    if (detail.name.empty()) {
        detail.name = LocalizeKey(detail.key);
    }

    // 2. Class & Subclass
    void* class_ptr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)leader + 0xE0), &class_ptr) && class_ptr) {
        SafeReadPdxString((const void*)((uintptr_t)class_ptr + 0x20), detail.class_key);
        detail.class_name = LocalizeKey(detail.class_key);
    }

    void* subclass_ptr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)leader + 0x6D0), &subclass_ptr) && subclass_ptr) {
        SafeReadPdxString((const void*)((uintptr_t)subclass_ptr + 0x20), detail.subclass_key);
        detail.subclass_name = LocalizeKey(detail.subclass_key);
    }

    // 3. Level (+0x9D8), Experience (+0xF0), Age (+0x108)
    SafeReadU32((const void*)((uintptr_t)leader + 0x9D8), &detail.level);
    int64_t raw_xp = 0;
    if (SafeReadI64((const void*)((uintptr_t)leader + 0xF0), &raw_xp)) {
        detail.experience = std::round((double)raw_xp / 1000.0) / 100.0;
    }
    SafeReadU32((const void*)((uintptr_t)leader + 0x108), &detail.age);

    void* ethic_ptr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)leader + 0x6D8), &ethic_ptr) && ethic_ptr) {
        SafeReadPdxString((const void*)((uintptr_t)ethic_ptr + 0x20), detail.ethic_key);
        detail.ethic_name = LocalizeKey(detail.ethic_key);
    }

    // 4. Assignment & Council
    SafeReadU8((const void*)((uintptr_t)leader + 0x110), &detail.assignment_type);
    SafeReadU32((const void*)((uintptr_t)leader + 0x118), &detail.assignment_target);
    SafeReadU32((const void*)((uintptr_t)leader + 0x9E0), &detail.hire_date);

    uint32_t assign_upper = 0;
    SafeReadU32((const void*)((uintptr_t)leader + 0x114), &assign_upper);
    detail.is_councilor = (assign_upper != 0);

    void* country = GetPlayerCountry();
    uint32_t ruler_id = 0;
    if (country) {
        SafeReadU32((const void*)((uintptr_t)country + 0x1BF0), &ruler_id);
    }

    if (leader_id == ruler_id) {
        detail.assignment_type_name = "ruler";
        detail.title = "总统";
        detail.is_councilor = true;
    } else {
        switch (detail.assignment_type) {
            case 0:
                detail.assignment_type_name = "unassigned";
                detail.title = "未指派";
                break;
            case 1:
                detail.assignment_type_name = "governor";
                detail.title = detail.is_councilor ? "国务卿" : "星区总督";
                break;
            case 2:
                detail.assignment_type_name = "fleet";
                detail.title = (detail.class_key == "scientist") ? "首席科学家" : "舰队司令";
                break;
            case 3:
                detail.assignment_type_name = "army";
                detail.title = "陆军将领";
                break;
            case 6:
                detail.assignment_type_name = "council";
                detail.title = "内阁官员";
                detail.is_councilor = true;
                break;
            case 8:
                detail.assignment_type_name = "envoy";
                detail.title = "特使";
                break;
            default:
                detail.assignment_type_name = "unknown";
                detail.title = "领袖";
                break;
        }
    }

    // 5. Traits
    void* t_arr = nullptr;
    uint32_t t_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)leader + 0x9B0), &t_arr) && t_arr &&
        SafeReadU32((const void*)((uintptr_t)leader + 0x9B8), &t_cnt) && t_cnt > 0) {
        for (uint32_t t = 0; t < t_cnt && t < 16; ++t) {
            void* t_obj = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)t_arr + t * 8), &t_obj) && t_obj) {
                std::string t_key;
                if (SafeReadPdxString((const void*)((uintptr_t)t_obj + 0x148), t_key) && !t_key.empty()) {
                    LeaderTraitDetail td;
                    td.key = t_key;
                    td.name = LocalizeKey(t_key);
                    if (t_key.rfind("_3") != std::string::npos) td.tier = 3;
                    else if (t_key.rfind("_2") != std::string::npos) td.tier = 2;
                    else td.tier = 1;
                    detail.traits.push_back(td);
                }
            }
        }
    }

    // 6. Unspent Trait Points Indicator ("+")
    if (detail.level > 1 && detail.class_key != "envoy") {
        if (detail.traits.size() < detail.level) {
            detail.has_unspent_trait_points = true;
        }
    }

    return detail;
}

LeaderSummary LeaderManager::GetSummary() {
    LeaderSummary summary{};
    void* country = GetPlayerCountry();
    if (!country) return summary;

    // 1. Total hired leaders from [Country + 0x275C]
    SafeReadU32((const void*)((uintptr_t)country + 0x275C), &summary.total_hired);

    // 2. Leader capacity & pool count from CCountryLeaderManager [Country + 0x2B48]
    void* c_leader_mgr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x2B48), &c_leader_mgr) && c_leader_mgr) {
        SafeReadU32((const void*)((uintptr_t)c_leader_mgr + 0x30), &summary.pool_count);
        SafeReadU32((const void*)((uintptr_t)c_leader_mgr + 0x50), &summary.leader_capacity);
    }

    // 3. Check for unspent leader trait points via Alert 40
    const auto& alerts = AlertManager::Get().GetAlerts();
    for (const auto& a : alerts) {
        if (a.alert_id == 40 || a.type == "alert_unspent_leader_trait_points") {
            summary.has_unspent_trait_points = true;
            break;
        }
    }

    return summary;
}

nlohmann::json LeaderManager::GetSummaryJson() {
    LeaderSummary s = GetSummary();
    return {
        {"total_hired", s.total_hired},
        {"leader_capacity", s.leader_capacity},
        {"pool_count", s.pool_count},
        {"has_unspent_trait_points", s.has_unspent_trait_points}
    };
}

nlohmann::json LeaderManager::GetLeadersJson() {
    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"error", {{"code", -32070}, {"message", "Player country not found"}}}
        };
    }

    nlohmann::json result;
    result["summary"] = GetSummaryJson();

    // 1. Hired leaders from [Country + 0x2750]
    void* l_arr = nullptr;
    uint32_t cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x2750), &l_arr);
    SafeReadU32((const void*)((uintptr_t)country + 0x275C), &cnt);

    nlohmann::json hired_list = nlohmann::json::array();
    if (l_arr && cnt > 0) {
        for (uint32_t i = 0; i < cnt && i < 256; ++i) {
            uint32_t lid = 0;
            if (SafeReadU32((const void*)((uintptr_t)l_arr + i * 4), &lid) && lid > 0) {
                HiredLeaderDetail d = ReadLeader(lid);
                nlohmann::json traits_arr = nlohmann::json::array();
                for (const auto& t : d.traits) {
                    traits_arr.push_back({
                        {"key", t.key},
                        {"name", t.name},
                        {"tier", t.tier}
                    });
                }
                hired_list.push_back({
                    {"id", d.id},
                    {"name", d.name.empty() ? d.key : d.name},
                    {"name_key", d.key},
                    {"title", d.title},
                    {"class", d.class_key},
                    {"class_name", d.class_name},
                    {"subclass", d.subclass_key},
                    {"subclass_name", d.subclass_name},
                    {"level", d.level},
                    {"experience", d.experience},
                    {"age", d.age},
                    {"ethic", d.ethic_key},
                    {"ethic_name", d.ethic_name},
                    {"assignment_type", d.assignment_type_name},
                    {"target_id", d.assignment_target},
                    {"hire_date", d.hire_date},
                    {"is_councilor", d.is_councilor},
                    {"has_unspent_trait_points", d.has_unspent_trait_points},
                    {"traits", traits_arr}
                });
            }
        }
    }
    result["hired_leaders"] = hired_list;

    // 2. Candidate pool from CCountryLeaderManager [Country + 0x2B48] + 0x28
    nlohmann::json pool_list = nlohmann::json::array();
    void* c_leader_mgr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x2B48), &c_leader_mgr) && c_leader_mgr) {
        void* pool_arr = nullptr;
        uint32_t pool_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)c_leader_mgr + 0x28), &pool_arr);
        SafeReadU32((const void*)((uintptr_t)c_leader_mgr + 0x30), &pool_cnt);

        if (pool_arr && pool_cnt > 0) {
            for (uint32_t i = 0; i < pool_cnt && i < 64; ++i) {
                uint32_t cid = 0;
                if (SafeReadU32((const void*)((uintptr_t)pool_arr + i * 4), &cid) && cid > 0) {
                    HiredLeaderDetail d = ReadLeader(cid);
                    if (!d.class_key.empty()) {
                        pool_list.push_back({
                            {"id", d.id},
                            {"name", d.name.empty() ? d.key : d.name},
                            {"name_key", d.key},
                            {"class", d.class_key},
                            {"class_name", d.class_name},
                            {"level", d.level},
                            {"age", d.age},
                            {"hire_cost", 200.0},
                            {"ethic", d.ethic_key},
                            {"ethic_name", d.ethic_name}
                        });
                    }
                }
            }
        }
    }
    result["pool_candidates"] = pool_list;

    return result;
}

nlohmann::json LeaderManager::HireLeader(uint32_t candidate_id) {
    if (!fn_engine_alloc_ || !fn_post_command_ || !hire_leader_cmd_vtable_) {
        return {
            {"error", {{"code", -32071}, {"message", "Native command dispatch functions not initialized"}}}
        };
    }

    void* leader = FindLeaderPtr(candidate_id);
    if (!leader) {
        return {
            {"error", {{"code", -32072}, {"message", "Candidate leader ID " + std::to_string(candidate_id) + " not found"}}}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &date_mgr) && date_mgr) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[LEADER] Posting CHireLeaderCommand (0x4073) for candidate %u, country %u, tick %u...",
        candidate_id, country_id, tick_timestamp);

    void* cmd = fn_engine_alloc_(0x28);
    if (!cmd) {
        return {
            {"error", {{"code", -32073}, {"message", "Engine allocator returned null for CHireLeaderCommand"}}}
        };
    }

    memset(cmd, 0, 0x28);
    *(void**)cmd = (void*)hire_leader_cmd_vtable_;
    *(uint32_t*)((uintptr_t)cmd + 0x08) = tick_timestamp;
    *(uint8_t*)((uintptr_t)cmd + 0x14) = 1; // satisfies IsValid()
    *(uint32_t*)((uintptr_t)cmd + 0x20) = country_id;
    *(uint32_t*)((uintptr_t)cmd + 0x24) = candidate_id;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[LEADER] Exception occurred executing PostCommand for CHireLeaderCommand!");
        return {
            {"error", {{"code", -32074}, {"message", "Exception occurred executing PostCommand for CHireLeaderCommand"}}}
        };
    }

    HiredLeaderDetail hired = ReadLeader(candidate_id);
    LOGF("[LEADER] CHireLeaderCommand successfully posted for %u.", candidate_id);

    return {
        {"success", true},
        {"candidate_id", candidate_id},
        {"name", hired.name.empty() ? hired.key : hired.name},
        {"class", hired.class_key},
        {"message", "Leader hired successfully"}
    };
}

nlohmann::json LeaderManager::DismissLeader(uint32_t leader_id) {
    if (!fn_engine_alloc_ || !fn_post_command_ || !fire_leader_cmd_vtable_) {
        return {
            {"error", {{"code", -32075}, {"message", "Native command dispatch functions not initialized"}}}
        };
    }

    void* leader = FindLeaderPtr(leader_id);
    if (!leader) {
        return {
            {"error", {{"code", -32076}, {"message", "Leader ID " + std::to_string(leader_id) + " not found"}}}
        };
    }

    void* country = GetPlayerCountry();
    uint32_t ruler_id = 0;
    if (country) {
        SafeReadU32((const void*)((uintptr_t)country + 0x1BF0), &ruler_id);
    }
    if (leader_id == ruler_id) {
        return {
            {"error", {{"code", -32077}, {"message", "Cannot dismiss the ruler of the empire"}}}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &date_mgr) && date_mgr) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[LEADER] Posting CFireLeaderCommand (0x2EB3) for leader %u, country %u, tick %u...",
        leader_id, country_id, tick_timestamp);

    void* cmd = fn_engine_alloc_(0x28);
    if (!cmd) {
        return {
            {"error", {{"code", -32078}, {"message", "Engine allocator returned null for CFireLeaderCommand"}}}
        };
    }

    memset(cmd, 0, 0x28);
    *(void**)cmd = (void*)fire_leader_cmd_vtable_;
    *(uint32_t*)((uintptr_t)cmd + 0x08) = tick_timestamp;
    *(uint8_t*)((uintptr_t)cmd + 0x14) = 1; // satisfies IsValid()
    *(uint32_t*)((uintptr_t)cmd + 0x20) = country_id;
    *(uint32_t*)((uintptr_t)cmd + 0x24) = leader_id;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[LEADER] Exception occurred executing PostCommand for CFireLeaderCommand!");
        return {
            {"error", {{"code", -32079}, {"message", "Exception occurred executing PostCommand for CFireLeaderCommand"}}}
        };
    }

    LOGF("[LEADER] CFireLeaderCommand successfully posted for %u.", leader_id);

    return {
        {"success", true},
        {"leader_id", leader_id},
        {"message", "Leader dismissed successfully"}
    };
}

nlohmann::json LeaderManager::AssignLeader(uint32_t leader_id, uint8_t assignment_type, uint32_t target_id) {
    if (!fn_engine_alloc_ || !fn_post_command_ || !assign_leader_cmd_vtable_) {
        return {
            {"error", {{"code", -32080}, {"message", "Native command dispatch functions not initialized"}}}
        };
    }

    void* leader = FindLeaderPtr(leader_id);
    if (!leader) {
        return {
            {"error", {{"code", -32081}, {"message", "Leader ID " + std::to_string(leader_id) + " not found"}}}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &date_mgr) && date_mgr) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[LEADER] Posting CAssignLeaderCommand (0x4076) for leader %u, type %u, target %u, tick %u...",
        leader_id, assignment_type, target_id, tick_timestamp);

    void* cmd = fn_engine_alloc_(0x38);
    if (!cmd) {
        return {
            {"error", {{"code", -32082}, {"message", "Engine allocator returned null for CAssignLeaderCommand"}}}
        };
    }

    memset(cmd, 0, 0x38);
    *(void**)cmd = (void*)assign_leader_cmd_vtable_;
    *(uint32_t*)((uintptr_t)cmd + 0x08) = tick_timestamp;
    *(uint32_t*)((uintptr_t)cmd + 0x10) = 0xFFFF0000;
    *(uint8_t*)((uintptr_t)cmd + 0x14) = 1;
    *(uint32_t*)((uintptr_t)cmd + 0x20) = leader_id;
    *(uint8_t*)((uintptr_t)cmd + 0x24) = assignment_type;
    *(uint32_t*)((uintptr_t)cmd + 0x28) = (assignment_type == 8) ? 4 : 9;
    *(uint32_t*)((uintptr_t)cmd + 0x2C) = target_id;
    *(uint8_t*)((uintptr_t)cmd + 0x34) = 1;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[LEADER] Exception occurred executing PostCommand for CAssignLeaderCommand!");
        return {
            {"error", {{"code", -32083}, {"message", "Exception occurred executing PostCommand for CAssignLeaderCommand"}}}
        };
    }

    LOGF("[LEADER] CAssignLeaderCommand successfully posted for %u.", leader_id);

    return {
        {"success", true},
        {"leader_id", leader_id},
        {"assignment_type", assignment_type},
        {"target_id", target_id},
        {"message", "Leader assigned successfully"}
    };
}

} // namespace bridge
