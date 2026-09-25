#include "ipc_server.hpp"
#include "task_queue.hpp"
#include "game_state.hpp"
#include "commands.hpp"
#include "event_manager.hpp"
#include "notification_manager.hpp"
#include "alert_manager.hpp"
#include "tech_manager.hpp"
#include "situation_log_manager.hpp"
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

IPCServer& IPCServer::Get() {
    static IPCServer instance;
    return instance;
}

bool IPCServer::Start() {
    if (is_running_) return true;

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
        L"\\\\.\\pipe\\stellaris_mcp_bridge",
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

    if (method == "get_status") {
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
        std::string action = params.value("action", "buy");
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
        fut = TaskQueue::Get().Enqueue([resource, action, amount, price_limit, cancel]() {
            std::string msg;
            bool ok = MarketManager::Get().SetMonthlyTrade(resource, action, amount, price_limit, cancel, msg);
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
        fut = TaskQueue::Get().Enqueue([planet_id, district_type]() {
            return OutlinerManager::Get().GetAvailableDistrictZonesJson(planet_id, district_type);
        });
    } else if (method == "get_buildable_buildings") {
        uint32_t planet_id = params.value("planet_id", 0);
        std::string district_type = params.value("district_type", "");
        int32_t slot_index = params.value("slot_index", -1);
        fut = TaskQueue::Get().Enqueue([planet_id, district_type, slot_index]() {
            return OutlinerManager::Get().GetBuildableBuildingsJson(planet_id, district_type, slot_index);
        });
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
    const wchar_t* pipe_name = L"\\\\.\\pipe\\stellaris_mcp_bridge";

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
