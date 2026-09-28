#include "discoveries_manager.hpp"
#include "command_builder.hpp"
#include "game_state.hpp"
#include <windows.h>
#include <cstring>
#include <algorithm>

namespace bridge {

// Relics owned by a country: CPdxArray<CRelic*> scanned by CCountry::CanActivateRelic (0x755060).
// Runtime state, not a serialized member, so the SDK dumper cannot see it.
constexpr std::ptrdiff_t kCountryOwnedRelics = 0x30F0;       // data (8-byte relic pointers)
constexpr std::ptrdiff_t kCountryOwnedRelicsCount = 0x30FC;  // element count

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

    return true;
}

void* DiscoveriesManager::GetPlayerCountry() {
    if (!base_address_) return nullptr;

    void* mgr = nullptr;
    if (!SafeReadPtr((const void*)(base_address_ + 0x3113F50), &mgr) || !mgr || (uintptr_t)mgr < 0x10000) {
        return nullptr;
    }
    if (mgr && (uintptr_t)mgr >= 0x10000) {
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
    return SafeLocalize(base_address_, key);
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
        SafeReadPtr((const void*)(base_address_ + 0x31119D8), &r_db);

        uint32_t total_relics_cnt = 0;
        void* r_arr = nullptr;
        if (r_db) {
            SafeReadU32((const void*)((uintptr_t)r_db + 0x5C), &total_relics_cnt);
            SafeReadPtr((const void*)((uintptr_t)r_db + 0x50), &r_arr);
        }

        // Player's held relics
        void* player_relics_arr = nullptr;
        uint32_t player_relics_cnt = 0;
        SafeReadPtr((const void*)((uintptr_t)country + kCountryOwnedRelics), &player_relics_arr);
        SafeReadU32((const void*)((uintptr_t)country + kCountryOwnedRelicsCount), &player_relics_cnt);

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

            // Ask the engine: build the activation command and run its IsValid without posting.
            namespace relic_cmd = sdk::cmd::activate_relic_command;
            auto probe = CommandBuilder::Get().Create(relic_cmd::kSpec);
            probe.SetString(relic_cmd::relic, r_key)
                 .Set<uint32_t>(relic_cmd::country, GameState::Get().GetPlayerCountryId());
            std::string why;
            bool can_activate = probe.IsValid(&why);

            nlohmann::json item = {
                {"key", r_key},
                {"name", loc_name},
                {"description", LocalizeKey(desc_key)},
                {"passive_effects", LocalizeKey(passive_key)},
                {"triumph_effects", LocalizeKey(triumph_key)},
                {"can_activate", can_activate}
            };
            if (!can_activate && !why.empty()) item["blocked_reason"] = why;
            held_list.push_back(item);
        }

        resp["relics"] = {
            {"held_count", held_list.size()},
            {"total_galaxy_relics", total_relics_cnt},
            {"can_activate_any", std::any_of(held_list.begin(), held_list.end(),
                                             [](const nlohmann::json& r) { return r.value("can_activate", false); })},
            {"held_relics", held_list}
        };
    }

    // 2. Astral Actions
    if (q_tab == "all" || q_tab == "astral_actions") {
        void* a_db = nullptr;
        SafeReadPtr((const void*)(base_address_ + 0x31118F0), &a_db);

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
        SafeReadPtr((const void*)(base_address_ + 0x3111A48), &art_db);

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
        SafeReadU32((const void*)((uintptr_t)country + kCountryOwnedRelicsCount), &relics_cnt);
    }
    return {
        {"held_relics_count", relics_cnt},
        {"can_activate_relic", relics_cnt > 0}
    };
}

bool DiscoveriesManager::ActivateRelic(const std::string& relic_key, std::string& out_message) {
    void* country = GetPlayerCountry();
    if (!country) {
        out_message = "Player country not found";
        return false;
    }

    // Verify player owns the relic
    void* player_relics_arr = nullptr;
    uint32_t player_relics_cnt = 0;
    SafeReadPtr((const void*)((uintptr_t)country + kCountryOwnedRelics), &player_relics_arr);
    SafeReadU32((const void*)((uintptr_t)country + kCountryOwnedRelicsCount), &player_relics_cnt);

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

    namespace relic = sdk::cmd::activate_relic_command;
    auto cmd = CommandBuilder::Get().Create(relic::kSpec);
    cmd.SetString(relic::relic, relic_key)
       .Set<uint32_t>(relic::country, GameState::Get().GetPlayerCountryId());
    if (!cmd.Post()) {
        out_message = cmd.error();
        return false;
    }

    out_message = "Successfully activated triumph effect of relic '" + LocalizeKey(relic_key) + "'";
    return true;
}

} // namespace bridge
