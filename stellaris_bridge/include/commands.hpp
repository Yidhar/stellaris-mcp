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

    using FnPostCommand = void(*)(void* cmd, bool unk);
    using FnCreateCommand = void*(*)();
    using FnSetPaused = void(*)(void* idler, void* data);
    using FnSetGameSpeed = void(*)(void* idler, int speed);

    FnPostCommand GetPostCommand() const { return fn_post_command_; }

private:
    Commands() = default;

    uintptr_t base_address_{ 0 };

    FnPostCommand fn_post_command_{ nullptr };
    FnCreateCommand fn_create_pause_game_{ nullptr };
    FnCreateCommand fn_create_inc_speed_{ nullptr };
    FnCreateCommand fn_create_dec_speed_{ nullptr };
    FnSetPaused fn_set_paused_{ nullptr };
    FnSetGameSpeed fn_set_game_speed_{ nullptr };

};

} // namespace bridge
