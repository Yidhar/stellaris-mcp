#include "ship_designer.hpp"
#include "sdk/stellaris_sdk.hpp"
#include "fleet_access.hpp"
#include "common.hpp"
#include "command_builder.hpp"
#include "game_state.hpp"
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

static bool SafeReadI64(const void* addr, int64_t* out) {
    if (!addr || !out) return false;
    __try {
        *out = *(const int64_t*)addr;
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

struct StringView {
    const char* data{ nullptr };
    size_t size{ 0 };
};

struct PdxCString {
    char meta[16]{ 0 };
    union {
        char buf[16]{ 0 };
        char* heap_ptr;
    };
    uint64_t size{ 0 };
    uint64_t capacity{ 15 };
};

static bool SafeLocalizeCall(ShipDesigner::FnLocalize fn_localize,
                             const StringView* in_sv,
                             PdxCString* out_str) {
    __try {
        fn_localize(out_str, in_sv);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(ShipDesigner::FnFreePdxStr fn_free_pdx, PdxCString* str) {
    __try {
        fn_free_pdx(str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SafeSetComponentOnSlot(ShipDesigner::FnSetComponentOnSlot fn, void* p_sec, void* p_tmpl, void* p_slot_def) {
    if (!fn || !p_sec || !p_tmpl || !p_slot_def) return false;
    __try {
        fn(p_sec, p_tmpl, p_slot_def);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeStageUpdateResources(ShipDesigner::FnStageUpdateResources fn, void* p_stage) {
    if (!fn || !p_stage) return false;
    __try {
        fn(p_stage);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

ShipDesigner& ShipDesigner::Get() {
    static ShipDesigner instance;
    return instance;
}

bool ShipDesigner::Init(uintptr_t base_address) {
    base_address_ = base_address;
    if (!base_address_) return false;

    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + kRvaEngineAlloc);
    fn_register_design_ = (FnRegisterDesign)(base_address_ + 0x266670);
    fn_country_add_design_ = (FnCountryAddDesign)(base_address_ + 0x689FE0);
    fn_calc_long_name_ = (FnCalcLongName)(base_address_ + 0xE0FEE0);
    fn_can_be_built_by_ = (FnCanBeBuiltBy)(base_address_ + 0x3B9A10);
    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_set_component_on_slot_ = (FnSetComponentOnSlot)(base_address_ + 0xD6E290);
    fn_stage_update_resources_ = (FnStageUpdateResources)(base_address_ + 0xD6C420);

    LOGF("[SHIP_DESIGNER] Initialized with Base=0x%llX, Alloc=0x%llX, RegDes=0x%llX, AddDes=0x%llX, CalcLongName=0x%llX, CanBeBuiltBy=0x%llX, SetComp=0x%llX, UpdRes=0x%llX",
         (unsigned long long)base_address_,
         (unsigned long long)fn_engine_alloc_,
         (unsigned long long)fn_register_design_,
         (unsigned long long)fn_country_add_design_,
         (unsigned long long)fn_calc_long_name_,
         (unsigned long long)fn_can_be_built_by_,
         (unsigned long long)fn_set_component_on_slot_,
         (unsigned long long)fn_stage_update_resources_);
    return true;
}

std::string ShipDesigner::LocalizeKey(const std::string& key) {
    return SafeLocalize(base_address_, key);
}

bool ShipDesigner::CanCountryUseComponent(void* p_tmpl, void* p_country) {
    if (!p_tmpl || !p_country || !fn_can_be_built_by_) return false;
    __try {
        return fn_can_be_built_by_(p_tmpl, p_country, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* ShipDesigner::GetPlayerCountry() {
    if (!base_address_) return nullptr;
    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CCountry), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
    void* countries_arr = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &countries_arr) || !countries_arr) return nullptr;
    void* player_country = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)countries_arr + 8), &player_country)) return nullptr;
    return player_country;
}

void* ShipDesigner::FindShipDesign(uint32_t design_id) {
    if (!base_address_) return nullptr;
    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::db::CShipDesign), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
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
    return fleets::Find(base_address_, fleet_id);
}

void ShipDesigner::BuildComponentIndexIfNeeded() {
    if (component_cache_built_ && !component_cache_.empty()) return;
    if (!base_address_) return;

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CShipDesignTemplatesDatabase_pInstance), &comp_db) || !comp_db) return;

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

        void* p_sub = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)design + 0x20), &p_sub) || !p_sub) continue;

        void* p_size = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)p_sub + 0x08), &p_size) || !p_size) continue;

        uint32_t sec_templates_cnt = 0;
        if (!SafeReadU32((const void*)((uintptr_t)p_size + 0x848), &sec_templates_cnt) || sec_templates_cnt == 0) {
            // Ship Designer interface is strictly for designable ships (excludes fixed civilian/station templates)
            continue;
        }

        ShipDesignInfo d_info{};
        d_info.design_id = did;

        SafeReadPdxString((const void*)((uintptr_t)design + 0x50), d_info.name);
        SafeReadPdxString((const void*)((uintptr_t)design + 0xA8), d_info.class_prefix);
        SafeReadPdxString((const void*)((uintptr_t)p_size + 0x20), d_info.ship_size);

        // Sections
        void* sec_arr = nullptr;
        uint32_t sec_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)p_sub + 0x18), &sec_arr) && sec_arr &&
            SafeReadU32((const void*)((uintptr_t)p_sub + 0x20), &sec_cnt) && sec_cnt <= 16) {
            for (uint32_t s = 0; s < sec_cnt; ++s) {
                void* p_sec = nullptr;
                if (!SafeReadPtr((const void*)((uintptr_t)sec_arr + s * sizeof(void*)), &p_sec) || !p_sec) continue;

                SectionInfo s_info{};
                SafeReadPdxString((const void*)((uintptr_t)p_sec + 0x18), s_info.name);

                void* p_sec_tmpl = nullptr;
                SafeReadPtr((const void*)((uintptr_t)p_sec + 0x40), &p_sec_tmpl);

                void* tmpl_slots_arr = nullptr;
                uint32_t tmpl_slots_cnt = 0;
                if (p_sec_tmpl) {
                    SafeReadPtr((const void*)((uintptr_t)p_sec_tmpl + 0x178), &tmpl_slots_arr);
                    SafeReadU32((const void*)((uintptr_t)p_sec_tmpl + 0x184), &tmpl_slots_cnt);
                }

                void* inst_arr = nullptr;
                uint32_t inst_cnt = 0;
                SafeReadPtr((const void*)((uintptr_t)p_sec + 0x50), &inst_arr);
                SafeReadU32((const void*)((uintptr_t)p_sec + 0x58), &inst_cnt);

                if (tmpl_slots_arr && tmpl_slots_cnt > 0 && tmpl_slots_cnt <= 128) {
                    for (uint32_t k = 0; k < tmpl_slots_cnt; ++k) {
                        uintptr_t slot_def = (uintptr_t)tmpl_slots_arr + k * 0x98;
                        SlotInfo sl_info{};
                        sl_info.slot_index = k;
                        SafeReadPdxString((const void*)(slot_def + 0x18), sl_info.slot_name);

                        // Find corresponding installed component in inst_arr
                        if (inst_arr && inst_cnt <= 128) {
                            for (uint32_t i = 0; i < inst_cnt; ++i) {
                                uintptr_t item_p = (uintptr_t)inst_arr + i * 0x20;
                                void* item_slot_def = nullptr;
                                SafeReadPtr((const void*)(item_p + 8), &item_slot_def);
                                std::string item_slot_name;
                                if (item_slot_def) {
                                    SafeReadPdxString((const void*)((uintptr_t)item_slot_def + 0x18), item_slot_name);
                                }
                                if (item_slot_def == (void*)slot_def || (!item_slot_name.empty() && item_slot_name == sl_info.slot_name)) {
                                    void* slot_comp = nullptr;
                                    SafeReadPtr((const void*)(item_p + 0x10), &slot_comp);
                                    if (slot_comp) {
                                        SafeReadPdxString((const void*)((uintptr_t)slot_comp + 0x1B0), sl_info.component_key);
                                        sl_info.component_name = LocalizeKey(sl_info.component_key);
                                    }
                                    break;
                                }
                            }
                        }
                        s_info.slots.push_back(sl_info);
                    }
                } else if (inst_arr && inst_cnt > 0 && inst_cnt <= 128) {
                    for (uint32_t k = 0; k < inst_cnt; ++k) {
                        uintptr_t slot_p = (uintptr_t)inst_arr + k * 0x20;
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
                            sl_info.component_name = LocalizeKey(sl_info.component_key);
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
                    else if (c == 5) d_info.core_components.aura = c_key;
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
                    {"component_key", sl.component_key},
                    {"component_name", sl.component_name}
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
            {"combat_computer", d.core_components.combat_computer},
            {"aura", d.core_components.aura}
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

    void* country = GetPlayerCountry();
    bool unlocked_only = params.value("unlocked_only", true);

    std::string cat_filter;
    if (params.contains("category") && params["category"].is_string()) {
        cat_filter = params["category"].get<std::string>();
        std::transform(cat_filter.begin(), cat_filter.end(), cat_filter.begin(), ::tolower);
    }

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CShipDesignTemplatesDatabase_pInstance), &comp_db) || !comp_db) {
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

            bool is_unlocked = CanCountryUseComponent(p_tmpl, country);
            if (unlocked_only && !is_unlocked) {
                continue;
            }

            std::string size_str = "standard";
            if (t_key.rfind("SMALL_", 0) == 0) size_str = "small";
            else if (t_key.rfind("MEDIUM_", 0) == 0) size_str = "medium";
            else if (t_key.rfind("LARGE_", 0) == 0) size_str = "large";
            else if (t_key.rfind("AUX_", 0) == 0) size_str = "aux";

            std::string var_name = LocalizeKey(t_key);

            variants.push_back({
                {"component_key", t_key},
                {"name", var_name},
                {"size", size_str},
                {"is_unlocked", is_unlocked}
            });
        }

        if (!variants.empty()) {
            std::string set_loc = LocalizeKey(set_key);
            if (set_loc.empty() || set_loc == set_key) set_loc = loc_name;
            components_json.push_back({
                {"set_key", set_key},
                {"localized_name", set_loc},
                {"icon", icon_gfx},
                {"variants", variants}
            });
        }
    }

    std::vector<std::string> standard_sizes = {
        "corvette", "frigate", "destroyer", "cruiser", "battleship", "titan",
        "colossus", "juggernaut", "military_station_small", "ion_cannon"
    };

    return {
        {"total_component_sets", components_json.size()},
        {"components", components_json},
        {"ship_sizes", standard_sizes}
    };
}

