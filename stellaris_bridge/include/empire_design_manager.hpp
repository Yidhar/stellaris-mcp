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

    // L2 write (main menu): read the one design in a file the way the game reads its user designs
    // (a bare file name is a VFS path: the user folder), run the game's fix-ups and validation, and
    // keep it as a user design (overwrite: in place of the user design with its key). validate_only,
    // or an invalid design: checked, then dropped. Memory only: the caller writes the design file
    nlohmann::json SaveFromFile(const std::string& vfs_file, bool validate_only, bool overwrite);
    // remove a user design from memory (the caller removes it from the design file)
    nlohmann::json Delete(const std::string& key);
    // the user design file the game reads and writes (VFS path in the user folder)
    std::string UserFile();

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
