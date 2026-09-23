#include "government_manager.hpp"
#include <cstring>
#include <cmath>

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

static bool SafeReadI64(const void* addr, int64_t* out) {
    __try {
        *out = *(const int64_t*)addr;
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

static bool SafeLocalizeCall(GovernmentManager::FnLocalize fn_localize,
                             GovernmentManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(GovernmentManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SafePostCommand(GovernmentManager::FnPostCommand fn_post, void* cmd) {
    __try {
        fn_post(cmd, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeGetAgendaCost(GovernmentManager::FnGetAgendaCost fn_cost, void* agenda, int64_t* out_cost, void* country) {
    if (!fn_cost || !agenda || !country || !out_cost) return false;
    __try {
        fn_cost(agenda, out_cost, country, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out_cost = 0;
        return false;
    }
}

GovernmentManager& GovernmentManager::Get() {
    static GovernmentManager instance;
    return instance;
}

bool GovernmentManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_get_agenda_cost_ = (FnGetAgendaCost)(base_address_ + 0x4AC160);

    finish_agenda_cmd_vtable_ = base_address_ + 0x2393B20;

    LOGF("[GOVERNMENT] Initialized (Base: 0x%llX, CmdVT: 0x%llX, CostFn: 0x%llX)",
        (unsigned long long)base_address_,
        (unsigned long long)finish_agenda_cmd_vtable_,
        (unsigned long long)fn_get_agenda_cost_);

    return fn_engine_alloc_ != nullptr && fn_post_command_ != nullptr && fn_get_agenda_cost_ != nullptr;
}

void* GovernmentManager::GetPlayerCountry() {
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

uint32_t GovernmentManager::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;

    uint32_t cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid);
    return cid;
}

std::string GovernmentManager::LocalizeKey(const std::string& key) {
    if (key.empty() || !fn_localize_) return key;

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

void* GovernmentManager::FindLeaderPtr(uint32_t leader_id) {
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

    // 1. Check direct slot index (low 16 bits)
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

    // 2. Scan if direct slot did not match
    for (uint32_t i = 0; i < leader_cap && i < 1024; ++i) {
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

LeaderDetail GovernmentManager::ReadLeader(uint32_t leader_id) {
    LeaderDetail detail{};
    detail.id = leader_id;

    void* leader = FindLeaderPtr(leader_id);
    if (!leader) return detail;

    SafeReadPdxString((const void*)((uintptr_t)leader + 0x50), detail.key);
    detail.name = LocalizeKey(detail.key);

    void* class_ptr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)leader + 0xE0), &class_ptr) && class_ptr) {
        SafeReadPdxString((const void*)((uintptr_t)class_ptr + 0x20), detail.class_key);
        detail.class_name = LocalizeKey(detail.class_key);
    }

    SafeReadU32((const void*)((uintptr_t)leader + 0xD0), &detail.level);
    SafeReadU32((const void*)((uintptr_t)leader + 0x108), &detail.age);

    void* ethic_ptr = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)leader + 0x6D8), &ethic_ptr) && ethic_ptr) {
        SafeReadPdxString((const void*)((uintptr_t)ethic_ptr + 0x20), detail.ethic_key);
        detail.ethic_name = LocalizeKey(detail.ethic_key);
    }

    return detail;
}

CouncilSummary GovernmentManager::GetSummary() {
    CouncilSummary summary{};
    void* country = GetPlayerCountry();
    if (!country) return summary;

    // 1. Ruler name
    uint32_t ruler_id = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x1BF0), &ruler_id);
    if (ruler_id != 0 && ruler_id != 0xFFFFFFFF) {
        LeaderDetail ruler = ReadLeader(ruler_id);
        summary.ruler_name = ruler.name.empty() ? ruler.key : ruler.name;
    }

    // 2. Council seats count
    uint32_t seats_cnt = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x0AAC), &seats_cnt);
    summary.councilor_count = seats_cnt;

    // 3. Council Agenda & Progress from CCountryCouncil
    void* council = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x19F0), &council) && council) {
        void* agenda = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)council + 0x60), &agenda) && agenda) {
            SafeReadPdxString((const void*)((uintptr_t)agenda + 0x20), summary.active_agenda);
            summary.active_agenda_name = LocalizeKey(summary.active_agenda);

            int64_t raw_progress = 0;
            SafeReadI64((const void*)((uintptr_t)council + 0x68), &raw_progress);
            summary.agenda_progress = std::round((double)raw_progress / 1000.0) / 100.0;

            int64_t raw_cost = 0;
            SafeGetAgendaCost(fn_get_agenda_cost_, agenda, &raw_cost, country);
            summary.agenda_cost = std::round((double)raw_cost / 1000.0) / 100.0;
            summary.agenda_ready = (raw_cost > 0 && raw_progress >= raw_cost);
        }
    }

    return summary;
}