nlohmann::json ShipDesigner::GetComponentDetailsJson(const nlohmann::json& params) {
    BuildComponentIndexIfNeeded();

    std::string query;
    if (params.contains("key") && params["key"].is_string()) {
        query = params["key"].get<std::string>();
    } else if (params.contains("component_key") && params["component_key"].is_string()) {
        query = params["component_key"].get<std::string>();
    } else if (params.contains("set_key") && params["set_key"].is_string()) {
        query = params["set_key"].get<std::string>();
    }

    if (query.empty()) {
        return { {"error", "Missing required parameter 'key' (set_key or component_key)"} };
    }

    void* country = GetPlayerCountry();

    void* comp_db = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::CShipDesignTemplatesDatabase_pInstance), &comp_db) || !comp_db) {
        return { {"error", "Component database not found"} };
    }

    void* arr = nullptr;
    uint32_t cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)comp_db + 0x20), &arr) || !arr ||
        !SafeReadU32((const void*)((uintptr_t)comp_db + 0x2C), &cnt)) {
        return { {"error", "Failed to read component sets"} };
    }

    std::string query_upper = query;
    std::transform(query_upper.begin(), query_upper.end(), query_upper.begin(), ::toupper);

    auto format_set_json = [&](void* set_ptr) -> nlohmann::json {
        if (!set_ptr) return nullptr;

        std::string set_key, loc_name, icon_gfx;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x50), loc_name);
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x80), icon_gfx);

        std::string set_localized = LocalizeKey(set_key);
        if (set_localized.empty() || set_localized == set_key) {
            set_localized = loc_name;
        }

        void* c_vec = nullptr;
        uint32_t c_cnt = 0;
        if (!SafeReadPtr((const void*)((uintptr_t)set_ptr + 0xB8), &c_vec) || !c_vec ||
            !SafeReadU32((const void*)((uintptr_t)set_ptr + 0xC4), &c_cnt)) {
            return {
                {"set_key", set_key},
                {"localized_name", set_localized},
                {"icon", icon_gfx},
                {"total_variants", 0},
                {"variants", nlohmann::json::array()}
            };
        }

        nlohmann::json variants = nlohmann::json::array();

        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* p_tmpl = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)c_vec + j * 8), &p_tmpl) || !p_tmpl) continue;

            std::string t_key;
            SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), t_key);
            if (t_key.empty()) continue;

            std::string var_name = LocalizeKey(t_key);
            bool is_unlocked = CanCountryUseComponent(p_tmpl, country);

            int64_t power_raw = 0;
            SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x2A8), &power_raw);
            double power = power_raw / 100000.0;

            uint8_t s_enum = 0;
            SafeCopyChars((char*)&s_enum, (const char*)((uintptr_t)p_tmpl + 0x1E0), 1);
            std::string size_str = "standard";
            switch (s_enum) {
                case 1: size_str = "small"; break;
                case 2: size_str = "medium"; break;
                case 3: size_str = "large"; break;
                case 4: size_str = "torpedo"; break;
                case 5: size_str = "extra_large"; break;
                case 6: size_str = "titanic"; break;
                case 7: size_str = "planet_killer"; break;
                case 8: size_str = "hangar"; break;
                case 9: size_str = "aux"; break;
                default:
                    if (t_key.rfind("SMALL_", 0) == 0) size_str = "small";
                    else if (t_key.rfind("MEDIUM_", 0) == 0) size_str = "medium";
                    else if (t_key.rfind("LARGE_", 0) == 0) size_str = "large";
                    else if (t_key.rfind("AUX_", 0) == 0) size_str = "aux";
                    else if (t_key.find("TITAN") != std::string::npos || t_key.find("ION") != std::string::npos) size_str = "titanic";
                    break;
            }

            uintptr_t vt = 0;
            SafeReadPtr((const void*)p_tmpl, (void**)&vt);
            uintptr_t vt_rva = (vt > base_address_) ? (vt - base_address_) : 0;

            nlohmann::json vj = {
                {"component_key", t_key},
                {"name", var_name},
                {"size", size_str},
                {"power", power},
                {"is_unlocked", is_unlocked}
            };

            if (vt_rva == 0x23714F0) {
                // CWeaponComponentTemplate
                vj["type"] = "weapon";

                int64_t min_d_raw = 0, delta_d_raw = 0, rng_raw = 0, min_rng_raw = 0, cd_raw = 0;
                int64_t acc_raw = 0, trk_raw = 0, hull_m_raw = 0, armor_m_raw = 0, shield_m_raw = 0;
                int64_t sh_pen_raw = 0, ar_pen_raw = 0, min_w_raw = 0, delta_w_raw = 0;
                int64_t col_dmg_raw = 0, col_delta_raw = 0, col_rng_raw = 0;

                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1218), &min_d_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1220), &delta_d_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11E8), &rng_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11F0), &min_rng_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11E0), &cd_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11F8), &acc_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1200), &trk_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1228), &hull_m_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1230), &armor_m_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1238), &shield_m_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1240), &sh_pen_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1248), &ar_pen_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11D0), &min_w_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11D8), &delta_w_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1268), &col_dmg_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1270), &col_delta_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1278), &col_rng_raw);

                vj["weapon_stats"] = {
                    {"min_damage", min_d_raw / 100000.0},
                    {"max_damage", (min_d_raw + delta_d_raw) / 100000.0},
                    {"range", rng_raw / 100000.0},
                    {"max_range", rng_raw / 100000.0},
                    {"min_range", min_rng_raw / 100000.0},
                    {"cooldown", cd_raw / 100000.0},
                    {"accuracy", acc_raw / 100000.0},
                    {"tracking", trk_raw / 100000.0},
                    {"shield_mult", shield_m_raw / 100000.0},
                    {"armor_mult", armor_m_raw / 100000.0},
                    {"hull_mult", hull_m_raw / 100000.0},
                    {"shield_penetration", sh_pen_raw / 100000.0},
                    {"armor_penetration", ar_pen_raw / 100000.0},
                    {"min_windup", min_w_raw / 100000.0},
                    {"max_windup", (min_w_raw + delta_w_raw) / 100000.0},
                    {"aoe_damage", col_dmg_raw / 100000.0},
                    {"aoe_range", col_rng_raw / 100000.0}
                };
            } else if (vt_rva == 0x2371380) {
                // CStrikeCraftComponentTemplate
                vj["type"] = "strike_craft";

                int32_t c_cnt_val = 0;
                int64_t rng_raw = 0, cd_raw = 0, d_min_raw = 0, d_max_raw = 0, acc_raw = 0, trk_raw = 0;

                SafeReadI32((const void*)((uintptr_t)p_tmpl + 0x11C8), &c_cnt_val);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11E8), &rng_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11D8), &cd_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1250), &d_min_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1258), &d_max_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x11F8), &acc_raw);
                SafeReadI64((const void*)((uintptr_t)p_tmpl + 0x1200), &trk_raw);

                vj["strike_craft_stats"] = {
                    {"craft_count", c_cnt_val},
                    {"engagement_range", rng_raw / 100000.0},
                    {"min_damage", d_min_raw / 100000.0},
                    {"max_damage", d_max_raw / 100000.0},
                    {"cooldown", cd_raw / 100000.0},
                    {"accuracy", acc_raw / 100000.0},
                    {"tracking", trk_raw / 100000.0}
                };
            } else if (vt_rva == 0x2371468) {
                // CUtilityComponentTemplate
                vj["type"] = "utility";
                nlohmann::json uj = nlohmann::json::object();

                void* p_entries = nullptr;
                uint32_t m_cnt_val = 0;
                uintptr_t mod_addr = (uintptr_t)p_tmpl + 0x2B0;
                if (SafeReadPtr((const void*)(mod_addr + 0x38), &p_entries) && p_entries &&
                    SafeReadU32((const void*)(mod_addr + 0x44), &m_cnt_val) && m_cnt_val > 0 && m_cnt_val < 32) {
                    for (uint32_t idx = 0; idx < m_cnt_val; ++idx) {
                        int64_t val_raw = 0, type_raw = 0;
                        SafeReadI64((const void*)((uintptr_t)p_entries + idx * 16), &val_raw);
                        SafeReadI64((const void*)((uintptr_t)p_entries + idx * 16 + 8), &type_raw);
                        double val = val_raw / 100000.0;

                        switch (type_raw) {
                            case 30: uj["hull_add"] = val; break;
                            case 34: uj["hull_regen"] = val; break;
                            case 36: uj["armor_add"] = val; break;
                            case 41: uj["armor_regen"] = val; break;
                            case 46: uj["shield_add"] = val; break;
                            case 51: uj["shield_regen"] = val; break;
                            case 59: uj["weapon_range_mult"] = val; break;
                            case 64: uj["fire_rate_mult"] = val; break;
                            case 73: uj["evasion_add"] = val; break;
                            case 74: uj["evasion_mult"] = val; break;
                            case 75: uj["accuracy_add"] = val; break;
                            case 79: uj["base_speed_mult"] = val; break;
                            case 80: uj["speed_mult"] = val; break;
                            default: {
                                std::string custom_mod = "mod_" + std::to_string(type_raw);
                                uj[custom_mod] = val;
                                break;
                            }
                        }
                    }
                }

                int32_t sr = 0, hl = 0;
                SafeReadI32((const void*)((uintptr_t)p_tmpl + 0x11C8), &sr);
                SafeReadI32((const void*)((uintptr_t)p_tmpl + 0x11CC), &hl);
                if (sr > 0) uj["sensor_range"] = sr;
                if (hl > 0) uj["hyperlane_range"] = hl;

                std::string beh;
                SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x11E8), beh);
                if (!beh.empty()) uj["ship_behavior"] = beh;

                vj["utility_stats"] = uj;
            } else {
                vj["type"] = "other";
            }

            variants.push_back(vj);
        }

        return {
            {"set_key", set_key},
            {"localized_name", set_localized},
            {"icon", icon_gfx},
            {"total_variants", variants.size()},
            {"variants", variants}
        };
    };

    // Priority 1: Exact match on set_key
    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        std::string set_key;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        std::string set_key_upper = set_key;
        std::transform(set_key_upper.begin(), set_key_upper.end(), set_key_upper.begin(), ::toupper);

        if (set_key_upper == query_upper) {
            return format_set_json(set_ptr);
        }
    }

    // Priority 2: Exact match on component_key
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

            std::string t_key;
            SafeReadPdxString((const void*)((uintptr_t)p_tmpl + 0x1B0), t_key);
            std::string t_key_upper = t_key;
            std::transform(t_key_upper.begin(), t_key_upper.end(), t_key_upper.begin(), ::toupper);

            if (t_key_upper == query_upper) {
                return format_set_json(set_ptr);
            }
        }
    }

    // Priority 3: Prefix/fuzzy match on set_key (e.g. "PLASMA" matches "PLASMA_1", "PLASMA_2", "PLASMA_3")
    std::vector<void*> matched_sets;
    for (uint32_t i = 0; i < cnt; ++i) {
        void* set_ptr = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)arr + i * 8), &set_ptr) || !set_ptr) continue;

        std::string set_key;
        SafeReadPdxString((const void*)((uintptr_t)set_ptr + 0x18), set_key);
        std::string set_key_upper = set_key;
        std::transform(set_key_upper.begin(), set_key_upper.end(), set_key_upper.begin(), ::toupper);

        if (set_key_upper.find(query_upper) != std::string::npos) {
            matched_sets.push_back(set_ptr);
        }
    }

    if (matched_sets.size() == 1) {
        return format_set_json(matched_sets[0]);
    } else if (matched_sets.size() > 1) {
        nlohmann::json sets_arr = nlohmann::json::array();
        for (void* s : matched_sets) {
            sets_arr.push_back(format_set_json(s));
        }
        return {
            {"query", query},
            {"matched_count", sets_arr.size()},
            {"matching_sets", sets_arr}
        };
    }

    return { {"error", "Component or component set not found: " + query} };
}

