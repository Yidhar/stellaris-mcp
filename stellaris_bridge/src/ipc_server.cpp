#include "ipc_server.hpp"
#include "command_builder.hpp"
#include "task_queue.hpp"
#include "game_state.hpp"
#include "commands.hpp"
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

namespace bridge {

static constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\stellaris_mcp_bridge";

IPCServer& IPCServer::Get() {
    static IPCServer instance;
    return instance;
}

bool IPCServer::Start() {
    if (is_running_) return true;

    // Refuse to share the pipe name: a stale game instance or a bridge that did not unload
    // cleanly would otherwise receive a random share of the client connections.
    if (WaitNamedPipeW(kPipeName, 1) || GetLastError() != ERROR_FILE_NOT_FOUND) {
        LOG("[IPC_FATAL] Pipe stellaris_mcp_bridge is already served by another bridge "
            "(another stellaris.exe, or a previous injection still loaded). Not starting.");
        return false;
    }

    is_running_ = true;
    worker_thread_ = std::thread(&IPCServer::WorkerLoop, this);
    LOG("[IPC] Named Pipe server thread started.");
    return true;
}

void IPCServer::Stop() {
    if (!is_running_) return;

    is_running_ = false;

    // Connect to the pipe locally to unblock ConnectNamedPipe
    HANDLE hDummy = CreateFileW(
        kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr
    );
    if (hDummy != INVALID_HANDLE_VALUE) {
        CloseHandle(hDummy);
    }

    if (worker_thread_.joinable()) {
        worker_thread_.detach();
    }

    LOG("[IPC] Named Pipe server stopped.");
}

nlohmann::json IPCServer::ProcessRequest(const nlohmann::json& req) {
    auto id = req.value("id", nlohmann::json(nullptr));
    std::string method = req.value("method", "");
    nlohmann::json params = req.value("params", nlohmann::json::object());

    LOGF("[IPC_REQ] Method: %s, ID: %s", method.c_str(), id.dump().c_str());

    if (method == "ping") {
        return {
            {"jsonrpc", "2.0"},
            {"result", {{"status", "pong"}}},
            {"id", id}
        };
    }

    std::future<nlohmann::json> fut;

    if (method == "self_test_purecall_guard") {
        fut = TaskQueue::Get().Enqueue([]() {
            return CommandBuilder::Get().SelfTestPurecallGuard();
        });
    } else if (method == "run_console") {
        // Debug/test helper: runs one line in the in-game console (e.g. "effect ...").
        std::string line = params.value("command", std::string());
        fut = TaskQueue::Get().Enqueue([line]() {
            std::string error;
            bool ok = CommandBuilder::Get().RunConsoleCommand(line, &error);
            nlohmann::json r = {{"ok", ok}, {"command", line}};
            if (!ok) r["error"] = error;
            return r;
        });
    } else if (method == "get_status") {
        fut = TaskQueue::Get().Enqueue([]() {
            return GameState::Get().GetStatusJson();
        });
    } else if (method == "set_paused") {
        bool paused = params.value("paused", true);
        fut = TaskQueue::Get().Enqueue([paused]() {
            return Commands::Get().SetPaused(paused);
        });
    } else if (method == "set_speed") {
        uint32_t speed = params.value("speed", 0);
        fut = TaskQueue::Get().Enqueue([speed]() {
            return Commands::Get().SetSpeed(speed);
        });
    } else if (method == "get_active_events") {
        fut = TaskQueue::Get().Enqueue([]() {
            return EventManager::Get().GetActiveEventsJson();
        });
    } else if (method == "resolve_event") {
        uint32_t window_id = params.value("window_id", 0);
        int option_index = params.value("option_index", 0);
        fut = TaskQueue::Get().Enqueue([window_id, option_index]() {
            return EventManager::Get().ResolveEvent(window_id, option_index);
        });
    } else if (method == "get_notifications") {
        fut = TaskQueue::Get().Enqueue([]() {
            return NotificationManager::Get().GetNotificationsJson();
        });
    } else if (method == "open_notification") {
        uint32_t index = params.value("index", 0);
        fut = TaskQueue::Get().Enqueue([index]() {
            return NotificationManager::Get().OpenNotification(index);
        });
    } else if (method == "get_alerts") {
        fut = TaskQueue::Get().Enqueue([]() {
            return AlertManager::Get().GetAlertsJson();
        });
    } else if (method == "open_alert") {
        uint32_t alert_id = params.value("alert_id", 0);
        fut = TaskQueue::Get().Enqueue([alert_id]() {
            return AlertManager::Get().OpenAlert(alert_id);
        });
    } else if (method == "get_research_state") {
        fut = TaskQueue::Get().Enqueue([]() {
            return TechManager::Get().GetResearchStateJson();
        });
    } else if (method == "select_research") {
        uint32_t area = 0;
        if (params.contains("area")) {
            if (params["area"].is_number()) {
                area = params["area"].get<uint32_t>();
            } else if (params["area"].is_string()) {
                std::string a_str = params["area"].get<std::string>();
                if (a_str == "physics" || a_str == "0") area = 0;
                else if (a_str == "society" || a_str == "1") area = 1;
                else if (a_str == "engineering" || a_str == "2") area = 2;
            }
        }
        std::string tech_key = params.value("tech_key", "");
        fut = TaskQueue::Get().Enqueue([area, tech_key]() {
            return TechManager::Get().SelectResearch(area, tech_key);
        });
    } else if (method == "cancel_research") {
        uint32_t area = 0;
        if (params.contains("area")) {
            if (params["area"].is_number()) {
                area = params["area"].get<uint32_t>();
            } else if (params["area"].is_string()) {
                std::string a_str = params["area"].get<std::string>();
                if (a_str == "physics" || a_str == "0") area = 0;
                else if (a_str == "society" || a_str == "1") area = 1;
                else if (a_str == "engineering" || a_str == "2") area = 2;
            }
        }
        fut = TaskQueue::Get().Enqueue([area]() {
            return TechManager::Get().CancelResearch(area);
        });
    } else if (method == "get_situation_log") {
        bool player_only = params.value("player_only", true);
        fut = TaskQueue::Get().Enqueue([player_only]() {
            return SituationLogManager::Get().GetSituationLogJson(player_only);
        });
    } else if (method == "get_galaxy_overview") {
        fut = TaskQueue::Get().Enqueue([]() { return GalaxyManager::Get().GetOverviewJson(); });
    } else if (method == "get_galaxy_map") {
        uint32_t center = params.value("center_system_id", 0xFFFFFFFFu);
        int jumps = params.value("jumps", 3);
        fut = TaskQueue::Get().Enqueue([center, jumps]() { return GalaxyManager::Get().GetMapJson(center, jumps); });
    } else if (method == "get_system") {
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        fut = TaskQueue::Get().Enqueue([system_id]() { return GalaxyManager::Get().GetSystemJson(system_id); });
    } else if (method == "move_fleet") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, system_id, queue]() {
            return GalaxyManager::Get().MoveFleet(fleet_id, system_id, queue);
        });
    } else if (method == "build_outpost") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, system_id, queue]() {
            return GalaxyManager::Get().BuildOutpost(fleet_id, system_id, queue);
        });
    } else if (method == "get_megastructure") {
        uint32_t id = params.value("megastructure_id", 0xFFFFFFFFu);
        fut = TaskQueue::Get().Enqueue([id]() { return GalaxyManager::Get().GetMegastructureJson(id); });
    } else if (method == "upgrade_megastructure") {
        uint32_t id = params.value("megastructure_id", 0xFFFFFFFFu);
        std::string type = params.value("type", std::string());
        fut = TaskQueue::Get().Enqueue([id, type]() { return GalaxyManager::Get().UpgradeMegastructure(id, type); });
    } else if (method == "get_buildable_megastructures") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        fut = TaskQueue::Get().Enqueue([fleet_id, system_id]() {
            return GalaxyManager::Get().GetBuildableMegastructures(fleet_id, system_id);
        });
    } else if (method == "build_megastructure") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        std::string type = params.value("type", std::string());
        uint32_t planet_id = params.value("planet_id", 0xFFFFFFFFu);
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        uint32_t toward = params.value("toward_system_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, type, planet_id, system_id, toward, queue]() {
            return GalaxyManager::Get().BuildMegastructure(fleet_id, type, planet_id, system_id, toward, queue);
        });
    } else if (method == "colonize") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t planet_id = params.value("planet_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, planet_id, queue]() {
            return GalaxyManager::Get().Colonize(fleet_id, planet_id, queue);
        });
    } else if (method == "cancel_fleet_orders") {
        std::vector<uint32_t> ids = params.value("fleet_ids", std::vector<uint32_t>{});
        fut = TaskQueue::Get().Enqueue([ids]() { return GalaxyManager::Get().CancelFleetOrders(ids); });
    } else if (method == "follow_fleet") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t target = params.value("target_fleet_id", 0xFFFFFFFFu);
        bool attack = params.value("attack", false);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, target, attack, queue]() {
            return GalaxyManager::Get().FollowFleet(fleet_id, target, attack, queue);
        });
    } else if (method == "set_fleet_stance") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        std::string stance = params.value("stance", "");
        fut = TaskQueue::Get().Enqueue([fleet_id, stance]() { return GalaxyManager::Get().SetFleetStance(fleet_id, stance); });
    } else if (method == "fleet_mia") {
        std::vector<uint32_t> ids = params.value("fleet_ids", std::vector<uint32_t>{});
        std::string type = params.value("type", "return_home");
        fut = TaskQueue::Get().Enqueue([ids, type]() { return GalaxyManager::Get().FleetMia(ids, type); });
    } else if (method == "claim_system") {
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        bool remove = params.value("remove", false);
        int count = params.value("count", 0);
        fut = TaskQueue::Get().Enqueue([system_id, remove, count]() {
            return GalaxyManager::Get().ClaimSystem(system_id, remove, count);
        });
    } else if (method == "orbit_planet" || method == "research_anomalies" || method == "excavate_site" ||
               method == "use_bypass" || method == "explore_bypass") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t target = params.value(method == "orbit_planet" ? "planet_id" : method == "research_anomalies" ? "system_id"
                                       : method == "excavate_site" ? "site_id" : "bypass_id", 0xFFFFFFFFu);
        uint32_t to_system = params.value("to_system_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        std::string m = method;
        fut = TaskQueue::Get().Enqueue([m, fleet_id, target, to_system, queue]() {
            auto& g = GalaxyManager::Get();
            if (m == "orbit_planet") return g.OrbitPlanet(fleet_id, target, queue);
            if (m == "research_anomalies") return g.ResearchAnomalies(fleet_id, target, queue);
            if (m == "excavate_site") return g.ExcavateSite(fleet_id, target, queue);
            if (m == "use_bypass") return g.UseBypass(fleet_id, target, to_system, queue);
            return g.ExploreBypass(fleet_id, target, queue);
        });
    } else if (method == "land_armies") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t planet_id = params.value("planet_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, planet_id, queue]() { return GalaxyManager::Get().LandArmies(fleet_id, planet_id, queue); });
    } else if (method == "collect_data") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t project_id = params.value("project_id", 0xFFFFFFFFu);
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, project_id, system_id, queue]() {
            return GalaxyManager::Get().CollectData(fleet_id, project_id, system_id, queue);
        });
    } else if (method == "find_path") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t to = params.value("to_system_id", 0xFFFFFFFFu);
        fut = TaskQueue::Get().Enqueue([fleet_id, to]() { return GalaxyManager::Get().FindPath(fleet_id, to); });
    } else if (method == "find_systems") {
        std::string purpose = params.value("purpose", "");
        uint32_t from = params.value("from_system_id", 0xFFFFFFFFu);
        int limit = params.value("limit", 10);
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t species_id = params.value("species_id", 0xFFFFFFFFu);
        std::string resource = params.value("resource", "");
        fut = TaskQueue::Get().Enqueue([purpose, from, limit, fleet_id, resource, species_id]() {
            return GalaxyManager::Get().FindSystems(purpose, from, limit, fleet_id, resource, species_id);
        });
    } else if (method == "survey") {
        uint32_t fleet_id = params.value("fleet_id", 0xFFFFFFFFu);
        uint32_t system_id = params.value("system_id", 0xFFFFFFFFu);
        uint32_t planet_id = params.value("planet_id", 0xFFFFFFFFu);
        bool queue = params.value("queue", false);
        fut = TaskQueue::Get().Enqueue([fleet_id, system_id, planet_id, queue]() {
            return GalaxyManager::Get().Survey(fleet_id, system_id, planet_id, queue);
        });
    } else if (method == "set_situation_approach") {
        uint32_t situation_id = params.value("situation_id", 0);
        std::string approach_key = params.value("approach_key", "");
        fut = TaskQueue::Get().Enqueue([situation_id, approach_key]() {
            return SituationLogManager::Get().SetSituationApproach(situation_id, approach_key);
        });
    } else if (method == "get_government") {
        fut = TaskQueue::Get().Enqueue([]() {
            return GovernmentManager::Get().GetGovernmentJson();
        });
    } else if (method == "set_council_agenda") {
        std::string agenda_key = params.value("agenda_key", "");
        fut = TaskQueue::Get().Enqueue([agenda_key]() {
            return GovernmentManager::Get().SetCouncilAgenda(agenda_key);
        });
    } else if (method == "get_civics") {
        std::string civic_key = params.value("civic_key", "");
        fut = TaskQueue::Get().Enqueue([civic_key]() {
            return GovernmentManager::Get().GetCivicsJson(civic_key);
        });
    } else if (method == "change_civics") {
        std::vector<std::string> add = params.value("add", std::vector<std::string>{});
        std::vector<std::string> remove = params.value("remove", std::vector<std::string>{});
        fut = TaskQueue::Get().Enqueue([add, remove]() {
            return GovernmentManager::Get().ChangeCivics(add, remove);
        });
    } else if (method == "launch_council_agenda") {
        fut = TaskQueue::Get().Enqueue([]() {
            return GovernmentManager::Get().LaunchCouncilAgenda();
        });
    } else if (method == "get_traditions") {
        fut = TaskQueue::Get().Enqueue([]() {
            return SocietyManager::Get().GetTraditionsJson();
        });
    } else if (method == "adopt_tradition") {
        std::string tradition_key = params.value("tradition_key", "");
        fut = TaskQueue::Get().Enqueue([tradition_key]() {
            return SocietyManager::Get().AdoptTradition(tradition_key);
        });
    } else if (method == "get_edicts") {
        fut = TaskQueue::Get().Enqueue([]() {
            return SocietyManager::Get().GetEdictsJson();
        });
    } else if (method == "toggle_edict") {
        std::string edict_key = params.value("edict_key", "");
        bool enabled = params.value("enabled", true);
        fut = TaskQueue::Get().Enqueue([edict_key, enabled]() {
            return SocietyManager::Get().ToggleEdict(edict_key, enabled);
        });
    } else if (method == "get_leaders") {
        fut = TaskQueue::Get().Enqueue([]() {
            return LeaderManager::Get().GetLeadersJson();
        });
    } else if (method == "hire_leader") {
        uint32_t candidate_id = params.value("candidate_id", 0);
        fut = TaskQueue::Get().Enqueue([candidate_id]() {
            return LeaderManager::Get().HireLeader(candidate_id);
        });
    } else if (method == "dismiss_leader") {
        uint32_t leader_id = params.value("leader_id", 0);
        fut = TaskQueue::Get().Enqueue([leader_id]() {
            return LeaderManager::Get().DismissLeader(leader_id);
        });
    } else if (method == "select_leader_trait") {
        uint32_t leader_id = params.value("leader_id", 0u);
        std::string trait_key = params.value("trait_key", "");
        fut = TaskQueue::Get().Enqueue([leader_id, trait_key]() {
            return LeaderManager::Get().SelectTrait(leader_id, trait_key);
        });
    } else if (method == "assign_leader") {
        uint32_t leader_id = params.value("leader_id", 0);
        uint8_t assignment_type = (uint8_t)params.value("assignment_type", 0);
        uint32_t target_id = params.value("target_id", 0);
        fut = TaskQueue::Get().Enqueue([leader_id, assignment_type, target_id]() {
            return LeaderManager::Get().AssignLeader(leader_id, assignment_type, target_id);
        });
    } else if (method == "get_species") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return SpeciesManager::Get().GetSpeciesJson(params);
        });
    } else if (method == "set_species_rights") {
        uint32_t species_id = params.value("species_id", 0);
        std::string category = params.value("category", "");
        if (category.empty() && params.contains("right_category")) {
            category = params.value("right_category", "");
        }
        std::string right_value = params.value("right_value", "");
        fut = TaskQueue::Get().Enqueue([species_id, category, right_value]() {
            return SpeciesManager::Get().SetSpeciesRight(species_id, category, right_value);
        });
    } else if (method == "get_species_modification_info") {
        uint32_t species_id = params.value("species_id", 0);
        fut = TaskQueue::Get().Enqueue([species_id]() {
            return SpeciesManager::Get().GetSpeciesModificationInfoJson(species_id);
        });
    } else if (method == "create_species_template") {
        uint32_t base_species_id = params.value("base_species_id", (uint32_t)params.value("species_id", 0));
        std::string name = params.value("name", "");
        std::vector<std::string> traits = params.value("traits", std::vector<std::string>{});
        fut = TaskQueue::Get().Enqueue([base_species_id, name, traits]() {
            return SpeciesManager::Get().CreateSpeciesTemplateJson(base_species_id, name, traits);
        });
    } else if (method == "delete_species_template") {
        uint32_t species_id = params.value("species_id", 0);
        fut = TaskQueue::Get().Enqueue([species_id]() {
            return SpeciesManager::Get().DeleteSpeciesTemplateJson(species_id);
        });
    } else if (method == "modify_species_template") {
        uint32_t template_species_id = params.value("template_species_id", (uint32_t)params.value("species_id", 0));
        std::string name = params.value("name", "");
        std::vector<std::string> traits = params.value("traits", std::vector<std::string>{});
        fut = TaskQueue::Get().Enqueue([template_species_id, name, traits]() {
            return SpeciesManager::Get().ModifySpeciesTemplateJson(template_species_id, name, traits);
        });
    } else if (method == "apply_species_template") {
        uint32_t template_species_id = params.value("template_species_id", (uint32_t)params.value("species_id", 0));
        std::vector<uint32_t> colony_ids = params.value("colony_ids", std::vector<uint32_t>{});
        fut = TaskQueue::Get().Enqueue([template_species_id, colony_ids]() {
            return SpeciesManager::Get().ApplySpeciesTemplateJson(template_species_id, colony_ids);
        });
    } else if (method == "get_fleets") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return FleetManager::Get().GetFleetsJson(params);
        });
    } else if (method == "reinforce_fleet") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return FleetManager::Get().ReinforceFleetJson(params);
        });
    } else if (method == "set_fleet_template_quota") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return FleetManager::Get().SetFleetTemplateQuotaJson(params);
        });
    } else if (method == "get_ship_designs") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return ShipDesigner::Get().GetShipDesignsJson(params);
        });
    } else if (method == "get_ship_design_catalog") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return ShipDesigner::Get().GetShipDesignCatalogJson(params);
        });
    } else if (method == "get_component_details") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return ShipDesigner::Get().GetComponentDetailsJson(params);
        });
    } else if (method == "create_ship_design") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return ShipDesigner::Get().CreateShipDesignJson(params);
        });
    } else if (method == "update_ship_design") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return ShipDesigner::Get().UpdateShipDesignJson(params);
        });
    } else if (method == "upgrade_fleet") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return ShipDesigner::Get().UpgradeFleetJson(params);
        });
    } else if (method == "delete_ship_design") {
        fut = TaskQueue::Get().Enqueue([params]() {
            return ShipDesigner::Get().DeleteShipDesignJson(params);
        });
    } else if (method == "get_market") {
        fut = TaskQueue::Get().Enqueue([]() {
            return MarketManager::Get().GetMarketInfo();
        });
    } else if (method == "market_trade") {
        std::string resource = params.value("resource", "");
        std::string action = params.value("action", "");
        if (action.empty()) {
            if (params.contains("is_buy")) {
                action = params["is_buy"].get<bool>() ? "buy" : "sell";
            } else {
                action = "buy";
            }
        }
        uint32_t units = params.value("units", (uint32_t)params.value("amount", 0));
        fut = TaskQueue::Get().Enqueue([resource, action, units]() {
            std::string msg;
            bool ok = MarketManager::Get().ExecuteInstantTrade(resource, action, units, msg);
            return nlohmann::json{
                {"success", ok},
                {"message", msg}
            };
        });
    } else if (method == "set_monthly_trade") {
        std::string resource = params.value("resource", "");
        std::string action = params.value("action", "buy");
        double amount = params.value("amount", 0.0);
        double price_limit = params.value("price_limit", 0.0);
        bool cancel = params.value("cancel", false);
        int32_t order_id = params.value("order_id", -1);
        fut = TaskQueue::Get().Enqueue([resource, action, amount, price_limit, cancel, order_id]() {
            std::string msg;
            bool ok = MarketManager::Get().SetMonthlyTrade(resource, action, amount, price_limit, cancel, order_id, msg);
            return nlohmann::json{
                {"success", ok},
                {"message", msg}
            };
        });
    } else if (method == "get_discoveries") {
        std::string tab = params.value("tab", "all");
        fut = TaskQueue::Get().Enqueue([tab]() {
            return DiscoveriesManager::Get().GetDiscoveriesInfo(tab);
        });
    } else if (method == "activate_relic") {
        std::string relic_key = params.value("relic_key", params.value("relic", ""));
        fut = TaskQueue::Get().Enqueue([relic_key]() {
            std::string msg;
            bool ok = DiscoveriesManager::Get().ActivateRelic(relic_key, msg);
            return nlohmann::json{
                {"success", ok},
                {"message", msg}
            };
        });
    } else if (method == "get_contacts") {
        std::string mode = params.value("mode", "all");
        fut = TaskQueue::Get().Enqueue([mode]() {
            return ContactsManager::Get().GetContactsInfo(mode);
        });
    } else if (method == "get_outliner") {
        fut = TaskQueue::Get().Enqueue([]() {
            return OutlinerManager::Get().GetOutlinerSummaryJson();
        });
    } else if (method == "get_sectors") {
        int32_t sector_id = params.value("sector_id", -1);
        fut = TaskQueue::Get().Enqueue([sector_id]() {
            return OutlinerManager::Get().GetSectorsJson(sector_id);
        });
    } else if (method == "get_military_fleets") {
        fut = TaskQueue::Get().Enqueue([]() {
            return OutlinerManager::Get().GetMilitaryFleetsJson();
        });
    } else if (method == "get_civilian_fleets") {
        fut = TaskQueue::Get().Enqueue([]() {
            return OutlinerManager::Get().GetCivilianFleetsJson();
        });
    } else if (method == "get_armies") {
        fut = TaskQueue::Get().Enqueue([]() {
            return OutlinerManager::Get().GetArmiesJson();
        });
    } else if (method == "get_planet_details") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().GetPlanetDetailsJson(planet_id);
        });
    } else if (method == "get_available_district_zones") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string district_type = params.value("district_type", "");
        bool include_blocked = params.value("include_blocked", false);
        fut = TaskQueue::Get().Enqueue([planet_id, district_type, include_blocked]() {
            return OutlinerManager::Get().GetAvailableDistrictZonesJson(planet_id, district_type, include_blocked);
        });
    } else if (method == "set_district_zone") {
        uint32_t planet_id = params.value("planet_id", 0xFFFFFFFFu);
        uint32_t district_id = params.value("district_id", 0xFFFFFFFFu);
        int32_t slot = params.value("slot", -1);
        std::string zone_key = params.value("zone_key", "");
        fut = TaskQueue::Get().Enqueue([planet_id, district_id, slot, zone_key]() {
            return OutlinerManager::Get().SetDistrictZoneJson(planet_id, district_id, slot, zone_key);
        });
    } else if (method == "get_buildable_buildings") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string building_key = params.value("building_key", "");
        int32_t zone_id = params.value("zone_id", params.value("slot_index", -1));
        fut = TaskQueue::Get().Enqueue([planet_id, building_key, zone_id]() {
            return OutlinerManager::Get().GetBuildableBuildingsJson(planet_id, building_key, zone_id);
        });
    } else if (method == "get_buildable_districts") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string district_key = params.value("district_key", "");
        fut = TaskQueue::Get().Enqueue([planet_id, district_key]() {
            return OutlinerManager::Get().GetBuildableDistrictsJson(planet_id, district_key);
        });
    } else if (method == "build_district") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string district_key = params.value("district_key", "");
        fut = TaskQueue::Get().Enqueue([planet_id, district_key]() {
            return OutlinerManager::Get().BuildDistrictJson(planet_id, district_key);
        });
    } else if (method == "demolish_district") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string district_key = params.value("district_key", "");
        fut = TaskQueue::Get().Enqueue([planet_id, district_key]() {
            return OutlinerManager::Get().DemolishDistrictJson(planet_id, district_key);
        });
    } else if (method == "cancel_construction") {
        uint32_t item_id = params.value("item_id", 0xFFFFFFFFu);
        fut = TaskQueue::Get().Enqueue([item_id]() { return OutlinerManager::Get().CancelConstructionJson(item_id); });
    } else if (method == "get_buildable_ships") {
        fut = TaskQueue::Get().Enqueue([params]() { return ShipDesigner::Get().GetBuildableShipsJson(params); });
    } else if (method == "build_ship") {
        fut = TaskQueue::Get().Enqueue([params]() { return ShipDesigner::Get().BuildShipJson(params); });
    } else if (method == "build_building") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string building_key = params.value("building_key", "");
        std::string district_type = params.value("district_type", "");
        int32_t slot_index = params.value("slot_index", -1);
        if (district_type == "upgrade" || params.contains("building_id")) {
            uint32_t building_id = params.value("building_id", (uint32_t)slot_index);
            fut = TaskQueue::Get().Enqueue([planet_id, building_id, building_key]() {
                return OutlinerManager::Get().UpgradeBuildingJson(planet_id, building_id, building_key);
            });
        } else {
            fut = TaskQueue::Get().Enqueue([planet_id, building_key, district_type, slot_index]() {
                return OutlinerManager::Get().BuildBuildingJson(planet_id, building_key, district_type, slot_index);
            });
        }
    } else if (method == "upgrade_building") {
        uint32_t planet_id = params.value("planet_id", 0);
        uint32_t building_id = params.value("building_id", 0);
        std::string upgrade_to_key = params.value("upgrade_to_key", "");
        fut = TaskQueue::Get().Enqueue([planet_id, building_id, upgrade_to_key]() {
            return OutlinerManager::Get().UpgradeBuildingJson(planet_id, building_id, upgrade_to_key);
        });
    } else if (method == "get_clearable_blockers") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().GetClearableBlockersJson(planet_id);
        });
    } else if (method == "clear_blocker") {
        uint32_t planet_id = params.value("planet_id", 0);
        uint32_t deposit_id = params.value("deposit_id", 0);
        std::string deposit_key = params.value("deposit_key", "");
        fut = TaskQueue::Get().Enqueue([planet_id, deposit_id, deposit_key]() {
            return OutlinerManager::Get().ClearBlockerJson(planet_id, deposit_id, deposit_key);
        });
    } else if (method == "get_planetary_decisions") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().GetPlanetaryDecisionsJson(planet_id);
        });
    } else if (method == "enact_decision" || method == "enact_planetary_decision") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string decision_key = params.value("decision_key", params.value("decision", ""));
        fut = TaskQueue::Get().Enqueue([planet_id, decision_key]() {
            return OutlinerManager::Get().EnactDecisionJson(planet_id, decision_key);
        });
    } else if (method == "get_terraforming_options") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().GetTerraformingOptionsJson(planet_id);
        });
    } else if (method == "start_terraforming") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string target_class = params.value("target_class", params.value("target_planet_class", ""));
        int32_t link_index = params.value("link_index", -1);
        fut = TaskQueue::Get().Enqueue([planet_id, target_class, link_index]() {
            return OutlinerManager::Get().StartTerraformingJson(planet_id, target_class, link_index);
        });
    } else if (method == "cancel_terraforming") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().CancelTerraformingJson(planet_id);
        });
    } else if (method == "get_planetary_features") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().GetPlanetaryFeaturesJson(planet_id);
        });
    } else if (method == "ascend_colony") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().AscendColonyJson(planet_id);
        });
    } else if (method == "get_planet_jobs") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().GetPlanetJobsJson(planet_id);
        });
    } else if (method == "set_job_priority" || method == "prioritize_job") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string job_key = params.value("job_key", "");
        fut = TaskQueue::Get().Enqueue([planet_id, job_key]() {
            return OutlinerManager::Get().SetJobPriorityJson(planet_id, job_key);
        });
    } else if (method == "set_job_workforce_limit") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string job_key = params.value("job_key", "");
        int32_t limit = params.value("limit", -1);
        fut = TaskQueue::Get().Enqueue([planet_id, job_key, limit]() {
            return OutlinerManager::Get().SetJobWorkforceLimitJson(planet_id, job_key, limit);
        });
    } else if (method == "get_planet_armies") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().GetPlanetArmiesJson(planet_id);
        });
    } else if (method == "set_planet_army_settings") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::optional<bool> deploy_in_orbit;
        if (params.contains("deploy_in_orbit") && !params["deploy_in_orbit"].is_null()) {
            deploy_in_orbit = params["deploy_in_orbit"].get<bool>();
        }
        std::optional<bool> include_in_builder;
        if (params.contains("include_in_builder") && !params["include_in_builder"].is_null()) {
            include_in_builder = params["include_in_builder"].get<bool>();
        }
        fut = TaskQueue::Get().Enqueue([planet_id, deploy_in_orbit, include_in_builder]() {
            return OutlinerManager::Get().SetPlanetArmySettingsJson(planet_id, deploy_in_orbit, include_in_builder);
        });
    } else if (method == "embark_all_armies") {
        uint32_t planet_id = params.value("planet_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id]() {
            return OutlinerManager::Get().EmbarkAllArmiesJson(planet_id);
        });
    } else if (method == "disband_planet_army") {
        uint32_t planet_id = params.value("planet_id", 0);
        uint32_t army_id = params.value("army_id", 0);
        fut = TaskQueue::Get().Enqueue([planet_id, army_id]() {
            return OutlinerManager::Get().DisbandArmyJson(planet_id, army_id);
        });
    } else if (method == "recruit_planet_army") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string army_key = params.value("army_key", "");
        std::optional<uint32_t> species_id;
        if (params.contains("species_id") && !params["species_id"].is_null()) {
            species_id = params["species_id"].get<uint32_t>();
        }
        fut = TaskQueue::Get().Enqueue([planet_id, army_key, species_id]() {
            return OutlinerManager::Get().RecruitArmyJson(planet_id, army_key, species_id);
        });
    } else {
        return {
            {"jsonrpc", "2.0"},
            {"error", {
                {"code", -32601},
                {"message", "Method not found: " + method}
            }},
            {"id", id}
        };
    }

    // Wait for the game main thread to complete the task
    if (fut.wait_for(std::chrono::milliseconds(2500)) == std::future_status::timeout) {
        LOGF("[IPC_TIMEOUT] Task timed out for method: %s", method.c_str());
        return {
            {"jsonrpc", "2.0"},
            {"error", {
                {"code", -32000},
                {"message", "Main thread task timed out (game may be loading, frozen, or background throttled)"}
            }},
            {"id", id}
        };
    }

    nlohmann::json result = fut.get();
    if (result.is_object() && result.contains("error") && result["error"].is_object()) {
        return {
            {"jsonrpc", "2.0"},
            {"error", result["error"]},
            {"id", id}
        };
    }

    return {
        {"jsonrpc", "2.0"},
        {"result", result},
        {"id", id}
    };
}

