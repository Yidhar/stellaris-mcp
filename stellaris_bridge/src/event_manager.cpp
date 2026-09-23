#include "event_manager.hpp"
#include "game_state.hpp"

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

struct OptionNode {
    void* button;
    void* unk_8;
    OptionNode* next;
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

static bool SafeReadInt(const void* addr, int* out) {
    __try {
        *out = *(const int*)addr;
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

static void* SafeFindChild(EventManager::FnFindChild fn, void* container, void* pdx_str) {
    __try {
        return fn(container, pdx_str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool SafeSelectOption(EventManager::FnSelectOption fn, void* win, int option_index) {
    __try {
        fn(win, option_index);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeStartScreenDismiss(EventManager::FnStartScreenDismiss fn, void* start_screen) {
    __try {
        fn(start_screen);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeAnomalyDismiss(EventManager::FnAnomalyDismiss fn, void* anomaly_view) {
    __try {
        fn(anomaly_view);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeAnomalyResearch(EventManager::FnAnomalyResearch fn, void* anomaly_view) {
    __try {
        fn(anomaly_view);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeFirstContactDismiss(EventManager::FnFirstContactDismiss fn, void* fc_view) {
    __try {
        fn(fc_view);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

EventManager& EventManager::Get() {
    static EventManager instance;
    return instance;
}

bool EventManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    // Updated RVAs for hot-updated game version
    fn_find_child_ = nullptr;
    fn_select_option_ = (FnSelectOption)(base_address_ + 0x107AD50);
    fn_start_screen_dismiss_ = (FnStartScreenDismiss)(base_address_ + 0x12D1E50);
    fn_anomaly_dismiss_ = (FnAnomalyDismiss)(base_address_ + 0x11AFAD0);
    fn_anomaly_research_ = (FnAnomalyResearch)(base_address_ + 0xFBA080);
    fn_first_contact_dismiss_ = (FnFirstContactDismiss)(base_address_ + 0x1139FD0);

    LOGF("[EVENT_MGR] Initialized: Base=0x%llX, SelectOption=0x%llX, StartScreenDismiss=0x%llX, AnomalyDismiss=0x%llX, AnomalyResearch=0x%llX, FirstContactDismiss=0x%llX",
        (unsigned long long)base_address_,
        (unsigned long long)fn_select_option_,
        (unsigned long long)fn_start_screen_dismiss_,
        (unsigned long long)fn_anomaly_dismiss_,
        (unsigned long long)fn_anomaly_research_,
        (unsigned long long)fn_first_contact_dismiss_);

    return true;
}

static std::string CleanPdxString(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\x11') {
            if (i + 1 < input.size()) {
                ++i; // skip format char (e.g. Y, !, W, R, etc.)
            }
            continue;
        }
        result += input[i];
    }
    return result;
}

std::string EventManager::ExtractPdxString(void* ptr) {
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
            return CleanPdxString(std::string(temp, len));
        }
    } else if (raw.heap_ptr) {
        uintptr_t addr = (uintptr_t)raw.heap_ptr;
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
            size_t len = raw.size < 4096 ? (size_t)raw.size : 4096;
            std::string result(len, '\0');
            if (SafeCopyChars(&result[0], raw.heap_ptr, len)) {
                return CleanPdxString(result);
            }
        }
    }
    return "";
}

void* EventManager::FindChildByName(void* container, const char* name) {
    if (!container || !name) return nullptr;

    void* keys_arr = nullptr;
    uint32_t cnt = 0;
    void* vals_arr = nullptr;

    if (SafeReadPtr((const void*)((uintptr_t)container + 0x710), &keys_arr) &&
        SafeReadU32((const void*)((uintptr_t)container + 0x71C), &cnt) &&
        SafeReadPtr((const void*)((uintptr_t)container + 0x6F8), &vals_arr) &&
        keys_arr && vals_arr && cnt > 0 && cnt < 200) {

        for (uint32_t i = 0; i < cnt; ++i) {
            std::string k_name = ExtractPdxString((void*)((uintptr_t)keys_arr + i * 48 + 16));
            if (_stricmp(k_name.c_str(), name) == 0) {
                void* ctrl = nullptr;
                if (SafeReadPtr((const void*)((uintptr_t)vals_arr + i * 8), &ctrl) && ctrl) {
                    return ctrl;
                }
            }
        }
    }

    // Recurse into child containers in [container + 0x878]
    void* c_arr = nullptr;
    uint32_t c_cnt = 0;
    if (SafeReadPtr((const void*)((uintptr_t)container + 0x878), &c_arr) &&
        SafeReadU32((const void*)((uintptr_t)container + 0x884), &c_cnt) &&
        c_arr && c_cnt > 0 && c_cnt < 50) {

        for (uint32_t j = 0; j < c_cnt; ++j) {
            void* child = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)c_arr + j * 8), &child) && child) {
                void* found = FindChildByName(child, name);
                if (found) return found;
            }
        }
    }

    return nullptr;
}

std::vector<EventInfo> EventManager::GetActiveEvents() {
    std::vector<EventInfo> events;

    void* idler = GameState::Get().GetInGameIdler();
    if (!idler) {
        return events;
    }

    // 1. Check Opening Start Screen ("开局背景特殊事件") at [idler + 0xBE8]
    void* start_screen = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)idler + 0xBE8), &start_screen) && start_screen) {
        void* ui_window = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)start_screen + 0x78), &ui_window) && ui_window) {
            uint8_t is_vis = 0;
            if (SafeReadU8((const void*)((uintptr_t)ui_window + 0x41), &is_vis) && is_vis != 0) {
                EventInfo start_ev;
                start_ev.window_id = START_SCREEN_EVENT_ID;

                // Title: empire_name - origin_name
                void* emp_ctrl = FindChildByName(ui_window, "empire_name");
                std::string emp_name = emp_ctrl ? ExtractPdxString((void*)((uintptr_t)emp_ctrl + 0x168)) : "";

                void* orig_ctrl = FindChildByName(ui_window, "origin_name");
                std::string orig_name = orig_ctrl ? ExtractPdxString((void*)((uintptr_t)orig_ctrl + 0x168)) : "";

                if (!emp_name.empty() && !orig_name.empty()) {
                    start_ev.title = emp_name + " - " + orig_name;
                } else if (!emp_name.empty()) {
                    start_ev.title = emp_name;
                } else {
                    start_ev.title = "开局帝国背景";
                }

                // Description: starting_description
                void* desc_ctrl = FindChildByName(ui_window, "starting_description");
                if (desc_ctrl) {
                    start_ev.description = ExtractPdxString((void*)((uintptr_t)desc_ctrl + 0x168));
                }

                // Close Button Option
                void* close_btn = FindChildByName(ui_window, "close");
                std::string btn_text = close_btn ? ExtractPdxString((void*)((uintptr_t)close_btn + 0x168)) : "";
                if (btn_text.empty()) {
                    btn_text = "我们的征途是星辰大海！";
                }

                start_ev.options.push_back({ 0, btn_text, true });
                events.push_back(start_ev);
            }
        }
    }

    // 2. Check Anomaly Window ("异常现象发现窗口") at [idler + 0xB08]
    void* anomaly_view = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)idler + 0xB08), &anomaly_view) && anomaly_view) {
        void* ui_window = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)anomaly_view + 0x78), &ui_window) && ui_window) {
            uint8_t is_vis = 0;
            if (SafeReadU8((const void*)((uintptr_t)ui_window + 0x41), &is_vis) && is_vis != 0) {
                EventInfo anom_ev;
                anom_ev.window_id = ANOMALY_EVENT_ID;

                // Anomaly title & description from header_win at [anomaly_view + 0xD0]
                void* header_win = nullptr;
                SafeReadPtr((const void*)((uintptr_t)anomaly_view + 0xD0), &header_win);

                std::string header_str;
                std::string desc_str;
                if (header_win) {
                    header_str = ExtractPdxString((void*)((uintptr_t)header_win + 0x168));
                    desc_str = ExtractPdxString((void*)((uintptr_t)header_win + 0x12E8));
                }

                if (!header_str.empty()) {
                    anom_ev.title = header_str;
                } else {
                    void* title_ctrl = FindChildByName(ui_window, "title");
                    std::string title_str = title_ctrl ? ExtractPdxString((void*)((uintptr_t)title_ctrl + 0x168)) : "";
                    anom_ev.title = !title_str.empty() ? title_str : "异常现象";
                }

                if (!desc_str.empty()) {
                    anom_ev.description = desc_str;
                } else {
                    void* anom_sub_win = FindChildByName(ui_window, "anomaly_window");
                    if (anom_sub_win) {
                        void* desc_ctrl = FindChildByName(anom_sub_win, "desc");
                        if (desc_ctrl) anom_ev.description = ExtractPdxString((void*)((uintptr_t)desc_ctrl + 0x168));
                    }
                }

                // Option 0: ok ("暂时离开")
                void* ok_btn = FindChildByName(ui_window, "ok");
                std::string ok_text = ok_btn ? ExtractPdxString((void*)((uintptr_t)ok_btn + 0x168)) : "";
                if (ok_text.empty()) ok_text = "暂时离开";
                anom_ev.options.push_back({ 0, ok_text, true });

                // Option 1: research ("调查")
                void* res_btn = FindChildByName(ui_window, "research");
                std::string res_text = res_btn ? ExtractPdxString((void*)((uintptr_t)res_btn + 0x168)) : "";
                if (res_text.empty()) res_text = "调查";
                anom_ev.options.push_back({ 1, res_text, true });

                events.push_back(anom_ev);
            }
        }
    }

    // 3. Check First Contact View ("第一次接触事件/阶段窗口") at [idler + 0xC90]
    void* fc_view = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)idler + 0xC90), &fc_view) && fc_view) {
        void* ui_window = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)fc_view + 0x78), &ui_window) && ui_window) {
            uint8_t is_vis = 0;
            if (SafeReadU8((const void*)((uintptr_t)ui_window + 0x41), &is_vis) && is_vis != 0) {
                EventInfo fc_ev;
                fc_ev.window_id = FIRST_CONTACT_EVENT_ID;

                // Title: site_name + " - " + chapter_subtitle
                void* site_ctrl = FindChildByName(ui_window, "site_name");
                std::string site_name = site_ctrl ? ExtractPdxString((void*)((uintptr_t)site_ctrl + 0x168)) : "";

                void* sub_ctrl = FindChildByName(ui_window, "chapter_subtitle");
                std::string subtitle = sub_ctrl ? ExtractPdxString((void*)((uintptr_t)sub_ctrl + 0x168)) : "";

                void* stage_ctrl = FindChildByName(ui_window, "title_text");
                std::string stage_name = stage_ctrl ? ExtractPdxString((void*)((uintptr_t)stage_ctrl + 0x168)) : "";

                if (!site_name.empty() && !subtitle.empty()) {
                    fc_ev.title = site_name + " - " + subtitle;
                } else if (!site_name.empty()) {
                    fc_ev.title = site_name;
                } else {
                    fc_ev.title = "第一次接触事件";
                }
                if (!stage_name.empty()) {
                    fc_ev.title += " (" + stage_name + ")";
                }

                // Description: check description control, or locked status text
                void* desc_ctrl = FindChildByName(ui_window, "description");
                std::string desc_str = desc_ctrl ? ExtractPdxString((void*)((uintptr_t)desc_ctrl + 0x168)) : "";
                if (desc_str.empty() && desc_ctrl) {
                    desc_str = ExtractPdxString((void*)((uintptr_t)desc_ctrl + 0x268));
                }

                if (desc_str.empty()) {
                    void* status_text_ctrl = FindChildByName(ui_window, "text");
                    if (status_text_ctrl) {
                        desc_str = ExtractPdxString((void*)((uintptr_t)status_text_ctrl + 0x168));
                    }
                }
                if (desc_str.empty()) {
                    void* no_rep_ctrl = FindChildByName(ui_window, "no_report_text");
                    if (no_rep_ctrl) {
                        desc_str = ExtractPdxString((void*)((uintptr_t)no_rep_ctrl + 0x168));
                    }
                }
                fc_ev.description = desc_str;

                // Options: check options_box
                void* opt_box = nullptr;
                SafeReadPtr((const void*)((uintptr_t)fc_view + 0xE0), &opt_box);
                if (opt_box) {
                    void* c_arr = nullptr;
                    uint32_t c_cnt = 0;
                    if (SafeReadPtr((const void*)((uintptr_t)opt_box + 0x878), &c_arr) &&
                        SafeReadU32((const void*)((uintptr_t)opt_box + 0x884), &c_cnt) &&
                        c_arr && c_cnt > 0 && c_cnt < 20) {

                        for (uint32_t j = 0; j < c_cnt; ++j) {
                            void* child_opt = nullptr;
                            if (SafeReadPtr((const void*)((uintptr_t)c_arr + j * 8), &child_opt) && child_opt) {
                                void* text_ctrl = FindChildByName(child_opt, "text");
                                std::string opt_text = text_ctrl ? ExtractPdxString((void*)((uintptr_t)text_ctrl + 0x168)) : "";
                                if (opt_text.empty()) {
                                    opt_text = "选项 " + std::to_string(j);
                                }
                                fc_ev.options.push_back({ (int)j, opt_text, true });
                            }
                        }
                    }
                }

                if (fc_ev.options.empty()) {
                    fc_ev.options.push_back({ 0, "关闭", true });
                }

                events.push_back(fc_ev);
            }
        }
    }

    // 4. Check Standard Modal Event Windows at [idler + 0x170]
    void** windows = nullptr;
    uint32_t count = 0;

    if (!SafeReadPtr((const void*)((uintptr_t)idler + 0x170), (void**)&windows) ||
        !SafeReadU32((const void*)((uintptr_t)idler + 0x17c), &count)) {
        return events;
    }

    if (!windows || count == 0 || count > 200) {
        return events;
    }

    for (uint32_t i = 0; i < count; ++i) {
        void* win = nullptr;
        if (!SafeReadPtr((const void*)&windows[i], &win) || !win) {
            continue;
        }

        EventInfo info;
        SafeReadU32((const void*)((uintptr_t)win + 0x98), &info.window_id);

        void* ui_window = nullptr;
        SafeReadPtr((const void*)((uintptr_t)win + 0x78), &ui_window);

        if (ui_window) {
            // Extract Title
            void* title_ctrl = FindChildByName(ui_window, "title");
            if (!title_ctrl) title_ctrl = FindChildByName(win, "title");
            if (title_ctrl) {
                info.title = ExtractPdxString((void*)((uintptr_t)title_ctrl + 0x168));
                if (info.title.empty()) {
                    info.title = ExtractPdxString((void*)((uintptr_t)title_ctrl + 0x268));
                }
            }

            // Extract Description (search container tree for description)
            void* desc_ctrl = FindChildByName(ui_window, "description");
            if (!desc_ctrl) desc_ctrl = FindChildByName(win, "description");
            if (desc_ctrl) {
                info.description = ExtractPdxString((void*)((uintptr_t)desc_ctrl + 0x168));
                if (info.description.empty()) {
                    info.description = ExtractPdxString((void*)((uintptr_t)desc_ctrl + 0x268));
                }
            }
        }

        // Extract Options list from linked list at +0x9d8
        void* node = nullptr;
        SafeReadPtr((const void*)((uintptr_t)win + 0x9d8), &node);

        uint32_t opt_count = 0;
        SafeReadU32((const void*)((uintptr_t)win + 0x9e8), &opt_count);

        for (uint32_t o = 0; o < opt_count && node != nullptr; ++o) {
            void* button = nullptr;
            SafeReadPtr((const void*)((uintptr_t)node + 0), &button);

            if (button) {
                int opt_idx = 0;
                SafeReadInt((const void*)((uintptr_t)button + 0x20), &opt_idx);

                std::string opt_text = ExtractPdxString((void*)((uintptr_t)button + 0x268));
                if (opt_text.empty()) {
                    opt_text = ExtractPdxString((void*)((uintptr_t)button + 0x168));
                }

                info.options.push_back({ opt_idx, opt_text, true });
            }

            void* next_node = nullptr;
            SafeReadPtr((const void*)((uintptr_t)node + 16), &next_node);
            node = next_node;
        }

        events.push_back(info);
    }

    return events;
}

