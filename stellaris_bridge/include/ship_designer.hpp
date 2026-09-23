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

struct CoreComponentsInfo {
    std::string reactor;
    std::string ftl;
    std::string thruster;
    std::string sensor;
    std::string combat_computer;
    std::string aura;
};

struct ShipDesignInfo {
    uint32_t design_id{ 0 };
    std::string name;
    std::string ship_size;
    std::string class_prefix;
    std::vector<SectionInfo> sections;
    CoreComponentsInfo core_components;
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
    bool CreateShipDesign(const std::string& ship_size, std::string& name,
                          const nlohmann::json& slots_json, const nlohmann::json& cores_json,
                          uint32_t& out_design_id, std::string& out_message);
    nlohmann::json CreateShipDesignJson(const nlohmann::json& params);

    bool UpdateShipDesign(uint32_t design_id, const std::string& new_name,
                          const nlohmann::json& slots_json, const nlohmann::json& cores_json,
                          std::string& out_message);
    nlohmann::json UpdateShipDesignJson(const nlohmann::json& params);

    bool UpgradeFleet(uint32_t fleet_id, uint32_t starbase_id, uint32_t target_design_id,
                      std::string& out_message);
    nlohmann::json UpgradeFleetJson(const nlohmann::json& params);

    bool DeleteShipDesign(uint32_t design_id, std::string& out_message);
    nlohmann::json DeleteShipDesignJson(const nlohmann::json& params);

    using FnSetComponentOnSlot = void (*)(void* pSection, void* pComponentTemplate, void* pSlotDef);
    using FnStageUpdateResources = void (*)(void* pStage);

    using FnEngineAlloc = void* (*)(size_t size);
    using FnPostCommand = void (*)(void* pCmd, int flag);
    using FnRegisterDesign = void* (*)(void* manager_ctx, void* source_design);
    using FnCountryAddDesign = void (*)(void* country_designs_vec, uint32_t index, void** pp_new_design);
    using FnCalcLongName = void (*)(void* pDesign);
    using FnCanBeBuiltBy = bool (*)(void* pComponentTemplate, void* pCountry, int designOwnerType);
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

    FnSetComponentOnSlot fn_set_component_on_slot_{ nullptr };
    FnStageUpdateResources fn_stage_update_resources_{ nullptr };
    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
    FnRegisterDesign fn_register_design_{ nullptr };
    FnCountryAddDesign fn_country_add_design_{ nullptr };
    FnCalcLongName fn_calc_long_name_{ nullptr };
    FnCanBeBuiltBy fn_can_be_built_by_{ nullptr };
    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };

    bool CanCountryUseComponent(void* p_tmpl, void* p_country);
    std::string LocalizeKey(const std::string& key);
    bool SetShipDesignName(void* design, const std::string& name);

    std::unordered_map<std::string, void*> component_cache_;
    bool component_cache_built_{ false };
};

} // namespace bridge
