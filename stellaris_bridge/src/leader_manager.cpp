#include "leader_manager.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "command_builder.hpp"
#include <algorithm>
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

nlohmann::json TraitsJson(const std::vector<LeaderTraitDetail>& traits) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : traits) {
        arr.push_back({ {"key", t.key}, {"name", t.name}, {"tier", t.tier} });
    }
    return arr;
}

LeaderManager& LeaderManager::Get() {
    static LeaderManager instance;
    return instance;
}

bool LeaderManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_get_localized_leader_name_ = (FnGetLocalizedLeaderName)(base_address_ + 0x3E8E20);

    LOGF("[LEADER] Initialized (Base: 0x%llX)", (unsigned long long)base_address_);
    return true;
}

void* LeaderManager::GetPlayerCountry() {
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

    return SafeLocalize(base_address_, key);
}

void* LeaderManager::FindLeaderPtr(uint32_t leader_id) {
    if (!base_address_ || leader_id == 0 || leader_id == 0xFFFFFFFF) return nullptr;

    void* leader_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CLeader), &leader_mgr) || !leader_mgr || (uintptr_t)leader_mgr < 0x10000) {
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

    // The job the leader held before recruitment (save token "job"; 4.5 has no leader subclasses).
    void* job_ptr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)leader + sdk::ent::CLeader::job), &job_ptr) && job_ptr) {
        SafeReadPdxString((const void*)((uintptr_t)job_ptr + 0x20), detail.background_job_key);
        if (!detail.background_job_key.empty()) {
            detail.background_job_name = LocalizeKey("job_" + detail.background_job_key);
        }
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

    // 5. Traits. CPdxArray<CLeaderTrait*> fields: data pointer at the SDK offset, count 0xC
    // further; the trait key is the std::string at trait+0x148 (CString at +0x138 on Linux).
    namespace L = sdk::ent::CLeader;
    constexpr std::ptrdiff_t kTraitKey = 0x148;
    auto read_traits = [&](std::ptrdiff_t field, std::vector<LeaderTraitDetail>& out) {
        void* arr = nullptr;
        uint32_t cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)leader + field), &arr) || !arr ||
            !SafeReadU32((const void*)((uintptr_t)leader + field + 0xC), &cnt)) {
            return;
        }
        for (uint32_t t = 0; t < cnt && t < 16; ++t) {
            void* t_obj = nullptr;
            std::string t_key;
            if (SafeReadPtr((const void*)((uintptr_t)arr + t * 8), &t_obj) && t_obj &&
                SafeReadPdxString((const void*)((uintptr_t)t_obj + kTraitKey), t_key) && !t_key.empty()) {
                LeaderTraitDetail td;
                td.key = t_key;
                td.name = LocalizeKey(t_key);
                if (t_key.rfind("_3") != std::string::npos) td.tier = 3;
                else if (t_key.rfind("_2") != std::string::npos) td.tier = 2;
                else td.tier = 1;
                out.push_back(td);
            }
        }
    };
    read_traits(L::traits, detail.traits);
    read_traits(L::available_trait, detail.trait_options);
    read_traits(L::available_trait_2, detail.trait_upgrade_options);

    // 6. Unspent trait picks: the engine's own counter ("+" badge in the UI)
    SafeReadU32((const void*)((uintptr_t)leader + L::available_trait_selections), (uint32_t*)&detail.trait_selections_available);
    detail.has_unspent_trait_points = detail.trait_selections_available > 0 &&
        (!detail.trait_options.empty() || !detail.trait_upgrade_options.empty());

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
                    {"background_job", d.background_job_key},
                    {"background_job_name", d.background_job_name},
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
                    {"traits", traits_arr},
                    {"trait_selections_available", d.trait_selections_available},
                    {"trait_options", TraitsJson(d.trait_options)},
                    {"trait_upgrade_options", TraitsJson(d.trait_upgrade_options)}
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
    void* leader = FindLeaderPtr(candidate_id);
    if (!leader) {
        return {
            {"error", {{"code", -32072}, {"message", "Candidate leader ID " + std::to_string(candidate_id) + " not found"}}}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &date_mgr) && date_mgr && (uintptr_t)date_mgr >= 0x10000) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[LEADER] Posting CHireLeaderCommand (0x4073) for candidate %u, country %u, tick %u...",
        candidate_id, country_id, tick_timestamp);

    namespace hire = sdk::cmd::hire_leader;
    auto cmd = CommandBuilder::Get().Create(hire::kSpec);
    cmd.Set<uint32_t>(hire::country, country_id)
       .Set<uint32_t>(hire::leader, candidate_id);
    if (!cmd.Post()) {
        return {
            {"error", {{"code", -32074}, {"message", cmd.error()}}}
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
    if (SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &date_mgr) && date_mgr && (uintptr_t)date_mgr >= 0x10000) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[LEADER] Posting CFireLeaderCommand (0x2EB3) for leader %u, country %u, tick %u...",
        leader_id, country_id, tick_timestamp);

    namespace fire = sdk::cmd::fire_leader_command;
    auto cmd = CommandBuilder::Get().Create(fire::kSpec);
    cmd.Set<uint32_t>(fire::country, country_id)
       .Set<uint32_t>(fire::leader, leader_id);
    if (!cmd.Post()) {
        return {
            {"error", {{"code", -32079}, {"message", cmd.error()}}}
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
    void* leader = FindLeaderPtr(leader_id);
    if (!leader) {
        return {
            {"error", {{"code", -32081}, {"message", "Leader ID " + std::to_string(leader_id) + " not found"}}}
        };
    }

    uint32_t country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &date_mgr) && date_mgr && (uintptr_t)date_mgr >= 0x10000) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[LEADER] Posting CAssignLeaderCommand (0x4076) for leader %u, type %u, target %u, tick %u...",
        leader_id, assignment_type, target_id, tick_timestamp);

    namespace assign = sdk::cmd::assign_leader_command;
    // `location` is a nested persistent the SDK does not expand yet. Layout from the engine
    // factory (0xBE8A00): +0 u8 assignment type, +4 u32 slot kind (default 9), +8 u32 target.
    constexpr std::ptrdiff_t kLocType = assign::location + 0x0;
    constexpr std::ptrdiff_t kLocKind = assign::location + 0x4;
    constexpr std::ptrdiff_t kLocTarget = assign::location + 0x8;
    auto cmd = CommandBuilder::Get().Create(assign::kSpec);
    cmd.Set<uint32_t>(assign::leader, leader_id)
       .Set<uint8_t>(kLocType, assignment_type)
       .Set<uint32_t>(kLocKind, (assignment_type == 8) ? 4u : 9u)
       .Set<uint32_t>(kLocTarget, target_id);
    if (!cmd.Post()) {
        return {
            {"error", {{"code", -32083}, {"message", cmd.error()}}}
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

nlohmann::json LeaderManager::SelectTrait(uint32_t leader_id, const std::string& trait_key) {
    if (!FindLeaderPtr(leader_id)) {
        return { {"error", {{"code", -32084}, {"message", "Leader ID " + std::to_string(leader_id) + " not found"}}} };
    }
    HiredLeaderDetail d = ReadLeader(leader_id);
    auto offered = [&](const std::vector<LeaderTraitDetail>& v) {
        return std::any_of(v.begin(), v.end(), [&](const LeaderTraitDetail& t) { return t.key == trait_key; });
    };
    if (!offered(d.trait_options) && !offered(d.trait_upgrade_options)) {
        nlohmann::json opts = TraitsJson(d.trait_options);
        for (auto& t : TraitsJson(d.trait_upgrade_options)) opts.push_back(t);
        return { {"error", {{"code", -32085},
                            {"message", "Trait '" + trait_key + "' is not currently offered to this leader"},
                            {"offered", opts}}} };
    }

    namespace pick = sdk::cmd::add_trait_from_pool_command;
    auto cmd = CommandBuilder::Get().Create(pick::kSpec);
    cmd.Set<uint32_t>(pick::leader, leader_id)
       .Set<uint32_t>(pick::country, GetPlayerCountryId())
       .SetString(pick::trait, trait_key);
    if (!cmd.Post()) {
        return { {"error", {{"code", -32086}, {"message", cmd.error()}}} };
    }
    return {
        {"success", true},
        {"leader_id", leader_id},
        {"trait_key", trait_key},
        {"trait_name", LocalizeKey(trait_key)},
        {"message", "Trait selection posted"}
    };
}

} // namespace bridge
