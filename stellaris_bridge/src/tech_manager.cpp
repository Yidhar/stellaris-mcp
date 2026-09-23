#include "tech_manager.hpp"
#include "game_state.hpp"
#include "task_queue.hpp"
#include <unordered_map>
#include <algorithm>

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

// Safe wrappers to avoid C2712 SEH unwind conflicts
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

static bool SafeLocalizeCall(TechManager::FnLocalize fn, TechManager::FnFreePdxStr fn_free,
                             const RawPdxString* in_key, RawPdxString* out_str) {
    __try {
        fn(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeFreePdxStr(TechManager::FnFreePdxStr fn, RawPdxString* str) {
    __try {
        fn(str);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafePostCommand(TechManager::FnPostCommand fn, void* cmd) {
    __try {
        fn(cmd, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

TechManager& TechManager::Get() {
    static TechManager instance;
    return instance;
}

bool TechManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    // RVAs discovered from binary reverse engineering
    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
    fn_localize_     = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    command_vtable_  = base_address_ + 0x2393C90;
    cancel_vtable_   = base_address_ + 0x2393E00;
    fn_cancel_execute_ = (FnCancelExecute)(base_address_ + 0x6BB250);
    fn_scalar_dtor_  = (FnScalarDtor)(base_address_ + 0x1AF230);

    LOGF("[TECH_MGR] Initialized: Base=0x%llX, Alloc=0x%llX, PostCmd=0x%llX, Vtable=0x%llX, CancelVt=0x%llX",
        (unsigned long long)base_address_,
        (unsigned long long)fn_engine_alloc_,
        (unsigned long long)fn_post_command_,
        (unsigned long long)command_vtable_,
        (unsigned long long)cancel_vtable_);

    return true;
}

void* TechManager::GetPlayerCountry() {
    return GameState::Get().GetPlayerCountry();
}

void* TechManager::GetTechManagerPtr() {
    void* country = GetPlayerCountry();
    if (!country) return nullptr;
    return (void*)((uintptr_t)country + 0x16E0);
}

std::string TechManager::ExtractTechKey(void* tech_ptr) {
    if (!tech_ptr) return "";

    void* key_addr = (void*)((uintptr_t)tech_ptr + 0x20);
    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)key_addr, sizeof(RawPdxString))) {
        return "";
    }

    if (raw.size == 0 || raw.size > 4096) return "";

    if (raw.capacity < 16) {
        size_t len = raw.size < 16 ? (size_t)raw.size : 15;
        char temp[16]{ 0 };
        if (SafeCopyChars(temp, raw.buf, len)) {
            temp[len] = '\0';
            return std::string(temp, len);
        }
    } else if (raw.heap_ptr) {
        uintptr_t addr = (uintptr_t)raw.heap_ptr;
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
            size_t len = raw.size < 256 ? (size_t)raw.size : 256;
            std::string result(len, '\0');
            if (SafeCopyChars(&result[0], raw.heap_ptr, len)) {
                return result;
            }
        }
    }
    return "";
}

std::string TechManager::LocalizeTechKey(const std::string& key) {
    if (key.empty() || !fn_localize_) return key;

    // Prepare input RawPdxString
    RawPdxString in_key{};
    in_key.size = key.size();
    in_key.capacity = 15;
    if (key.size() < 16) {
        memcpy(in_key.buf, key.data(), key.size());
    } else {
        // If long, just use key directly as fallback
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

    // Free the heap buffer allocated by Localize if applicable
    if (fn_free_pdx_str_) {
        SafeFreePdxStr(fn_free_pdx_str_, &out_str);
    }

    return result.empty() ? key : result;
}

uint32_t TechManager::ExtractTechTier(void* tech_ptr) {
    if (!tech_ptr) return 0;
    uint32_t tier = 0;
    SafeReadU32((const void*)((uintptr_t)tech_ptr + 0x60), &tier);
    return tier;
}

uint64_t TechManager::ExtractTechCost(void* tech_ptr) {
    if (!tech_ptr) return 0;
    uint64_t raw_cost = 0;
    SafeReadU64((const void*)((uintptr_t)tech_ptr + 0x68), &raw_cost);
    return raw_cost / 50000;
}

FullResearchState TechManager::GetResearchState() {
    FullResearchState state;

    void* tech_mgr = GetTechManagerPtr();
    if (!tech_mgr) return state;

    const char* area_names[] = { "physics", "society", "engineering" };
    ResearchAreaState* area_states[] = { &state.physics, &state.society, &state.engineering };

    for (uint32_t area = 0; area < 3; ++area) {
        ResearchAreaState* a_state = area_states[area];
        a_state->current.area = area;
        a_state->current.area_name = area_names[area];

        // 1. Check Active Research Slot: tech_mgr + 0x48 + area * 24
        uintptr_t active_vec = (uintptr_t)tech_mgr + 0x48 + area * 24;
        int32_t active_cnt = 0;
        void* active_entry = nullptr;
        SafeReadI32((const void*)(active_vec + 0x14), &active_cnt);
        SafeReadPtr((const void*)(active_vec + 8), &active_entry);

        if (active_cnt > 0 && active_entry) {
            void* tech_ptr = nullptr;
            SafeReadPtr((const void*)((uintptr_t)active_entry + 0x18), &tech_ptr);
            if (tech_ptr) {
                a_state->current.is_researching = true;
                a_state->current.tech_ptr = (uintptr_t)tech_ptr;
                a_state->current.key = ExtractTechKey(tech_ptr);
                a_state->current.name = LocalizeTechKey(a_state->current.key);
                a_state->current.tier = ExtractTechTier(tech_ptr);
                a_state->current.cost = ExtractTechCost(tech_ptr);
            }
        }

        // 2. Check Candidate Cards Pool: tech_mgr + 0x108 + area * 24
        uintptr_t cand_vec = (uintptr_t)tech_mgr + 0x108 + area * 24;
        int32_t cand_cnt = 0;
        void* cand_arr = nullptr;
        SafeReadI32((const void*)(cand_vec + 0x14), &cand_cnt);
        SafeReadPtr((const void*)(cand_vec + 8), &cand_arr);

        if (cand_cnt > 0 && cand_arr) {
            for (int32_t i = 0; i < cand_cnt; ++i) {
                void* tech_ptr = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)cand_arr + i * 8), &tech_ptr) || !tech_ptr) {
                    continue;
                }

                TechCard card;
                card.area = area;
                card.area_name = area_names[area];
                card.tech_ptr = (uintptr_t)tech_ptr;
                card.key = ExtractTechKey(tech_ptr);
                card.name = LocalizeTechKey(card.key);
                card.tier = ExtractTechTier(tech_ptr);
                card.cost = ExtractTechCost(tech_ptr);

                a_state->candidates.push_back(card);
            }
        }
    }

    return state;
}

