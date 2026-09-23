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
