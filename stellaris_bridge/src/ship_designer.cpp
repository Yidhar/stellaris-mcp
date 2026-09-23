#include "ship_designer.hpp"
#include <windows.h>
#include <algorithm>

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

static bool SafeWritePtr(void* addr, void* val) {
    if (!addr) return false;
    __try {
        *(void**)addr = val;
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

static bool SafeWritePdxString(void* pdx_str_addr, const std::string& new_str) {
    if (!pdx_str_addr) return false;
    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)pdx_str_addr, sizeof(RawPdxString))) {
        return false;
    }

    if (raw.capacity < 16 && new_str.size() < 16) {
        char temp[16]{ 0 };
        memcpy(temp, new_str.data(), new_str.size());
        SafeCopyChars(raw.buf, temp, 16);
        raw.size = new_str.size();
        return SafeCopyChars((char*)pdx_str_addr, (const char*)&raw, sizeof(RawPdxString));
    } else if (raw.capacity >= 16 && raw.heap_ptr && new_str.size() <= raw.capacity) {
        SafeCopyChars(raw.heap_ptr, new_str.data(), new_str.size());
        raw.heap_ptr[new_str.size()] = '\0';
        raw.size = new_str.size();
        return SafeCopyChars((char*)pdx_str_addr, (const char*)&raw, sizeof(RawPdxString));
    }
    return false;
}

ShipDesigner& ShipDesigner::Get() {
    static ShipDesigner instance;
    return instance;
}

bool ShipDesigner::Init(uintptr_t base_address) {
    base_address_ = base_address;
    if (!base_address_) return false;

    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);

    LOGF("[SHIP_DESIGNER] Initialized with Base=0x%llX, Alloc=0x%llX, PostCmd=0x%llX",
         (unsigned long long)base_address_,
         (unsigned long long)fn_engine_alloc_,
         (unsigned long long)fn_post_command_);
    return true;
}

void* ShipDesigner::GetPlayerCountry() {
    if (!base_address_) return nullptr;
    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3112F50), &mgr) || !mgr) return nullptr;
    void* countries_arr = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &countries_arr) || !countries_arr) return nullptr;
    void* player_country = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)countries_arr + 8), &player_country)) return nullptr;
    return player_country;
}

void* ShipDesigner::FindShipDesign(uint32_t design_id) {
    if (!base_address_) return nullptr;
    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3112980), &mgr) || !mgr) return nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &cap)) return nullptr;

    uint32_t idx = design_id & 0xFFFFFF;
    if (idx >= cap) return nullptr;

    void* candidate = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)arr + idx * 16 + 8), &candidate) || !candidate) return nullptr;

    uint32_t actual_id = 0;
    if (SafeReadU32((const void*)((uintptr_t)candidate + 0x10), &actual_id) && actual_id == design_id) {
        return candidate;
    }
    return nullptr;
}

void* ShipDesigner::FindFleet(uint32_t fleet_id) {
    if (!base_address_) return nullptr;
    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113008), &mgr) || !mgr) return nullptr;
    void* arr = nullptr;
    uint32_t cap = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &cap)) return nullptr;

    uint32_t idx = fleet_id & 0xFFFFFF;
    if (idx >= cap) return nullptr;

    void* candidate = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)arr + idx * 16 + 8), &candidate) || !candidate) return nullptr;

    uint32_t actual_id = 0;
    if (SafeReadU32((const void*)((uintptr_t)candidate + 0x08), &actual_id) && actual_id == fleet_id) {
        return candidate;
    }
    return nullptr;
}

void ShipDesigner::BuildComponentIndexIfNeeded() {
    if (component_cache_built_ && !component_cache_.empty()) return;
    if (!base_address_) return;

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3156198), &comp_db) || !comp_db) return;

    void* arr = nullptr;
    uint32_t cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)comp_db + 0x20), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)comp_db + 0x2C), &cnt) || cnt == 0) return;

    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        void* c_vec = nullptr;
        uint32_t c_cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)set_ptr + 0xB8), &c_vec) || !c_vec ||
            !SafeReadU32((const void*)((uintptr_t)set_ptr + 0xC4), &c_cnt)) continue;

        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* p_tmpl = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)c_vec + j * 8), &p_tmpl) || !p_tmpl) continue;

            std::string key;
            if (SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), key) && !key.empty()) {
                component_cache_[key] = p_tmpl;
            }
        }
    }

    component_cache_built_ = true;
    LOGF("[SHIP_DESIGNER] Indexed %zu component templates", component_cache_.size());
}