nlohmann::json TechManager::GetResearchStateJson() {
    FullResearchState state = GetResearchState();

    auto area_to_json = [](const ResearchAreaState& area) {
        nlohmann::json obj;
        obj["area"] = area.current.area;
        obj["area_name"] = area.current.area_name;

        nlohmann::json cur;
        cur["is_researching"] = area.current.is_researching;
        if (area.current.is_researching) {
            cur["key"] = area.current.key;
            cur["name"] = area.current.name;
            cur["tier"] = area.current.tier;
            cur["cost"] = area.current.cost;
        } else {
            cur["key"] = nullptr;
            cur["name"] = nullptr;
            cur["tier"] = 0;
            cur["cost"] = 0;
        }
        obj["current"] = cur;

        nlohmann::json cands = nlohmann::json::array();
        for (const auto& card : area.candidates) {
            cands.push_back({
                {"key", card.key},
                {"name", card.name},
                {"tier", card.tier},
                {"cost", card.cost}
            });
        }
        obj["candidates"] = cands;
        return obj;
    };

    return {
        {"physics", area_to_json(state.physics)},
        {"society", area_to_json(state.society)},
        {"engineering", area_to_json(state.engineering)}
    };
}

nlohmann::json TechManager::SelectResearch(uint32_t area, const std::string& tech_key) {
    if (area > 2) {
        return {
            {"error", {
                {"code", -32041},
                {"message", "Invalid research area. Must be 0 (physics), 1 (society), or 2 (engineering)."}
            }}
        };
    }

    void* tech_mgr = GetTechManagerPtr();
    if (!tech_mgr) {
        return {
            {"error", {
                {"code", -32042},
                {"message", "Failed to locate CTechManager for player empire."}
            }}
        };
    }

    // Find the target CTechnology* in the candidate pool for this area
    uintptr_t cand_vec = (uintptr_t)tech_mgr + 0x108 + area * 24;
    int32_t cand_cnt = 0;
    void* cand_arr = nullptr;
    SafeReadI32((const void*)(cand_vec + 0x14), &cand_cnt);
    SafeReadPtr((const void*)(cand_vec + 8), &cand_arr);

    if (cand_cnt <= 0 || !cand_arr) {
        return {
            {"error", {
                {"code", -32043},
                {"message", "No candidate technologies available in this area."}
            }}
        };
    }

    void* target_tech_ptr = nullptr;
    std::string target_name;

    for (int32_t i = 0; i < cand_cnt; ++i) {
        void* t_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)cand_arr + i * 8), &t_ptr) || !t_ptr) {
            continue;
        }

        std::string key = ExtractTechKey(t_ptr);
        if (key == tech_key) {
            target_tech_ptr = t_ptr;
            target_name = LocalizeTechKey(key);
            break;
        }
    }

    if (!target_tech_ptr) {
        return {
            {"error", {
                {"code", -32044},
                {"message", "Technology key '" + tech_key + "' not found in candidate pool for this area."}
            }}
        };
    }

    if (!fn_engine_alloc_ || !fn_post_command_ || !command_vtable_) {
        return {
            {"error", {
                {"code", -32045},
                {"message", "Command dispatch pointers not initialized."}
            }}
        };
    }

    // Check if an existing technology is currently being researched in this area
    uintptr_t active_vec = (uintptr_t)tech_mgr + 0x48 + area * 24;
    int32_t active_cnt = 0;
    void* active_arr = nullptr;
    SafeReadI32((const void*)(active_vec + 0x14), &active_cnt);
    SafeReadPtr((const void*)(active_vec + 8), &active_arr);

    void* cur_tech = nullptr;
    if (active_cnt > 0 && active_arr) {
        SafeReadPtr((const void*)((uintptr_t)active_arr + 0x18), &cur_tech);
    }

    if (cur_tech == target_tech_ptr) {
        return {
            {"success", true},
            {"area", area},
            {"tech_key", tech_key},
            {"name", target_name},
            {"message", "Technology is already currently being researched"}
        };
    }

    if (cur_tech) {
        LOGF("[TECH_MGR] Area %u already researching tech 0x%p. Cancelling previous research before selecting '%s'...",
            area, cur_tech, tech_key.c_str());
        nlohmann::json cancel_res = CancelResearch(area);
        if (cancel_res.contains("error")) {
            return cancel_res;
        }
    }

    // Executed on the game main thread (called via TaskQueue::Get().Enqueue from ipc_server)
    LOGF("[TECH_MGR] Constructing CSelectTechCommand on main thread for tech '%s' (0x%p)...",
        tech_key.c_str(), target_tech_ptr);

    void* cmd = fn_engine_alloc_(0x30);
    if (!cmd) {
        return {
            {"error", {
                {"code", -32046},
                {"message", "Engine allocator returned null for CSelectTechCommand"}
            }}
        };
    }

    memset(cmd, 0, 0x30);
    *(void**)cmd = (void*)command_vtable_;
    *(uint32_t*)((uintptr_t)cmd + 0x08) = 0xFFFFFFFF;
    *(uint32_t*)((uintptr_t)cmd + 0x0C) = 0;
    *(uint32_t*)((uintptr_t)cmd + 0x10) = 0xFFFF0000;
    *(uint16_t*)((uintptr_t)cmd + 0x14) = 0;
    *(uint8_t*)((uintptr_t)cmd + 0x16) = 0;
    *(uint32_t*)((uintptr_t)cmd + 0x18) = 0;
    *(uint32_t*)((uintptr_t)cmd + 0x20) = 0; // player country id
    *(void**)((uintptr_t)cmd + 0x28) = target_tech_ptr;

    if (!SafePostCommand(fn_post_command_, cmd)) {
        LOGF("[TECH_MGR] Exception occurred during PostCommand!");
        return {
            {"error", {
                {"code", -32047},
                {"message", "Exception occurred executing PostCommand for CSelectTechCommand"}
            }}
        };
    }

    LOGF("[TECH_MGR] CSelectTechCommand posted successfully.");
    return {
        {"success", true},
        {"area", area},
        {"tech_key", tech_key},
        {"name", target_name},
        {"message", "Technology selection command posted successfully"}
    };
}

