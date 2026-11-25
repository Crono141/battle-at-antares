#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "antares/game_files.h"
#include "antares/game_install.h"
#include "antares/hd_packs.h"
#include "antares/updates.h"
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
    constexpr config::t_variable_id UPDATES_ROW = config::CONFIG_NO_VARIABLE - 3;     // check for updates (WP5.19)
    constexpr config::t_variable_id LOOK_ROW = config::CONFIG_NO_VARIABLE - 4;        // the launcher's look (WP5.20)

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
        // The Godot interface is on: the profile's `interface legacy_ui` is off (D27).
        bool Native_UI_On_() const;
        bool Toggle_Pending() const { return !pending_toggle_.id.empty(); }
        const std::string& Message() const { return message_; }
        bool Game_Running() const {
            return game_ != nullptr;
        }
        /** Polls the running game, if any. */
        void Update();

        /** The setup wizard (WP5.11): finds the player's Master of Orion II, checks it, copies its
         *  files and imports its saves. It opens by itself when the files are missing; with a
         *  `folder` it starts on that folder's checklist, with "find" on the search, "get" on the store page (the -setup-folder
         *  option). */
        void Open_Setup(const std::string& folder = "");
        /** The whole wizard without a window (the -setup-run option): check `folder`, copy, import
         *  the saves keeping Antares's own, and stop on the last page. False when a step failed. */
        bool Run_Setup(const std::string& folder, bool stop_at_saves = false);
        /** The Done page's buttons: the choices made there (full screen, the applications menu),
         *  then the launcher, or the game when `play`. */
        void Finish_Setup(bool play);
        /** -update (tests): Update is pressed as soon as the check finds a newer release. */
        void Update_When_Available() {
            update_auto_ = true;
        }
        bool Setup_Open() const {
            return setup_.active;
        }
        /** Presses "Copy the files" on the checklist when it allows it (the -setup-copy option). */
        bool Start_Setup_Copy();
        bool Setup_Copying() const {
            return setup_.active && setup_.page == SETUP_COPY && setup_.progress && !setup_.progress->failed;
        }
        /** The HD art list is still being fetched, or a pack is downloading (headless waits). */
        bool Hd_Waiting() const {
            return hd_.listing || hd_.busy || gog_.busy;
        }
        /** Ticks the pack `id` and starts its download (the -hd-download option). */
        bool Download_Hd_Pack(const std::string& id);
        /** Opens the wizard on GOG sign-in and starts it; GOG's answers are saved in `dump_folder`
         *  (the -gog-dump option, for building test fixtures; the token is never saved). */
        void Start_Gog_Signin(const std::string& dump_folder);
        /** Opens the HD art window (the -hd option). */
        void Open_Hd_Window() {
            hd_.window = true;
        }
        /** The wizard is waiting for a store to install the game (headless waits with -wait-store). */
        bool Setup_Waiting() const {
            return setup_.active && setup_.page == SETUP_WAIT;
        }
        /** Presses Cancel on the copy page. */
        void Cancel_Setup_Copy() {
            if (setup_.progress) setup_.progress->cancel = true;
        }

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
        std::string Profile_File_Path_(const s_profile_entry& entry) const; // written here
        std::string Profile_Read_Path_(const s_profile_entry& entry) const; // the player's copy, else bundled
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
        std::vector<config::t_variable_id> Settings_Rows_() const;
        bool Profile_Sets_(config::t_variable_id id) const;

        std::string game_dir_;
        std::string data_dir_;      // <game folder>/data/: the bundled antares.cfg and profiles
        std::string config_dir_;    // where the player's own copies go (data_dir_ when portable)
        std::string settings_path_; // launcher.json
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
        // WP3.1a: start the Godot build (antares-godot, beside antares) instead of the SDL one.
        bool Godot_Available_() const;
        std::string Godot_Executable_() const;
        bool auto_window_ = true; // set the window size to match the scaler (WP5.5)

        SDL_Process* game_ = nullptr;
        bool quit_ = false;

        // WP5.10: the original files (checked at start, on Rescan and after a game), and why the
        // last game stopped when it stopped with an error (its message and the end of its log).
        antares::game_files::s_files_check files_;
        int64_t game_pid_ = 0;
        struct s_game_exit {
            bool show = false;
            int code = 0;
            std::string reason;   // the game's fatal message, from its log
            std::string log_tail; // the last lines of the log
            std::string log_path;
        } exit_;
        void Check_Files_();

        // The setup wizard (WP5.11, launcher_setup.cpp).
        enum e_setup_page { SETUP_WELCOME,
            SETUP_FIND,
            SETUP_GET,
            SETUP_WAIT,
            SETUP_GOG,
            SETUP_CHECK,
            SETUP_COPY,
            SETUP_SAVES,
            SETUP_HD,
            SETUP_DONE };
        struct s_setup {
            bool active = false;
            bool required = false; // opened because the files are missing: no way back to the launcher
            e_setup_page page = SETUP_WELCOME;
            std::vector<antares::game_install::s_candidate> candidates;
            int chosen = -1;
            antares::game_install::s_folder_check check;
            std::string target; // where the files go
            uint64_t free_bytes = 0;
            std::unique_ptr<antares::game_install::s_copy_progress> progress;
            std::thread worker;
            std::vector<antares::game_install::s_found_save> saves;
            bool import_saves = true;
            bool replace_taken = false; // owner, question 22: keep Antares's saves unless asked
            bool add_to_menu = true;    // owner 2026-11-25: the applications menu (portable builds)
            bool fullscreen = true;     // owner 2026-11-24: the first game shouldn't open in a 640x480 window
            // Waiting for a store to install the game (WP5.12): "Steam" or "GOG".
            std::string wait_store;
            std::string check_error; // why an installer can't be read (Check_Installer)
            uint64_t wait_next_poll = 0;
            antares::game_install::s_steam_app steam;
            int imported = -1;
            std::string import_error;
            // Browse: SDL's folder dialog answers on its own thread.
            std::mutex dialog_mutex;
            bool dialog_answered = false;
            std::string dialog_folder;
        } setup_;
        void Draw_Setup_();

        // The HD art packs (WP5.14, launcher_hd.cpp): offered by the wizard and by the HD art
        // button, downloaded one at a time on a worker thread, switched on when verified.
        struct s_hd {
            std::mutex mutex; // guards the fields the worker threads write
            bool listing = false;
            std::atomic<bool> list_done{false};
            bool listed = false;
            std::string list_error;
            std::string tag;
            std::vector<antares::hd_packs::s_pack> packs;
            std::vector<uint8_t> ticked;
            std::thread list_worker;
            std::thread worker;
            std::atomic<bool> busy{false};
            std::vector<size_t> queue; // pack indices still to fetch
            int current = -1;          // the pack being fetched
            int cancel_index = -1;     // a Cancel on one pack (the others carry on); -1: Stop all
            antares::hd_packs::s_progress progress;
            std::mutex phase_mutex;
            std::string phase;
            std::string error;
            std::vector<std::string> finished; // ids to switch on (main thread)
            std::vector<std::string> session_done; // ids finished since the launcher started (for the bars)
            bool window = false;               // the launcher's HD art window is open
            std::string mods_folder;
        } hd_;
        void Hd_List_();
        void Hd_Start_();
        void Hd_Poll_();
        void Hd_Stop_();
        /** Cancel one pack: drop it from the queue, or stop it if it's downloading (the rest go on). */
        void Hd_Cancel_(size_t index);
        std::string Hd_Status_();
        bool Hd_Any_Ticked_() const;
        void Draw_Hd_List_();
        void Draw_Hd_Window_();
        /** One bar per pack this session (downloading, waiting, done), up to three (owner
         *  2026-11-24). `compact`: the name beside its bar; otherwise the name above. */
        void Draw_Hd_Bars_(bool compact);

        // GOG sign-in and download (WP5.13b, launcher_gog.cpp): the sign-in program gives a
        // one-time code; a worker trades it for a token, finds the game in the library, downloads
        // the installer into Downloads and checks it. The code and token never leave the worker's
        // memory: no file, no log, no command line.
        struct s_gog {
            std::thread worker;
            std::atomic<bool> busy{false};
            std::mutex mutex;
            std::string phase;       // what is happening, for people
            std::string error;       // why it stopped
            std::string result_path; // the checked installer, once there
            bool finished = false;   // the worker is done (result_path or error set)
            bool no_engine = false;  // the sign-in program couldn't start
            uint64_t total = 0;      // the download's expected size
            antares::hd_packs::s_progress progress;
            std::string dump_folder; // -gog-dump: GOG's library and game answers are saved here (never the token)
        } gog_;
        // Updates from the launcher (WP5.19, launcher_update.cpp).
        struct s_update {
            bool check = true;            // the setting: look at start
            std::string installed;        // this copy's release label ("" in a development build)
            antares::updates::e_kind kind = antares::updates::KIND_NONE;
            std::thread worker;
            std::atomic<bool> busy{false};        // checking
            std::atomic<bool> downloading{false};
            std::mutex mutex;
            bool available = false;       // a newer release
            antares::updates::s_release release;
            bool start_requested = false; // Update pressed (main thread)
            bool ready = false;           // the download is checked: apply it
            std::string download_path;
            std::string error;
            antares::hd_packs::s_progress progress;
        } update_;
        void Update_Begin_();
        bool update_auto_ = false; // -update
        void Update_Stop_();
        void Update_Poll_();
        void Update_Start_();
        bool Update_Installable_() const;
        bool Update_Apply_(std::string* error);
        void Draw_Update_Notice_(float x, float y);
        void Draw_Updates_Row_();
        void Draw_Look_Row_();
        bool Gog_Available_() const;
        void Gog_Start_();
        void Gog_Poll_();
        void Gog_Stop_();
        void Setup_Find_();
        void Setup_Check_(const std::string& folder);
        void Setup_Start_Copy_();
        void Setup_After_Copy_();
        void Setup_Import_();
        void Setup_Close_();
        void Setup_Poll_();
        void Setup_Wait_(const std::string& store);
        void Setup_Wait_Poll_();
        void Read_Exit_Log_(int exit_code);
        void Draw_Exit_Popup_();

        // Saves: read and checked again when the profile, its packages or the files change.
        std::vector<s_save_row> saves_; // slots 1-10 in order
        bool saves_dirty_ = true;
        double saves_ms_ = 0.0;
        int selected_save_ = 0;

        // Settings.
        int tab_ = 0;           // 0 packages, 1 settings, 2 saves
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

        // Package information.
        std::map<std::string, s_package_preview> previews_; // cleared when packages are read again

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