void IPCServer::WorkerLoop() {
    const wchar_t* pipe_name = kPipeName;

    while (is_running_) {
        pipe_handle_ = CreateNamedPipeW(
            pipe_name,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536,
            65536,
            0,
            nullptr
        );

        if (pipe_handle_ == INVALID_HANDLE_VALUE) {
            LOGF("[IPC_ERROR] Failed to create named pipe (0x%08X). Retrying in 1s...", GetLastError());
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        LOG("[IPC] Named pipe created. Awaiting client connection...");
        BOOL connected = ConnectNamedPipe(pipe_handle_, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!is_running_) {
            CloseHandle(pipe_handle_);
            break;
        }

        if (connected) {
            LOG("[IPC] Client connected to Named Pipe.");
            std::string read_buffer;
            char chunk[4096];

            while (is_running_) {
                DWORD bytes_read = 0;
                BOOL ok = ReadFile(pipe_handle_, chunk, sizeof(chunk) - 1, &bytes_read, nullptr);
                if (!ok || bytes_read == 0) {
                    DWORD err = GetLastError();
                    if (err == ERROR_BROKEN_PIPE) {
                        LOG("[IPC] Client disconnected normally.");
                    } else {
                        LOGF("[IPC] Pipe read error: 0x%08X", err);
                    }
                    break;
                }

                chunk[bytes_read] = '\0';
                read_buffer.append(chunk, bytes_read);

                // Process all full lines
                size_t pos;
                while ((pos = read_buffer.find('\n')) != std::string::npos) {
                    std::string line = read_buffer.substr(0, pos);
                    read_buffer.erase(0, pos + 1);

                    // Trim CR if present
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }

                    if (line.empty()) continue;

                    nlohmann::json resp_json;
                    try {
                        auto req_json = nlohmann::json::parse(line);
                        resp_json = ProcessRequest(req_json);
                    } catch (const std::exception& ex) {
                        resp_json = {
                            {"jsonrpc", "2.0"},
                            {"error", {
                                {"code", -32700},
                                {"message", std::string("Parse error: ") + ex.what()}
                            }},
                            {"id", nullptr}
                        };
                    }

                    std::string out_str = resp_json.dump() + "\n";
                    DWORD written = 0;
                    WriteFile(pipe_handle_, out_str.data(), (DWORD)out_str.size(), &written, nullptr);
                    FlushFileBuffers(pipe_handle_);
                }
            }
        }

        DisconnectNamedPipe(pipe_handle_);
        CloseHandle(pipe_handle_);
        pipe_handle_ = INVALID_HANDLE_VALUE;
    }
}

} // namespace bridge
