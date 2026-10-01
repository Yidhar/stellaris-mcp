#include "fleet_manager.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "fleet_access.hpp"
#include "task_queue.hpp"
#include "command_builder.hpp"
#include "game_state.hpp"
#include <windows.h>
#include <sstream>

namespace bridge {

static bool SafeReadPtr(const void* addr, void** out) {
    if (!addr || !out) return false;
    __try {
        *out = *(void**)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(const void* addr, uint32_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const uint32_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeWriteU32(void* addr, uint32_t val) {
    if (!addr) return false;
    __try {
        *(uint32_t*)addr = val;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU64(const void* addr, uint64_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const uint64_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
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

FleetManager& FleetManager::Get() {
    static FleetManager instance;
    return instance;
}

bool FleetManager::Init(uintptr_t base_address) {
    base_address_ = base_address;
    if (!base_address_) return false;


    LOGF("[FLEET_MGR] Initialized with base address: 0x%llX", (unsigned long long)base_address_);
    return true;
}

void* FleetManager::GetPlayerCountry() {
    return GameState::Get().GetPlayerCountry();  // the local player's country (not country 0)
}

void* FleetManager::FindFleetTemplate(uint32_t template_id) {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CFleetTemplate), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
    if (!mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }

    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &cap) || cap == 0) {
        return nullptr;
    }

    uint32_t slot = template_id & 0xFFFFFF;
    if (slot < cap) {
        void* ptr = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)arr + slot * 16 + 8), &ptr) && ptr) {
            uint32_t check_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)ptr + 8), &check_id) && check_id == template_id) {
                return ptr;
            }
        }
    }
    return nullptr;
}

void* FleetManager::FindFleet(uint32_t fleet_id) {
    return fleets::Find(base_address_, fleet_id);
}

FleetSummary FleetManager::GetFleetSummary() {
    FleetSummary summary{};
    auto fleets = GetFleets(false);
    summary.military_fleets_count = (uint32_t)fleets.size();

    for (const auto& f : fleets) {
        summary.total_military_power += f.military_power;
        if (f.can_reinforce) {
            summary.total_reinforceable_fleets++;
        }
    }

    void* country = GetPlayerCountry();
    if (country) {
        for (uint32_t fid : fleets::Owned(country)) {
            if (fleets::IsCivilianShip(fleets::ClassOf(FindFleet(fid)))) {
                summary.civilian_fleets_count++;
            }
        }
    }

    return summary;
}

std::vector<FleetInfo> FleetManager::GetFleets(bool include_civilian, uint32_t specific_fleet_id) {
    std::vector<FleetInfo> result;
    void* country = GetPlayerCountry();
    if (!country) return result;

    void* vec_ptr = nullptr;
    uint32_t vec_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)country + 0x2648 + 8), &vec_ptr) || !vec_ptr ||
        !SafeReadU32((const void*)((uintptr_t)country + 0x2648 + 0x14), &vec_cnt) || vec_cnt == 0) {
        return result;
    }

    for (uint32_t i = 0; i < vec_cnt && i < 256; ++i) {
        uint32_t template_id = 0;
        if (!SafeReadU32((const void*)((uintptr_t)vec_ptr + i * 4), &template_id)) {
            continue;
        }

        void* ft_obj = FindFleetTemplate(template_id);
        if (!ft_obj) continue;

        uint32_t associated_fleet_id = 0;
        SafeReadU32((const void*)((uintptr_t)ft_obj + 0x88), &associated_fleet_id);

        if (specific_fleet_id != 0xFFFFFFFF && associated_fleet_id != specific_fleet_id && template_id != specific_fleet_id) {
            continue;
        }

        FleetInfo info{};
        info.fleet_id = associated_fleet_id;
        info.template_id = template_id;

        // Try reading fleet name
        void* fleet_obj = FindFleet(associated_fleet_id);
        if (fleet_obj) {
            info.name = fleets::Name(fleet_obj);
            if (info.name.empty()) {
                info.name = "第 " + std::to_string(i + 1) + " 舰队";
            }
            info.military_power = fleets::MilitaryPower(fleet_obj);
        } else {
            info.name = "舰队模板 #" + std::to_string(template_id);
        }

        // Read designs array
        void* designs_arr = nullptr;
        uint32_t designs_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)ft_obj + 0x28), &designs_arr) && designs_arr &&
            SafeReadU32((const void*)((uintptr_t)ft_obj + 0x34), &designs_cnt) && designs_cnt > 0) {

            for (uint32_t d = 0; d < designs_cnt && d < 64; ++d) {
                uintptr_t d_entry = (uintptr_t)designs_arr + d * 0x560;
                ShipDesignQuotaInfo d_info{};

                SafeReadU32((const void*)(d_entry + 0x28), &d_info.design_id);
                
                // Read actual count of ships of this design
                uint32_t actual = 0;
                if (!SafeReadU32((const void*)(d_entry + 0x4b0), &actual) || actual == 0) {
                    SafeReadU32((const void*)(d_entry + 0x108), &actual);
                }
                d_info.actual_count = actual;
                SafeReadU32((const void*)(d_entry + 0x558), &d_info.target_quota);

                d_info.deficit = (int32_t)d_info.target_quota - (int32_t)d_info.actual_count;
                if (d_info.deficit > 0) {
                    info.can_reinforce = true;
                }

                void* ship_design_ptr = nullptr;
                if (SafeReadPtr((const void*)(d_entry + 0x40), &ship_design_ptr) && ship_design_ptr) {
                    std::string d_name;
                    if (SafeReadPdxString((const void*)((uintptr_t)ship_design_ptr + 0x20), d_name) && !d_name.empty()) {
                        d_info.design_name = d_name;
                    }
                }
                if (d_info.design_name.empty()) {
                    d_info.design_name = "Design #" + std::to_string(d_info.design_id);
                }

                info.total_ships += d_info.actual_count;
                info.total_quota += d_info.target_quota;
                info.designs.push_back(d_info);
            }
        }

        result.push_back(info);
    }

    if (include_civilian) {
        for (uint32_t fid : fleets::Owned(country)) {
            void* flt = FindFleet(fid);
            if (!fleets::IsCivilianShip(fleets::ClassOf(flt))) continue;
            if (specific_fleet_id != 0xFFFFFFFF && fid != specific_fleet_id) continue;

            FleetInfo civ_info{};
            civ_info.fleet_id = fid;
            civ_info.template_id = 0xFFFFFFFF;
            civ_info.name = fleets::Name(flt);
            if (civ_info.name.empty()) {
                civ_info.name = "民用船队 #" + std::to_string(fid);
            }
            civ_info.military_power = 0.0;
            civ_info.total_ships = 1;
            civ_info.total_quota = 1;
            civ_info.can_reinforce = false;
            result.push_back(civ_info);
        }
    }

    return result;
}

