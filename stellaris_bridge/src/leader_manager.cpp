#include "leader_manager.hpp"
#include "game_state.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "command_builder.hpp"
#include <algorithm>
#include "alert_manager.hpp"
#include "fleet_access.hpp"
#include <map>
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

using FnFreeCString = void (*)(void* cstring);
static void SafeFreePdxStr(RawPdxString* str) {
    __try {
        ((FnFreeCString)((uintptr_t)GetModuleHandleA(nullptr) + sdk::fn::CString_Free))(str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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

// CLeaderLocation (16 bytes): +0 ELocationType, +4 sub-kind (9 for most; an envoy's task), +8 the
// target's id. The types, as CLeaderLocation::GetLocationTypeLocalization names them
constexpr std::ptrdiff_t kLocType = 0x0, kLocKind = 0x4, kLocTarget = 0x8;
constexpr uint8_t kLocationCouncil = 9;
constexpr uint8_t kLocationTypes = 11;
constexpr uint32_t kLocKindDefault = 9;
const char* const kLocationSlot[kLocationTypes] = { "unassigned", "governor", "ship", "army", "empire",
    "galactic_community", "federation", "spy_network", "first_contact", "council", "shroud" };
const char* const kLocationLoc[kLocationTypes] = { "", "LOCATION_TYPE_PLANET", "LOCATION_TYPE_SHIP",
    "LOCATION_TYPE_ARMY", "LOCATION_TYPE_EMPIRE", "LOCATION_TYPE_GALACTIC_COMMUNITY", "LOCATION_TYPE_FEDERATION",
    "LOCATION_TYPE_SPY_NETWORK", "LOCATION_TYPE_FIRST_CONTACT_SYSTEM", "LOCATION_TYPE_COUNCIL", "LOCATION_TYPE_SHROUD" };

const char* LocationSlot(uint8_t type) {
    return type < kLocationTypes ? kLocationSlot[type] : "unknown";
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

    fn_get_localized_leader_name_ = (FnGetLocalizedLeaderName)(base_address_ + sdk::fn::GetLocalizedLeaderName);

    LOGF("[LEADER] Initialized (Base: 0x%llX)", (unsigned long long)base_address_);
    return true;
}

void* LeaderManager::GetPlayerCountry() {
    return GameState::Get().GetPlayerCountry();  // the local player's country (not country 0)
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

// Name-list entries carry grammar tags: "&!" and a comma-separated tag list at the end of the
// entry (Chinese surnames: "孔$1$&!xing"). CPersistentName::ExtractTags cuts them off where it
// substitutes the entry; GetLocalizedLeaderName's output can still hold one between the surname and
// the first name, so remove "&!" and the tag list after it (tags are identifiers).
static std::string StripNameTags(std::string name) {
    for (size_t at = name.find("&!"); at != std::string::npos; at = name.find("&!", at)) {
        size_t end = at + 2;
        while (end < name.size() && (isalnum((unsigned char)name[end]) || name[end] == '_' || name[end] == ',')) {
            ++end;
        }
        name.erase(at, end - at);
    }
    // a regnal name carries its alternative after "|||" ("毛妮|||reg:婗"): the shown name is before it
    const size_t alt = name.find("|||");
    if (alt != std::string::npos) name.erase(alt);
    return name;
}

HiredLeaderDetail LeaderManager::ReadLeader(uint32_t leader_id) {
    HiredLeaderDetail detail{};
    detail.id = leader_id;

    void* leader = FindLeaderPtr(leader_id);
    if (!leader) return detail;

    SafeReadPdxString((const void*)((uintptr_t)leader + 0x50), detail.key);

    // 1. Resolve Localized Leader Name
    void* name_obj = (void*)((uintptr_t)leader + 0x38);
    if (fn_get_localized_leader_name_ && CommandBuilder::Get().SdkMatchesExe()) {
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
            SafeFreePdxStr(&out_name);
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
    detail.name = StripNameTags(detail.name);

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

    // 4. Where the leader serves (CLeader::location) and the council seat it holds
    //    (CLeader::council_location): CLeaderLocation = +0 ELocationType, +4 a sub-kind (an envoy's
    //    task), +8 the target's id
    uint8_t council_type = 0;
    SafeReadU8((const void*)((uintptr_t)leader + sdk::ent::CLeader::location + kLocType), &detail.assignment_type);
    SafeReadU32((const void*)((uintptr_t)leader + sdk::ent::CLeader::location + kLocTarget), &detail.assignment_target);
    SafeReadU8((const void*)((uintptr_t)leader + sdk::ent::CLeader::council_location + kLocType), &council_type);
    if (council_type == kLocationCouncil) {
        SafeReadU32((const void*)((uintptr_t)leader + sdk::ent::CLeader::council_location + kLocTarget), &detail.council_position);
    }
    SafeReadU32((const void*)((uintptr_t)leader + sdk::ent::CLeader::recruitment_date), &detail.hire_date);

    void* country = GetPlayerCountry();
    uint32_t ruler_id = 0xFFFFFFFF;
    if (country) {
        SafeReadU32((const void*)((uintptr_t)country + sdk::ent::CCountry::ruler), &ruler_id);
    }
    detail.is_councilor = leader_id == ruler_id || detail.council_position != 0xFFFFFFFF;
    if (leader_id == ruler_id) {
        detail.assignment_type_name = "ruler";
    } else {
        detail.assignment_type_name = LocationSlot(detail.assignment_type);
        if (detail.assignment_type < kLocationTypes) {
            detail.assignment_name = LocalizeKey(kLocationLoc[detail.assignment_type]);
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

    // 1. Total hired leaders from [Country + sdk::ent::CCountry::owned_leaders + 0x14]
    SafeReadU32((const void*)((uintptr_t)country + sdk::ent::CCountry::owned_leaders + 0x14), &summary.total_hired);

    // 2. Leader capacity & pool count from CCountryLeaderManager [Country + sdk::ent::CCountry::type + 0x20]
    void* c_leader_mgr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + sdk::ent::CCountry::type + 0x20), &c_leader_mgr) && c_leader_mgr) {
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

    // 1. Hired leaders from [Country + sdk::ent::CCountry::owned_leaders + 0x8]
    void* l_arr = nullptr;
    uint32_t cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + sdk::ent::CCountry::owned_leaders + 0x8), &l_arr);
    SafeReadU32((const void*)((uintptr_t)country + sdk::ent::CCountry::owned_leaders + 0x14), &cnt);

    nlohmann::json hired_list = nlohmann::json::array();
    if (l_arr && cnt > 0) {
        for (uint32_t i = 0; i < cnt && i < 256; ++i) {
            uint32_t lid = 0;
            if (SafeReadU32((const void*)((uintptr_t)l_arr + i * 4), &lid) && lid != 0xFFFFFFFF) {
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
                    {"assignment_name", d.assignment_name},
                    {"target_id", d.assignment_target},
                    {"council_position_id", d.council_position},
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

    // 2. Candidate pool from CCountryLeaderManager [Country + sdk::ent::CCountry::type + 0x20] + 0x28
    nlohmann::json pool_list = nlohmann::json::array();
    void* c_leader_mgr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + sdk::ent::CCountry::type + 0x20), &c_leader_mgr) && c_leader_mgr) {
        void* pool_arr = nullptr;
        uint32_t pool_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)c_leader_mgr + 0x28), &pool_arr);
        SafeReadU32((const void*)((uintptr_t)c_leader_mgr + 0x30), &pool_cnt);

        if (pool_arr && pool_cnt > 0) {
            for (uint32_t i = 0; i < pool_cnt && i < 64; ++i) {
                uint32_t cid = 0;
                if (SafeReadU32((const void*)((uintptr_t)pool_arr + i * 4), &cid) && cid != 0xFFFFFFFF) {
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
        SafeReadU32((const void*)((uintptr_t)country + sdk::ent::CCountry::ruler), &ruler_id);
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

namespace {

// an object of a TPdxRefDatabase by id (16-byte slots, object at +8; the id checked at id_off)
void* RefObject(uintptr_t base, uintptr_t db_rva, std::ptrdiff_t id_off, uint32_t id) {
    void* db = nullptr;
    void* slots = nullptr;
    void* obj = nullptr;
    uint32_t cap = 0, check = 0xFFFFFFFF;
    if (id == 0xFFFFFFFF || !SafeReadPtr((const void*)(base + db_rva), &db) || !db ||
        !SafeReadPtr((const void*)((uintptr_t)db + 0x18), &slots) || !slots ||
        !SafeReadU32((const void*)((uintptr_t)db + 0x20), &cap) || (id & 0xFFFFFF) >= cap ||
        !SafeReadPtr((const void*)((uintptr_t)slots + (id & 0xFFFFFF) * 16 + 8), &obj) || !obj ||
        !SafeReadU32((const void*)((uintptr_t)obj + id_off), &check) || check != id) {
        return nullptr;
    }
    return obj;
}

// the ids in a CPdxArray<TPdxRef<T>> member (data at +0x8, size at +0x14)
std::vector<uint32_t> RefIds(uintptr_t array) {
    std::vector<uint32_t> out;
    void* data = nullptr;
    uint32_t n = 0;
    if (!SafeReadPtr((const void*)(array + 0x8), &data) || !data || !SafeReadU32((const void*)(array + 0x14), &n) || n > 100000) {
        return out;
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t id = 0xFFFFFFFF;
        if (SafeReadU32((const void*)((uintptr_t)data + i * 4), &id) && id != 0xFFFFFFFF) out.push_back(id);
    }
    return out;
}

// a fleet's leader location is its first ship (CLeaderLocation::CollectShipLocations): CFleet::ships
// is the data pointer of its ship ids, the size 0xC further
uint32_t FleetLeaderShip(void* fleet) {
    void* data = nullptr;
    uint32_t n = 0, ship = 0xFFFFFFFF;
    if (fleet && SafeReadPtr((const void*)((uintptr_t)fleet + sdk::ent::CFleet::ships), &data) && data &&
        SafeReadU32((const void*)((uintptr_t)fleet + sdk::ent::CFleet::ships + 0xC), &n) && n > 0) {
        SafeReadU32(data, &ship);
    }
    return ship;
}

struct Slot {
    uint8_t type;
    uint32_t target;      // what the CLeaderLocation holds (colony, ship, army, council position)
    uint32_t shown_id;    // what the player knows it by (planet, fleet, army, council position)
    std::string name;
};

}  // namespace

nlohmann::json LeaderManager::GetAssignmentsJson(uint32_t leader_id) {
    void* leader = FindLeaderPtr(leader_id);
    void* country = GetPlayerCountry();
    if (!leader || !country) {
        return { {"error", {{"code", -32081}, {"message", "Leader ID " + std::to_string(leader_id) + " not found"}}} };
    }
    const uint32_t country_id = GetPlayerCountryId();
    std::vector<Slot> slots;

    // governors: the player's colonized planets (the location holds the colony)
    void* pdb = nullptr;
    void* parr = nullptr;
    uint32_t pcap = 0;
    if (SafeReadPtr((const void*)(base_address_ + sdk::db::CPlanet), &pdb) && pdb &&
        SafeReadPtr((const void*)((uintptr_t)pdb + 0x18), &parr) && parr && SafeReadU32((const void*)((uintptr_t)pdb + 0x20), &pcap)) {
        for (uint32_t i = 0; i < pcap && i < 200000; ++i) {
            void* planet = nullptr;
            uint32_t owner = 0xFFFFFFFF, colony = 0xFFFFFFFF, pid = 0xFFFFFFFF;
            if (!SafeReadPtr((const void*)((uintptr_t)parr + i * 16 + 8), &planet) || !planet ||
                !SafeReadU32((const void*)((uintptr_t)planet + sdk::ent::CPlanet::owner), &owner) || owner != country_id ||
                !SafeReadU32((const void*)((uintptr_t)planet + sdk::ent::CPlanet::colony), &colony) || colony == 0xFFFFFFFF ||
                !SafeReadU32((const void*)((uintptr_t)planet + sdk::rt::CPlanet_id), &pid)) {
                continue;
            }
            slots.push_back({ 1, colony, pid, PersistentNameText((const void*)((uintptr_t)planet + sdk::ent::CPlanet::name)) });
        }
    }
    // ships: the player's fleets, science and construction ships (the location holds the first ship)
    for (uint32_t fid : fleets::Owned(country)) {
        void* fleet = fleets::Find(base_address_, fid);
        if (!fleet || fleets::IsStation(fleets::ClassOf(fleet))) continue;
        const uint32_t ship = FleetLeaderShip(fleet);
        if (ship != 0xFFFFFFFF) slots.push_back({ 2, ship, fid, fleets::Name(fleet) });
    }
    // armies
    for (uint32_t aid : RefIds((uintptr_t)country + sdk::ent::CCountry::owned_armies)) {
        void* army = RefObject(base_address_, sdk::db::CArmy, sdk::rt::CArmy_id, aid);
        if (army) slots.push_back({ 3, aid, aid, PersistentNameText((const void*)((uintptr_t)army + sdk::ent::CArmy::name)) });
    }
    // council positions
    std::vector<Slot> council;
    void* cdb = nullptr;
    void* carr = nullptr;
    uint32_t ccap = 0;
    if (SafeReadPtr((const void*)(base_address_ + sdk::db::CCouncilPosition), &cdb) && cdb &&
        SafeReadPtr((const void*)((uintptr_t)cdb + 0x18), &carr) && carr && SafeReadU32((const void*)((uintptr_t)cdb + 0x20), &ccap)) {
        for (uint32_t i = 0; i < ccap && i < 65536; ++i) {
            void* pos = nullptr;
            uint32_t owner = 0xFFFFFFFF, id = 0xFFFFFFFF;
            if (!SafeReadPtr((const void*)((uintptr_t)carr + i * 16 + 8), &pos) || !pos ||
                !SafeReadU32((const void*)((uintptr_t)pos + sdk::ent::CCouncilPosition::country), &owner) || owner != country_id ||
                !SafeReadU32((const void*)((uintptr_t)pos + sdk::rt::CCouncilPosition_id), &id)) {
                continue;
            }
            void* type = nullptr;
            std::string key;
            if (SafeReadPtr((const void*)((uintptr_t)pos + sdk::ent::CCouncilPosition::type), &type) && type) {
                SafeReadPdxString((const void*)((uintptr_t)type + 0x20), key);
            }
            council.push_back({ kLocationCouncil, id, id, key.empty() ? "" : LocalizeKey(key) });
        }
    }

    // the game's verdict on each: CAssignLeaderCommand::IsValid (reassignment allowed, the location's
    // own checks, CanPlayerSetLeader)
    namespace assign = sdk::cmd::assign_leader_command;
    auto check = [&](const Slot& s, std::string* why) {
        auto cmd = CommandBuilder::Get().Create(assign::kSpec);
        cmd.Set<uint32_t>(assign::leader, leader_id)
           .Set<uint8_t>(assign::location + kLocType, s.type)
           .Set<uint32_t>(assign::location + kLocKind, kLocKindDefault)
           .Set<uint32_t>(assign::location + kLocTarget, s.target);
        return cmd.IsValid(why);
    };
    HiredLeaderDetail me = ReadLeader(leader_id);
    // the game's IsValid refuses some without a text: say why from the state
    auto explain = [&](const Slot& s, const std::string& why) -> std::string {
        std::string text = AlertManager::Get().CleanPdxString(why);
        if (!text.empty()) return text;
        if (s.type == kLocationCouncil && s.target == me.council_position) return "The leader already holds this seat";
        if (s.type == me.assignment_type && s.target == me.assignment_target) return "The leader already serves here";
        if (s.type == kLocationCouncil) {
            void* pos = RefObject(base_address_, sdk::db::CCouncilPosition, sdk::rt::CCouncilPosition_id, s.target);
            uint32_t holder = 0xFFFFFFFF;
            if (pos && SafeReadU32((const void*)((uintptr_t)pos + sdk::ent::CCouncilPosition::leader), &holder) && holder != 0xFFFFFFFF) {
                return "Held by leader " + std::to_string(holder) + " (" + ReadLeader(holder).name + ")";
            }
        }
        return "The game refuses it without a reason";
    };
    nlohmann::json ok = nlohmann::json::array(), seats = nlohmann::json::array();
    std::map<std::string, int> refused;
    for (const Slot& s : slots) {
        std::string why;
        if (check(s, &why)) {
            ok.push_back({ {"slot", kLocationSlot[s.type]}, {"target_id", s.shown_id}, {"name", s.name} });
        } else {
            refused[explain(s, why)]++;
        }
    }
    for (const Slot& s : council) {
        std::string why;
        nlohmann::json seat = { {"slot", "council"}, {"target_id", s.shown_id}, {"name", s.name} };
        const bool can = check(s, &why);
        seat["can_assign"] = can;
        if (!can) seat["reason"] = explain(s, why);
        seats.push_back(seat);
    }
    nlohmann::json refused_json = nlohmann::json::array();
    for (const auto& [why, n] : refused) refused_json.push_back({ {"reason", why}, {"count", n} });
    return { {"leader_id", leader_id}, {"name", me.name}, {"class", me.class_key},
             {"current", { {"slot", me.assignment_type_name}, {"target_id", me.assignment_target},
                           {"council_position_id", me.council_position} }},
             {"slots", ok}, {"council", seats}, {"refused", refused_json} };
}

nlohmann::json LeaderManager::AssignLeader(uint32_t leader_id, const std::string& slot, uint32_t target_id, const std::string& task) {
    if (!FindLeaderPtr(leader_id)) {
        return { {"error", {{"code", -32081}, {"message", "Leader ID " + std::to_string(leader_id) + " not found"}}} };
    }
    // the slot's location: the target as the player knows it -> what the location holds
    uint8_t type = 0;
    uint32_t target = 0xFFFFFFFF;
    uint32_t kind = kLocKindDefault;
    if (slot == "envoy") {
        // an envoy's location: the EEnvoyAssignment as its sub-kind; toward an empire
        // (CLeaderLocation type 4, the country's id) or the galactic community (type 5, no target),
        // as CLeaderLocation::CanHaveEnvoyAssignment allows them
        constexpr uint8_t kLocationEmpire = 4, kLocationCommunity = 5;
        uint32_t n = 0xFFFFFFFF;
        for (uint32_t i = 0; i < std::size(sdk::enums::EnvoyAssignment); ++i) {
            if (task == sdk::enums::EnvoyAssignment[i]) n = i;
        }
        const std::string empire_tasks[] = { "improve_relations", "harm_relations", "spy_network", "patron_attunement" };
        const std::string community_tasks[] = { "galactic_community", "strengthen_imperial_authority", "undermine_imperial_authority" };
        if (n != 0xFFFFFFFF && std::find(std::begin(empire_tasks), std::end(empire_tasks), task) != std::end(empire_tasks)) {
            type = kLocationEmpire;
            target = target_id;
        } else if (n != 0xFFFFFFFF && std::find(std::begin(community_tasks), std::end(community_tasks), task) != std::end(community_tasks)) {
            type = kLocationCommunity;
        } else {
            return { {"error", {{"code", -32082}, {"message", "envoy task is one of improve_relations, harm_relations, spy_network, "
                                                              "patron_attunement (target_id: the country), galactic_community, "
                                                              "strengthen_imperial_authority, undermine_imperial_authority"}}} };
        }
        kind = n;
    } else if (slot == "governor") {
        void* planet = RefObject(base_address_, sdk::db::CPlanet, sdk::rt::CPlanet_id, target_id);
        type = 1;
        if (planet) SafeReadU32((const void*)((uintptr_t)planet + sdk::ent::CPlanet::colony), &target);
        if (target == 0xFFFFFFFF) {
            return { {"error", {{"code", -32082}, {"message", "Planet " + std::to_string(target_id) + " is not a colony"}}} };
        }
    } else if (slot == "ship") {
        type = 2;
        target = FleetLeaderShip(fleets::Find(base_address_, target_id));
        if (target == 0xFFFFFFFF) {
            return { {"error", {{"code", -32082}, {"message", "Fleet " + std::to_string(target_id) + " not found"}}} };
        }
    } else if (slot == "army") {
        type = 3;
        target = target_id;
    } else if (slot == "council") {
        type = kLocationCouncil;
        target = target_id;
    } else {
        return { {"error", {{"code", -32082}, {"message", "slot is governor, ship, army, council or envoy"}}} };
    }

    // the game's IsValid compares only the leader's main location: a council seat already held
    // would be posted again for nothing
    if (type == kLocationCouncil && target == ReadLeader(leader_id).council_position) {
        return { {"error", {{"code", -32083}, {"message", "The leader already holds this seat"}}} };
    }
    namespace assign = sdk::cmd::assign_leader_command;
    auto cmd = CommandBuilder::Get().Create(assign::kSpec);
    cmd.Set<uint32_t>(assign::leader, leader_id)
       .Set<uint8_t>(assign::location + kLocType, type)
       .Set<uint32_t>(assign::location + kLocKind, kind)
       .Set<uint32_t>(assign::location + kLocTarget, target);
    // Post runs the game's CAssignLeaderCommand::IsValid first: refused with its reason
    if (!cmd.Post()) {
        std::string why = AlertManager::Get().CleanPdxString(cmd.error());
        if (why.empty() || why.find("rejected by the engine") != std::string::npos) {
            HiredLeaderDetail me = ReadLeader(leader_id);
            if (type == kLocationCouncil && target == me.council_position) why = "The leader already holds this seat";
            else if (type == me.assignment_type && target == me.assignment_target) why = "The leader already serves here";
            else why = "The game refuses it without a reason (stellaris_get_leader_assignments lists the slots it accepts)";
        }
        return { {"error", {{"code", -32083}, {"message", why}}} };
    }
    LOGF("[LEADER] CAssignLeaderCommand posted: leader %u -> %s %u", leader_id, slot.c_str(), target_id);
    nlohmann::json out = { {"success", true}, {"leader_id", leader_id}, {"slot", slot}, {"target_id", target_id},
                           {"message", "Posted; the game assigns the leader when it runs the command"} };
    if (!task.empty()) out["task"] = task;
    return out;
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
