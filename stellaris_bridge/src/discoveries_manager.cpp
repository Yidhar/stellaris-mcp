#include "discoveries_manager.hpp"
#include <windows.h>
#include <cstring>
#include <algorithm>

namespace bridge {

static bool SafeReadPtr(const void* addr, void** out) {
    __try {
        *out = *(void**)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32(const void* addr, uint32_t* out) {
    __try {
        *out = *(const uint32_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadI64(const void* addr, int64_t* out) {
    __try {
        *out = *(const int64_t*)addr;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeCopyChars(char* dest, const char* src, size_t count) {
    __try {
        memcpy(dest, src, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct RawPdxString {
    union {
        char buf[16];
        char* heap_ptr;
    };
    uint64_t size;
    uint64_t capacity;
};

static bool SafeReadPdxString(const void* pdx_str_addr, std::string& out) {
    out.clear();
    if (!pdx_str_addr) return false;

    RawPdxString raw{};
    if (!SafeCopyChars((char*)&raw, (const char*)pdx_str_addr, sizeof(RawPdxString))) {
        return false;
    }

    if (raw.size == 0 || raw.size > 1024) return false;

    if (raw.capacity < 16) {
        size_t len = raw.size < 16 ? (size_t)raw.size : 15;
        char temp[16]{ 0 };
        if (SafeCopyChars(temp, raw.buf, len)) {
            out = std::string(temp, len);
            return true;
        }
    } else if (raw.heap_ptr) {
        uintptr_t addr = (uintptr_t)raw.heap_ptr;
        if (addr > 0x10000 && addr < 0x7FFFFFFFFFFF) {
            size_t len = raw.size < 512 ? (size_t)raw.size : 512;
            std::string temp(len, '\0');
            if (SafeCopyChars(&temp[0], raw.heap_ptr, len)) {
                out = temp;
                return true;
            }
        }
    }
    return false;
}

static bool SafeLocalizeCall(DiscoveriesManager::FnLocalize fn_localize,
                             DiscoveriesManager::FnFreePdxStr fn_free_pdx,
                             const RawPdxString* in_key,
                             RawPdxString* out_str) {
    __try {
        fn_localize(out_str, in_key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeFreePdxStr(DiscoveriesManager::FnFreePdxStr fn_free_pdx, RawPdxString* str) {
    __try {
        if (str->capacity >= 16 && str->heap_ptr) {
            fn_free_pdx(str);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

DiscoveriesManager& DiscoveriesManager::Get() {
    static DiscoveriesManager instance;
    return instance;
}

bool DiscoveriesManager::Init(uintptr_t base_address) {
    base_address_ = base_address;

    fn_localize_ = (FnLocalize)(base_address_ + 0x16D2D0);
    fn_free_pdx_str_ = (FnFreePdxStr)(base_address_ + 0x15BBE0);
    fn_engine_alloc_ = (FnEngineAlloc)(base_address_ + 0x20208C8);
    fn_post_command_ = (FnPostCommand)(base_address_ + 0x5F8590);

    return true;
}

void* DiscoveriesManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (SafeReadPtr((const void*)(base_address_ + 0x3112F50), &mgr) && mgr) {
        void* countries_arr = nullptr;
        uint32_t count = 0;
        if (SafeReadPtr((const void*)((uintptr_t)mgr + 0x18), &countries_arr) && countries_arr &&
            SafeReadU32((const void*)((uintptr_t)mgr + 0x20), &count) && count > 0) {
            void* country_0 = nullptr;
            if (SafeReadPtr((const void*)((uintptr_t)countries_arr + 8), &country_0) && country_0) {
                return country_0;
            }
        }
    }
    return nullptr;
}

std::string DiscoveriesManager::LocalizeKey(const std::string& key) {
    if (key.empty() || !fn_localize_) return key;

    RawPdxString in_key{};
    in_key.size = key.size();
    in_key.capacity = 15;
    if (key.size() < 16) {
        memcpy(in_key.buf, key.data(), key.size());
    } else {
        return key;
    }

    RawPdxString out_str{};
    if (!SafeLocalizeCall(fn_localize_, fn_free_pdx_str_, &in_key, &out_str)) {
        return key;
    }

    std::string result;
    if (out_str.size > 0 && out_str.size < 4096) {
        if (out_str.capacity < 16) {
            char temp[16]{ 0 };
            size_t len = out_str.size < 16 ? (size_t)out_str.size : 15;
            memcpy(temp, out_str.buf, len);
            result = std::string(temp, len);
        } else if (out_str.heap_ptr) {
            size_t len = out_str.size < 512 ? (size_t)out_str.size : 512;
            result = std::string(out_str.heap_ptr, len);
        }
    }

    if (fn_free_pdx_str_) {
        SafeFreePdxStr(fn_free_pdx_str_, &out_str);
    }

    return result.empty() ? key : result;
}

nlohmann::json DiscoveriesManager::GetDiscoveriesInfo(const std::string& tab) {
    if (!base_address_) return { {"error", "Bridge base address not set"} };

    void* country = GetPlayerCountry();
    if (!country) return { {"error", "Player country not found"} };

    std::string q_tab = tab;
    std::transform(q_tab.begin(), q_tab.end(), q_tab.begin(), ::tolower);
    if (q_tab.empty()) q_tab = "all";

    nlohmann::json resp = {
        {"tab", q_tab}
    };

    // 1. Relics
    if (q_tab == "all" || q_tab == "relics") {
        void* r_db = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x31109D8), &r_db);

        uint32_t total_relics_cnt = 0;
        void* r_arr = nullptr;
        if (r_db) {
            SafeReadU32((const void*)((uintptr_t)r_db + 0x5C), &total_relics_cnt);
            SafeReadPtr((const void*)((uintptr_t)r_db + 0x50), &r_arr);
        }

        // Player's held relics
        void* player_relics_arr = nullptr;
        uint32_t player_relics_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)country + 0x30C0), &player_relics_arr);
        SafeReadU32((const void*)((uintptr_t)country + 0x30CC), &player_relics_cnt);

        nlohmann::json held_list = nlohmann::json::array();
        for (uint32_t i = 0; i < player_relics_cnt; ++i) {
            void* p_relic = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)player_relics_arr + i * 8), &p_relic) || !p_relic) continue;

            std::string r_key;
            SafeReadPdxString((const void*)((uintptr_t)p_relic + 0x20), r_key);
            if (r_key.empty()) continue;

            std::string loc_name = LocalizeKey(r_key);
            std::string desc_key = r_key + "_desc";
            std::string passive_key = r_key + "_passive";
            std::string triumph_key = r_key + "_triumph";

            held_list.push_back({
                {"key", r_key},
                {"name", loc_name},
                {"description", LocalizeKey(desc_key)},
                {"passive_effects", LocalizeKey(passive_key)},
                {"triumph_effects", LocalizeKey(triumph_key)},
                {"can_activate", true},
                {"cooldown_days_remaining", 0}
            });
        }

        resp["relics"] = {
            {"held_count", held_list.size()},
            {"total_galaxy_relics", total_relics_cnt},
            {"can_activate_any", !held_list.empty()},
            {"held_relics", held_list}
        };
    }

    // 2. Astral Actions
    if (q_tab == "all" || q_tab == "astral_actions") {
        void* a_db = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x31108F0), &a_db);

        uint32_t a_cnt = 0;
        void* a_arr = nullptr;
        if (a_db) {
            SafeReadU32((const void*)((uintptr_t)a_db + 0x5C), &a_cnt);
            SafeReadPtr((const void*)((uintptr_t)a_db + 0x50), &a_arr);
        }

        nlohmann::json actions_list = nlohmann::json::array();
        for (uint32_t i = 0; i < a_cnt && a_arr; ++i) {
            void* p_act = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)a_arr + i * 8), &p_act) || !p_act) continue;

            std::string a_key;
            SafeReadPdxString((const void*)((uintptr_t)p_act + 0x20), a_key);
            if (a_key.empty()) continue;

            actions_list.push_back({
                {"key", a_key},
                {"name", LocalizeKey(a_key)},
                {"description", LocalizeKey(a_key + "_desc")}
            });
        }

        resp["astral_actions"] = {
            {"total_actions", actions_list.size()},
            {"actions", actions_list}
        };
    }

    // 3. Artifact Actions (Minor Relics)
    if (q_tab == "all" || q_tab == "artifact_actions") {
        void* art_db = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x3110A48), &art_db);

