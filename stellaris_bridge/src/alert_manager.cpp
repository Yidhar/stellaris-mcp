#include "alert_manager.hpp"
#include "game_state.hpp"
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

static bool SafeReadU8(const void* addr, uint8_t* out) {
    __try {
        *out = *(const uint8_t*)addr;
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

static bool SafeAlertClick(AlertManager::FnOnAlertClick fn, void* alert_win, void* banner_btn) {
    __try {
        fn(alert_win, banner_btn);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

AlertManager& AlertManager::Get() {
    static AlertManager instance;
    return instance;
}

bool AlertManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    // RVA: CAlertIconsWindow::OnAlertClick = 0x9E31E0
    fn_on_alert_click_ = (FnOnAlertClick)(base_address_ + 0x9E31E0);

    LOGF("[ALERT_MGR] Initialized: Base=0x%llX, OnAlertClick=0x%llX",
        (unsigned long long)base_address_,
        (unsigned long long)fn_on_alert_click_);

    return true;
}

std::string AlertManager::CleanPdxString(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        // Strip Paradox formatting tags \x11X or §X
        if (input[i] == '\x11') {
            if (i + 1 < input.size()) {
                ++i;
            }
            continue;
        }
        if ((uint8_t)input[i] == 0xC2 && i + 1 < input.size() && (uint8_t)input[i + 1] == 0xA7) { // UTF-8 §
            i += 2; // skip § and formatting tag char
            continue;
        }
        if (input[i] == '\x13') { // icon delimiter \x13... \x13
            continue;
        }
        result += input[i];
    }
    // Trim leading/trailing whitespace
    size_t start = result.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = result.find_last_not_of(" \t\r\n");
    return result.substr(start, end - start + 1);
}

std::string AlertManager::ExtractPdxString(void* ptr) {
    if (!ptr) return "";

    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)ptr, sizeof(RawPdxString))) {
        return "";
    }

    if (raw.size == 0) return "";

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
            size_t len = raw.size < 4096 ? (size_t)raw.size : 4096;
            std::string result(len, '\0');
            if (SafeCopyChars(&result[0], raw.heap_ptr, len)) {
                return result;
            }
        }
    }
    return "";
}

std::string AlertManager::GetAlertTypeName(uint32_t id) {
    static const std::unordered_map<uint32_t, std::string> kAlertNames = {
        {0, "alert_no_research"},
        {1, "alert_physics_research"},
        {2, "alert_society_research"},
        {3, "alert_engineering_research"},
        {4, "alert_idle_fleets"},
        {5, "alert_timed_project"},
        {6, "alert_fleet_combat"},
        {7, "alert_low_stability"},
        {8, "alert_mia"},
        {9, "alert_spynetwork_lacking_power"},
        {10, "alert_espionage_event"},
        {11, "alert_can_modify_species"},
        {12, "alert_army_combat"},
        {13, "alert_hostile_in_system"},
        {14, "alert_capped_resources"},
        {15, "alert_set_war_goal"},
        {16, "alert_resource_shortage"},
        {17, "alert_unlock_tradition"},
        {18, "alert_unlock_ascension_perk"},
        {19, "alert_can_upgrade_government"},
        {20, "alert_inactive_civics"},
        {21, "alert_can_enter_shroud"},
        {22, "alert_hopeless_war"},
        {23, "alert_full_occupation"},
        {24, "alert_high_war_exhaustion"},
        {25, "alert_cannot_afford_monthly_trade"},
        {26, "alert_activate_relic"},
        {27, "alert_site_event"},
        {28, "alert_in_breach"},
        {29, "alert_federation_losing_level"},
        {30, "alert_council_election"},
        {31, "alert_federation_losing_xp"},
        {32, "alert_federation_low_cohesion"},
        {33, "alert_mia_reinforce"},
        {34, "alert_mia_forced_to_decloak"},
        {35, "alert_mia_forced_to_decloak_by_us"},
        {36, "alert_forced_to_decloak"},
        {37, "alert_forced_to_decloak_by_us"},
        {38, "alert_first_contact_stage_done"},
        {39, "alert_declining_pops"},
        {40, "alert_unspent_leader_trait_points"},
        {41, "alert_unassigned_leader"},
        {42, "alert_available_council_agenda"},
        {43, "alert_available_council_position"},
        {44, "alert_council_agenda_ready_for_activation"},
        {45, "alert_open_council_position"},
        {46, "alert_necrophage_low_pops"},
        {47, "alert_imminent_situation"},
        {48, "alert_intel_negative"},
        {49, "alert_mia_phase_out_fleet"},
        {50, "alert_astral_rift_event"},
        {51, "alert_formless_reward_available"},
        {52, "alert_astral_action_available"},
        {53, "alert_mia_fleet_eaten"},
        {54, "alert_situation_blocked"},
        {55, "alert_space_fauna_bombardment"},
        {56, "alert_mia_ship_stolen"},
        {57, "alert_election"},
        {58, "alert_cycle_of_fortune"},
        {59, "alert_cycle_of_omens"},
        {60, "alert_cycle_of_growth"},
        {61, "alert_cycle_of_prosperity"},
        {62, "alert_cycle_of_conflict"},
        {63, "alert_cycle_of_harmony"},
        {64, "alert_cycle_of_knowledge"},
        {65, "alert_can_delve_shroud"}
    };
    auto it = kAlertNames.find(id);
    if (it != kAlertNames.end()) return it->second;
    return "alert_" + std::to_string(id);
}

