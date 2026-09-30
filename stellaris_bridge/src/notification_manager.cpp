#include "notification_manager.hpp"
#include "sdk/stellaris_sdk.hpp"

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

static bool SafeCopyChars(char* dest, const char* src, size_t count) {
    __try {
        memcpy(dest, src, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeNotifClick(NotificationManager::FnNotificationLeftClick fn, void* notif) {
    __try {
        fn(notif);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeCanClick(void* fn_ptr, void* notif) {
    typedef bool(*FnCanClick)(void*);
    FnCanClick fn = (FnCanClick)fn_ptr;
    __try {
        return fn(notif);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

NotificationManager& NotificationManager::Get() {
    static NotificationManager instance;
    return instance;
}

bool NotificationManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    // RVA: Notification::OnLeftClick = 0x3211A0
    fn_notif_left_click_ = (FnNotificationLeftClick)(base_address_ + 0x3211A0);

    LOGF("[NOTIF_MGR] Initialized: Base=0x%llX, NotifLeftClick=0x%llX",
        (unsigned long long)base_address_,
        (unsigned long long)fn_notif_left_click_);

    return true;
}

std::string NotificationManager::ExtractPdxString(void* ptr) {
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

std::vector<NotificationItem> NotificationManager::GetNotifications() {
    std::vector<NotificationItem> items;

    if (!base_address_) return items;

    // Global NotificationManager pointer at [base + sdk::glob::g_CurrentGameState]
    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &mgr) || !mgr) {
        return items;
    }

    void** notifs = nullptr;
    uint32_t count = 0;

    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x498), (void**)&notifs) ||
        !SafeReadU32((const void*)((uintptr_t)mgr + 0x4A4), &count)) {
        return items;
    }

    if (!notifs || count == 0 || count > 200) {
        return items;
    }

    for (uint32_t i = 0; i < count; ++i) {
        void* notif = nullptr;
        if (!SafeReadPtr((const void*)&notifs[i], &notif) || !notif) {
            continue;
        }

        NotificationItem item;
        item.index = i;

        // Type info at notif + 0x18
        void* type_info = nullptr;
        if (SafeReadPtr((const void*)((uintptr_t)notif + 0x18), &type_info) && type_info) {
            item.type_name = ExtractPdxString((void*)((uintptr_t)type_info + 0x18));
            item.title_key = ExtractPdxString((void*)((uintptr_t)type_info + 0xB0));
        }

        // Custom parameter text at notif + 0x30
        item.param_text = ExtractPdxString((void*)((uintptr_t)notif + 0x30));

        // Can click check via vtable[7] (RVA 0x31FF20)
        void* vt = nullptr;
        if (SafeReadPtr((const void*)notif, &vt) && vt) {
            void* fn_can_click_ptr = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)vt + 7 * sizeof(void*)), &fn_can_click_ptr) && fn_can_click_ptr) {
                item.can_click = SafeCanClick(fn_can_click_ptr, notif);
            }
        }

        items.push_back(item);
    }

    return items;
}

nlohmann::json NotificationManager::GetNotificationsJson() {
    auto notifs = GetNotifications();
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& item : notifs) {
        arr.push_back({
            {"index", item.index},
            {"type", item.type_name},
            {"title_key", item.title_key},
            {"param", item.param_text},
            {"can_click", item.can_click}
        });
    }

    return arr;
}

nlohmann::json NotificationManager::OpenNotification(uint32_t index) {
    if (!base_address_) {
        return {
            {"error", {
                {"code", -32020},
                {"message", "Base address not initialized"}
            }}
        };
    }

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + sdk::glob::g_CurrentGameState), &mgr) || !mgr) {
        return {
            {"error", {
                {"code", -32021},
                {"message", "NotificationManager is null"}
            }}
        };
    }

    void** notifs = nullptr;
    uint32_t count = 0;

    if (!SafeReadPtr((const void*)((uintptr_t)mgr + 0x498), (void**)&notifs) ||
        !SafeReadU32((const void*)((uintptr_t)mgr + 0x4A4), &count)) {
        return {
            {"error", {
                {"code", -32022},
                {"message", "Failed to read notifications array"}
            }}
        };
    }

    if (index >= count || !notifs) {
        return {
            {"error", {
                {"code", -32023},
                {"message", "Notification index out of range. Current count: " + std::to_string(count)}
            }}
        };
    }

    void* target_notif = nullptr;
    if (!SafeReadPtr((const void*)&notifs[index], &target_notif) || !target_notif) {
        return {
            {"error", {
                {"code", -32024},
                {"message", "Target notification pointer is invalid"}
            }}
        };
    }

    if (!fn_notif_left_click_) {
        return {
            {"error", {
                {"code", -32025},
                {"message", "fn_notif_left_click_ is null"}
            }}
        };
    }

    // Capture type info before opening
    std::string type_name;
    void* type_info = nullptr;
    if (SafeReadPtr((const void*)((uintptr_t)target_notif + 0x18), &type_info) && type_info) {
        type_name = ExtractPdxString((void*)((uintptr_t)type_info + 0x18));
    }

    LOGF("[NOTIF_MGR] Opening notification index %u (Type: %s)...", index, type_name.c_str());
    if (!SafeNotifClick(fn_notif_left_click_, target_notif)) {
        LOGF("[NOTIF_MGR] Exception occurred executing Notification::OnLeftClick for index %u", index);
        return {
            {"error", {
                {"code", -32026},
                {"message", "Exception occurred executing Notification::OnLeftClick"}
            }}
        };
    }

    LOGF("[NOTIF_MGR] Notification index %u opened successfully.", index);
    return {
        {"success", true},
        {"opened_index", index},
        {"type", type_name},
        {"message", "Notification opened successfully"}
    };
}

} // namespace bridge
