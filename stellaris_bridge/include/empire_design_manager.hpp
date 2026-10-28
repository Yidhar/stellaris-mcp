#pragma once
// Empire designs (the empires the main menu offers): the prescripted ones from
// prescripted_countries\ and the user's from user_empire_designs_v3.4.txt, both in
// CEmpireDesignDatabase. Reading, and the game's own validation of a design for play.
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace bridge {

class EmpireDesignManager {
public:
    static EmpireDesignManager& Get();
    bool Init(uintptr_t base_address);

    // L1: every design, compact (key, name, type, species, ethics, origin, playable)
    nlohmann::json ListJson();
    // L2: one design in full, with the game's validation (main menu only: it builds a scratch
    // country in the game state, as the empire list does)
    nlohmann::json DetailJson(const std::string& key);

    // the design with that key (prescripted first, then user, as the game looks them up); with
    // its type "prescripted" / "user"
    void* Find(const std::string& key, std::string* type = nullptr);

private:
    EmpireDesignManager() = default;
    nlohmann::json Summary(void* design, const std::string& type);
    // IsEmpireDesignValid(game setup, design, scratch country, reason): one scratch country for all
    // the designs, as the empire list builds; results in order (-1 = the check raised)
    std::vector<int> ValidateAll(const std::vector<void*>& designs, std::vector<std::string>* reasons);
    bool Validate(void* design, bool* valid, std::string* reason);
    uintptr_t base_address_ = 0;
};

}  // namespace bridge