bool ShipDesigner::SetShipDesignName(void* design, const std::string& name) {
    if (!design || name.empty()) return false;

    void* str_addr = (void*)((uintptr_t)design + 0x50);
    if (!SafeWritePdxString(str_addr, name)) {
        RawPdxString raw{};
        if (SafeCopyChars((char*)&raw, (const char*)str_addr, sizeof(RawPdxString))) {
            size_t new_cap = name.size() + 16;
            void* new_buf = fn_engine_alloc_ ? fn_engine_alloc_(new_cap) : nullptr;
            if (new_buf) {
                memcpy(new_buf, name.data(), name.size());
                ((char*)new_buf)[name.size()] = '\0';
                raw.heap_ptr = (char*)new_buf;
                raw.size = name.size();
                raw.capacity = new_cap - 1;
                SafeCopyChars((char*)str_addr, (const char*)&raw, sizeof(RawPdxString));
            }
        }
    }

    // Determine literal flag:
    // If name contains "_SHIP_", it is a localization namelist key (literal = 0).
    // Otherwise, it is a custom literal string (e.g. "幽灵", "先锋", "Spectre"), so literal = 1.
    bool is_namelist_key = (name.find("_SHIP_") != std::string::npos);
    uint8_t lit_val = is_namelist_key ? 0 : 1;
    *(uint8_t*)((uintptr_t)design + 0x70) = lit_val;
    *(uint8_t*)((uintptr_t)design + 0x30) &= ~0x08; // Clear m_auto_gen: design is now user-saved

    // Call engine CalcLongName to recalculate m_longName and both variables (NAME and SIZE)
    if (fn_calc_long_name_) {
        __try {
            fn_calc_long_name_(design);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LOGF("[SHIP_DESIGNER] Exception in CalcLongName for design at 0x%p", design);
        }
    }

    LOGF("[SHIP_DESIGNER] SetShipDesignName: design=0x%p, name='%s', literal=%u",
         design, name.c_str(), (unsigned int)lit_val);
    return true;
}