void* ShipDesigner::FindComponentTemplate(const std::string& component_key) {
    BuildComponentIndexIfNeeded();
    auto it = component_cache_.find(component_key);
    if (it != component_cache_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<ShipDesignInfo> ShipDesigner::GetShipDesigns(uint32_t specific_design_id) {
    std::vector<ShipDesignInfo> results;
    void* country = GetPlayerCountry();
    if (!country) return results;

    void* arr_ptr = nullptr;
    uint32_t count = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)country + 0x1AD0), &arr_ptr) || !arr_ptr ||
        !SafeReadU32((const void*)((uintptr_t)country + 0x1ADC), &count)) {
        return results;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t did = 0;
        if (!SafeReadU32((const void*)((uintptr_t)arr_ptr + i * 4), &did)) continue;
        if (specific_design_id != 0xFFFFFFFF && did != specific_design_id) continue;

        void* design = FindShipDesign(did);
        if (!design) continue;

        ShipDesignInfo d_info{};
        d_info.design_id = did;

        SafeReadPdxString((const void*)((uintptr_t)design + 0x50), d_info.name);
        SafeReadPdxString((const void*)((uintptr_t)design + 0xA8), d_info.class_prefix);

        void* p_sub = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)design + 0x20), &p_sub) && p_sub) {
            void* p_size = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)p_sub + 0x08), &p_size) && p_size) {
                SafeReadPdxString((const void*)((uintptr_t)p_size + 0x20), d_info.ship_size);
            }

            // Sections
            void* sec_arr = nullptr;
            uint32_t sec_cnt = 0;
            if (SafeReadPtr((const void*)((uintptr_t)p_sub + 0x18), &sec_arr) && sec_arr &&
                SafeReadU32((const void*)((uintptr_t)p_sub + 0x20), &sec_cnt)) {
                for (uint32_t s = 0; s < sec_cnt; ++s) {
                    void* p_sec = nullptr;
                    if (!SafeReadPtr((const void*)((uintptr_t)sec_arr + s * 0x80), &p_sec) || !p_sec) continue;

                    SectionInfo s_info{};
                    SafeReadPdxString((const void*)((uintptr_t)p_sec + 0x18), s_info.name);

                    void* slots_arr = nullptr;
                    uint32_t slots_cnt = 0;
                    if (SafeReadPtr((const void*)((uintptr_t)p_sec + 0x50), &slots_arr) && slots_arr &&
                        SafeReadU32((const void*)((uintptr_t)p_sec + 0x58), &slots_cnt)) {
                        for (uint32_t k = 0; k < slots_cnt; ++k) {
                            uintptr_t slot_p = (uintptr_t)slots_arr + k * 0x20;
                            void* slot_def = nullptr;
                            void* slot_comp = nullptr;
                            SafeReadPtr((const void*)(slot_p + 8), &slot_def);
                            SafeReadPtr((const void*)(slot_p + 0x10), &slot_comp);

                            SlotInfo sl_info{};
                            sl_info.slot_index = k;
                            if (slot_def) {
                                SafeReadPdxString((const void*)((uintptr_t)slot_def + 0x18), sl_info.slot_name);
                            }
                            if (slot_comp) {
                                SafeReadPdxString((const void*)((uintptr_t)slot_comp + 0x1B0), sl_info.component_key);
                            }
                            s_info.slots.push_back(sl_info);
                        }
                    }
                    d_info.sections.push_back(s_info);
                }
            }

            // Core components
            void* comp_arr = nullptr;
            uint32_t comp_cnt = 0;
            if (SafeReadPtr((const void*)((uintptr_t)p_sub + 0x30), &comp_arr) && comp_arr &&
                SafeReadU32((const void*)((uintptr_t)p_sub + 0x38), &comp_cnt)) {
                for (uint32_t c = 0; c < comp_cnt; ++c) {
                    void* p_comp = nullptr;
                    if (!SafeReadPtr((const void*)((uintptr_t)comp_arr + c * 8), &p_comp) || !p_comp) continue;
                    std::string c_key;
                    SafeReadPdxString((const void*)((uintptr_t)p_comp + 0x1B0), c_key);
                    if (c == 0) d_info.core_components.reactor = c_key;
                    else if (c == 1) d_info.core_components.ftl = c_key;
                    else if (c == 2) d_info.core_components.thruster = c_key;
                    else if (c == 3) d_info.core_components.sensor = c_key;
                    else if (c == 4) d_info.core_components.combat_computer = c_key;
                }
            }
        }

        results.push_back(d_info);
    }
    return results;
}

