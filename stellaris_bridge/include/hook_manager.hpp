#pragma once

#include "common.hpp"
#include <MinHook.h>

namespace bridge {

class HookManager {
public:
    static HookManager& Get();

    bool Init();
    void Shutdown();

    static HRESULT __stdcall Hooked_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

private:
    HookManager() = default;
    ~HookManager() = default;

    using PfnPresent = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    static PfnPresent original_present_;

    void* present_target_{ nullptr };
    bool is_hooked_{ false };
};

} // namespace bridge
