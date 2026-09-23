#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <future>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <nlohmann/json.hpp>

namespace bridge {

class Logger {
public:
    static Logger& Get() {
        static Logger instance;
        return instance;
    }

    void Init(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_.open(filepath, std::ios::out | std::ios::app);
        LogInternal("[INIT] Stellaris MCP Bridge Logger started.");
    }

    void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        LogInternal(msg);
    }

    template<typename... Args>
    void LogFmt(const char* fmt, Args... args) {
        char buf[1024];
        snprintf(buf, sizeof(buf), fmt, args...);
        Log(std::string(buf));
    }

private:
    Logger() = default;
    ~Logger() {
        if (file_.is_open()) file_.close();
    }

    void LogInternal(const std::string& msg) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char time_str[64];
        ctime_s(time_str, sizeof(time_str), &now);
        // strip trailing newline
        for (int i = 0; time_str[i]; ++i) {
            if (time_str[i] == '\n' || time_str[i] == '\r') time_str[i] = '\0';
        }

        std::string line = "[" + std::string(time_str) + "] " + msg + "\n";
        OutputDebugStringA(line.c_str());
        if (file_.is_open()) {
            file_ << line;
            file_.flush();
        }
    }

    std::mutex mutex_;
    std::ofstream file_;
};

#define LOG(msg) ::bridge::Logger::Get().Log(msg)
#define LOGF(fmt, ...) ::bridge::Logger::Get().LogFmt(fmt, __VA_ARGS__)

} // namespace bridge