        uint32_t art_cnt = 0;
        void* art_arr = nullptr;
        if (art_db) {
            SafeReadU32((const void*)((uintptr_t)art_db + 0x5C), &art_cnt);
            SafeReadPtr((const void*)((uintptr_t)art_db + 0x50), &art_arr);
        }

        nlohmann::json art_list = nlohmann::json::array();
        for (uint32_t i = 0; i < art_cnt && art_arr; ++i) {
            void* p_art = nullptr;
            if (!SafeReadPtr((const void*)((uintptr_t)art_arr + i * 8), &p_art) || !p_art) continue;

            std::string art_key;
            SafeReadPdxString((const void*)((uintptr_t)p_art + 0x20), art_key);
            if (art_key.empty()) continue;

            art_list.push_back({
                {"key", art_key},
                {"name", LocalizeKey(art_key)},
                {"description", LocalizeKey(art_key + "_desc")}
            });
        }

        resp["artifact_actions"] = {
            {"total_actions", art_list.size()},
            {"actions", art_list}
        };
    }

    return resp;
}

nlohmann::json DiscoveriesManager::GetSummaryJson() {
    void* country = GetPlayerCountry();
    uint32_t relics_cnt = 0;
    if (country) {
        SafeReadU32((const void*)((uintptr_t)country + 0x30CC), &relics_cnt);
    }
    return {
        {"held_relics_count", relics_cnt},
        {"can_activate_relic", relics_cnt > 0}
    };
}

