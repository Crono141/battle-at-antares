#include "pch.h"

#include "launcher/launcher_app.h"

#include <imgui.h>

#include <filesystem>

#include "antares/identity.h"
#include "antares/user_folder.h"

#if defined(_WIN32)
#include <windows.h>
#endif

// The setup wizard (WP5.11, D28): the first thing a new player sees. Plain words, one decision per
// page, nothing to type. The file work is antares::game_install.
namespace launcher {
    namespace gi = antares::game_install;

    namespace {
        const ImVec4 COLOUR_OK(0.45f, 0.85f, 0.50f, 1.0f);
        const ImVec4 COLOUR_ERROR(1.0f, 0.45f, 0.40f, 1.0f);
        const ImVec4 COLOUR_WARN(1.0f, 0.75f, 0.35f, 1.0f);
        const ImVec4 COLOUR_DIM(0.62f, 0.64f, 0.70f, 1.0f);

        std::filesystem::path U8_(const std::string& text) {
            return std::filesystem::path(std::u8string(text.begin(), text.end()));
        }

        std::string Megabytes_(uint64_t bytes) {
            if (bytes >= (uint64_t(1) << 40)) return std::to_string(bytes >> 40) + " TB";
            if (bytes >= (uint64_t(1) << 30)) {
                char text[32];
                snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / static_cast<double>(uint64_t(1) << 30));
                return text;
            }
            return std::to_string((bytes + (1 << 20) - 1) >> 20) + " MB";
        }

        std::string Join_Names_(const std::vector<std::string>& names, size_t shown = 6) {
            std::string text;
            for (size_t index = 0; index < names.size() && index < shown; ++index) text += (index == 0 ? "" : ", ") + names[index];
            if (names.size() > shown) text += " and " + std::to_string(names.size() - shown) + " more";
            return text;
        }

        // One line of the checklist: a coloured word, then the text.
        void Check_Line_(bool ok, const char* ok_word, const char* bad_word, const std::string& text, bool warning = false) {
            const float column = ImGui::GetFontSize() * 6.0f;
            ImGui::TextColored(ok ? COLOUR_OK : (warning ? COLOUR_WARN : COLOUR_ERROR), "%s", ok ? ok_word : bad_word);
            ImGui::SameLine(column);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
        }

        bool Big_Button_(const char* label, bool enabled = true) {
            ImGui::BeginDisabled(!enabled);
            const bool pressed = ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 10.0f, ImGui::GetFrameHeight() * 1.3f));
            ImGui::EndDisabled();
            return pressed;
        }

        // The buttons sit on the window's bottom edge, the main one on the right.
        void Bottom_Bar_() {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float bar_top = ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight() * 1.3f - style.ItemSpacing.y;
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), bar_top - style.ItemSpacing.y));
            ImGui::Separator();
        }

        void Right_Align_(int buttons) {
            const float width = ImGui::GetFontSize() * 10.0f * static_cast<float>(buttons) + ImGui::GetStyle().ItemSpacing.x * static_cast<float>(buttons - 1);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - width);
        }