std::vector<AlertItem> AlertManager::GetAlerts() {
    std::vector<AlertItem> items;

    if (!base_address_) return items;

    void* idler = GameState::Get().GetInGameIdler();
    if (!idler) return items;

    void* alert_win = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)idler + 0xBC8), &alert_win) || !alert_win) {
        return items;
    }

    for (uint32_t id = 0; id < 66; ++id) {
        void* ui_win = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)alert_win + 0x368 + id * 0x110), &ui_win) || !ui_win) {
            continue;
        }

        uint8_t vis = 0;
        if (!SafeReadU8((const void*)((uintptr_t)ui_win + 0x41), &vis) || vis == 0) {
            continue;
        }

        void* desc = (void*)((uintptr_t)alert_win + 0x378 + id * 0x110);

        std::string raw_text1 = ExtractPdxString((void*)((uintptr_t)desc + 0x18));
        std::string raw_text2 = ExtractPdxString((void*)((uintptr_t)desc + 0x48));

        AlertItem item;
        item.alert_id = id;
        item.type = GetAlertTypeName(id);

        size_t newline_pos = raw_text1.find('\n');
        if (newline_pos != std::string::npos) {
            item.title = CleanPdxString(raw_text1.substr(0, newline_pos));
            item.description = CleanPdxString(raw_text1.substr(newline_pos + 1));
        } else {
            item.title = CleanPdxString(raw_text1);
        }

        item.tooltip = CleanPdxString(raw_text2);

        items.push_back(item);
    }

    return items;
}

nlohmann::json AlertManager::GetAlertsJson() {
    auto alerts = GetAlerts();
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& item : alerts) {
        arr.push_back({
            {"alert_id", item.alert_id},
            {"type", item.type},
            {"title", item.title},
            {"description", item.description},
            {"tooltip", item.tooltip}
        });
    }

    return arr;
}

nlohmann::json AlertManager::OpenAlert(uint32_t alert_id) {
    if (!base_address_) {
        return {
            {"error", {
                {"code", -32030},
                {"message", "Base address not initialized"}
            }}
        };
    }

    void* idler = GameState::Get().GetInGameIdler();
    if (!idler) {
        return {
            {"error", {
                {"code", -32031},
                {"message", "InGameIdler is null"}
            }}
        };
    }

    void* alert_win = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)idler + 0xBC8), &alert_win) || !alert_win) {
        return {
            {"error", {
                {"code", -32032},
                {"message", "CAlertIconsWindow is null"}
            }}
        };
    }

    if (alert_id >= 66) {
        return {
            {"error", {
                {"code", -32033},
                {"message", "Alert ID out of range (0..65)"}
            }}
        };
    }

    void* ui_win = nullptr;
    if (!SafeReadPtr((const void*)((uintptr_t)alert_win + 0x368 + alert_id * 0x110), &ui_win) || !ui_win) {
        return {
            {"error", {
                {"code", -32034},
                {"message", "Alert UI window is null"}
            }}
        };
    }

    uint8_t vis = 0;
    if (!SafeReadU8((const void*)((uintptr_t)ui_win + 0x41), &vis) || vis == 0) {
        return {
            {"error", {
                {"code", -32035},
                {"message", "Alert is not currently active/visible on screen"}
            }}
        };
    }

    // Banner button is the first child control at *(ui_win + 0x668)
    void* controls_arr = nullptr;
    void* banner_btn = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)ui_win + 0x668), &controls_arr) && controls_arr) {
        SafeReadPtr((const void*)controls_arr, &banner_btn);
    }

    if (!banner_btn) {
        return {
            {"error", {
                {"code", -32036},
                {"message", "Failed to retrieve alerticon_banner button for alert"}
            }}
        };
    }

    if (!fn_on_alert_click_) {
        return {
            {"error", {
                {"code", -32037},
                {"message", "fn_on_alert_click_ is null"}
            }}
        };
    }

    std::string type_name = GetAlertTypeName(alert_id);
    LOGF("[ALERT_MGR] Opening alert %u (type: %s, banner: %p)...",
        alert_id, type_name.c_str(), banner_btn);

    if (!SafeAlertClick(fn_on_alert_click_, alert_win, banner_btn)) {
        LOGF("[ALERT_MGR] Exception occurred executing CAlertIconsWindow::OnAlertClick for alert %u", alert_id);
        return {
            {"error", {
                {"code", -32038},
                {"message", "Exception occurred executing CAlertIconsWindow::OnAlertClick"}
            }}
        };
    }

    LOGF("[ALERT_MGR] Alert %u opened successfully.", alert_id);
    return {
        {"success", true},
        {"opened_alert_id", alert_id},
        {"type", type_name},
        {"message", "Alert clicked and opened successfully"}
    };
}

} // namespace bridge