bool ShipDesigner::CreateShipDesign(const std::string& ship_size, std::string& name,
                                    const nlohmann::json& slots_json, const nlohmann::json& cores_json,
                                    uint32_t& out_design_id, std::string& out_message) {
    if (!fn_register_design_ || !fn_country_add_design_) {
        out_message = "Engine register/add design functions not initialized";
        return false;
    }

    void* country = GetPlayerCountry();
    if (!country) {
        out_message = "Player country not available";
        return false;
    }

    std::string target_size = ship_size.empty() ? "corvette" : ship_size;

    // Find prototype template design with matching ship_size from player's country
    void* proto_design = nullptr;
    void* arr_ptr = nullptr;
    uint32_t count = 0;
    if (SafeReadPtr((const void*)((uintptr_t)country + 0x1AD0), &arr_ptr) && arr_ptr &&
        SafeReadU32((const void*)((uintptr_t)country + 0x1ADC), &count)) {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t did = 0;
            if (!SafeReadU32((const void*)((uintptr_t)arr_ptr + i * 4), &did)) continue;
            void* d = FindShipDesign(did);
            if (!d) continue;

            void* p_sub = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)d + 0x20), &p_sub) || !p_sub) continue;

            void* p_size = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)p_sub + 0x08), &p_size) || !p_size) continue;

            uint32_t sec_templates_cnt = 0;
            if (!SafeReadU32((const void*)((uintptr_t)p_size + 0x848), &sec_templates_cnt) || sec_templates_cnt == 0) {
                continue;
            }

            std::string d_size;
            SafeReadPdxString((const void*)((uintptr_t)p_size + 0x20), d_size);
            if (d_size == target_size) {
                proto_design = d;
                break;
            }
        }
    }

    if (!proto_design) {
        out_message = "No existing design prototype found for customizable ship size: " + target_size;
        return false;
    }

    void* manager_ctx = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &manager_ctx) || !manager_ctx || (uintptr_t)manager_ctx < 0x10000) {
        out_message = "CShipDesignManager context not found";
        return false;
    }

    // Register design in CShipDesignManager: clones proto_design and assigns new unique design_id
    void* new_design = fn_register_design_(manager_ctx, proto_design);
    if (!new_design) {
        out_message = "Failed to register new ship design in manager";
        return false;
    }

    uint32_t new_id = 0;
    if (!SafeReadU32((const void*)((uintptr_t)new_design + 0x10), &new_id) || new_id == 0 || new_id == 0xFFFFFFFF) {
        out_message = "Failed to retrieve valid design ID for newly created design";
        return false;
    }
    out_design_id = new_id;

    // Add new design to player country design array (country + 0x1AC8)
    uint32_t cur_cnt = 0;
    SafeReadU32((const void*)((uintptr_t)country + 0x1ADC), &cur_cnt);
    void* p_new = new_design;
    fn_country_add_design_((void*)((uintptr_t)country + 0x1AC8), cur_cnt, &p_new);

    // Apply custom name if provided, or generate a sensible default name
    if (!name.empty()) {
        SetShipDesignName(new_design, name);
    } else {
        name = target_size + " " + std::to_string(new_id);
        SetShipDesignName(new_design, name);
    }

    // Apply custom slots or core components if provided
    if ((slots_json.is_array() && !slots_json.empty()) || (cores_json.is_object() && !cores_json.empty())) {
        std::string upd_msg;
        UpdateShipDesign(new_id, "", slots_json, cores_json, upd_msg);
    }

    out_message = "New ship design ID " + std::to_string(new_id) + " ('" + name + "') successfully created";
    return true;
}

