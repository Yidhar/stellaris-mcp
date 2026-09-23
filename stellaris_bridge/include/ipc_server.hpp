#pragma once

#include "common.hpp"
#include <thread>
#include <atomic>

namespace bridge {

class IPCServer {
public:
    static IPCServer& Get();

    bool Start();
    void Stop();

private:
    IPCServer() = default;
    ~IPCServer() { Stop(); }

    void WorkerLoop();
    nlohmann::json ProcessRequest(const nlohmann::json& req);

    std::thread worker_thread_;
    std::atomic<bool> is_running_{ false };
    HANDLE pipe_handle_{ INVALID_HANDLE_VALUE };
};

} // namespace bridge