bool FleetManager::ReinforceFleet(uint32_t fleet_id, std::string& out_message) {
    void* fleet_obj = FindFleet(fleet_id);
    if (!fleet_obj) {
        // Also check if fleet_id is template_id
        void* ft_obj = FindFleetTemplate(fleet_id);
        if (!ft_obj) {
            out_message = "Fleet or template ID " + std::to_string(fleet_id) + " not found";
            return false;
        }
        uint32_t assoc = 0;
        if (SafeReadU32((const void*)((uintptr_t)ft_obj + 0x88), &assoc) && assoc != 0xFFFFFFFF) {
            fleet_id = assoc;
        }
    }

    uint32_t template_id = fleet_id;
    void* country = GetPlayerCountry();
    if (country) {
        void* vec_ptr = nullptr;
        uint32_t vec_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)country + 0x2648 + 8), &vec_ptr) && vec_ptr &&
            SafeReadU32((const void*)((uintptr_t)country + 0x2648 + 0x14), &vec_cnt)) {
            for (uint32_t i = 0; i < vec_cnt; ++i) {
                uint32_t tid = 0;
                if (SafeReadU32((const void*)((uintptr_t)vec_ptr + i * 4), &tid)) {
                    void* candidate = FindFleetTemplate(tid);
                    if (candidate) {
                        uint32_t assoc = 0;
                        SafeReadU32((const void*)((uintptr_t)candidate + 0x88), &assoc);
                        if (tid == fleet_id || assoc == fleet_id) {
                            template_id = tid;
                            break;
                        }
                    }
                }
            }
        }
    }

    namespace reinforce = sdk::cmd::reinforce_fleet_command;
    auto cmd = CommandBuilder::Get().Create(reinforce::kSpec);
    cmd.Set<uint32_t>(reinforce::country, GameState::Get().GetPlayerCountryId())
       .Set<uint32_t>(reinforce::fleet_template, template_id);
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

    out_message = "CReinforceFleetCommand dispatched successfully for fleet " + std::to_string(fleet_id);
    LOGF("[FLEET_MGR] Reinforce fleet command posted for fleet %u", fleet_id);
    return true;
}