nlohmann::json ShipDesigner::CreateShipDesignJson(const nlohmann::json& params) {
    std::string ship_size = params.value("ship_size", "corvette");
    std::string name = params.value("name", "");
    nlohmann::json slots = params.contains("slots") ? params["slots"] : nlohmann::json::array();
    nlohmann::json cores = params.contains("core_components") ? params["core_components"] : nlohmann::json::object();

    uint32_t design_id = 0;
    std::string msg;
    bool ok = CreateShipDesign(ship_size, name, slots, cores, design_id, msg);

    if (!ok) {
        return {
            {"success", false},
            {"error", msg}
        };
    }

    return {
        {"success", true},
        {"design_id", design_id},
        {"ship_size", ship_size},
        {"name", name},
        {"message", msg}
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

    void* p_sub = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)design + 0x20), &p_sub) || !p_sub) {
        out_message = "Failed to access ship design sub-structure";
        return false;
    }

    void* p_size = nullptr;
    uint32_t sec_templates_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)p_sub + 0x08), &p_size) || !p_size ||
        !SafeReadU32((const void*)((uintptr_t)p_size + 0x848), &sec_templates_cnt) || sec_templates_cnt == 0) {
        out_message = "Ship design ID " + std::to_string(design_id) + " is not a customizable ship design";
        return false;
    }

    if (!new_name.empty()) {
        SetShipDesignName(design, new_name);
    }

    uint32_t replaced_slots = 0;

    // Update Section slots
    if (slots_json.is_array()) {
        void* sec_arr = nullptr;
        uint32_t sec_cnt = 0;
        if (SafeReadPtr((const void*)((uintptr_t)p_sub + 0x18), &sec_arr) && sec_arr &&
            SafeReadU32((const void*)((uintptr_t)p_sub + 0x20), &sec_cnt) && sec_cnt <= 16) {

            for (const auto& item : slots_json) {
                if (!item.contains("component_key")) continue;
                std::string comp_key = item["component_key"].get<std::string>();
                void* p_tmpl = FindComponentTemplate(comp_key);
                if (!p_tmpl) {
                    out_message = "Component key '" + comp_key + "' not found in catalog";
                    return false;
                }

                void* country = GetPlayerCountry();
                if (!CanCountryUseComponent(p_tmpl, country)) {
                    std::string loc_name = LocalizeKey(comp_key);
                    out_message = "Cannot equip '" + loc_name + "' (" + comp_key + "): technology not unlocked by your empire";
                    return false;
                }

                int target_slot_idx = item.value("slot_index", -1);
                std::string target_slot_name = item.value("slot_name", "");
                int target_sec_idx = item.value("section_index", -1);
                std::string target_sec_name = item.value("section_name", "");

                // Traverse sections to find matching slot
                for (uint32_t s = 0; s < sec_cnt; ++s) {
                    if (target_sec_idx != -1 && (int)s != target_sec_idx) continue;

                    void* p_sec = nullptr;
                    if (!SafeReadPtr((const void*)((uintptr_t)sec_arr + s * sizeof(void*)), &p_sec) || !p_sec) continue;

                    std::string cur_sec_name;
                    SafeReadPdxString((const void*)((uintptr_t)p_sec + 0x18), cur_sec_name);
                    if (!target_sec_name.empty() && cur_sec_name != target_sec_name) continue;

                    void* p_sec_tmpl = nullptr;
                    SafeReadPtr((const void*)((uintptr_t)p_sec + 0x40), &p_sec_tmpl);

                    void* tmpl_slots_arr = nullptr;
                    uint32_t tmpl_slots_cnt = 0;
                    if (p_sec_tmpl) {
                        SafeReadPtr((const void*)((uintptr_t)p_sec_tmpl + 0x178), &tmpl_slots_arr);
                        SafeReadU32((const void*)((uintptr_t)p_sec_tmpl + 0x184), &tmpl_slots_cnt);
                    }

                    bool matched_in_sec = false;
                    if (tmpl_slots_arr && tmpl_slots_cnt > 0 && tmpl_slots_cnt <= 128) {
                        for (uint32_t k = 0; k < tmpl_slots_cnt; ++k) {
                            uintptr_t slot_p = (uintptr_t)tmpl_slots_arr + k * 0x98;
                            std::string sl_name;
                            SafeReadPdxString((const void*)(slot_p + 0x18), sl_name);

                            if ((target_slot_idx != -1 && (int)k == target_slot_idx) ||
                                (!target_slot_name.empty() && sl_name == target_slot_name)) {
                                if (SafeSetComponentOnSlot(fn_set_component_on_slot_, p_sec, p_tmpl, (void*)slot_p)) {
                                    replaced_slots++;
                                    matched_in_sec = true;
                                }
                                break;
                            }
                        }
                    } else {
                        void* slots_arr = nullptr;
                        uint32_t slots_cnt = 0;
                        if (SafeReadPtr((const void*)((uintptr_t)p_sec + 0x50), &slots_arr) && slots_arr &&
                            SafeReadU32((const void*)((uintptr_t)p_sec + 0x58), &slots_cnt) && slots_cnt <= 128) {
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
                                    matched_in_sec = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (matched_in_sec && (!target_sec_name.empty() || target_sec_idx != -1)) {
                        break;
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

            auto update_core = [&](const std::string& field, uint32_t core_idx) -> bool {
                if (cores_json.contains(field) && core_idx < comp_cnt) {
                    std::string key = cores_json[field].get<std::string>();
                    void* p_tmpl = FindComponentTemplate(key);
                    if (!p_tmpl) {
                        out_message = "Core component key '" + key + "' not found in catalog";
                        return false;
                    }
                    void* country = GetPlayerCountry();
                    if (!CanCountryUseComponent(p_tmpl, country)) {
                        std::string loc_name = LocalizeKey(key);
                        out_message = "Cannot equip core '" + loc_name + "' (" + key + "): technology not unlocked by your empire";
                        return false;
                    }
                    SafeWritePtr((void*)((uintptr_t)comp_arr + core_idx * 8), p_tmpl);
                    replaced_slots++;
                }
                return true;
            };

            if (!update_core("reactor", 0) ||
                !update_core("ftl", 1) ||
                !update_core("thruster", 2) ||
                !update_core("sensor", 3) ||
                !update_core("combat_computer", 4) ||
                !update_core("aura", 5)) {
                return false;
            }
        }
    }

    // Recalculate stage resources / stats
    SafeStageUpdateResources(fn_stage_update_resources_, p_sub);

    // Mark design as user-saved: clear m_auto_gen (bit 0x08 at design + 0x30)
    *(uint8_t*)((uintptr_t)design + 0x30) &= ~0x08;

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
    void* fleet = FindFleet(fleet_id);
    if (!fleet) {
        out_message = "Fleet ID " + std::to_string(fleet_id) + " not found";
        return false;
    }

    // `starbase_id` is really the construction queue that builds the upgrade; the factory
    // default 0xFFFFFFFF lets the engine pick the nearest shipyard. Queue flags keep defaults.
    namespace upgrade = sdk::cmd::fleet_upgrade_design_command;
    auto cmd = CommandBuilder::Get().Create(upgrade::kSpec);
    cmd.Set<uint32_t>(upgrade::country, GameState::Get().GetPlayerCountryId())
       .Set<uint32_t>(upgrade::fleet, fleet_id)
       .Set<uint32_t>(upgrade::construction_queue, starbase_id);
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

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
    void* design = FindShipDesign(design_id);
    if (!design) {
        out_message = "Ship design ID " + std::to_string(design_id) + " not found";
        return false;
    }

    void* p_sub = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)design + 0x20), &p_sub) || !p_sub) {
        out_message = "Failed to access ship design sub-structure";
        return false;
    }

    void* p_size = nullptr;
    uint32_t sec_templates_cnt = 0;
    if (!SafeReadPtr((const void*)((uintptr_t)p_sub + 0x08), &p_size) || !p_size ||
        !SafeReadU32((const void*)((uintptr_t)p_size + 0x848), &sec_templates_cnt) || sec_templates_cnt == 0) {
        out_message = "Ship design ID " + std::to_string(design_id) + " is a fixed core design and cannot be deleted";
        return false;
    }

    namespace remove = sdk::cmd::remove_ship_design;
    auto cmd = CommandBuilder::Get().Create(remove::kSpec);
    cmd.Set<uint32_t>(remove::country, GameState::Get().GetPlayerCountryId())
       .Set<uint32_t>(remove::design, design_id);
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

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