bool DiscoveriesManager::ActivateRelic(const std::string& relic_key, std::string& out_message) {
    if (!fn_engine_alloc_ || !fn_post_command_) {
        out_message = "Engine functions not initialized";
        return false;
    }

    void* country = GetPlayerCountry();
    if (!country) {
        out_message = "Player country not found";
        return false;
    }

    // Verify player owns the relic
    void* player_relics_arr = nullptr;
    uint32_t player_relics_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + 0x30C0), &player_relics_arr);
    SafeReadU32((const void*)((uintptr_t)country + 0x30CC), &player_relics_cnt);

    bool has_relic = false;
    for (uint32_t i = 0; i < player_relics_cnt; ++i) {
        void* p_relic = nullptr;
        if (!SafeReadPtr((const void*)((uintptr_t)player_relics_arr + i * 8), &p_relic) || !p_relic) continue;

        std::string r_key;
        SafeReadPdxString((const void*)((uintptr_t)p_relic + 0x20), r_key);
        if (r_key == relic_key) {
            has_relic = true;
            break;
        }
    }

    if (!has_relic) {
        out_message = "Your empire does not possess the relic '" + relic_key + "'";
        return false;
    }

    // Allocate 0x58 bytes for CActivateRelicCommand
    void* pCmd = fn_engine_alloc_(0x58);
    if (!pCmd) {
        out_message = "Engine memory allocation failed";
        return false;
    }

    memset(pCmd, 0, 0x58);

    uintptr_t vt = base_address_ + 0x2392298;

    *(uintptr_t*)pCmd = vt;
    *(uint32_t*)((uintptr_t)pCmd + 0x08) = 0; // player country
    *(uint32_t*)((uintptr_t)pCmd + 0x10) = 0xFFFF0000;

    // Relic key at +0x20 (PdxString)
    RawPdxString* str = (RawPdxString*)((uintptr_t)pCmd + 0x20);
    str->size = relic_key.size();
    str->capacity = 15;
    if (relic_key.size() < 16) {
        memcpy(str->buf, relic_key.data(), relic_key.size());
    } else {
        void* heap_buf = fn_engine_alloc_(relic_key.size() + 1);
        if (heap_buf) {
            memcpy(heap_buf, relic_key.data(), relic_key.size());
            ((char*)heap_buf)[relic_key.size()] = '\0';
            str->heap_ptr = (char*)heap_buf;
            str->capacity = relic_key.size() + 1;
        }
    }

    *(uint32_t*)((uintptr_t)pCmd + 0x50) = 0; // country ref

    fn_post_command_(pCmd, 0);

    out_message = "Successfully activated triumph effect of relic '" + LocalizeKey(relic_key) + "'";
    return true;
}

} // namespace bridge