nlohmann::json ShipDesigner::GetShipDesignsJson(const nlohmann::json& params) {
    uint32_t did = 0xFFFFFFFF;
    if (params.contains("design_id") && params["design_id"].is_number()) {
        did = params["design_id"].get<uint32_t>();
    }

    auto list = GetShipDesigns(did);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : list) {
        nlohmann::json dj;
        dj["design_id"] = d.design_id;
        dj["name"] = d.name;
        dj["ship_size"] = d.ship_size;
        dj["class_prefix"] = d.class_prefix;

        nlohmann::json sec_arr = nlohmann::json::array();
        for (const auto& s : d.sections) {
            nlohmann::json sj;
            sj["name"] = s.name;
            nlohmann::json sl_arr = nlohmann::json::array();
            for (const auto& sl : s.slots) {
                sl_arr.push_back({
                    {"slot_index", sl.slot_index},
                    {"slot_name", sl.slot_name},
                    {"component_key", sl.component_key}
                });
            }
            sj["slots"] = sl_arr;
            sec_arr.push_back(sj);
        }
        dj["sections"] = sec_arr;

        dj["core_components"] = {
            {"reactor", d.core_components.reactor},
            {"ftl", d.core_components.ftl},
            {"thruster", d.core_components.thruster},
            {"sensor", d.core_components.sensor},
            {"combat_computer", d.core_components.combat_computer}
        };

        arr.push_back(dj);
    }

    return {
        {"count", arr.size()},
        {"designs", arr}
    };
}

nlohmann::json ShipDesigner::GetShipDesignCatalogJson(const nlohmann::json& params) {
    BuildComponentIndexIfNeeded();

    std::string cat_filter;
    if (params.contains("category") && params["category"].is_string()) {
        cat_filter = params["category"].get<std::string>();
        std::transform(cat_filter.begin(), cat_filter.end(), cat_filter.begin(), ::tolower);
    }

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3156198), &comp_db) || !comp_db) {
        return { {"error", "Component database not found"} };
    }

    void* arr = nullptr;
    uint32_t cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)comp_db + 0x20), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)comp_db + 0x2C), &cnt)) {
        return { {"error", "Failed to read component sets"} };
    }

    nlohmann::json components_json = nlohmann::json::array();

    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        std::string set_key, loc_name, icon_gfx;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x50), loc_name);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x80), icon_gfx);

        void* c_vec = nullptr;
        uint32_t c_cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)set_ptr + 0xB8), &c_vec) || !c_vec ||
            !SafeReadU32((const void*)((uintptr_t)set_ptr + 0xC4), &c_cnt)) continue;

        nlohmann::json variants = nlohmann::json::array();
        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* p_tmpl = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)c_vec + j * 8), &p_tmpl) || !p_tmpl) continue;

            std::string t_key;
            SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), t_key);

            std::string size_str = "standard";
            if (t_key.rfind("SMALL_", 0) == 0) size_str = "small";
            else if (t_key.rfind("MEDIUM_", 0) == 0) size_str = "medium";
            else if (t_key.rfind("LARGE_", 0) == 0) size_str = "large";
            else if (t_key.rfind("AUX_", 0) == 0) size_str = "aux";

            variants.push_back({
                {"component_key", t_key},
                {"size", size_str}
            });
        }

        if (!variants.empty()) {
            components_json.push_back({
                {"set_key", set_key},
                {"localized_name", loc_name},
                {"icon", icon_gfx},
                {"variants", variants}
            });
        }
    }

    std::vector<std::string> standard_sizes = {
        "corvette", "destroyer", "cruiser", "battleship", "titan",
        "colossus", "juggernaut", "science", "constructor", "colonizer"
    };

    return {
        {"total_component_sets", components_json.size()},
        {"components", components_json},
        {"ship_sizes", standard_sizes}
    };
}