bool FleetManager::SetFleetTemplateQuota(uint32_t fleet_id, uint32_t design_id, uint32_t target_quota, std::string& out_message) {
    void* ft_obj = nullptr;
    // Search player's own templates vector first to avoid colliding with AI empires
    void* country = GetPlayerCountry();
    if (country) {
        void* vec_ptr = nullptr;
        uint32_t vec_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)country + 0x2648 + 8), &vec_ptr) && vec_ptr &&
            SafeReadU32((const void*)((uintptr_t)country + 0x2648 + 0x14), &vec_cnt)) {
            for (uint32_t i = 0; i < vec_cnt; ++i) {
                uint32_t tid = 0;
                if (SafeReadU32((const void*)((uintptr_t)vec_ptr + i * 4), &tid)) {
                    void* candidate = FindFleetTemplate(tid);
                    if (candidate) {
                        uint32_t assoc = 0;
                        SafeReadU32((const void*)((uintptr_t)candidate + 0x88), &assoc);
                        if (tid == fleet_id || assoc == fleet_id) {
                            ft_obj = candidate;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!ft_obj) {
        ft_obj = FindFleetTemplate(fleet_id);
    }

    if (!ft_obj) {
        out_message = "Could not find fleet template for fleet ID " + std::to_string(fleet_id);
        return false;
    }

    void* designs_arr = nullptr;
    uint32_t designs_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)ft_obj + 0x28), &designs_arr) || !designs_arr ||
        !SafeReadU32((const void*)((uintptr_t)ft_obj + 0x34), &designs_cnt) || designs_cnt == 0) {
        out_message = "Fleet template has no design entries";
        return false;
    }

    // Locate design entry
    uintptr_t target_entry = 0;
    for (uint32_t d = 0; d < designs_cnt; ++d) {
        uintptr_t entry = (uintptr_t)designs_arr + d * 0x560;
        uint32_t did = 0;
        if (SafeReadU32((const void*)(entry + 0x28), &did) && did == design_id) {
            target_entry = entry;
            break;
        }
    }

    if (!target_entry) {
        out_message = "Design ID " + std::to_string(design_id) + " not found in template";
        return false;
    }

    // Direct write to target_entry + 0x558 (already executing on main thread via TaskQueue)
    if (SafeWriteU32((void*)(target_entry + 0x558), target_quota)) {
        out_message = "Fleet template quota updated to " + std::to_string(target_quota) + " for design " + std::to_string(design_id);
        LOGF("[FLEET_MGR] Set quota to %u for design %u", target_quota, design_id);
        return true;
    }

    out_message = "Exception writing to fleet template memory";
    return false;
}

nlohmann::json FleetManager::GetSummaryJson() {
    auto summary = GetFleetSummary();
    return {
        {"military_fleets_count", summary.military_fleets_count},
        {"civilian_fleets_count", summary.civilian_fleets_count},
        {"total_military_power", summary.total_military_power},
        {"total_reinforceable_fleets", summary.total_reinforceable_fleets}
    };
}

nlohmann::json FleetManager::GetFleetsJson(const nlohmann::json& params) {
    bool include_civilian = false;
    if (params.contains("include_civilian") && params["include_civilian"].is_boolean()) {
        include_civilian = params["include_civilian"].get<bool>();
    }
    uint32_t specific_id = 0xFFFFFFFF;
    if (params.contains("fleet_id") && params["fleet_id"].is_number()) {
        specific_id = params["fleet_id"].get<uint32_t>();
    }

    auto fleets = GetFleets(include_civilian, specific_id);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& f : fleets) {
        nlohmann::json d_arr = nlohmann::json::array();
        for (const auto& d : f.designs) {
            d_arr.push_back({
                {"design_id", d.design_id},
                {"design_name", d.design_name},
                {"actual_count", d.actual_count},
                {"target_quota", d.target_quota},
                {"deficit", d.deficit}
            });
        }
        arr.push_back({
            {"fleet_id", f.fleet_id},
            {"template_id", f.template_id},
            {"name", f.name},
            {"military_power", f.military_power},
            {"total_ships", f.total_ships},
            {"total_quota", f.total_quota},
            {"can_reinforce", f.can_reinforce},
            {"designs", d_arr}
        });
    }
    return {
        {"fleet_count", fleets.size()},
        {"fleets_count", fleets.size()},
        {"fleets", arr}
    };
}

nlohmann::json FleetManager::ReinforceFleetJson(const nlohmann::json& params) {
    if (!params.contains("fleet_id") || !params["fleet_id"].is_number()) {
        return {
            {"success", false},
            {"error", "Missing or invalid 'fleet_id' parameter"}
        };
    }
    uint32_t fleet_id = params["fleet_id"].get<uint32_t>();
    std::string msg;
    bool ok = ReinforceFleet(fleet_id, msg);
    return {
        {"success", ok},
        {"fleet_id", fleet_id},
        {"message", msg}
    };
}

nlohmann::json FleetManager::SetFleetTemplateQuotaJson(const nlohmann::json& params) {
    if (!params.contains("fleet_id") || !params["fleet_id"].is_number() ||
        !params.contains("design_id") || !params["design_id"].is_number() ||
        !params.contains("target_quota") || !params["target_quota"].is_number()) {
        return {
            {"success", false},
            {"error", "Missing required parameters: 'fleet_id', 'design_id', 'target_quota'"}
        };
    }
    uint32_t fleet_id = params["fleet_id"].get<uint32_t>();
    uint32_t design_id = params["design_id"].get<uint32_t>();
    uint32_t target_quota = params["target_quota"].get<uint32_t>();
    std::string msg;
    bool ok = SetFleetTemplateQuota(fleet_id, design_id, target_quota, msg);
    return {
        {"success", ok},
        {"fleet_id", fleet_id},
        {"design_id", design_id},
        {"target_quota", target_quota},
        {"message", msg}
    };
}

} // namespace bridge
