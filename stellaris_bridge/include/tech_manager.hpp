#pragma once

#include "common.hpp"
#include <string>
#include <vector>

namespace bridge {

struct TechCard {
    std::string key;
    std::string name;
    uint32_t area{ 0 };             // 0: physics, 1: society, 2: engineering
    std::string area_name;          // "physics", "society", "engineering"
    uint32_t tier{ 0 };
    uint64_t cost{ 0 };
    uintptr_t tech_ptr{ 0 };
};

struct ActiveResearch {
    bool is_researching{ false };
    std::string key;
    std::string name;
    uint32_t area{ 0 };
    std::string area_name;
    uint32_t tier{ 0 };
    uint64_t cost{ 0 };
    uintptr_t tech_ptr{ 0 };
};

struct ResearchAreaState {
    ActiveResearch current;
    std::vector<TechCard> candidates;
};

struct FullResearchState {
    ResearchAreaState physics;
    ResearchAreaState society;
    ResearchAreaState engineering;
};

class TechManager {
public:
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);

    static TechManager& Get();

    bool Init(uintptr_t base_address);

    FullResearchState GetResearchState();
    nlohmann::json GetResearchStateJson();

    // Select research in a given area by tech key (e.g. "tech_fusion_power")
    // If an existing technology is being researched, it is safely cancelled first.
    // area: 0=physics, 1=society, 2=engineering
    nlohmann::json SelectResearch(uint32_t area, const std::string& tech_key);

    // Cancel currently active research in the given area
    nlohmann::json CancelResearch(uint32_t area);

private:
    TechManager() = default;

    uintptr_t base_address_{ 0 };

    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };

    void* GetPlayerCountry();
    void* GetTechManagerPtr();

    std::string ExtractTechKey(void* tech_ptr);
    std::string LocalizeTechKey(const std::string& key);
    uint32_t ExtractTechTier(void* tech_ptr);
    uint64_t ExtractTechCost(void* tech_ptr);
};

} // namespace bridge