bool ShipDesigner::UpdateShipDesign(uint32_t design_id, const std::string& new_name,
                                    const nlohmann::json& slots_json, const nlohmann::json& cores_json,
                                    std::string& out_message) {
    void* design = FindShipDesign(design_id);
    if (!design) {
        out_message = "Ship design ID " + std::to_string(design_id) + " not found";
        return false;
    }

    if (!new_name.empty()) {
        SafeWritePdxString((void*)((uintptr_t)design + 0x50), new_name);
    }

    void* p_sub = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)design + 0x20), &p_sub) || !p_sub) {
        out_message = "Failed to access ship design sub-structure";
        return false;
    }

    uint32_t replaced_slots = 0;

    // Update Section slots
    if (slots_json.is_array()) {
        void* sec_arr = nullptr;
        uint32_t sec_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)p_sub + 0x18), &sec_arr) && sec_arr &&
            SafeReadU32((const void*)((uintptr_t)p_sub + 0x20), &sec_cnt)) {

            for (const auto& item : slots_json) {
                if (!item.contains("component_key")) continue;
                std::string comp_key = item["component_key"].get<std::string>();
                void* p_tmpl = FindComponentTemplate(comp_key);
                if (!p_tmpl) {
                    out_message = "Component key '" + comp_key + "' not found in catalog";
                    return false;
                }

                int target_slot_idx = item.value("slot_index", -1);
                std::string target_slot_name = item.value("slot_name", "");

                // Traverse sections to find matching slot
                for (uint32_t s = 0; s < sec_cnt; ++s) {
                    void* p_sec = nullptr;
                    if (!SafeReadPtr((const void*)((uintptr_t)sec_arr + s * 0x80), &p_sec) || !p_sec) continue;

                    void* slots_arr = nullptr;
                    uint32_t slots_cnt = 0;
                    if (!SafeReadPtr((const void*)((uintptr_t)p_sec + 0x50), &slots_arr) || !slots_arr ||
                        !SafeReadU32((const void*)((uintptr_t)p_sec + 0x58), &slots_cnt)) continue;

                    for (uint32_t k = 0; k < slots_cnt; ++k) {
                        uintptr_t slot_p = (uintptr_t)slots_arr + k * 0x20;
                        void* slot_def = nullptr;
                        SafeReadPtr((const void*)(slot_p + 8), &slot_def);
                        std::string sl_name;
                        if (slot_def) {
                            SafeReadPdxString((const void*)((uintptr_t)slot_def + 0x18), sl_name);
                        }

                        if ((target_slot_idx != -1 && (int)k == target_slot_idx) ||
                            (!target_slot_name.empty() && sl_name == target_slot_name)) {
                            SafeWritePtr((void*)(slot_p + 0x10), p_tmpl);
                            replaced_slots++;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Update Core components
    if (cores_json.is_object()) {
        void* comp_arr = nullptr;
        uint32_t comp_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)p_sub + 0x30), &comp_arr) && comp_arr &&
            SafeReadU32((const void*)((uintptr_t)p_sub + 0x38), &comp_cnt)) {

            auto update_core = [&](const std::string& field, uint32_t core_idx) {
                if (cores_json.contains(field) && core_idx < comp_cnt) {
                    std::string key = cores_json[field].get<std::string>();
                    void* p_tmpl = FindComponentTemplate(key);
                    if (p_tmpl) {
                        SafeWritePtr((void*)((uintptr_t)comp_arr + core_idx * 8), p_tmpl);
                        replaced_slots++;
                    }
                }
            };

            update_core("reactor", 0);
            update_core("ftl", 1);
            update_core("thruster", 2);
            update_core("sensor", 3);
            update_core("combat_computer", 4);
        }
    }

    out_message = "Ship design " + std::to_string(design_id) + " successfully updated with " +
                  std::to_string(replaced_slots) + " component changes";
    LOGF("[SHIP_DESIGNER] %s", out_message.c_str());
    return true;
}

nlohmann::json ShipDesigner::UpdateShipDesignJson(const nlohmann::json& params) {
    if (!params.contains("design_id") || !params["design_id"].is_number()) {
        return {
            {"error", {
                {"code", -32602},
                {"message", "Missing required parameter: design_id"}
            }}
        };
    }

    uint32_t did = params["design_id"].get<uint32_t>();
    std::string new_name = params.value("name", "");
    nlohmann::json slots = params.value("slots", nlohmann::json::array());
    nlohmann::json cores = params.value("core_components", nlohmann::json::object());

    std::string msg;
    bool ok = UpdateShipDesign(did, new_name, slots, cores, msg);
    if (!ok) {
        return {
            {"error", {
                {"code", -32001},
                {"message", msg}
            }}
        };
    }

    return {
        {"success", true},
        {"design_id", did},
        {"message", msg}
    };
}