nlohmann::json GovernmentManager::GetSummaryJson() {
    CouncilSummary s = GetSummary();
    return {
        {"ruler_name", s.ruler_name},
        {"active_agenda", s.active_agenda},
        {"active_agenda_name", s.active_agenda_name},
        {"agenda_progress", s.agenda_progress},
        {"agenda_cost", s.agenda_cost},
        {"agenda_ready", s.agenda_ready},
        {"councilor_count", s.councilor_count}
    };
}

FullGovernmentState GovernmentManager::GetGovernmentState() {
    FullGovernmentState state{};
    void* country = GetPlayerCountry();
    if (!country) return state;

    state.summary = GetSummary();

    // 1. Ruler
    uint32_t ruler_id = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x1BF0), &ruler_id);
    state.ruler = ReadLeader(ruler_id);

    // 2. Ethics from Country + 0x1670 (Array ptr) and + 0x1678 (Count)
    void* ethics_arr = nullptr;
    uint32_t ethics_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x1670), &ethics_arr) && ethics_arr &&
        SafeReadU32((const void*)((uintptr_t)country + 0x1678), &ethics_cnt) && ethics_cnt > 0) {
        for (uint32_t i = 0; i < ethics_cnt && i < 16; ++i) {
            void* ethic_ptr = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)ethics_arr + i * 8), &ethic_ptr) && ethic_ptr) {
                std::string e_key;
                if (SafeReadPdxString((const void*)((uintptr_t)ethic_ptr + 0x20), e_key)) {
                    state.ethics.push_back({ e_key, LocalizeKey(e_key) });
                }
            }
        }
    }

    // 3. CCountryCouncil details (+0x19F0)
    void* council = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x19F0), &council) && council) {
        // Government Type
        void* gov_type_ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)council + 0x10), &gov_type_ptr) && gov_type_ptr) {
            SafeReadPdxString((const void*)((uintptr_t)gov_type_ptr + 0x20), state.government_type);
            state.government_type_name = LocalizeKey(state.government_type);
        }

        // Authority Type
        void* auth_type_ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)council + 0x18), &auth_type_ptr) && auth_type_ptr) {
            SafeReadPdxString((const void*)((uintptr_t)auth_type_ptr + 0x20), state.authority);
            state.authority_name = LocalizeKey(state.authority);
        }

        // Origin
        void* origin_ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)council + 0x50), &origin_ptr) && origin_ptr) {
            SafeReadPdxString((const void*)((uintptr_t)origin_ptr + 0x20), state.origin);
            state.origin_name = LocalizeKey(state.origin);
        }

        // Civics
        void* civics_arr = nullptr;
        uint32_t civics_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)council + 0x28), &civics_arr) && civics_arr &&
            SafeReadU32((const void*)((uintptr_t)council + 0x30), &civics_cnt) && civics_cnt > 0) {
            for (uint32_t i = 0; i < civics_cnt && i < 16; ++i) {
                void* civic_ptr = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)civics_arr + i * 8), &civic_ptr) && civic_ptr) {
                    std::string c_key;
                    if (SafeReadPdxString((const void*)((uintptr_t)civic_ptr + 0x20), c_key)) {
                        state.civics.push_back({ c_key, LocalizeKey(c_key) });
                    }
                }
            }
        }

        // Agenda
        void* agenda_ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)council + 0x60), &agenda_ptr) && agenda_ptr) {
            SafeReadPdxString((const void*)((uintptr_t)agenda_ptr + 0x20), state.agenda.key);
            state.agenda.name = LocalizeKey(state.agenda.key);

            int64_t raw_progress = 0;
            SafeReadI64((const void*)((uintptr_t)council + 0x68), &raw_progress);
            state.agenda.progress = std::round((double)raw_progress / 1000.0) / 100.0;

            int64_t raw_cost = 0;
            SafeGetAgendaCost(fn_get_agenda_cost_, agenda_ptr, &raw_cost, country);
            state.agenda.cost = std::round((double)raw_cost / 1000.0) / 100.0;
            state.agenda.is_ready = (raw_cost > 0 && raw_progress >= raw_cost);
        }
    }

    // 4. Council Seats (+0x0AA0)
    void* seats_arr = nullptr;
    uint32_t seats_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x0AA0), &seats_arr) && seats_arr &&
        SafeReadU32((const void*)((uintptr_t)country + 0x0AAC), &seats_cnt) && seats_cnt > 0) {
        for (uint32_t i = 0; i < seats_cnt && i < 16; ++i) {
            void* seat_ptr = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)seats_arr + i * 0x20), &seat_ptr) || !seat_ptr) {
                continue;
            }

            CouncilSeatDetail seat{};
            seat.seat_index = i;

            void* pos_type = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)seat_ptr + 0xA0), &pos_type) && pos_type) {
                SafeReadPdxString((const void*)((uintptr_t)pos_type + 0x20), seat.position_key);
                seat.position_name = LocalizeKey(seat.position_key);
            }

            if (i == 0 || seat.position_key.find("ruler") != std::string::npos) {
                seat.is_ruler = true;
                seat.is_assigned = true;
                seat.leader = state.ruler;
            } else {
                seat.is_ruler = false;
                uint32_t assigned_id = 0xFFFFFFFF;
                SafeReadU32((const void*)((uintptr_t)seat_ptr + 0xAC), &assigned_id);
                if (assigned_id != 0xFFFFFFFF && assigned_id != 0) {
                    seat.is_assigned = true;
                    seat.leader = ReadLeader(assigned_id);
                } else {
                    seat.is_assigned = false;
                }
            }

            state.seats.push_back(seat);
        }
    }

    return state;
}

