#pragma once

#include "common.hpp"

namespace bridge {

struct NotificationItem {
    uint32_t index{ 0 };
    std::string type_name;
    std::string title_key;
    std::string param_text;
    bool can_click{ true };
};

class NotificationManager {
public:
    static NotificationManager& Get();

    bool Init(uintptr_t base_address);

    // Call on main thread to get active notifications snapshot
    std::vector<NotificationItem> GetNotifications();
    nlohmann::json GetNotificationsJson();

    // Call on main thread to trigger notification left-click (open into modal event / anomaly window)
    nlohmann::json OpenNotification(uint32_t index);

    using FnNotificationLeftClick = void(*)(void* notif);

private:
    NotificationManager() = default;
    ~NotificationManager() = default;

    uintptr_t base_address_{ 0 };
    FnNotificationLeftClick fn_notif_left_click_{ nullptr };

    std::string ExtractPdxString(void* ptr);
};

} // namespace bridge
