#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace bridge {

class DiscoveriesManager {
public:
    static DiscoveriesManager& Get();

    using FnLocalize = void(*)(void* out_pdx_str, const void* in_key);
    using FnFreePdxStr = void(*)(void* pdx_str);
    using FnEngineAlloc = void*(*)(size_t size);
    using FnPostCommand = void(*)(void* cmd, int unk);

    bool Init(uintptr_t base_address);

    // Queries
    nlohmann::json GetDiscoveriesInfo(const std::string& tab);
    nlohmann::json GetSummaryJson();

    // Actions
    bool ActivateRelic(const std::string& relic_key, std::string& out_message);

private:
    DiscoveriesManager() = default;

    void* GetPlayerCountry();
    std::string LocalizeKey(const std::string& key);

    uintptr_t base_address_{ 0 };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
};

} // namespace bridge