bool ShipDesigner::UpgradeFleet(uint32_t fleet_id, uint32_t starbase_id, uint32_t target_design_id,
                               std::string& out_message) {
    if (!fn_engine_alloc_ || !fn_post_command_) {
        out_message = "Engine functions not initialized";
        return false;
    }

    void* fleet = FindFleet(fleet_id);
    if (!fleet) {
        out_message = "Fleet ID " + std::to_string(fleet_id) + " not found";
        return false;
    }

    // Allocate 0x30 bytes for CFleetUpgradeDesignCommand (Opcode 0x2F93)
    void* pCmd = fn_engine_alloc_(0x30);
    if (!pCmd) {
        out_message = "Engine memory allocation failed";
        return false;
    }

    memset(pCmd, 0, 0x30);
    *(uintptr_t*)pCmd = base_address_ + 0x23B98B0; // VTable
    *(uint32_t*)((uintptr_t)pCmd + 0x08) = 0; // country_id (player = 0)
    *(uint32_t*)((uintptr_t)pCmd + 0x0C) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x10) = 0xFFFF0000;
    *(uint16_t*)((uintptr_t)pCmd + 0x14) = 0;
    *(uint8_t*)((uintptr_t)pCmd + 0x16) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x18) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x20) = fleet_id;
    *(uint32_t*)((uintptr_t)pCmd + 0x24) = starbase_id; // 0xFFFFFFFF for nearest
    *(uint32_t*)((uintptr_t)pCmd + 0x28) = target_design_id; // 0xFFFFFFFF for all designs
    *(uint8_t*)((uintptr_t)pCmd + 0x2C) = 0;
    *(uint8_t*)((uintptr_t)pCmd + 0x2D) = 0;

    fn_post_command_(pCmd, 0);

    out_message = "CFleetUpgradeDesignCommand posted successfully for fleet " + std::to_string(fleet_id);
    LOGF("[SHIP_DESIGNER] Fleet upgrade command dispatched for fleet %u", fleet_id);
    return true;
}

nlohmann::json ShipDesigner::UpgradeFleetJson(const nlohmann::json& params) {
    if (!params.contains("fleet_id") || !params["fleet_id"].is_number()) {
        return {
            {"error", {
                {"code", -32602},
                {"message", "Missing required parameter: fleet_id"}
            }}
        };
    }

    uint32_t fid = params["fleet_id"].get<uint32_t>();
    uint32_t sbid = params.value("starbase_id", 0xFFFFFFFF);
    uint32_t tdid = params.value("target_design_id", 0xFFFFFFFF);

    std::string msg;
    bool ok = UpgradeFleet(fid, sbid, tdid, msg);
    if (!ok) {
        return {
            {"error", {
                {"code", -32002},
                {"message", msg}
            }}
        };
    }

    return {
        {"success", true},
        {"fleet_id", fid},
        {"message", msg}
    };
}

bool ShipDesigner::DeleteShipDesign(uint32_t design_id, std::string& out_message) {
    if (!fn_engine_alloc_ || !fn_post_command_) {
        out_message = "Engine functions not initialized";
        return false;
    }

    void* design = FindShipDesign(design_id);
    if (!design) {
        out_message = "Ship design ID " + std::to_string(design_id) + " not found";
        return false;
    }

    // Allocate 0x28 bytes for CRemoveShipDesignCommand (Opcode 0x31B2)
    void* pCmd = fn_engine_alloc_(0x28);
    if (!pCmd) {
        out_message = "Engine memory allocation failed";
        return false;
    }

    memset(pCmd, 0, 0x28);
    *(uintptr_t*)pCmd = base_address_ + 0x258C958; // VTable
    *(uint32_t*)((uintptr_t)pCmd + 0x08) = 0; // country_id (player = 0)
    *(uint32_t*)((uintptr_t)pCmd + 0x0C) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x10) = 0xFFFF0000;
    *(uint16_t*)((uintptr_t)pCmd + 0x14) = 0;
    *(uint8_t*)((uintptr_t)pCmd + 0x16) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x18) = 0;
    *(uint32_t*)((uintptr_t)pCmd + 0x20) = 0; // country_id (player = 0)
    *(uint32_t*)((uintptr_t)pCmd + 0x24) = design_id;

    fn_post_command_(pCmd, 0);

    out_message = "CRemoveShipDesignCommand posted successfully for design " + std::to_string(design_id);
    LOGF("[SHIP_DESIGNER] Ship design deletion command dispatched for design %u", design_id);
    return true;
}

nlohmann::json ShipDesigner::DeleteShipDesignJson(const nlohmann::json& params) {
    if (!params.contains("design_id") || !params["design_id"].is_number()) {
        return {
            {"error", {
                {"code", -32602},
                {"message", "Missing required parameter: design_id"}
            }}
        };
    }

    uint32_t did = params["design_id"].get<uint32_t>();
    std::string msg;
    bool ok = DeleteShipDesign(did, msg);
    if (!ok) {
        return {
            {"error", {
                {"code", -32003},
                {"message", msg}
            }}
        };
    }

    return {
        {"success", true},
        {"design_id", did},
        {"message", msg}
    };
}

} // namespace bridge
