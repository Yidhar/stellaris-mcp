#pragma once
// The main menu and the game session: which screen the game is on, loading a save, saving,
// and leaving for the main menu. Screen() only reads globals and is safe from any thread; the
// rest runs as TaskQueue tasks on the main thread.
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace bridge {

class MenuManager {
public:
    static MenuManager& Get();
    bool Init(uintptr_t base_address);

    // "main_menu", "loading", "in_game" (or "unknown" before either idler exists)
    std::string Screen() const;
    // a save or a new galaxy is being loaded (s_pLoadGameProgress is set): the engine renders the
    // loading screen from inside its own load, so the bridge must not run engine code then
    bool Loading() const;
    // { screen, quick_start_pending, user_dir, game_dir, user_design_file }; any thread
    nlohmann::json ScreenJson() const;
    // a save load requested (g_bQuickStart) and not yet taken or cleared
    bool QuickStartPending() const;

    // the user data folder the game reads saves from: Documents\Paradox Interactive\Stellaris
    static std::wstring UserDir();
    // the game's install folder (stellaris.exe's)
    static std::wstring GameDir();

    // main menu: load "<campaign>/<file>" (the file name without .sav) the way the main menu's
    // continue does (g_QuickStartSave + g_bQuickStart; the menu's own update loads it)
    nlohmann::json LoadSave(const std::string& campaign, const std::string& file);
    // in game, after LoadSave: drop the load request. The menu's update would clear it after the
    // load, but the load replaces the menu first; a request left set reloads that save the next
    // time the main menu shows
    nlohmann::json FinishLoad();
    // in game: leave for the main menu (what the resign dialog's accept runs, deferred)
    nlohmann::json ExitToMainMenu();
    // in game: save into the campaign's folder (CInGameIdler::SaveGame); the file is written over
    // the next frames
    nlohmann::json SaveGame(const std::string& name, bool overwrite);

    // main menu: start a new single-player game with the galaxy settings as they are, the way the
    // main menu's quick start does (g_bQuickStart with no save named: the menu adds a human with a
    // random design and calls LaunchGame). design_key empty or "random": that random empire;
    // otherwise the human takes that design on the frame the launch is set (StartNewGame runs on
    // the next update), checked first with the game's validation
    nlohmann::json StartNewGame(const std::string& design_key);
    // every frame (the Present hook, not while loading): hands a pending design to the local human
    void OnFrame();

private:
    MenuManager() = default;
    void* FrontEndIdler() const;
    void* InGameIdler() const;
    uintptr_t base_address_ = 0;
    void* pending_design_ = nullptr;  // for the local human once the launch is set
    int pending_frames_ = 0;
};

}  // namespace bridge
