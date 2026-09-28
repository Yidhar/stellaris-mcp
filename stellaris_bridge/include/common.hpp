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

constexpr uintptr_t kRvaPostCommand = 0x5F8640;
constexpr uintptr_t kRvaEngineAlloc = 0x20213E8;
constexpr uintptr_t kRvaFreePdxString = 0x15BBE0;  // releases a PdxString's heap buffer

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

struct PdxStringView {
    const char* data;
    uint64_t size;
};

struct PdxLocResult {
    uint32_t flags{ 0 };
    uint32_t pad0{ 0 };
    uint64_t pad1{ 0 };
    union {
        char buf[16]{ 0 };
        char* heap_ptr;
    };
    uint64_t size{ 0 };
    uint64_t capacity{ 15 };
};

static inline bool RawLocalizeCall(void* fn_loc, void* out_str, const void* in_sv) {
    __try {
        using FnLocalize = void* (*)(void* out_str, const void* in_key);
        ((FnLocalize)fn_loc)(out_str, in_sv);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static inline void RawFreeCall(void* fn_free, void* out_str) {
    __try {
        using FnFreePdxStr = void (*)(void* str);
        ((FnFreePdxStr)fn_free)(out_str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Renders the engine's rich-text markup (as returned by localization and text-building functions)
// as plain text. 0x13 starts an icon name; a second 0x13 or, for framed icons ("energy|1 -500"),
// the next space or control byte ends it. Icons become "[energy]" without the "|frame" suffix.
// Other control bytes start a colour code whose one-letter key ('Y', 'R', ... or '!' to close)
// follows and is dropped with it.
inline std::string RenderPdxMarkup(const char* p, size_t n) {
    std::string out;
    out.reserve(n);
    bool in_icon = false, icon_frame = false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (in_icon) {
            if (c == 0x13 || c == ' ' || c < 0x20) {
                in_icon = false;
                out.push_back(']');
                if (c == 0x13) {
                    unsigned char next = i + 1 < n ? (unsigned char)p[i + 1] : ' ';
                    if (next != ' ' && next != 0x0A) out.push_back(' ');
                    continue;
                }
                // the terminating byte is ordinary text or markup: fall through
            } else {
                if (c == '|') icon_frame = true;
                if (!icon_frame) out.push_back((char)c);
                continue;
            }
        }
        if (c == 0x13) {
            in_icon = true;
            icon_frame = false;
            out.push_back('[');
        } else if (c >= 0x20 || c == 0x0A) {
            out.push_back((char)c);
        } else if (i + 1 < n) {
            unsigned char k = (unsigned char)p[i + 1];
            if (k == '!' || (k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z')) ++i;
        }
    }
    if (in_icon) out.push_back(']');
    return out;
}

inline std::string RenderPdxMarkup(const std::string& s) {
    return RenderPdxMarkup(s.data(), s.size());
}

inline std::string SafeLocalize(uintptr_t base_address, const std::string& key) {
    if (key.empty() || !base_address) return key;

    void* fn_localize = (void*)(base_address + 0x16D2D0);
    void* fn_free = (void*)(base_address + kRvaFreePdxString);

    PdxStringView in_sv{ key.data(), key.size() };
    PdxLocResult out_str{};

    if (!RawLocalizeCall(fn_localize, &out_str, &in_sv)) {
        return key;
    }

    std::string result;
    if (out_str.size > 0 && out_str.size < 65536) {
        if (out_str.capacity < 16) {
            result.assign(out_str.buf, (size_t)out_str.size);
        } else if (out_str.heap_ptr && (uintptr_t)out_str.heap_ptr > 0x10000) {
            result.assign(out_str.heap_ptr, (size_t)out_str.size);
        }
    }

    if (out_str.capacity >= 16 && out_str.heap_ptr && (uintptr_t)out_str.heap_ptr > 0x10000) {
        RawFreeCall(fn_free, &out_str);
    }

    return result.empty() ? key : RenderPdxMarkup(result);
}

} // namespace bridge
