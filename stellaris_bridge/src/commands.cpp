#include "commands.hpp"
#include "game_state.hpp"
#include "common.hpp"

namespace bridge {

Commands& Commands::Get() {
    static Commands instance;
    return instance;
}

bool Commands::Init(uintptr_t base_address) {
    base_address_ = base_address;

    if (SdkMatchesImage(base_address_)) {
        fn_post_command_ = (FnPostCommand)(base_address_ + sdk::fn::PostCommand);
        fn_set_paused_ = (FnSetPaused)(base_address_ + sdk::fn::CInGameIdler_SetPaused);
        fn_set_game_speed_ = (FnSetGameSpeed)(base_address_ + sdk::fn::CInGameIdler_SetGameSpeed);
    } else {
        LOG("[COMMANDS] SDK does not match stellaris.exe: engine calls disabled");
        fn_post_command_ = nullptr;
        fn_set_paused_ = nullptr;
        fn_set_game_speed_ = nullptr;
    }

    LOGF("[COMMANDS] Target addresses initialized (Base: 0x%llX, SetPaused: 0x%llX, SetGameSpeed: 0x%llX)",
        (unsigned long long)base_address_,
        (unsigned long long)fn_set_paused_,
        (unsigned long long)fn_set_game_speed_);

    return fn_post_command_ != nullptr && fn_set_paused_ != nullptr && fn_set_game_speed_ != nullptr;
}

static bool CallNativeSetPaused(Commands::FnSetPaused fn, void* idler, void* data) {
    if (!idler || !fn) return false;
    __try {
        void* timer = *(void**)((uintptr_t)idler + 0xf08);
        if (!timer) return false;
        fn(idler, data);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool CallNativeSetGameSpeed(Commands::FnSetGameSpeed fn, void* idler, int speed) {
    if (!idler || !fn) return false;
    __try {
        void* timer = *(void**)((uintptr_t)idler + 0xf08);
        if (!timer) return false;
        fn(idler, speed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

nlohmann::json Commands::SetPaused(bool paused) {
    auto status = GameState::Get().ReadStatus();
    if (!status.in_game || !GameState::Get().GetPlayerCountry()) {
        return {
            {"success", false},
            {"error", "Game is not currently loaded in an active session."}
        };
    }

    void* idler = GameState::Get().GetInGameIdler();
    if (!idler) {
        return {
            {"success", false},
            {"error", "InGameIdler not available."}
        };
    }

    struct SPauseGameSettings {
        uint64_t pad0{ 0 };
        uint64_t pad8{ 0 };
        char* p_buf{ nullptr };      // +0x10: must point to inline_buf at +0x20
        uint64_t length{ 0 };        // +0x18: 0
        char inline_buf[16]{ 0 };    // +0x20: null-terminated empty string
        uint8_t is_paused{ 1 };      // +0x30: 1 = paused, 0 = running
        uint8_t override_flag{ 2 };  // +0x31: 2 = bypass lock
        uint8_t pad32[14]{ 0 };
    } pause_data;

    pause_data.p_buf = pause_data.inline_buf;
    pause_data.is_paused = paused ? 1 : 0;
    pause_data.override_flag = 2;

    LOGF("[COMMANDS] Calling native CInGameIdler::SetPaused(is_paused=%d)...", pause_data.is_paused);
    if (!CallNativeSetPaused(fn_set_paused_, idler, &pause_data)) {
        return {
            {"success", false},
            {"error", "Exception in native SetPaused"}
        };
    }

    return {
        {"success", true},
        {"paused", paused}
    };
}

nlohmann::json Commands::SetSpeed(uint32_t target_speed) {
    if (target_speed > 4) {
        target_speed = 4;
    }

    auto status = GameState::Get().ReadStatus();
    if (!status.in_game || !GameState::Get().GetPlayerCountry()) {
        return {
            {"success", false},
            {"error", "Game is not currently loaded in an active session."}
        };
    }

    void* idler = GameState::Get().GetInGameIdler();
    if (!idler) {
        return {
            {"success", false},
            {"error", "InGameIdler not available."}
        };
    }

    if (!fn_set_game_speed_) {
        return {
            {"success", false},
            {"error", "SetGameSpeed function not initialized."}
        };
    }

    uint32_t cur_speed = status.speed;
    LOGF("[COMMANDS] Calling native CInGameIdler::SetGameSpeed: current=%u, target=%u", cur_speed, target_speed);

    if (!CallNativeSetGameSpeed(fn_set_game_speed_, idler, (int)target_speed)) {
        return {
            {"success", false},
            {"error", "Exception in native SetGameSpeed"}
        };
    }

    return {
        {"success", true},
        {"previous_speed", cur_speed},
        {"target_speed", target_speed}
    };
}

} // namespace bridge