nlohmann::json GovernmentManager::GetGovernmentJson() {
    FullGovernmentState s = GetGovernmentState();

    nlohmann::json civics_json = nlohmann::json::array();
    for (const auto& [k, n] : s.civics) {
        civics_json.push_back({{"key", k}, {"name", n}});
    }

    nlohmann::json ethics_json = nlohmann::json::array();
    for (const auto& [k, n] : s.ethics) {
        ethics_json.push_back({{"key", k}, {"name", n}});
    }

    nlohmann::json seats_json = nlohmann::json::array();
    for (const auto& seat : s.seats) {
        nlohmann::json sj = {
            {"seat_index", seat.seat_index},
            {"position_key", seat.position_key},
            {"position_name", seat.position_name},
            {"is_ruler", seat.is_ruler},
            {"is_assigned", seat.is_assigned}
        };
        if (seat.is_assigned) {
            sj["leader"] = {
                {"id", seat.leader.id},
                {"name", seat.leader.name},
                {"class_key", seat.leader.class_key},
                {"class_name", seat.leader.class_name},
                {"level", seat.leader.level},
                {"age", seat.leader.age},
                {"ethic_key", seat.leader.ethic_key},
                {"ethic_name", seat.leader.ethic_name}
            };
        } else {
            sj["leader"] = nullptr;
        }
        seats_json.push_back(sj);
    }

    return {
        {"summary", {
            {"ruler_name", s.summary.ruler_name},
            {"active_agenda", s.summary.active_agenda},
            {"active_agenda_name", s.summary.active_agenda_name},
            {"agenda_progress", s.summary.agenda_progress},
            {"agenda_cost", s.summary.agenda_cost},
            {"agenda_ready", s.summary.agenda_ready},
            {"councilor_count", s.summary.councilor_count}
        }},
        {"authority", s.authority},
        {"authority_name", s.authority_name},
        {"government_type", s.government_type},
        {"government_type_name", s.government_type_name},
        {"origin", s.origin},
        {"origin_name", s.origin_name},
        {"civics", civics_json},
        {"ethics", ethics_json},
        {"ruler", {
            {"id", s.ruler.id},
            {"name", s.ruler.name},
            {"class_key", s.ruler.class_key},
            {"class_name", s.ruler.class_name},
            {"level", s.ruler.level},
            {"age", s.ruler.age},
            {"ethic_key", s.ruler.ethic_key},
            {"ethic_name", s.ruler.ethic_name}
        }},
        {"agenda", {
            {"key", s.agenda.key},
            {"name", s.agenda.name},
            {"progress", s.agenda.progress},
            {"cost", s.agenda.cost},
            {"is_ready", s.agenda.is_ready}
        }},
        {"council_seats", seats_json}
    };
}

