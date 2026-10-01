#pragma once

#include "common.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace bridge {

struct SlotInfo {
    uint32_t slot_index{ 0 };
    std::string slot_name;
    std::string component_key;
    std::string component_name;
};

struct SectionInfo {
    std::string name;
    std::vector<SlotInfo> slots;
};

// a required ("core") component: reactor, FTL drive, thrusters, sensor, combat computer, aura ...
// The hull defines which; the engine replaces one by its component set.
struct CoreComponentInfo {
    uint32_t index{ 0 };
    std::string component_key;
    std::string component_name;
};

struct ShipDesignInfo {
    uint32_t design_id{ 0 };
    std::string name;
    std::string ship_size;
    std::vector<SectionInfo> sections;
    std::vector<CoreComponentInfo> core_components;
};

struct ComponentVariantInfo {
    std::string component_key;
    std::string size;
};

struct ComponentSetInfo {
    std::string set_key;
    std::string localized_name;
    std::string icon;
    std::vector<ComponentVariantInfo> variants;
};

class ShipDesigner {
public:
    static ShipDesigner& Get();

    bool Init(uintptr_t base_address);

    // Queries
    std::vector<ShipDesignInfo> GetShipDesigns(uint32_t specific_design_id = 0xFFFFFFFF);
    nlohmann::json GetShipDesignsJson(const nlohmann::json& params);

    nlohmann::json GetShipDesignCatalogJson(const nlohmann::json& params);
    nlohmann::json GetComponentDetailsJson(const nlohmann::json& params);

    // Actions
    // Both go through the native CCreateOrUpdateShipDesignCommand, as the ship designer's save:
    // create starts from an existing design of the hull; update replaces a design (the engine
    // matches it by name, gives the result a new id and retrofits fleet templates).
    nlohmann::json CreateShipDesignJson(const nlohmann::json& params);
    nlohmann::json UpdateShipDesignJson(const nlohmann::json& params);

    bool UpgradeFleet(uint32_t fleet_id, uint32_t starbase_id, uint32_t target_design_id,
                      std::string& out_message);
    nlohmann::json UpgradeFleetJson(const nlohmann::json& params);

    bool DeleteShipDesign(uint32_t design_id, std::string& out_message);
    nlohmann::json DeleteShipDesignJson(const nlohmann::json& params);

    using FnEngineAlloc = void* (*)(size_t size);
    using FnLocalize = void* (*)(void* out_str, const void* in_key);
    using FnFreePdxStr = void (*)(void* str);

private:
    ShipDesigner() = default;
    ~ShipDesigner() = default;

    void* GetPlayerCountry();
    void* FindShipDesign(uint32_t design_id);
    void* FindComponentTemplate(const std::string& component_key);
    void* FindFleet(uint32_t fleet_id);

    void BuildComponentIndexIfNeeded();

    uintptr_t base_address_{ 0 };

    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };

    bool CanCountryUseComponent(void* p_tmpl, void* p_country);
    std::string LocalizeKey(const std::string& key);
    bool SetShipDesignName(void* design, const std::string& name);
    std::vector<void*> PlayerDesigns();  // the player's design collection
    // edits a design object (component slots and required components); counts real changes
    bool ApplyDesignEdits(void* design, void* country, const nlohmann::json& slots_json,
                          const nlohmann::json& cores_json, int* changes, std::string* why);
    // copies `source` into a create_or_update_ship_design command, applies name and edits,
    // validates like the designer's save and posts it
    bool PostDesign(void* source, const std::string& name, const nlohmann::json& slots_json,
                    const nlohmann::json& cores_json, bool is_new, std::string* message);

    std::unordered_map<std::string, void*> component_cache_;
    bool component_cache_built_{ false };
};

} // namespace bridge
