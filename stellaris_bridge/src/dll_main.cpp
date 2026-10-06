#include "common.hpp"
#include "hook_manager.hpp"
#include "game_state.hpp"
#include "commands.hpp"
#include "command_builder.hpp"
#include "event_manager.hpp"
#include "notification_manager.hpp"
#include "alert_manager.hpp"
#include "tech_manager.hpp"
#include "situation_log_manager.hpp"
#include "galaxy_manager.hpp"
#include "government_manager.hpp"
#include "society_manager.hpp"
#include "leader_manager.hpp"
#include "species_manager.hpp"
#include "fleet_manager.hpp"
#include "ship_designer.hpp"
#include "market_manager.hpp"
#include "discoveries_manager.hpp"
#include "contacts_manager.hpp"
#include "outliner_manager.hpp"
#include "ipc_server.hpp"
#include "plugin.hpp"

namespace bridge {

DWORD WINAPI MainInitThread(LPVOID lpParam) {
    (void)lpParam;

    // settings and log live in the plugin's own folder (config\, logs\), never the game folder
    plugin::Init();
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    LOG("=================================================");
    LOG("[INIT] Stellaris MCP Bridge DLL Loaded");
    LOGF("[INIT] Host Process: %s", exePath);

    uintptr_t base_address = (uintptr_t)GetModuleHandleA(nullptr);
    LOGF("[INIT] Stellaris ImageBase: 0x%llX", (unsigned long long)base_address);

    GameState::Get().Init(base_address);
    Commands::Get().Init(base_address);
    CommandBuilder::Get().Init(base_address);
    EventManager::Get().Init(base_address);
    NotificationManager::Get().Init(base_address);
    AlertManager::Get().Init(base_address);
    TechManager::Get().Init(base_address);
    SituationLogManager::Get().Init(base_address);
    GalaxyManager::Get().Init(base_address);
    GovernmentManager::Get().Init(base_address);
    SocietyManager::Get().Init(base_address);
    LeaderManager::Get().Init(base_address);
    SpeciesManager::Get().Init(base_address);
    FleetManager::Get().Init(base_address);
    ShipDesigner::Get().Init(base_address);
    MarketManager::Get().Init(base_address);
    DiscoveriesManager::Get().Init(base_address);
    ContactsManager::Get().Init(base_address);
    OutlinerManager::Get().Init(base_address);

    if (!HookManager::Get().Init()) {
        LOG("[INIT_FATAL] Failed to hook DXGI Present. Aborting.");
        return 1;
    }
    CommandBuilder::Get().InstallGuards();

    if (!IPCServer::Get().Start()) {
        LOG("[INIT_FATAL] Failed to start IPC Named Pipe server. Aborting.");
        return 1;
    }

    LOG("[INIT_SUCCESS] Stellaris MCP Bridge fully operational.");
    LOG("=================================================");
    return 0;
}

} // namespace bridge

// Loaded by the Stellaris launcher (or by hand during development) into a running game: DllMain only
// starts the init thread; everything else happens there and in the Present hook.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)bridge::MainInitThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        bridge::IPCServer::Get().Stop();
        bridge::HookManager::Get().Shutdown();
        LOG("[SHUTDOWN] Stellaris MCP Bridge Detached.");
        break;
    }
    return TRUE;
}
