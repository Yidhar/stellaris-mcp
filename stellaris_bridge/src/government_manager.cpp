#include "government_manager.hpp"
#include "command_builder.hpp"
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

// CCouncilAgenda::GetCost(CCountry const*, CString*) const: the cost scales with empire size and
// modifiers, so ask the engine (sdk::fn, located by fingerprint). Main thread only.
static bool SafeAgendaCost(uintptr_t fn, void* agenda, void* country, int64_t* out_cost) {
    using FnGetCost = int64_t* (*)(void* agenda, int64_t* out_cost, void* country, void* reason);
    __try {
        ((FnGetCost)fn)(agenda, out_cost, country, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

GovernmentManager& GovernmentManager::Get() {
    static GovernmentManager instance;
    return instance;
}

bool GovernmentManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    LOGF("[GOVERNMENT] Initialized (Base: 0x%llX)", (unsigned long long)base_address_);

    return true;
}

void* GovernmentManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113F50), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
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

uint32_t GovernmentManager::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;

    uint32_t cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid);
    return cid;
}

std::string GovernmentManager::LocalizeKey(const std::string& key) {
    return SafeLocalize(base_address_, key);
}

void* GovernmentManager::FindLeaderPtr(uint32_t leader_id) {
    if (!base_address_ || leader_id == 0 || leader_id == 0xFFFFFFFF) return nullptr;

    void* leader_mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3114120), &leader_mgr) || !leader_mgr) {
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
    // LeaderManager owns the CLeader layout; reuse it instead of a second copy of the offsets.
    HiredLeaderDetail h = LeaderManager::Get().ReadLeader(leader_id);
    LeaderDetail d{};
    d.id = leader_id;
    d.key = h.key;
    d.name = h.name.empty() ? h.key : h.name;
    d.class_key = h.class_key;
    d.class_name = h.class_name;
    d.level = h.level;
    d.age = h.age;
    d.ethic_key = h.ethic_key;
    d.ethic_name = h.ethic_name;
    d.traits = h.traits;
    d.trait_selections_available = h.trait_selections_available;
    d.trait_options = h.trait_options;
    d.trait_upgrade_options = h.trait_upgrade_options;
    return d;
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
            summary.active_agenda_name = AgendaName(summary.active_agenda);

            int64_t raw_progress = 0;
            SafeReadI64((const void*)((uintptr_t)council + 0x68), &raw_progress);
            summary.agenda_progress = std::round((double)raw_progress / 1000.0) / 100.0;

            int64_t raw_cost = 0;
            if (SafeAgendaCost(base_address_ + sdk::fn::CCouncilAgenda_GetCost, agenda, country, &raw_cost) && raw_cost > 0) {
                summary.agenda_cost = std::round((double)raw_cost / 1000.0) / 100.0;
                summary.agenda_ready = raw_progress >= raw_cost;
            }
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
            state.agenda.name = AgendaName(state.agenda.key);

            int64_t raw_progress = 0;
            SafeReadI64((const void*)((uintptr_t)council + 0x68), &raw_progress);
            state.agenda.progress = std::round((double)raw_progress / 1000.0) / 100.0;

            int64_t raw_cost = 0;
            if (SafeAgendaCost(base_address_ + sdk::fn::CCouncilAgenda_GetCost, agenda_ptr, country, &raw_cost) && raw_cost > 0) {
                state.agenda.cost = std::round((double)raw_cost / 1000.0) / 100.0;
                state.agenda.is_ready = raw_progress >= raw_cost;
            }
        }
    }

    // 4. Council seats: the ruler, then every CCouncilPosition owned by the player
    //    (sdk::db::CCouncilPosition; country / leader / type per sdk::ent::CCouncilPosition).
    {
        CouncilSeatDetail ruler_seat{};
        ruler_seat.seat_index = 0;
        ruler_seat.position_key = "ruler";
        ruler_seat.position_name = LocalizeKey("RULER");
        ruler_seat.is_ruler = true;
        ruler_seat.is_assigned = ruler_id != 0xFFFFFFFF;
        ruler_seat.leader = state.ruler;
        state.seats.push_back(ruler_seat);

        namespace P = sdk::ent::CCouncilPosition;
        const uint32_t country_id = GetPlayerCountryId();
        void* db = nullptr;
        void* slots = nullptr;
        uint32_t capacity = 0;
        if (SafeReadPtr((const void*)(base_address_ + sdk::db::CCouncilPosition), &db) && db &&
            SafeReadPtr((const void*)((uintptr_t)db + 0x18), &slots) && slots &&
            SafeReadU32((const void*)((uintptr_t)db + 0x20), &capacity)) {
            for (uint32_t i = 0; i < capacity && i < 65536; ++i) {
                void* pos = nullptr;
                uint32_t owner = 0xFFFFFFFF;
                if (!SafeReadPtr((const void*)((uintptr_t)slots + i * 16 + 8), &pos) || !pos ||
                    !SafeReadU32((const void*)((uintptr_t)pos + P::country), &owner) || owner != country_id) {
                    continue;
                }
                CouncilSeatDetail seat{};
                seat.seat_index = (uint32_t)state.seats.size();
                void* type = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)pos + P::type), &type) && type) {
                    SafeReadPdxString((const void*)((uintptr_t)type + 0x20), seat.position_key);
                    seat.position_name = LocalizeKey(seat.position_key);
                }
                uint32_t leader_id = 0xFFFFFFFF;
                SafeReadU32((const void*)((uintptr_t)pos + P::leader), &leader_id);
                seat.is_assigned = leader_id != 0xFFFFFFFF;  // 0 is a valid leader id
                if (seat.is_assigned) seat.leader = ReadLeader(leader_id);
                state.seats.push_back(seat);
            }
        }
        state.summary.councilor_count = (uint32_t)state.seats.size();
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
                {"ethic_name", seat.leader.ethic_name},
                {"traits", TraitsJson(seat.leader.traits)},
                {"trait_selections_available", seat.leader.trait_selections_available},
                {"trait_options", TraitsJson(seat.leader.trait_options)},
                {"trait_upgrade_options", TraitsJson(seat.leader.trait_upgrade_options)}
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
        {"available_agendas", AvailableAgendasJson()},
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

    uint32_t target_country_id = GetPlayerCountryId();

    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3113A08), &date_mgr) && date_mgr && (uintptr_t)date_mgr >= 0x10000) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[GOVERNMENT] Posting CFinishAgendaCommand for country %u, agenda '%s', tick %u...",
        target_country_id, summary.active_agenda.c_str(), tick_timestamp);

    namespace finish = sdk::cmd::finish_agenda_command;
    auto cmd = CommandBuilder::Get().Create(finish::kSpec);
    cmd.Set<uint32_t>(finish::country, target_country_id);
    if (!cmd.Post()) {
        return {
            {"error", {
                {"code", -32066},
                {"message", cmd.error()}
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

nlohmann::json GovernmentManager::AvailableAgendasJson() {
    // Agendas the engine would accept right now: every definition in
    // TGameDatabase<CCouncilAgendaDatabase> (pointer array at +0x50, count at +0x5C, key
    // std::string at +0x20) checked with CSetCouncilAgendaCommand's own IsValid.
    nlohmann::json out = nlohmann::json::array();
    void* db = nullptr;
    void* items = nullptr;
    uint32_t count = 0;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::TGameDatabase_CCouncilAgendaDatabase_pInstance), &db) || !db ||
        !SafeReadPtr((const void*)((uintptr_t)db + 0x50), &items) || !items ||
        !SafeReadU32((const void*)((uintptr_t)db + 0x5C), &count)) {
        return out;
    }
    namespace set = sdk::cmd::set_council_agenda_command;
    const uint32_t country_id = GetPlayerCountryId();
    for (uint32_t i = 0; i < count && i < 512; ++i) {
        void* agenda = nullptr;
        std::string key;
        if (!SafeReadPtr((const void*)((uintptr_t)items + i * 8), &agenda) || !agenda ||
            !SafeReadPdxString((const void*)((uintptr_t)agenda + 0x20), key) || key.empty()) {
            continue;
        }
        auto probe = CommandBuilder::Get().Create(set::kSpec);
        probe.SetString(set::name, key).Set<uint32_t>(set::country, country_id);
        std::string why;
        bool can_set = probe.IsValid(&why);
        if (can_set) {  // only what can be chosen now; the database holds ~90 definitions
            out.push_back({ {"key", key}, {"name", AgendaName(key)} });
        }
    }
    return out;
}

nlohmann::json GovernmentManager::SetCouncilAgenda(const std::string& agenda_key) {
    if (agenda_key.empty()) {
        return { {"error", {{"code", -32067}, {"message", "agenda_key must not be empty"}}} };
    }
    namespace set = sdk::cmd::set_council_agenda_command;
    auto cmd = CommandBuilder::Get().Create(set::kSpec);
    cmd.SetString(set::name, agenda_key).Set<uint32_t>(set::country, GetPlayerCountryId());
    if (!cmd.Post()) {
        return { {"error", {{"code", -32068}, {"message", cmd.error()}}} };
    }
    return {
        {"success", true},
        {"agenda_key", agenda_key},
        {"agenda_name", AgendaName(agenda_key)},
        {"message", "Council agenda set"}
    };
}

std::string GovernmentManager::AgendaName(const std::string& key) {
    // localisation/*: council_agenda_<key>_name
    if (key.empty()) return key;
    std::string loc_key = "council_agenda_" + key + "_name";
    std::string name = LocalizeKey(loc_key);
    return name == loc_key ? key : name;
}

} // namespace bridge
