#pragma once
// The single-player galaxy settings a new game is made with: SGalaxyConfigSettingsSP in CSettings
// (galaxy_config_sp), which the galaxy setup's pickers write and LaunchNewGame reads. Every member
// has an "unset" value meaning the galaxy size's (template's) own value.
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace bridge {

class GalaxySetupManager {
public:
    static GalaxySetupManager& Get();
    bool Init(uintptr_t base_address);

    // every setting (null: the template's value) and the choices of the named ones
    nlohmann::json GetJson();
    // set the given settings, as the galaxy setup's pickers do (null: back to the template's
    // value); all checked before any is written
    nlohmann::json Set(const nlohmann::json& settings);

private:
    GalaxySetupManager() = default;
    uintptr_t Settings() const;  // the SGalaxyConfigSettingsSP, 0 when CSettings is not there
    uintptr_t base_address_ = 0;
};

}  // namespace bridge
