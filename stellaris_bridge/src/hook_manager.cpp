#include "hook_manager.hpp"
#include "task_queue.hpp"
#include "plugin.hpp"

namespace bridge {

HookManager::PfnPresent HookManager::original_present_ = nullptr;

HookManager& HookManager::Get() {
    static HookManager instance;
    return instance;
}

HRESULT __stdcall HookManager::Hooked_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // Process all queued main-thread tasks
    TaskQueue::Get().ProcessAll();
    // config\stellaris_mcp.ini edited while the game runs (checked every couple of seconds)
    plugin::PollSettings();

    // Call the original Present function
    if (original_present_) {
        return original_present_(pSwapChain, SyncInterval, Flags);
    }
    return S_OK;
}

static HRESULT SafeD3D11Create(const D3D_FEATURE_LEVEL* featureLevels, const DXGI_SWAP_CHAIN_DESC* sd, IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppContext) {
    __try {
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 2, D3D11_SDK_VERSION, sd, ppSwapChain, ppDevice, pFeatureLevel, ppContext);
        if (FAILED(hr)) {
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, featureLevels, 2, D3D11_SDK_VERSION, sd, ppSwapChain, ppDevice, pFeatureLevel, ppContext);
        }
        return hr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

bool HookManager::Init() {
    if (is_hooked_) {
        return true;
    }

    LOG("[HOOK] Initializing MinHook...");
    auto mh_status = MH_Initialize();
    if (mh_status != MH_OK && mh_status != MH_ERROR_ALREADY_INITIALIZED) {
        LOGF("[HOOK] Failed to initialize MinHook (status: %d).", mh_status);
        return false;
    }

    LOG("[HOOK] Registering dummy window class...");
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0L, 0L, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"StellarisDummyClass", nullptr };
    RegisterClassExW(&wc);
    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"Dummy", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hWnd) {
        LOGF("[HOOK] Failed to create dummy window (err: 0x%08X).", GetLastError());
    }

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 2;
    sd.BufferDesc.Height = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    IDXGISwapChain* pSwapChain = nullptr;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;

    LOG("[HOOK] Calling D3D11CreateDeviceAndSwapChain...");
    HRESULT hr = SafeD3D11Create(featureLevels, &sd, &pSwapChain, &pDevice, &featureLevel, &pContext);

    if (FAILED(hr) || !pSwapChain) {
        LOGF("[HOOK] Failed to create dummy D3D11 device and swap chain (HRESULT: 0x%08X).", hr);
        if (hWnd) DestroyWindow(hWnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    void** pVMT = *(void***)pSwapChain;
    present_target_ = pVMT[8]; // IDXGISwapChain::Present is at index 8

    // Release dummy objects
    pSwapChain->Release();
    if (pContext) pContext->Release();
    if (pDevice) pDevice->Release();
    if (hWnd) DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    LOGF("[HOOK] Discovered IDXGISwapChain::Present at 0x%llX", (unsigned long long)present_target_);

    if (MH_CreateHook(present_target_, (LPVOID)&Hooked_Present, (LPVOID*)&original_present_) != MH_OK) {
        LOG("[HOOK] Failed to create hook for Present.");
        return false;
    }

    if (MH_EnableHook(present_target_) != MH_OK) {
        LOG("[HOOK] Failed to enable hook for Present.");
        return false;
    }

    is_hooked_ = true;
    LOG("[HOOK] IDXGISwapChain::Present hooked successfully.");
    return true;
}

void HookManager::Shutdown() {
    if (is_hooked_) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        is_hooked_ = false;
        LOG("[HOOK] Hook manager shut down.");
    }
}

} // namespace bridge
