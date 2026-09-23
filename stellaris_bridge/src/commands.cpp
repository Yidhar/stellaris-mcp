#include "commands.hpp"
#include "game_state.hpp"

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
        fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);
        LOGF("[COMMANDS] PostCommand falling back to static RVA 0x5F8590: 0x%llX", (unsigned long long)fn_post_command_);
    }

    fn_increase_speed_ = (FnCommandCtor)(base_address_ + 0x924990);
    fn_decrease_speed_ = (FnCommandCtor)(base_address_ + 0x9249C0);
    fn_operator_new_ = (FnOperatorNew)(base_address_ + 0x20208C8);
    vt_pause_command_ = 0;

    LOGF("[COMMANDS] Target addresses initialized (Base: 0x%llX, vt_Pause: 0x%llX, IncSpeed: 0x%llX, DecSpeed: 0x%llX)",
        (unsigned long long)base_address_,
        (unsigned long long)vt_pause_command_,
        (unsigned long long)fn_increase_speed_,
        (unsigned long long)fn_decrease_speed_);

    return fn_post_command_ != nullptr;
}

nlohmann::json Commands::SetPaused(bool paused) {
    auto status = GameState::Get().ReadStatus();
    if (!status.in_game) {
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

    // Call CInGameIdler::SetPaused directly on main thread (RVA 0x994170)
    struct {
        uint8_t pad0[0x10]{ 0 };
        char str_buf[16]{ 0 };
        uint64_t str_size{ 0 };
        uint64_t str_cap{ 0xf };
        uint8_t is_paused{ 1 };     // +0x30: 1 = paused, 0 = running
        uint8_t is_auto_pause{ 1 }; // +0x31: 1 = bypass name check / unlock
        uint8_t pad1[14]{ 0 };
    } pause_data;

    // Clausewitz idler convention: 1 = paused, 0 = running
    pause_data.is_paused = paused ? 1 : 0;
    pause_data.is_auto_pause = 1;

    using FnSetPaused = void(*)(void* idler, void* data);
    auto fn_set_paused = (FnSetPaused)(base_address_ + 0x935FB0);

    LOGF("[COMMANDS] Calling native CInGameIdler::SetPaused(is_paused=%d)...", pause_data.is_paused);
    fn_set_paused(idler, &pause_data);

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
    if (!status.in_game) {
        return {
            {"success", false},
            {"error", "Game is not currently loaded in an active session."}
        };
    }

    if (!fn_post_command_ || !fn_increase_speed_ || !fn_decrease_speed_) {
        return {
            {"success", false},
            {"error", "Speed command addresses not initialized."}
        };
    }

    uint32_t cur_speed = status.speed;
    LOGF("[COMMANDS] Setting speed: current=%u, target=%u", cur_speed, target_speed);

    if (target_speed > cur_speed) {
        uint32_t steps = target_speed - cur_speed;
        for (uint32_t i = 0; i < steps; ++i) {
            void* mem = fn_operator_new_ ? fn_operator_new_(0x20) : malloc(0x20);
            if (mem) {
                memset(mem, 0, 0x20);
                fn_increase_speed_(mem);
                fn_post_command_(mem, false);
            }
        }
    } else if (target_speed < cur_speed) {
        uint32_t steps = cur_speed - target_speed;
        for (uint32_t i = 0; i < steps; ++i) {
            void* mem = fn_operator_new_ ? fn_operator_new_(0x20) : malloc(0x20);
            if (mem) {
                memset(mem, 0, 0x20);
                fn_decrease_speed_(mem);
                fn_post_command_(mem, false);
            }
        }
    }

    return {
        {"success", true},
        {"previous_speed", cur_speed},
        {"target_speed", target_speed}
    };
}

} // namespace bridge