#if defined(_WIN32)
        std::string Registry_String_(HKEY root, const wchar_t* key, const wchar_t* value) {
            wchar_t buffer[1024];
            DWORD size = sizeof(buffer);
            if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return "";
            const int length = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            std::string text(length > 0 ? length - 1 : 0, '\0');
            if (length > 1) WideCharToMultiByte(CP_UTF8, 0, buffer, -1, text.data(), length, nullptr, nullptr);
            return text;
        }

        // GOG writes each game's folder under its product key; Steam its own folder.
        std::vector<gi::s_root> Registry_Roots_() {
            std::vector<gi::s_root> roots;
            for (const wchar_t* key : {L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1207661633", L"SOFTWARE\\GOG.com\\Games\\1207661633"}) {
                const std::string path = Registry_String_(HKEY_LOCAL_MACHINE, key, L"path");
                if (!path.empty()) roots.push_back({path, "GOG", 1});
            }
            const std::string steam = Registry_String_(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
            if (!steam.empty()) roots.push_back({steam, "Steam", 0});
            return roots;
        }
#endif

        std::string Home_() {
#if defined(_WIN32)
            const char* profile = SDL_getenv("USERPROFILE");
            return profile != nullptr ? profile : "";
#else
            const char* home = SDL_getenv("HOME");
            return home != nullptr ? home : "";
#endif
        }
    } // namespace

    void c_launcher::Open_Setup(const std::string& folder) {
        Setup_Close_();
        setup_.active = true;
        setup_.required = !files_.ok;
        setup_.target = gi::Target_Folder(game_dir_);
        setup_.page = SETUP_WELCOME;
        if (folder == "find") {
            Setup_Find_();
        } else if (!folder.empty()) {
            Setup_Check_(folder);
        }
    }

    void c_launcher::Setup_Close_() {
        if (setup_.worker.joinable()) {
            if (setup_.progress) setup_.progress->cancel = true;
            setup_.worker.join();
        }
        setup_.active = false;
        setup_.candidates.clear();
        setup_.chosen = -1;
        setup_.progress.reset();
        setup_.saves.clear();
        setup_.imported = -1;
        setup_.import_error.clear();
    }

    void c_launcher::Setup_Find_() {
        std::vector<gi::s_root> registry;
#if defined(_WIN32)
        registry = Registry_Roots_();
#endif
        const auto start = SDL_GetTicks();
        setup_.candidates = gi::Detect(gi::Detect_Roots(Home_(), registry));
        // The folder the files would go to is no source.
        const std::string target = setup_.target;
        std::erase_if(setup_.candidates, [&](const gi::s_candidate& candidate) {
            std::error_code error;
            return std::filesystem::equivalent(U8_(candidate.path), U8_(target), error);
        });
        setup_.chosen = setup_.candidates.empty() ? -1 : 0;
        SDL_Log("setup: %zu copies found in %llu ms", setup_.candidates.size(), static_cast<unsigned long long>(SDL_GetTicks() - start));
        setup_.page = SETUP_FIND;
    }

    void c_launcher::Setup_Check_(const std::string& folder) {
        setup_.check = gi::Check_Folder(folder);
        std::error_code error;
        std::filesystem::path probe = U8_(setup_.target);
        while (!probe.empty() && !std::filesystem::exists(probe, error)) probe = probe.parent_path();
        const std::filesystem::space_info space = std::filesystem::space(probe.empty() ? std::filesystem::path(".") : probe, error);
        setup_.free_bytes = error ? 0 : space.available;
        setup_.page = SETUP_CHECK;
        SDL_Log("setup: checked %s: %zu found, %zu missing, %zu empty, %s", setup_.check.folder.c_str(), setup_.check.found, setup_.check.missing.size(), setup_.check.empty.size(), setup_.check.patched ? "1.31" : "not 1.31");
    }

    void c_launcher::Setup_Start_Copy_() {
        setup_.progress = std::make_unique<gi::s_copy_progress>();
        setup_.page = SETUP_COPY;
        gi::s_copy_progress* progress = setup_.progress.get();
        const gi::s_folder_check check = setup_.check;
        const std::string target = setup_.target;
        setup_.worker = std::thread([check, target, progress]() { gi::Copy_Files(check, target, progress); });
    }

    void c_launcher::Setup_After_Copy_() {
        if (setup_.worker.joinable()) setup_.worker.join();
        Check_Files_();
        setup_.saves = gi::Find_Saves(setup_.check.folder, gi::Virtual_Store_For(setup_.check.folder), antares::user_folder::Saves_Directory(game_dir_));
        setup_.page = setup_.saves.empty() ? SETUP_DONE : SETUP_SAVES;
        SDL_Log("setup: copied to %s; %zu saves found; files %s", setup_.target.c_str(), setup_.saves.size(), files_.ok ? "ok" : "still missing");
    }

    void c_launcher::Setup_Import_() {
        setup_.import_error.clear();
        setup_.imported = gi::Import_Saves(setup_.saves, antares::user_folder::Saves_Directory(game_dir_), setup_.replace_taken, &setup_.import_error);
        saves_dirty_ = true;
        setup_.page = SETUP_DONE;
    }

    bool c_launcher::Run_Setup(const std::string& folder, bool stop_at_saves) {
        Open_Setup(folder);
        if (!setup_.check.ok()) return false;
        gi::s_copy_progress progress;
        if (!gi::Copy_Files(setup_.check, setup_.target, &progress)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "setup: %s", progress.error.c_str());
            return false;
        }
        Setup_After_Copy_();
        if (setup_.page == SETUP_SAVES && !stop_at_saves) Setup_Import_();
        return files_.ok;
    }

    void c_launcher::Setup_Poll_() {
        if (!setup_.active) return;
        if (setup_.page == SETUP_COPY && setup_.progress && setup_.progress->finished && !setup_.progress->failed) {
            Setup_After_Copy_();
        }
        std::string folder;
        {
            std::lock_guard<std::mutex> lock(setup_.dialog_mutex);
            if (setup_.dialog_answered) {
                setup_.dialog_answered = false;
                folder = setup_.dialog_folder;
            }
        }
        if (!folder.empty()) Setup_Check_(folder);
    }

    void c_launcher::Draw_Setup_() {
        const float wrap = ImGui::GetFontSize() * 40.0f;
        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.35f);
        const char* titles[] = {"Welcome to Battle at Antares", "Where is your Master of Orion II?", "Get Master of Orion II", "Checking your copy", "Copying the game files", "Your saved games", "All set"};
        ImGui::TextUnformatted(titles[setup_.page]);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(wrap);

        switch (setup_.page) {
            case SETUP_WELCOME: {
                ImGui::TextUnformatted("Battle at Antares plays with the art, sound and text of your own copy of Master of Orion II "
                                       "(version 1.31, as sold by GOG and Steam).");
                ImGui::Spacing();
                ImGui::TextUnformatted("This takes a minute: Battle at Antares finds your copy and copies its game files (about 300 MB) "
                                       "into its own folder. Your Master of Orion II stays exactly as it is, and you can keep "
                                       "playing it or uninstall it later.");
                ImGui::Spacing();
                ImGui::TextColored(COLOUR_DIM, "The files go to %s", setup_.target.c_str());
                ImGui::PopTextWrapPos();
                Bottom_Bar_();
                if (!setup_.required && Big_Button_("Cancel")) Setup_Close_();
                Right_Align_(1);
                if (Big_Button_("Find my copy")) Setup_Find_();
                return;
            }
            case SETUP_FIND: {
                if (setup_.candidates.empty()) {
                    ImGui::TextUnformatted("Battle at Antares didn't find it in the usual places (Steam, GOG, GOG Galaxy, Heroic, Lutris). "
                                           "Choose its folder: the one with ORION95.EXE and the .LBX files.");
                } else {
                    ImGui::TextUnformatted(setup_.candidates.size() == 1 ? "Found it. Is this the one?" : "Found these. Pick the one to use:");
                    ImGui::Spacing();
                    for (size_t index = 0; index < setup_.candidates.size(); ++index) {
                        const gi::s_candidate& candidate = setup_.candidates[index];
                        ImGui::PushID(static_cast<int>(index));
                        const size_t needed = antares::game_files::Required_Files().size();
                        const std::string label = candidate.source + ", " + std::to_string(candidate.found) + " of " + std::to_string(needed) + " files";
                        if (ImGui::RadioButton(label.c_str(), setup_.chosen == static_cast<int>(index))) setup_.chosen = static_cast<int>(index);
                        ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
                        ImGui::TextColored(candidate.found == needed ? COLOUR_DIM : COLOUR_WARN, "%s", candidate.path.c_str());
                        ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
                        ImGui::Spacing();
                        ImGui::PopID();
                    }
                }
                ImGui::Spacing();
                if (ImGui::Button("Choose a folder...")) {
                    const SDL_DialogFileCallback callback = [](void* user, const char* const* list, int) {
                        auto* setup = static_cast<s_setup*>(user);
                        if (list == nullptr || list[0] == nullptr) return; // cancelled or failed
                        std::lock_guard<std::mutex> lock(setup->dialog_mutex);
                        setup->dialog_folder = list[0];
                        setup->dialog_answered = true;
                    };
                    SDL_ShowOpenFolderDialog(callback, &setup_, SDL_GetKeyboardFocus(), nullptr, false);
                }
                ImGui::SameLine();
                if (ImGui::Button("I don't have it yet")) setup_.page = SETUP_GET;
                ImGui::PopTextWrapPos();
                Bottom_Bar_();
                if (Big_Button_("Back")) setup_.page = SETUP_WELCOME;
                Right_Align_(1);
                if (Big_Button_("Next", setup_.chosen >= 0)) Setup_Check_(setup_.candidates[setup_.chosen].path);
                return;
            }
            case SETUP_GET: {
                ImGui::TextUnformatted("Master of Orion II is sold by GOG and Steam. Buy it there and install it with GOG Galaxy, "
                                       "GOG's installer or Steam, then come back here and press Find again.");
                ImGui::Spacing();
                ImGui::TextUnformatted("Battle at Antares never asks for your GOG or Steam password: you sign in to their own "
                                       "website or app.");
                ImGui::Spacing();
                if (ImGui::Button("Open GOG's page")) (void)SDL_OpenURL("https://www.gog.com/game/master_of_orion_1_2");
                ImGui::SameLine();
                if (ImGui::Button("Open Steam's page")) (void)SDL_OpenURL("https://store.steampowered.com/search/?term=Master+of+Orion+2");
                ImGui::PopTextWrapPos();
                Bottom_Bar_();
                if (Big_Button_("Back")) setup_.page = SETUP_FIND;
                Right_Align_(1);
                if (Big_Button_("Find again")) Setup_Find_();
                return;
            }
            case SETUP_CHECK: {
                const gi::s_folder_check& check = setup_.check;
                const size_t needed = antares::game_files::Required_Files().size();
                ImGui::TextColored(COLOUR_DIM, "%s", check.folder.c_str());
                ImGui::Spacing();
                if (!check.exists) {
                    Check_Line_(false, "", "Not found", "This folder doesn't exist any more.");
                } else {
                    Check_Line_(check.missing.empty(), "Found", "Missing", check.missing.empty() ? "All " + std::to_string(needed) + " game files are there." : std::to_string(check.missing.size()) + " of the " + std::to_string(needed) + " game files aren't in this folder: " + Join_Names_(check.missing) + ". Is it the right folder? It's the one with ORION95.EXE and the .LBX files.");
                    Check_Line_(check.patched, "1.31", "Version", check.patched ? "Version 1.31, as the game needs." : "This copy isn't version 1.31 (PATCH13.LBX is missing). Install the 1.31 patch, or use the GOG "
                                                                                                                       "or Steam version.");
                    if (!check.empty.empty()) {
                        Check_Line_(false, "", "Damaged", "These files are empty, so this copy is broken: " + Join_Names_(check.empty) + ". Reinstall Master of Orion II, or verify it in Steam or GOG Galaxy.");
                    }
                    std::error_code error;
                    const bool same = std::filesystem::equivalent(U8_(check.folder), U8_(setup_.target), error);
                    if (same) {
                        Check_Line_(true, "In place", "", "These files are already where Battle at Antares reads them.");
                    } else {
                        Check_Line_(setup_.free_bytes >= check.bytes || setup_.free_bytes == 0, "Room", "No room", "Copying needs " + Megabytes_(check.bytes) + (setup_.free_bytes != 0 ? "; " + Megabytes_(setup_.free_bytes) + " free." : "."));
                    }
                }
                ImGui::PopTextWrapPos();
                Bottom_Bar_();
                if (Big_Button_("Back")) Setup_Find_();
                Right_Align_(1);
                const bool room = setup_.free_bytes == 0 || setup_.free_bytes >= check.bytes;
                if (Big_Button_("Copy the files", check.ok() && room)) Setup_Start_Copy_();
                return;
            }
            case SETUP_COPY: {
                gi::s_copy_progress& progress = *setup_.progress;
                const uint64_t total = std::max<uint64_t>(progress.bytes_total, 1);
                const float fraction = static_cast<float>(static_cast<double>(progress.bytes_done) / static_cast<double>(total));
                std::string current, error;
                {
                    std::lock_guard<std::mutex> lock(progress.mutex);
                    current = progress.current;
                    error = progress.error;
                }
                const std::string label = Megabytes_(progress.bytes_done) + " of " + Megabytes_(progress.bytes_total);
                ImGui::ProgressBar(fraction, ImVec2(wrap, 0.0f), label.c_str());
                if (progress.failed) {
                    ImGui::TextColored(COLOUR_ERROR, "%s", error.c_str());
                } else {
                    ImGui::TextColored(COLOUR_DIM, "%s", current.c_str());
                }
                ImGui::PopTextWrapPos();
                Bottom_Bar_();
                if (progress.failed) {
                    if (setup_.worker.joinable()) setup_.worker.join();
                    if (Big_Button_("Back")) setup_.page = SETUP_CHECK;
                    Right_Align_(1);
                    if (Big_Button_("Try again")) Setup_Start_Copy_();
                } else if (Big_Button_("Cancel")) {
                    progress.cancel = true;
                }
                return;
            }
            case SETUP_SAVES: {
                std::string taken;
                for (const gi::s_found_save& save : setup_.saves) {
                    if (save.taken) taken += (taken.empty() ? "" : ", ") + std::to_string(save.slot);
                }
                ImGui::Text("Your Master of Orion II has %zu saved game%s. Battle at Antares can continue them.", setup_.saves.size(), setup_.saves.size() == 1 ? "" : "s");
                ImGui::Spacing();
                if (ImGui::BeginTable("saves", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV, ImVec2(wrap, 0.0f))) {
                    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.0f);
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Stardate", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
                    ImGui::TableHeadersRow();
                    for (const gi::s_found_save& save : setup_.saves) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", save.slot);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(save.summary.readable ? save.summary.description.c_str() : "(can't be read)");
                        ImGui::TableNextColumn();
                        if (save.summary.readable) ImGui::Text("%d.%d", save.summary.stardate / 10, save.summary.stardate % 10);
                    }
                    ImGui::EndTable();
                }
                ImGui::Spacing();
                ImGui::Checkbox("Bring them into Battle at Antares", &setup_.import_saves);
                if (!taken.empty() && setup_.import_saves) {
                    ImGui::Spacing();
                    ImGui::Text("Battle at Antares already has saves in slot%s %s. For those:", taken.find(',') != std::string::npos ? "s" : "", taken.c_str());
                    if (ImGui::RadioButton("Keep Battle at Antares's saves", !setup_.replace_taken)) setup_.replace_taken = false;
                    if (ImGui::RadioButton("Use the original game's saves", setup_.replace_taken)) setup_.replace_taken = true;
                }
                ImGui::PopTextWrapPos();
                Bottom_Bar_();
                Right_Align_(1);
                if (Big_Button_("Next")) {
                    if (setup_.import_saves) {
                        Setup_Import_();
                    } else {
                        setup_.page = SETUP_DONE;
                    }
                }
                return;
            }
            case SETUP_DONE: {
                if (files_.ok) {
                    ImGui::TextUnformatted("Battle at Antares has everything it needs.");
                    ImGui::Spacing();
                    ImGui::TextColored(COLOUR_DIM, "The game files are in %s", setup_.target.c_str());
                } else {
                    ImGui::TextColored(COLOUR_ERROR, "Something is still missing:");
                    ImGui::TextUnformatted(antares::game_files::Describe(files_).c_str());
                }
                if (setup_.imported > 0) ImGui::Text("Imported %d saved game%s.", setup_.imported, setup_.imported == 1 ? "" : "s");
                if (!setup_.import_error.empty()) ImGui::TextColored(COLOUR_ERROR, "%s", setup_.import_error.c_str());
                ImGui::PopTextWrapPos();
                Bottom_Bar_();
                if (!files_.ok) {
                    if (Big_Button_("Back")) Setup_Find_();
                    return;
                }
                if (Big_Button_("Open the launcher")) Setup_Close_();
                Right_Align_(1);
                if (Big_Button_("Play")) {
                    Setup_Close_();
                    Launch_(0);
                }
                return;
            }
        }
        ImGui::PopTextWrapPos();
    }
} // namespace launcher
