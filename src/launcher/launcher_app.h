#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "antares/package_catalog.h"

// The Battle at Antares launcher (WP5.1, D23): pick a profile, choose and order its packages,
// check it with the game's own resolver, and start the game.
namespace launcher {
    struct s_profile_entry {
        std::string label;    // what the list shows
        std::string vfs_path; // "antares.cfg" or "profiles/<name>.cfg"
        std::string name;     // profile name; empty for the base config
        bool is_base = false; // data/antares.cfg, launched without -config
    };

    class c_launcher {
      public:
        bool Init(const std::string& game_dir, const std::string& start_profile, const std::string& select_package = "");
        void Shutdown();
        /** Draws one frame of the UI. Returns false when the launcher should close. */
        bool Draw();
        /** Checks the selected profile and starts the game with it (the -play option). Logs and
         *  returns false when the profile doesn't pass the check or the game can't start. */
        bool Play();
        /** Polls the running game, if any. */
        void Update();

      private:
        void Load_Settings_();
        void Save_Settings_();
        void Refresh_Packages_();
        void Refresh_Profiles_();
        void Select_Profile_(size_t index);
        bool Write_Profile_Text_(const std::string& text);
        void Apply_Enabled_(const std::vector<std::string>& enabled);
        void Run_Check_();
        void Launch_();
        std::string Profile_File_Path_(const s_profile_entry& entry) const;
        const antares::s_package_info* Find_Package_(const std::string& id) const;

        void Draw_Profile_Bar_();
        void Draw_Profile_Popups_();
        void Draw_Mods_Table_(float height);
        void Draw_Package_Details_(float height);
        void Draw_Check_();
        void Draw_Launch_Bar_();

        std::string game_dir_;
        std::string data_dir_;
        std::vector<antares::s_package_info> packages_;
        std::vector<s_profile_entry> profiles_;
        size_t profile_index_ = 0;
        std::string profile_text_;
        std::vector<std::string> enabled_;
        antares::s_config_check check_;
        double check_ms_ = 0.0;
        std::string selected_package_;
        std::string message_;
        bool message_is_error_ = false;

        // Launch options, remembered in launcher.json beside the binary.
        std::string last_profile_;
        bool skip_intro_ = false;
        int load_save_ = 0; // 0: none, 1-10: SAVE<n>.GAM
        bool close_on_launch_ = false;

        SDL_Process* game_ = nullptr;
        bool quit_ = false;

        // Profile popups.
        int popup_ = 0; // 0 none, 1 new, 2 copy, 3 rename, 4 delete
        char name_buffer_[65] = {};
        std::string popup_error_;
    };
} // namespace launcher
