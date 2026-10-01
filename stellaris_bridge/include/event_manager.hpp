#pragma once

#include "common.hpp"

namespace bridge {

struct EventOptionInfo {
    int index{ 0 };
    std::string text;
    bool is_valid{ true };
    std::string effects;  // the option's effect tooltip, as the game shows it on hover
};

struct EventInfo {
    uint32_t window_id{ 0 };
    std::string title;
    std::string description;
    std::string event_key;                 // script id, e.g. "colony.1"
    uint32_t open_event_id{ 0xFFFFFFFF };  // COpenPlayerEvent id behind a standard event window
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

private:
    void ReadEventData(void* win, EventInfo& info);
    void ReadShownOptions(void* win, void* event, EventInfo& info);
public:

    using FnFindChild = void*(*)(void* container, void* pdx_string);
    using FnSelectOption = void(*)(void* event_window, int option_index);

private:
    EventManager() = default;
    ~EventManager() = default;

    uintptr_t base_address_{ 0 };

    FnFindChild fn_find_child_{ nullptr };
    FnSelectOption fn_select_option_{ nullptr };

    void* ShownView(void* idler, std::ptrdiff_t member, uintptr_t vtable_rva);
    bool HideView(void* view);
    void* FindChildByName(void* container, const char* name);
    std::string ExtractPdxString(void* ptr);
};

} // namespace bridge
