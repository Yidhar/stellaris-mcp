#pragma once

#include "common.hpp"

namespace bridge {

struct EventOptionInfo {
    int index{ 0 };
    std::string text;
    bool is_valid{ true };
};

struct EventInfo {
    uint32_t window_id{ 0 };
    std::string title;
    std::string description;
    std::vector<EventOptionInfo> options;
};

class EventManager {
public:
    static constexpr uint32_t START_SCREEN_EVENT_ID = 0xFFFF0001;
    static constexpr uint32_t ANOMALY_EVENT_ID = 0xFFFF0002;
    static constexpr uint32_t FIRST_CONTACT_EVENT_ID = 0xFFFF0003;

    static EventManager& Get();

    bool Init(uintptr_t base_address);

    // Call on main thread to get active events snapshot
    std::vector<EventInfo> GetActiveEvents();
    nlohmann::json GetActiveEventsJson();

    // Call on main thread to resolve an event choice
    nlohmann::json ResolveEvent(uint32_t window_id, int option_index);

    using FnFindChild = void*(*)(void* container, void* pdx_string);
    using FnSelectOption = void(*)(void* event_window, int option_index);
    using FnStartScreenDismiss = void(*)(void* start_screen);
    using FnAnomalyDismiss = void(*)(void* anomaly_view);
    using FnAnomalyResearch = void(*)(void* anomaly_view);
    using FnFirstContactDismiss = void(*)(void* fc_view);

private:
    EventManager() = default;
    ~EventManager() = default;

    uintptr_t base_address_{ 0 };

    FnFindChild fn_find_child_{ nullptr };
    FnSelectOption fn_select_option_{ nullptr };
    FnStartScreenDismiss fn_start_screen_dismiss_{ nullptr };
    FnAnomalyDismiss fn_anomaly_dismiss_{ nullptr };
    FnAnomalyResearch fn_anomaly_research_{ nullptr };
    FnFirstContactDismiss fn_first_contact_dismiss_{ nullptr };

    void* FindChildByName(void* container, const char* name);
    std::string ExtractPdxString(void* ptr);
};

} // namespace bridge