nlohmann::json EventManager::GetActiveEventsJson() {
    auto events = GetActiveEvents();
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& ev : events) {
        nlohmann::json opt_arr = nlohmann::json::array();
        for (const auto& opt : ev.options) {
            opt_arr.push_back({
                {"index", opt.index},
                {"text", opt.text},
                {"is_valid", opt.is_valid}
            });
        }

        arr.push_back({
            {"window_id", ev.window_id},
            {"title", ev.title},
            {"description", ev.description},
            {"options", opt_arr}
        });
    }

    return arr;
}

nlohmann::json EventManager::ResolveEvent(uint32_t window_id, int option_index) {
    void* idler = GameState::Get().GetInGameIdler();
    if (!idler) {
        return {
            {"error", {
                {"code", -32001},
                {"message", "InGameIdler is null"}
            }}
        };
    }

    // Handle Start Screen dismissal
    if (window_id == START_SCREEN_EVENT_ID) {
        void* start_screen = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)idler + 0xBE8), &start_screen) || !start_screen) {
            return {
                {"error", {
                    {"code", -32002},
                    {"message", "Start screen is not active"}
                }}
            };
        }

        if (!fn_start_screen_dismiss_) {
            return {
                {"error", {
                    {"code", -32003},
                    {"message", "fn_start_screen_dismiss_ not initialized"}
                }}
            };
        }

        LOG("[EVENT_MGR] Resolving Start Screen: invoking native CStartScreenView::Dismiss...");
        if (!SafeStartScreenDismiss(fn_start_screen_dismiss_, start_screen)) {
            LOG("[EVENT_MGR] CStartScreenView::Dismiss threw exception.");
            return {
                {"error", {
                    {"code", -32004},
                    {"message", "Failed to dismiss start screen due to exception"}
                }}
            };
        }

        LOG("[EVENT_MGR] Start screen dismissed successfully.");
        return {
            {"success", true},
            {"resolved_window_id", window_id},
            {"selected_option", option_index},
            {"message", "Start screen dismissed successfully"}
        };
    }

    // Handle Anomaly Window resolution
    if (window_id == ANOMALY_EVENT_ID) {
        void* anomaly_view = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)idler + 0xB08), &anomaly_view) || !anomaly_view) {
            return {
                {"error", {
                    {"code", -32010},
                    {"message", "Anomaly view is not active"}
                }}
            };
        }

        if (option_index == 0) {
            // Dismiss (暂时离开)
            if (!fn_anomaly_dismiss_) {
                return {
                    {"error", {
                        {"code", -32011},
                        {"message", "fn_anomaly_dismiss_ is null"}
                    }}
                };
            }
            LOG("[EVENT_MGR] Resolving Anomaly: invoking native CAnomalyView::Dismiss...");
            if (!SafeAnomalyDismiss(fn_anomaly_dismiss_, anomaly_view)) {
                return {
                    {"error", {
                        {"code", -32012},
                        {"message", "Exception occurred executing CAnomalyView::Dismiss"}
                    }}
                };
            }
            LOG("[EVENT_MGR] Anomaly window dismissed successfully.");
            return {
                {"success", true},
                {"resolved_window_id", window_id},
                {"selected_option", option_index},
                {"message", "Anomaly dismissed (暂时离开)"}
            };
        } else if (option_index == 1) {
            // Research (调查)
            if (!fn_anomaly_research_) {
                return {
                    {"error", {
                        {"code", -32013},
                        {"message", "fn_anomaly_research_ is null"}
                    }}
                };
            }
            LOG("[EVENT_MGR] Resolving Anomaly: invoking native CAnomalyView::OnResearchClicked...");
            if (!SafeAnomalyResearch(fn_anomaly_research_, anomaly_view)) {
                return {
                    {"error", {
                        {"code", -32014},
                        {"message", "Exception occurred executing CAnomalyView::OnResearchClicked"}
                    }}
                };
            }
            LOG("[EVENT_MGR] Anomaly research command dispatched successfully.");
            return {
                {"success", true},
                {"resolved_window_id", window_id},
                {"selected_option", option_index},
                {"message", "Anomaly research dispatched (调查)"}
            };
        } else {
            return {
                {"error", {
                    {"code", -32015},
                    {"message", "Invalid option_index for anomaly view. Legal options: 0 (暂时离开), 1 (调查)"}
                }}
            };
        }
    }

    // Handle First Contact View resolution
    if (window_id == FIRST_CONTACT_EVENT_ID) {
        void* fc_view = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)idler + 0xC90), &fc_view) || !fc_view) {
            return {
                {"error", {
                    {"code", -32016},
                    {"message", "First contact view is not active"}
                }}
            };
        }

        void* ui_window = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)fc_view + 0x78), &ui_window) || !ui_window) {
            return {
                {"error", {
                    {"code", -32017},
                    {"message", "First contact UI window is null"}
                }}
            };
        }

        uint8_t is_vis = 0;
        if (!SafeReadU8((const void*)((uintptr_t)ui_window + 0x41), &is_vis) || is_vis == 0) {
            return {
                {"error", {
                    {"code", -32018},
                    {"message", "First contact view is not visible"}
                }}
            };
        }

        if (!fn_first_contact_dismiss_) {
            return {
                {"error", {
                    {"code", -32019},
                    {"message", "fn_first_contact_dismiss_ is null"}
                }}
            };
        }

        LOGF("[EVENT_MGR] Resolving First Contact View with option %d...", option_index);
        if (!SafeFirstContactDismiss(fn_first_contact_dismiss_, fc_view)) {
            return {
                {"error", {
                    {"code", -32020},
                    {"message", "Exception occurred executing CFirstContactView::Dismiss"}
                }}
            };
        }

        LOG("[EVENT_MGR] First contact view dismissed successfully.");
        return {
            {"success", true},
            {"resolved_window_id", window_id},
            {"selected_option", option_index},
            {"message", "First contact view dismissed successfully"}
        };
    }

    // Handle Standard Modal Event Window
    void** windows = nullptr;
    uint32_t count = 0;

    if (!SafeReadPtr((const void*)((uintptr_t)idler + 0x170), (void**)&windows) ||
        !SafeReadU32((const void*)((uintptr_t)idler + 0x17c), &count)) {
        return {
            {"error", {
                {"code", -32005},
                {"message", "Failed to read active event windows array"}
            }}
        };
    }

    void* target_win = nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        void* win = nullptr;
        if (SafeReadPtr((const void*)&windows[i], &win) && win) {
            uint32_t cur_id = 0;
            if (SafeReadU32((const void*)((uintptr_t)win + 0x98), &cur_id) && cur_id == window_id) {
                target_win = win;
                break;
            }
        }
    }

    if (!target_win) {
        return {
            {"error", {
                {"code", -32006},
                {"message", "Event window_id not found among active events"}
            }}
        };
    }

    if (!fn_select_option_) {
        return {
            {"error", {
                {"code", -32007},
                {"message", "fn_select_option_ is null"}
            }}
        };
    }

    LOGF("[EVENT_MGR] Resolving standard event window_id %u with option %d...", window_id, option_index);
    if (!SafeSelectOption(fn_select_option_, target_win, option_index)) {
        LOGF("[EVENT_MGR] Exception in SelectOption for window_id %u", window_id);
        return {
            {"error", {
                {"code", -32008},
                {"message", "Exception occurred executing CEventWindow::SelectOption"}
            }}
        };
    }

    LOGF("[EVENT_MGR] Event window %u successfully resolved with option %d.", window_id, option_index);
    return {
        {"success", true},
        {"resolved_window_id", window_id},
        {"selected_option", option_index}
    };
}

} // namespace bridge
