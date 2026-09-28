#include "commands.hpp"
#include "game_state.hpp"
#include "common.hpp"

namespace bridge {

Commands& Commands::Get() {
    static Commands instance;
    return instance;
}

uintptr_t Commands::FindPattern(const uint8_t* pattern, const char* mask, size_t size) {
    if (!base_address_) return 0;

    auto dos_header = (PIMAGE_DOS_HEADER)base_address_;
    auto nt_headers = (PIMAGE_NT_HEADERS)(base_address_ + dos_header->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt_headers);

    for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, ++section) {
        if (memcmp(section->Name, ".text", 5) == 0) {
            uint8_t* start = (uint8_t*)(base_address_ + section->VirtualAddress);
            size_t sec_size = section->Misc.VirtualSize;

            for (size_t j = 0; j < sec_size - size; ++j) {
                bool match = true;
                for (size_t k = 0; k < size; ++k) {
                    if (mask[k] != '?' && pattern[k] != start[j + k]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return (uintptr_t)(start + j);
                }
            }
        }
    }
    return 0;
}

bool Commands::Init(uintptr_t base_address) {
    base_address_ = base_address;

    // Pattern for PostCommand: 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 30 0f b6 ea 48
    const uint8_t pc_pat[] = {
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
        0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x30, 0x0f, 0xb6, 0xea, 0x48
    };
    const char* pc_mask = "xxxxxxxxxxxxxxxxxxxxxxxx";

    uintptr_t scanned_pc = FindPattern(pc_pat, pc_mask, sizeof(pc_pat));
    if (scanned_pc) {
        fn_post_command_ = (FnPostCommand)scanned_pc;
        LOGF("[COMMANDS] PostCommand dynamically found at 0x%llX", (unsigned long long)scanned_pc);
    } else {
        fn_post_command_ = (FnPostCommand)(base_address_ + kRvaPostCommand);
        LOGF("[COMMANDS] PostCommand falling back to static RVA 0x%llX: 0x%llX", (unsigned long long)kRvaPostCommand, (unsigned long long)fn_post_command_);
    }

    fn_set_paused_ = (FnSetPaused)(base_address_ + 0x9362D0);
    fn_set_game_speed_ = (FnSetGameSpeed)(base_address_ + 0x935BE0);

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