nlohmann::json TechManager::CancelResearch(uint32_t area) {
    if (area > 2) {
        return {
            {"error", {
                {"code", -32041},
                {"message", "Invalid research area index. Must be 0 (physics), 1 (society), or 2 (engineering)."}
            }}
        };
    }

    void* tech_mgr = GetTechManagerPtr();
    if (!tech_mgr) {
        return {
            {"error", {
                {"code", -32042},
                {"message", "Failed to locate CTechManager for player empire."}
            }}
        };
    }

    uintptr_t active_vec = (uintptr_t)tech_mgr + 0x48 + area * 24;
    int32_t active_cnt = 0;
    void* active_arr = nullptr;
    SafeReadI32((const void*)(active_vec + 0x14), &active_cnt);
    SafeReadPtr((const void*)(active_vec + 8), &active_arr);

    void* cur_tech = nullptr;
    if (active_cnt > 0 && active_arr) {
        SafeReadPtr((const void*)((uintptr_t)active_arr + 0x18), &cur_tech);
    }

    if (!cur_tech) {
        return {
            {"success", true},
            {"area", area},
            {"message", "No technology is currently being researched in this area."}
        };
    }

    std::string key = ExtractTechKey(cur_tech);
    std::string name = LocalizeTechKey(key);

    LOGF("[TECH_MGR] Cancelling research in area %u: tech '%s' (0x%p)...", area, key.c_str(), cur_tech);

    if (!fn_engine_alloc_ || !cancel_vtable_ || !fn_cancel_execute_) {
        return {
            {"error", {
                {"code", -32045},
                {"message", "Cancel dispatch pointers not initialized."}
            }}
        };
    }

    void* cancel_cmd = fn_engine_alloc_(0x30);
    if (!cancel_cmd) {
        return {
            {"error", {
                {"code", -32046},
                {"message", "Engine allocator returned null for CCancelTechCommand"}
            }}
        };
    }

    memset(cancel_cmd, 0, 0x30);
    *(void**)cancel_cmd = (void*)cancel_vtable_;
    *(uint32_t*)((uintptr_t)cancel_cmd + 0x08) = 0xFFFFFFFF;
    *(uint32_t*)((uintptr_t)cancel_cmd + 0x0C) = 0;
    *(uint16_t*)((uintptr_t)cancel_cmd + 0x10) = 0;
    *(uint16_t*)((uintptr_t)cancel_cmd + 0x12) = 0xFFFF;
    *(uint8_t*)((uintptr_t)cancel_cmd + 0x16) = 0;
    *(uint32_t*)((uintptr_t)cancel_cmd + 0x18) = 0;
    *(uint32_t*)((uintptr_t)cancel_cmd + 0x20) = 0; // player country ID
    *(void**)((uintptr_t)cancel_cmd + 0x28) = cur_tech;

    fn_cancel_execute_(cancel_cmd);

    if (fn_scalar_dtor_) {
        fn_scalar_dtor_(cancel_cmd, 1);
    }

    LOGF("[TECH_MGR] CCancelTechCommand executed successfully on main thread.");
    return {
        {"success", true},
        {"area", area},
        {"cancelled_tech_key", key},
        {"name", name},
        {"message", "Technology research cancelled successfully"}
    };
}

} // namespace bridge