nlohmann::json GovernmentManager::LaunchCouncilAgenda() {
    void* country = GetPlayerCountry();
    if (!country) {
        return {
            {"error", {
                {"code", -32061},
                {"message", "Player country not found"}
            }}
        };
    }

    CouncilSummary summary = GetSummary();
    if (summary.active_agenda.empty()) {
        return {
            {"error", {
                {"code", -32062},
                {"message", "No active council agenda selected"}
            }}
        };
    }

    if (!summary.agenda_ready) {
        return {
            {"error", {
                {"code", -32063},
                {"message", "Council agenda is not ready to be launched (progress: " +
                    std::to_string(summary.agenda_progress) + " / " + std::to_string(summary.agenda_cost) + ")"}
            }}
        };
    }

    if (!fn_engine_alloc_ || !fn_post_command_ || !finish_agenda_cmd_vtable_) {
        return {
            {"error", {
                {"code", -32064},
                {"message", "Native command dispatch functions not initialized"}
            }}
        };
    }

    uint32_t target_country_id = GetPlayerCountryId();

    // Read tick timestamp from date manager (+0xC0)
    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &date_mgr) && date_mgr) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[GOVERNMENT] Posting CFinishAgendaCommand for country %u, agenda '%s', tick %u...",
        target_country_id, summary.active_agenda.c_str(), tick_timestamp);

    void* cmd = fn_engine_alloc_(0x28);
    if (!cmd) {
        return {
            {"error", {
                {"code", -32065},
                {"message", "Engine allocator returned null for CFinishAgendaCommand"}
            }}
        };
    }

    memset(cmd, 0, 0x28);
    *(void**)cmd = (void*)finish_agenda_cmd_vtable_;
    *(uint32_t*)((uintptr_t)cmd + 0x08) = tick_timestamp;
    *(uint32_t*)((uintptr_t)cmd + 0x0C) = 0;
    *(uint16_t*)((uintptr_t)cmd + 0x10) = 0xFFFF;
    *(uint16_t*)((uintptr_t)cmd + 0x12) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x14) = 1; // satisfies IsValid()
    *(uint8_t*)((uintptr_t)cmd + 0x15) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x16) = 0;
    *(uint32_t*)((uintptr_t)cmd + 0x18) = 0;
    *(uint32_t*)((uintptr_t)cmd + 0x20) = target_country_id;
    *(uint32_t*)((uintptr_t)cmd + 0x24) = 0;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[GOVERNMENT] Exception occurred executing PostCommand for CFinishAgendaCommand!");
        return {
            {"error", {
                {"code", -32066},
                {"message", "Exception occurred executing PostCommand for CFinishAgendaCommand"}
            }}
        };
    }

    LOGF("[GOVERNMENT] CFinishAgendaCommand successfully posted.");
    return {
        {"success", true},
        {"country_id", target_country_id},
        {"agenda", summary.active_agenda},
        {"agenda_name", summary.active_agenda_name},
        {"message", "Council agenda launched successfully"}
    };
}

} // namespace bridge
