#pragma once

#include "common.hpp"

namespace bridge {

class Commands {
public:
    static Commands& Get();

    bool Init(uintptr_t base_address);

    // Native pause/unpause dispatch
    nlohmann::json SetPaused(bool paused);

    // Native speed control (0 = 1x, ..., 4 = 5x)
    nlohmann::json SetSpeed(uint32_t target_speed);

private:
    Commands() = default;

    uintptr_t base_address_{ 0 };

    // Function pointers
    using FnPostCommand = void(*)(void* cmd, bool unk);
    using FnCommandCtor = void*(*)(void* cmd);
    using FnOperatorNew = void*(*)(size_t size);

    FnPostCommand fn_post_command_{ nullptr };
    FnCommandCtor fn_increase_speed_{ nullptr };
    FnCommandCtor fn_decrease_speed_{ nullptr };
    FnOperatorNew fn_operator_new_{ nullptr };

    uintptr_t vt_pause_command_{ 0 };

    uintptr_t FindPattern(const uint8_t* pattern, const char* mask, size_t size);
};

} // namespace bridge
