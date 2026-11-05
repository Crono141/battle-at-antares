#include "pch.h"

#include "launcher/launcher_app.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "antares/identity.h"
#include "antares/profile_file.h"
#include "command_line.h"
#include "version.h"
#include "yyjson.h"

namespace launcher {
    namespace {
        constexpr char SETTINGS_FILE[] = "launcher.json";
        constexpr char PAYLOAD_TYPE[] = "ANTARES_PACKAGE";

        const ImVec4 COLOUR_OK(0.45f, 0.85f, 0.45f, 1.0f);
        const ImVec4 COLOUR_ERROR(1.0f, 0.45f, 0.40f, 1.0f);
        const ImVec4 COLOUR_DIM(0.60f, 0.62f, 0.68f, 1.0f);

        bool Read_Text_(const std::string& path, std::string* text) {
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;
            std::ostringstream stream;
            stream << file.rdbuf();
            *text = stream.str();
            return true;
        }

        // Writes through a temporary file, so a crash never leaves half a profile.
        bool Write_Text_(const std::string& path, const std::string& text, std::string* error) {
            const std::string temporary = path + ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
                    *error = "unable to write " + temporary;
                    return false;
                }
            }
            std::error_code code;
            std::filesystem::rename(temporary, path, code);
            if (code) {
                *error = "unable to replace " + path + ": " + code.message();
                std::filesystem::remove(temporary, code);
                return false;
            }
            return true;
        }

        void Help_Marker_(const char* text) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                ImGui::TextUnformatted(text);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        std::string Join_(const std::vector<std::string>& items) {
            std::string text;
            for (const std::string& item : items) {
                if (!text.empty()) text += ", ";
                text += item;
            }
            return text;
        }
    } // namespace

    bool c_launcher::Init(const std::string& game_dir, const std::string& start_profile, const std::string& select_package) {
        selected_package_ = select_package;
        game_dir_ = game_dir;
        if (!game_dir_.empty() && game_dir_.back() != '/' && game_dir_.back() != '\\') {
            game_dir_ += '/';
        }
        data_dir_ = game_dir_ + "data/";
        Load_Settings_();
        Init_Settings_();
        if (!start_profile.empty()) {
            last_profile_ = start_profile;
        }
        Refresh_Packages_();
        Refresh_Profiles_();
        size_t index = 0;
        for (size_t candidate = 0; candidate < profiles_.size(); ++candidate) {
            if (profiles_[candidate].name == last_profile_ || (last_profile_.empty() && profiles_[candidate].is_base)) {
                index = candidate;
            }
        }
        Select_Profile_(index);
        return true;
    }

    void c_launcher::Shutdown() {
        Save_Settings_();
        if (game_ != nullptr) {
            SDL_DestroyProcess(game_); // the game keeps running
            game_ = nullptr;
        }
    }

    void c_launcher::Load_Settings_() {
        std::string text;
        if (!Read_Text_(game_dir_ + SETTINGS_FILE, &text)) {
            return;
        }
        yyjson_doc* document = yyjson_read(text.data(), text.size(), 0);
        if (document == nullptr) {
            return;
        }
        yyjson_val* root = yyjson_doc_get_root(document);
        if (yyjson_is_str(yyjson_obj_get(root, "profile"))) last_profile_ = yyjson_get_str(yyjson_obj_get(root, "profile"));
        skip_intro_ = yyjson_get_bool(yyjson_obj_get(root, "skip_intro"));
        close_on_launch_ = yyjson_get_bool(yyjson_obj_get(root, "close_on_launch"));
        load_save_ = std::clamp(static_cast<int>(yyjson_get_int(yyjson_obj_get(root, "load_save"))), 0, 10);
        yyjson_doc_free(document);
    }

    void c_launcher::Save_Settings_() {
        yyjson_mut_doc* document = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* root = yyjson_mut_obj(document);
        yyjson_mut_doc_set_root(document, root);
        const std::string profile = profiles_.empty() ? "" : profiles_[profile_index_].name;
        yyjson_mut_obj_add_strcpy(document, root, "profile", profile.c_str());
        yyjson_mut_obj_add_bool(document, root, "skip_intro", skip_intro_);
        yyjson_mut_obj_add_int(document, root, "load_save", load_save_);
        yyjson_mut_obj_add_bool(document, root, "close_on_launch", close_on_launch_);
        size_t length = 0;
        char* json = yyjson_mut_write(document, YYJSON_WRITE_PRETTY, &length);
        yyjson_mut_doc_free(document);
        if (json != nullptr) {
            std::string error;
            (void)Write_Text_(game_dir_ + SETTINGS_FILE, std::string(json, length) + "\n", &error);
            free(json);
        }
    }

    void c_launcher::Refresh_Packages_() {
        packages_ = antares::Discover_Packages();
        saves_dirty_ = true;
    }

    void c_launcher::Refresh_Profiles_() {
        profiles_.clear();
        profiles_.push_back({"Default (antares.cfg)", antares::CONFIG_FILE, "", true});
        std::vector<std::string> names;
        std::error_code code;
        for (const auto& entry : std::filesystem::directory_iterator(data_dir_ + "profiles", code)) {
            const std::string file = entry.path().filename().string();
            if (entry.is_regular_file() && file.size() > 4 && file.compare(file.size() - 4, 4, ".cfg") == 0 &&
                file[0] != '.') {
                names.push_back(file.substr(0, file.size() - 4));
            }
        }
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            profiles_.push_back({name, "profiles/" + name + ".cfg", name, false});
        }
    }

    std::string c_launcher::Profile_File_Path_(const s_profile_entry& entry) const {
        return data_dir_ + entry.vfs_path;
    }

    const antares::s_package_info* c_launcher::Find_Package_(const std::string& id) const {
        for (const antares::s_package_info& package : packages_) {
            if (package.id == id) return &package;
        }
        return nullptr;
    }

    void c_launcher::Select_Profile_(size_t index) {
        profile_index_ = std::min(index, profiles_.size() - 1);
        profile_text_.clear();
        if (!Read_Text_(Profile_File_Path_(profiles_[profile_index_]), &profile_text_)) {
            profile_text_.clear(); // a missing base config is fine: the game uses defaults
        }
        enabled_ = antares::profile::Enabled_Packages(profile_text_);
        profile_settings_ = antares::profile::Profile_Settings(profile_text_);
        Run_Check_();
    }

    bool c_launcher::Write_Profile_Text_(const std::string& text) {
        std::string error;
        if (!Write_Text_(Profile_File_Path_(profiles_[profile_index_]), text, &error)) {
            message_ = error;
            message_is_error_ = true;
            return false;
        }
        profile_text_ = text;
        profile_settings_ = antares::profile::Profile_Settings(profile_text_);
        return true;
    }

    void c_launcher::Apply_Enabled_(const std::vector<std::string>& enabled) {
        if (Write_Profile_Text_(antares::profile::With_Enabled_Packages(profile_text_, enabled))) {
            enabled_ = enabled;
        }
        Run_Check_();
    }

    void c_launcher::Run_Check_() {
        const auto start = std::chrono::steady_clock::now();
        const s_profile_entry& entry = profiles_[profile_index_];
        std::error_code code;
        if (entry.is_base && !std::filesystem::exists(Profile_File_Path_(entry), code)) {
            check_ = {};
            check_.ok = true; // the game starts with defaults
        } else {
            check_ = antares::Check_Config(entry.vfs_path);
        }
        saves_dirty_ = true; // saves load with the profile's art packages and settings
        check_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    void c_launcher::Launch_(int load_save) {
        const s_profile_entry& entry = profiles_[profile_index_];
#if defined(_WIN32)
        const std::string executable = game_dir_ + "antares.exe";
#else
        const std::string executable = game_dir_ + "antares";
#endif
        const std::string prefix(1, command_line::OPTION_PREFIX);
        std::vector<std::string> arguments = {executable};
        if (!entry.is_base) {
            arguments.push_back(prefix + "config");
            arguments.push_back(entry.vfs_path);
        }
        if (skip_intro_) {
            arguments.push_back(prefix + "skip-intro");
        }
        if (load_save > 0) {
            arguments.push_back(prefix + "load-save");
            arguments.push_back(std::to_string(load_save));
        }
        std::vector<const char*> argv;
        for (const std::string& argument : arguments) argv.push_back(argument.c_str());
        argv.push_back(nullptr);

        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
        SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, game_dir_.c_str());
        game_ = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        std::string command;
        for (size_t index = 1; index < arguments.size(); ++index) command += " " + arguments[index];
        if (game_ == nullptr) {
            message_ = std::string("Unable to start ") + executable + ": " + SDL_GetError();
            message_is_error_ = true;
            return;
        }
        SDL_Log("launcher: started antares%s", command.c_str());
        message_ = "Game running: antares" + command;
        message_is_error_ = false;
        Save_Settings_();
        if (close_on_launch_) {
            quit_ = true;
        }
    }

    bool c_launcher::Play() {
        if (!check_.ok) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: %s: %s", profiles_[profile_index_].vfs_path.c_str(), check_.error.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Battle at Antares launcher", check_.error.c_str(), nullptr);
            return false;
        }
        if (load_save_ > 0) {
            Refresh_Saves_();
            const s_save_row* row = Find_Save_(load_save_);
            const std::string problem = row != nullptr ? Save_Problem_(*row) : "no such save";
            if (!problem.empty()) {
                const std::string text = "Save " + std::to_string(load_save_) + " won't load: " + problem;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: %s", text.c_str());
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Battle at Antares launcher", text.c_str(), nullptr);
                return false;
            }
        }
        Launch_(load_save_);
        if (game_ == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: %s", message_.c_str());
            return false;
        }
        return true;
    }

    void c_launcher::Update() {
        if (game_ == nullptr) return;
        int exit_code = 0;
        if (SDL_WaitProcess(game_, false, &exit_code)) {
            SDL_DestroyProcess(game_);
            game_ = nullptr;
            message_ = exit_code == 0 ? "The game closed." : "The game exited with code " + std::to_string(exit_code) + ".";
            message_is_error_ = exit_code != 0;
            Refresh_Profiles_(); // the game doesn't write profiles, but a player may have
            saves_dirty_ = true; // it may have saved
        }
    }

    bool c_launcher::Draw() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::TextUnformatted(antares::PRODUCT_NAME);
        ImGui::SameLine();
        ImGui::TextColored(COLOUR_DIM, "%s  ·  launcher", antares::PRODUCT_VERSION);
        ImGui::Separator();

        Draw_Profile_Bar_();
        ImGui::Spacing();

        const ImGuiStyle& style = ImGui::GetStyle();
        // Below the table: the check (one or two lines, plus a message line) and the launch bar.
        const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() * 3.0f + style.ItemSpacing.y * 3.0f;
        const float body = std::max(120.0f, ImGui::GetContentRegionAvail().y - bottom);
        const int wanted_tab = force_tab_ ? tab_ : -1;
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Packages", nullptr, wanted_tab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                tab_ = 0;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings", nullptr, wanted_tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                tab_ = 1;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Saves", nullptr, wanted_tab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
                tab_ = 2;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            force_tab_ = false;
        }
        const float tab_body = std::max(120.0f, body - ImGui::GetFrameHeightWithSpacing());
        if (tab_ == 0) {
            const float table_width = ImGui::GetContentRegionAvail().x * 0.60f;
            ImGui::BeginChild("mods", ImVec2(table_width, tab_body));
            Draw_Mods_Table_(tab_body);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("details", ImVec2(0.0f, tab_body), ImGuiChildFlags_Borders);
            Draw_Package_Details_(tab_body);
            ImGui::EndChild();
        } else if (tab_ == 1) {
            Draw_Settings_(tab_body);
        } else {
            Draw_Saves_(tab_body);
        }

        ImGui::Spacing();
        Draw_Check_();
        // The launch bar sits on the bottom edge, however many lines the check took.
        const float bar_top = ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight() - style.ItemSpacing.y;
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), bar_top - style.ItemSpacing.y));
        ImGui::Separator();
        Draw_Launch_Bar_();
        Draw_Profile_Popups_();
        ImGui::End();
        return !quit_;
    }

    void c_launcher::Draw_Profile_Bar_() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Profile");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        if (ImGui::BeginCombo("##profile", profiles_[profile_index_].label.c_str())) {
            for (size_t index = 0; index < profiles_.size(); ++index) {
                const bool selected = index == profile_index_;
                if (ImGui::Selectable(profiles_[index].label.c_str(), selected)) {
                    Select_Profile_(index);
                    Save_Settings_();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const bool is_base = profiles_[profile_index_].is_base;
        const auto open = [&](int popup, const std::string& initial) {
            popup_ = popup;
            popup_error_.clear();
            SDL_strlcpy(name_buffer_, initial.c_str(), sizeof(name_buffer_));
        };
        ImGui::SameLine();
        if (ImGui::Button("New")) open(1, "");
        ImGui::SameLine();
        if (ImGui::Button("Copy")) open(2, (is_base ? std::string("default") : profiles_[profile_index_].name) + " copy");
        ImGui::SameLine();
        ImGui::BeginDisabled(is_base);
        if (ImGui::Button("Rename")) open(3, profiles_[profile_index_].name);
        ImGui::SameLine();
        if (ImGui::Button("Delete")) open(4, profiles_[profile_index_].name);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Rescan")) {
            const std::string current = profiles_[profile_index_].vfs_path;
            Refresh_Packages_();
            Refresh_Profiles_();
            size_t index = 0;
            for (size_t candidate = 0; candidate < profiles_.size(); ++candidate) {
                if (profiles_[candidate].vfs_path == current) index = candidate;
            }
            Select_Profile_(index);
        }
        Help_Marker_("Profiles are files in data/profiles/. The game starts with one through "
                     "-config profiles/<name>.cfg. The launcher only changes their enable lines; "
                     "anything else you write in them is kept. Rescan reads data/mods/ and the "
                     "profiles again.");
    }

    void c_launcher::Draw_Profile_Popups_() {
        static const char* const TITLES[] = {"", "New profile", "Copy profile", "Rename profile", "Delete profile"};
        if (popup_ == 0) return;
        ImGui::OpenPopup(TITLES[popup_]);
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(TITLES[popup_], nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        bool close = false;
        if (popup_ == 4) {
            ImGui::Text("Delete the profile \"%s\"?", name_buffer_);
            ImGui::TextColored(COLOUR_DIM, "data/profiles/%s.cfg is removed. Packages are not touched.", name_buffer_);
            if (ImGui::Button("Delete", ImVec2(ImGui::GetFontSize() * 7.0f, 0.0f))) {
                std::error_code code;
                std::filesystem::remove(Profile_File_Path_(profiles_[profile_index_]), code);
                if (code) {
                    popup_error_ = code.message();
                } else {
                    Refresh_Profiles_();
                    Select_Profile_(0);
                    close = true;
                }
            }
        } else {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            const bool entered = ImGui::InputText("Name", name_buffer_, sizeof(name_buffer_), ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string name = name_buffer_;
            if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 7.0f, 0.0f)) || entered) {
                const std::string path = data_dir_ + "profiles/" + name + ".cfg";
                std::error_code code;
                if (!antares::profile::Is_Valid_Profile_Name(name)) {
                    popup_error_ = "Use letters, digits, spaces, '-', '_' and '.'.";
                } else if (std::filesystem::exists(path, code)) {
                    popup_error_ = "A profile with that name already exists.";
                } else {
                    std::filesystem::create_directories(data_dir_ + "profiles", code);
                    std::string error;
                    bool done = false;
                    if (popup_ == 3) {
                        std::filesystem::rename(Profile_File_Path_(profiles_[profile_index_]), path, code);
                        done = !code;
                        if (code) error = code.message();
                    } else {
                        std::string text = antares::profile::New_Profile_Text();
                        if (popup_ == 2) {
                            text = profile_text_;
                            if (profiles_[profile_index_].is_base) {
                                // A copy of the base config gets the base's enable lines; its
                                // settings come through the include.
                                text = antares::profile::With_Enabled_Packages(antares::profile::New_Profile_Text(), enabled_);
                            }
                        }
                        done = Write_Text_(path, text, &error);
                    }
                    if (done) {
                        Refresh_Profiles_();
                        for (size_t index = 0; index < profiles_.size(); ++index) {
                            if (profiles_[index].name == name) Select_Profile_(index);
                        }
                        Save_Settings_();
                        close = true;
                    } else {
                        popup_error_ = error;
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 7.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            close = true;
        }
        if (!popup_error_.empty()) {
            ImGui::TextColored(COLOUR_ERROR, "%s", popup_error_.c_str());
        }
        if (close) {
            popup_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void c_launcher::Draw_Mods_Table_(float height) {
        // Rows: the profile's packages in priority order, then the rest by id.
        std::vector<std::string> rows = enabled_;
        for (const antares::s_package_info& package : packages_) {
            if (std::find(enabled_.begin(), enabled_.end(), package.id) == enabled_.end()) {
                rows.push_back(package.id);
            }
        }
        std::vector<std::string> enabled = enabled_;
        bool changed = false;

        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("packages", 5, flags, ImVec2(0.0f, height))) {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On");
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Version");
        ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.x);
        ImGui::TableHeadersRow();

        for (size_t row = 0; row < rows.size(); ++row) {
            const std::string& id = rows[row];
            const antares::s_package_info* package = Find_Package_(id);
            const bool on = row < enabled_.size();
            ImGui::PushID(id.c_str());
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            bool checked = on;
            ImGui::BeginDisabled(!on && (package == nullptr || !package->valid));
            if (ImGui::Checkbox("##on", &checked)) {
                if (checked) {
                    enabled.push_back(id);
                } else {
                    enabled.erase(std::find(enabled.begin(), enabled.end(), id));
                }
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (on) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%zu", row + 1);
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            std::string label = package != nullptr && package->valid ? package->manifest.name : id;
            if (ImGui::Selectable(label.c_str(), selected_package_ == id, ImGuiSelectableFlags_AllowOverlap)) {
                selected_package_ = id;
            }
            if (on && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(PAYLOAD_TYPE, &row, sizeof(row));
                ImGui::Text("Move %s", label.c_str());
                ImGui::EndDragDropSource();
            }
            if (on && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PAYLOAD_TYPE)) {
                    const size_t from = *static_cast<const size_t*>(payload->Data);
                    if (from < enabled.size() && from != row) {
                        const std::string moved = enabled[from];
                        enabled.erase(enabled.begin() + static_cast<std::ptrdiff_t>(from));
                        enabled.insert(enabled.begin() + static_cast<std::ptrdiff_t>(row), moved);
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (package == nullptr) {
                ImGui::SameLine();
                ImGui::TextColored(COLOUR_ERROR, "not installed");
            } else if (!package->valid) {
                ImGui::SameLine();
                ImGui::TextColored(COLOUR_ERROR, "invalid");
            } else if (package->art_only) {
                ImGui::SameLine();
                ImGui::TextColored(COLOUR_DIM, "art");
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (package != nullptr && package->valid) {
                ImGui::TextColored(COLOUR_DIM, "%s", package->manifest.version_text.c_str());
            }

            ImGui::TableNextColumn();
            if (on) {
                ImGui::BeginDisabled(row == 0);
                if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                    std::swap(enabled[row], enabled[row - 1]);
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(row + 1 >= enabled_.size());
                if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                    std::swap(enabled[row], enabled[row + 1]);
                    changed = true;
                }
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (changed) {
            Apply_Enabled_(enabled);
        }
    }

    void c_launcher::Draw_Package_Details_(float /*height*/) {
        const antares::s_package_info* package = Find_Package_(selected_package_);
        if (selected_package_.empty()) {
            ImGui::TextColored(COLOUR_DIM, "Select a package to see its details.");
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(COLOUR_DIM,
                "Tick a package to enable it in this profile. Packages higher in the list have "
                "priority: their files and settings win over those below. Drag a name or use the "
                "arrows to change the order.");
            ImGui::PopTextWrapPos();
            return;
        }
        ImGui::PushTextWrapPos(0.0f);
        if (package == nullptr) {
            ImGui::Text("%s", selected_package_.c_str());
            ImGui::TextColored(COLOUR_ERROR, "Not installed: there is no data/mods/%s/ or %s.zip.", selected_package_.c_str(), selected_package_.c_str());
            ImGui::PopTextWrapPos();
            return;
        }
        if (!package->valid) {
            ImGui::Text("%s", package->id.c_str());
            ImGui::TextColored(COLOUR_ERROR, "%s", package->error.c_str());
            ImGui::PopTextWrapPos();
            return;
        }
        const config::s_mod_manifest& manifest = package->manifest;
        ImGui::TextUnformatted(manifest.name.c_str());
        ImGui::TextColored(COLOUR_DIM, "%s  ·  version %s  ·  %s", manifest.id.c_str(), manifest.version_text.c_str(), package->archive ? "zip" : "folder");
        ImGui::Spacing();
        if (!manifest.description.empty()) {
            ImGui::TextUnformatted(manifest.description.c_str());
            ImGui::Spacing();
        }
        if (package->art_only) {
            ImGui::TextColored(COLOUR_DIM, "Art only: changes how the game looks, not how it plays. Saves don't record it.");
        } else {
            ImGui::TextColored(COLOUR_DIM, "Changes the rules: saves made with it record it and need it to load.");
        }
        ImGui::Spacing();
        const auto field = [](const char* name, const std::string& value) {
            if (value.empty()) return;
            ImGui::TextColored(COLOUR_DIM, "%s", name);
            ImGui::SameLine();
            ImGui::TextUnformatted(value.c_str());
        };
        field("Engine", manifest.engine_range);
        field("Antares", manifest.antares_range);
        std::vector<std::string> items;
        for (const config::s_mod_dependency& dependency : manifest.dependencies) items.push_back(dependency.id + " " + dependency.version);
        field("Requires", Join_(items));
        items.clear();
        for (const config::s_mod_dependency& dependency : manifest.optional_dependencies) items.push_back(dependency.id + " " + dependency.version);
        field("Uses if present", Join_(items));
        field("Conflicts with", Join_(manifest.conflicts));
        field("Loads after", Join_(manifest.load_after));
        ImGui::PopTextWrapPos();
    }

    void c_launcher::Draw_Check_() {
        ImGui::PushTextWrapPos(0.0f);
        if (check_.ok) {
            ImGui::TextColored(COLOUR_OK, "Ready.");
            ImGui::SameLine();
            if (check_.package_order.empty()) {
                ImGui::TextUnformatted("The game will start with no packages (the original rules and art).");
            } else {
                ImGui::Text("The game will load, highest priority first: %s", Join_(check_.package_order).c_str());
                std::vector<std::string> added;
                for (const std::string& id : check_.package_order) {
                    if (std::find(enabled_.begin(), enabled_.end(), id) == enabled_.end()) added.push_back(id);
                }
                if (!added.empty()) {
                    ImGui::TextColored(COLOUR_DIM, "Added by dependencies or an included file: %s", Join_(added).c_str());
                }
            }
        } else {
            ImGui::TextColored(COLOUR_ERROR, "The game would refuse this profile:");
            ImGui::SameLine();
            ImGui::TextUnformatted(check_.error.c_str());
        }
        if (load_save_ > 0) {
            const s_save_row* row = Find_Save_(load_save_);
            const std::string problem = row != nullptr ? Save_Problem_(*row) : "";
            if (!problem.empty()) {
                ImGui::TextColored(COLOUR_ERROR, "Save %d won't load:", load_save_);
                ImGui::SameLine();
                ImGui::TextUnformatted(problem.c_str());
            }
        }
        ImGui::PopTextWrapPos();
        if (!message_.empty()) {
            ImGui::TextColored(message_is_error_ ? COLOUR_ERROR : COLOUR_DIM, "%s", message_.c_str());
        }
    }

    void c_launcher::Draw_Launch_Bar_() {
        ImGui::Checkbox("Skip intro", &skip_intro_);
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Continue");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
        Draw_Continue_Combo_();
        Help_Marker_("Main menu starts normally. A save loads it straight away (-load-save); the Saves tab "
                     "shows what each one needs. Slot 10 is the autosave.");
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
        ImGui::Checkbox("Close launcher", &close_on_launch_);

        const float button_width = ImGui::GetFontSize() * 8.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - button_width);
        const s_save_row* save = Find_Save_(load_save_);
        const bool save_fails = load_save_ > 0 && (save == nullptr || !Save_Problem_(*save).empty());
        ImGui::BeginDisabled(!check_.ok || save_fails || game_ != nullptr);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.62f, 0.35f, 1.0f));
        if (ImGui::Button(game_ != nullptr ? "Running" : "Play", ImVec2(button_width, 0.0f))) {
            Launch_(load_save_);
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }
} // namespace launcher
