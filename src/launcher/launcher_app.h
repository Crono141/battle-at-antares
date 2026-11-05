#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "antares/package_catalog.h"
#include "antares/settings_docs.h"
#include "config/config_runtime.h"

// The Battle at Antares launcher (WP5.1, D23): pick a profile, choose and order its packages,
// check it with the game's own resolver, and start the game.
namespace launcher {
    namespace settings = antares::settings;
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
        /** Opens the Settings tab on a page ("common", "profile", a category id) or, when the text
         *  isn't a page, on a search for it (the -settings option). */
        void Open_Settings(const std::string& page_or_search);
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

        // Settings tab (WP5.2, launcher_settings.cpp).
        void Init_Settings_();
        void Draw_Settings_(float height);
        void Draw_Settings_Pages_();
        void Draw_Settings_Table_(const std::vector<config::t_variable_id>& rows, float height);
        void Draw_Setting_Details_();
        bool Draw_Setting_Editor_(config::t_variable_id id, const config::s_typed_value& value, std::string* value_text);
        void Set_Setting_(config::t_variable_id id, const std::string& value_text);
        std::vector<config::t_variable_id> Settings_Rows_() const;
        bool Profile_Sets_(config::t_variable_id id) const;

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

        // Settings.
        int tab_ = 0;           // 0 packages, 1 settings
        bool force_tab_ = true; // select tab_ on the first frame
        settings::c_schema_docs docs_;
        std::vector<std::string> setting_titles_;            // by variable id
        std::vector<std::string> setting_search_;            // lowercase title, path and description
        std::vector<std::string> setting_category_;          // category id by variable id ("" for other)
        std::vector<config::t_variable_id> common_settings_; // CONFIG_NO_VARIABLE marks a section break
        std::vector<std::string> common_sections_;
        std::vector<config::t_variable_id> profile_settings_;
        std::string settings_page_ = "common"; // "common", "profile", or a category id
        char settings_search_[128] = {};
        config::t_variable_id selected_setting_ = config::CONFIG_NO_VARIABLE;

        // Profile popups.
        int popup_ = 0; // 0 none, 1 new, 2 copy, 3 rename, 4 delete
        char name_buffer_[65] = {};
        std::string popup_error_;
    };
} // namespace launcher
