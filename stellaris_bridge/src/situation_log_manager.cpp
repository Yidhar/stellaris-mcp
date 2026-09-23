#include "situation_log_manager.hpp"
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

static bool SafePostCommand(SituationLogManager::FnPostCommand fn_post, void* cmd) {
    __try {
        fn_post(cmd, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

SituationLogManager& SituationLogManager::Get() {
    static SituationLogManager instance;
    return instance;
}

bool SituationLogManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_pdx_string_assign_ = (FnPdxStringAssign)(base_address_ + 0x15BA40);

    command_vtable_ = base_address_ + 0x2391B28;

    LOGF("[SITUATION_LOG] Initialized (Base: 0x%llX, CmdVT: 0x%llX, Alloc: 0x%llX, PostCmd: 0x%llX, Assign: 0x%llX)",
        (unsigned long long)base_address_,
        (unsigned long long)command_vtable_,
        (unsigned long long)fn_engine_alloc_,
        (unsigned long long)fn_post_command_,
        (unsigned long long)fn_pdx_string_assign_);

    return fn_engine_alloc_ != nullptr && fn_post_command_ != nullptr;
}

void* SituationLogManager::GetPlayerCountry() {
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

uint32_t SituationLogManager::GetPlayerCountryId() {
    void* country = GetPlayerCountry();
    if (!country) return 0;

    uint32_t cid = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x20), &cid);
    return cid;
}

std::string SituationLogManager::LocalizeKey(const std::string& key) {
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

SituationLogSummary SituationLogManager::GetSummary() {
    SituationLogSummary summary{};
    if (!base_address_) return summary;

    uint32_t player_id = GetPlayerCountryId();

    // 1. Situations count for player empire from global entity manager
    void* sit_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3113060), &sit_mgr) && sit_mgr) {
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

    // 2. Special projects and anomalies count from player CSituationLog
    void* sit_log = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x31130A0), &sit_log) && sit_log) {
        uint32_t sp_cnt = 0;
        SafeReadU32((const void*)((uintptr_t)sit_log + 0x3C), &sp_cnt);
        summary.special_projects_count = sp_cnt;

        uint32_t anom_cnt = 0;
        SafeReadU32((const void*)((uintptr_t)sit_log + 0xFC), &anom_cnt);
        summary.anomalies_count = anom_cnt;
    }

    return summary;
}

FullSituationLogState SituationLogManager::GetSituationLogState(bool player_only) {
    FullSituationLogState state;
    if (!base_address_) return state;

    uint32_t player_id = GetPlayerCountryId();
    state.summary = GetSummary();

    // 1. Extract Situations from Global Situation EntityManager
    void* sit_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3113060), &sit_mgr) && sit_mgr) {
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

                int32_t raw_prog = 0;
                SafeReadI32((const void*)((uintptr_t)sit + 0x218), &raw_prog);
                item.progress = (double)raw_prog / 50000.0;

                int32_t raw_rate = 0;
                SafeReadI32((const void*)((uintptr_t)sit + 0x220), &raw_rate);
                item.monthly_change = (double)raw_rate / 50000.0;

                state.situations.push_back(item);
            }
        }
    }

    // 2. Extract Special Projects from player CSituationLog if any exist
    void* sit_log = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x31130A0), &sit_log) && sit_log) {
        void* sp_arr = nullptr;
        uint32_t sp_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)sit_log + 0x30), &sp_arr) && sp_arr &&
            SafeReadU32((const void*)((uintptr_t)sit_log + 0x3C), &sp_cnt) && sp_cnt > 0 && sp_cnt < 100) {
            for (uint32_t i = 0; i < sp_cnt; ++i) {
                void* sp_entry = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)sp_arr + i * 8), &sp_entry) && sp_entry) {
                    SpecialProjectItem sp_item{};
                    sp_item.id = i;
                    SafeReadPdxString((const void*)((uintptr_t)sp_entry + 0x20), sp_item.key);
                    sp_item.name = LocalizeKey(sp_item.key);
                    state.special_projects.push_back(sp_item);
                }
            }
        }
    }

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
        sp_arr.push_back({
            {"id", item.id},
            {"key", item.key},
            {"name", item.name}
        });
    }

    nlohmann::json anom_arr = nlohmann::json::array();
    for (const auto& item : state.anomalies) {
        anom_arr.push_back({
            {"id", item.id},
            {"key", item.key},
            {"name", item.name}
        });
    }

    return {
        {"summary", {
            {"situations_count", state.summary.situations_count},
            {"special_projects_count", state.summary.special_projects_count},
            {"anomalies_count", state.summary.anomalies_count}
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
        if (SafeReadPtr((const void*)(base_address_ + 0x3113060), &sit_mgr) && sit_mgr) {
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

    if (!fn_engine_alloc_ || !command_vtable_ || !fn_pdx_string_assign_ || !fn_post_command_) {
        return {
            {"error", {
                {"code", -32051},
                {"message", "Native command dispatch functions not initialized"}
            }}
        };
    }

    // Retrieve tick timestamp from date manager (+0xC0)
    uint32_t tick_timestamp = 0;
    void* date_mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112A08), &date_mgr) && date_mgr) {
        SafeReadU32((const void*)((uintptr_t)date_mgr + 0xC0), &tick_timestamp);
    }

    LOGF("[SITUATION_LOG] Posting CSetSituationApproachCommand for country %u, approach '%s', tick %u...",
        target_country_id, approach_key.c_str(), tick_timestamp);

    void* cmd = fn_engine_alloc_(0x60);
    if (!cmd) {
        return {
            {"error", {
                {"code", -32052},
                {"message", "Engine allocator returned null for CSetSituationApproachCommand"}
            }}
        };
    }

    memset(cmd, 0, 0x60);
    *(void**)cmd = (void*)command_vtable_;
    *(uint32_t*)((uintptr_t)cmd + 0x08) = 0xFFFFFFFF;
    *(uint32_t*)((uintptr_t)cmd + 0x0C) = 0;
    *(uint16_t*)((uintptr_t)cmd + 0x10) = 0xFFFF;
    *(uint16_t*)((uintptr_t)cmd + 0x12) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x14) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x15) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x16) = 0;
    *(uint32_t*)((uintptr_t)cmd + 0x18) = 0;
    *(uint32_t*)((uintptr_t)cmd + 0x20) = target_country_id;

    // Initialize RawPdxString at cmd + 0x28 matching Clone (0x7125A0) exactly
    void* str_ptr = (void*)((uintptr_t)cmd + 0x28);
    *(uint32_t*)str_ptr = 0;
    *(uint64_t*)((uintptr_t)str_ptr + 0x08) = 0;
    *(uint64_t*)((uintptr_t)str_ptr + 0x10) = 0;
    *(uint64_t*)((uintptr_t)str_ptr + 0x20) = 0;
    *(uint64_t*)((uintptr_t)str_ptr + 0x28) = 0xF;
    fn_pdx_string_assign_(str_ptr, approach_key.data(), approach_key.size());

    *(uint32_t*)((uintptr_t)cmd + 0x58) = tick_timestamp;
    *(uint32_t*)((uintptr_t)cmd + 0x5C) = 0;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[SITUATION_LOG] Exception occurred executing PostCommand for CSetSituationApproachCommand!");
        return {
            {"error", {
                {"code", -32053},
                {"message", "Exception occurred executing PostCommand for CSetSituationApproachCommand"}
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
