#pragma once

#include "common.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace bridge {

struct SpeciesRights {
    std::string citizenship;
    std::string citizenship_localized;
    std::string living_standards;
    std::string living_standards_localized;
    std::string military_service;
    std::string military_service_localized;
    std::string slavery_type;
    std::string slavery_type_localized;
    std::string purge_type;
    std::string purge_type_localized;
    std::string population_controls;
    std::string population_controls_localized;
    std::string colonization_controls;
    std::string colonization_controls_localized;
    std::string migration_controls;
    std::string migration_controls_localized;
    std::string subspecies_integration;
    std::string subspecies_integration_localized;

    uint32_t cooldown_remaining_days{ 0 };
};

struct TraitInfo {
    std::string key;
    std::string localized_name;
};

struct SpeciesDetail {
    uint32_t species_id{ 0 };
    std::string key;
    std::string name;
    std::string plural;
    std::string adjective;
    std::string class_name;
    std::vector<TraitInfo> traits;
    SpeciesRights rights;
    uint32_t empire_pops{ 0 };
    bool is_founder{ false };
};

struct SpeciesSummary {
    uint32_t founder_species_id{ 0 };
    std::string founder_species_name;
    uint32_t total_species_in_galaxy{ 0 };
    uint32_t empire_colonies_count{ 0 };
    uint32_t total_empire_pops{ 0 };
};

struct RightOption {
    std::string key;
    std::string localized_name;
};

class SpeciesManager {
public:
    using FnLocalize = void(__fastcall*)(void* out_pdx_str, const void* in_key_pdx_str);
    using FnFreePdxStr = void(__fastcall*)(void* pdx_str);
    using FnGetSpeciesRights = void*(__fastcall*)(void* pRightsMgr, void* pSpecies, uint8_t* out_is_specific);
    using FnEngineAlloc = void* (*)(size_t size);
    using FnPostCommand = void (*)(void* cmd, int unk);
    using FnSetSpeciesRightCmdCtor = void*(__fastcall*)(void* this_ptr, void* pCountry, void* pSpecies, const void* pRights, uint8_t is_specific);

    static SpeciesManager& Get();

    bool Init(uintptr_t base_address);

    SpeciesSummary ReadSummary();
    nlohmann::json GetSummaryJson();
    nlohmann::json GetSpeciesJson(const nlohmann::json& req);

    // Layer 3: Set species rights
    nlohmann::json SetSpeciesRight(uint32_t species_id, const std::string& category, const std::string& right_value);

    void* FindSpeciesPtr(uint32_t species_id);
    void* GetSpeciesRightType(const std::string& category, const std::string& key);

    std::string LocalizeKey(const std::string& key);

    // Helpers
    void* GetPlayerCountry();
    uint32_t GetCurrentGameHours();

private:
    SpeciesManager() = default;

    uintptr_t base_address_{ 0 };

    FnLocalize fn_localize_{ nullptr };
    FnFreePdxStr fn_free_pdx_str_{ nullptr };
    FnGetSpeciesRights fn_get_species_rights_{ nullptr };
    FnEngineAlloc fn_engine_alloc_{ nullptr };
    FnPostCommand fn_post_command_{ nullptr };
    FnSetSpeciesRightCmdCtor fn_set_species_right_cmd_ctor_{ nullptr };

    // 9 Rights Databases caches: category -> map<key, void*>
    std::unordered_map<std::string, std::unordered_map<std::string, void*>> rights_cache_;
    std::unordered_map<std::string, std::vector<RightOption>> rights_catalog_;

    void EnsureDatabasesLoaded();
    void LoadRightDatabase(const std::string& category, uintptr_t db_rva);

    SpeciesRights ReadRights(void* pRightsMgr, void* pSpecies);
    std::vector<TraitInfo> ReadTraits(void* pSpecies);
    uint32_t CalculateEmpirePops(uint32_t* out_colony_count = nullptr);
};

} // namespace bridge
