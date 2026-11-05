#pragma once

#include <SDL3/SDL.h>

#include <memory>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "antares/package_catalog.h"
#include "antares/save_summary.h"
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

    struct s_save_row {
        antares::saves::s_save_summary summary;
        antares::s_config_check check; // loading it with the selected profile, when readable
    };

    /** A setting's value in CFG syntax ("hq2x", "true", "1280"), for display. */
    std::string Setting_Value_Text(config::t_variable_id id, const config::s_typed_value& value);

    /** Draws Markdown: # headings, paragraphs, "- " bullets and pipe tables; ** and ` are
     *  dropped. With a `filter`, only table rows containing it (any case) are drawn, under their
     *  headings. Returns the number of table rows drawn. */
    int Draw_Markdown(const std::string& text, const std::string& filter = "");

    // Pseudo-rows of the Settings table (WP5.5), beside real variable ids.
    constexpr config::t_variable_id AUTO_WINDOW_ROW = config::CONFIG_NO_VARIABLE - 1; // the toggle
    constexpr config::t_variable_id SCALER_NOTE_ROW = config::CONFIG_NO_VARIABLE - 2; // why it can't show

    // What a package does on its own: a dry run that enables only it (WP5.5).
    struct s_package_preview {
        bool ok = false;
        std::string error;
        std::vector<config::t_variable_id> own;            // settings the package itself decides
        std::map<std::string, int> from_dependencies;      // settings its dependencies decide, by package
        std::shared_ptr<const config::s_effective_config> effective;
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
        /** Opens the Saves tab with a slot selected (0: none) (the -saves option). */
        void Open_Saves(int slot);
        /** Changes a setting in the selected profile as the Settings tab does ("interface.scaler=hq2x";
         *  an empty value removes the line), with its message (the -set option). */
        bool Apply_Setting(const std::string& assignment);
        /** Ticks or unticks a package as the Packages tab does, switching off what conflicts with it
         *  (on) or still needs it (off); the message names them. When that switches anything else
         *  off and !confirmed, nothing changes yet: a popup asks first (Toggle_Pending). False when
         *  nothing changed. */
        bool Set_Package_Enabled(const std::string& package_id, bool on, bool confirmed = false);
        bool Toggle_Pending() const { return !pending_toggle_.id.empty(); }
        const std::string& Message() const { return message_; }
        /** Opens the Keys tab, filtered by `filter` (the -keys option). */
        void Open_Keys(const std::string& filter);
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
        void Launch_(int load_save); // 0: main menu, 1-10: -load-save
        std::string Profile_File_Path_(const s_profile_entry& entry) const;
        const antares::s_package_info* Find_Package_(const std::string& id) const;

        void Draw_Profile_Bar_();
        void Draw_Profile_Popups_();
        void Draw_Toggle_Popup_();
        std::string Package_Name_(const std::string& package_id) const;
        void Draw_Mods_Table_(float height);
        void Draw_Package_Details_(float height);
        void Draw_Check_();
        void Draw_Launch_Bar_();

        // Saves tab (WP5.3, launcher_saves.cpp).
        void Refresh_Saves_();
        const s_save_row* Find_Save_(int slot) const;
        std::string Save_Label_(int slot) const;
        std::string Save_Problem_(const s_save_row& row) const; // "" when the save loads
        std::string Short_Problem_(const s_save_row& row, const std::string& problem) const;
        void Draw_Saves_(float height);
        void Draw_Save_Details_(const s_save_row& row);
        void Draw_Save_Needs_(const std::vector<config::s_mod_requirement>& needs, bool loads);
        void Draw_Continue_Combo_();

        // Settings tab (WP5.2, launcher_settings.cpp).
        void Init_Settings_();
        void Draw_Settings_(float height);
        void Draw_Settings_Pages_();
        void Draw_Settings_Table_(const std::vector<config::t_variable_id>& rows, float height);
        void Draw_Setting_Details_();
        bool Draw_Setting_Editor_(config::t_variable_id id, const config::s_typed_value& value, std::string* value_text);
        void Set_Setting_(config::t_variable_id id, const std::string& value_text);
        /** Several setting lines in one write and one check, undone together if the game would
         *  refuse them. `note` is added to the message. */
        void Set_Settings_(const std::vector<std::pair<config::t_variable_id, std::string>>& edits, const std::string& note = "");
        // The scaler and the window (WP5.5).
        bool Hd_Art_Enabled_(std::string* package_id = nullptr) const;
        std::string Scaler_Note_() const; // why the scaler can't show, or ""
        /** The window size edits that match `scaler` ("hq2x"...), or none (bilinear, HD art). */
        std::vector<std::pair<config::t_variable_id, std::string>> Window_Edits_For_(const std::string& scaler, std::string* note) const;
        void Draw_Auto_Window_Row_();

        // Package information (WP5.5, launcher_info.cpp).
        const s_package_preview& Package_Preview_(const std::string& id);
        void Draw_Package_Changes_(const antares::s_package_info& package);
        void Draw_Keys_(float height);
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
        bool auto_window_ = true; // set the window size to match the scaler (WP5.5)

        SDL_Process* game_ = nullptr;
        bool quit_ = false;

        // Saves: read and checked again when the profile, its packages or the files change.
        std::vector<s_save_row> saves_; // slots 1-10 in order
        bool saves_dirty_ = true;
        double saves_ms_ = 0.0;
        int selected_save_ = 0;

        // Settings.
        int tab_ = 0;           // 0 packages, 1 settings, 2 saves, 3 keys
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

        // Package information and the Keys tab.
        std::map<std::string, s_package_preview> previews_; // cleared when packages are read again
        char keys_filter_[64] = {};

        // Profile popups.
        int popup_ = 0; // 0 none, 1 new, 2 copy, 3 rename, 4 delete
        char name_buffer_[65] = {};
        std::string popup_error_;
        struct s_pending_toggle {
            std::string id; // empty: no popup
            bool on = false;
            antares::s_package_toggle toggle;
        };
        s_pending_toggle pending_toggle_;
    };
} // namespace launcher
