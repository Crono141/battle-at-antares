#include "pch.h"
#include "antares/utf8_files.h"

#include "launcher/launcher_app.h"
#include "launcher/launcher_menu.h"

#include <imgui.h>

#include <filesystem>

#include "antares/identity.h"
#include "antares/user_folder.h"
#include "antares/gog.h"
#include "launcher/launcher_chrome.h"

#if defined(_WIN32)
#include <windows.h>
#endif

// The setup wizard (WP5.11, D28): the first thing a new player sees. Plain words, one decision per
// page, nothing to type. The file work is antares::game_install.
namespace launcher {
    namespace gi = antares::game_install;

    namespace {
        namespace ch = launcher::chrome;

        // A folder as the player's system writes it: backslashes on Windows (the user folder is
        // built with '/', which Windows accepts but players don't write).
        std::string Shown_(std::string path) {
#ifdef _WIN32
            for (char& c : path) {
                if (c == '/') c = '\\';
            }
#endif
            return path;
        }

        std::filesystem::path U8_(const std::string& text) {
            return antares::utf8::Path(text);
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

        std::string Upper_(std::string text) {
            for (char& character : text) character = static_cast<char>(SDL_toupper(static_cast<unsigned char>(character)));
            return text;
        }

        ImU32 U32_(const ImVec4& colour, float alpha = 1.0f) {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(colour.x, colour.y, colour.z, colour.w * alpha));
        }

        constexpr float BODY = 21.0f; // ImGui sizes by line height: Saira's tall lines draw small
        constexpr float SMALL = 18.0f;

        // Wrapped body text in a colour.
        void Text_(const std::string& text, const ImVec4& colour = ch::LIGHT, float size = BODY) {
            ImGui::PushFont(ch::Body_Font(), size);
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        // A dark plate: the cards, checklist rows and the saves list sit on these.
        void Plate_(ImDrawList* draw, ImVec2 min, ImVec2 max, bool selected) {
            draw->AddRectFilled(min, max, IM_COL32(10, 11, 14, 195));
            if (selected) {
                draw->AddRect(ImVec2(min.x + 2, min.y + 2), ImVec2(max.x - 2, max.y - 2), U32_(ch::AMBER, 0.18f), 0.0f, 0, 4.0f);
            }
            draw->AddRect(min, max, selected ? U32_(ch::AMBER) : IM_COL32(59, 63, 72, 255));
        }

        // A copy found: its store, whether it's complete, the path. True when clicked.
        bool Card_(int index, bool selected, const gi::s_candidate& candidate, size_t needed) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float width = ImGui::GetContentRegionAvail().x - 1.0f; // the border stays inside the clip
            const float height = 72.0f;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + width, min.y + height);
            ImGui::PushID(index);
            const bool pressed = ImGui::InvisibleButton("card", ImVec2(width, height));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            Plate_(draw, min, max, selected);
            if (hovered && !selected) draw->AddRect(min, max, U32_(ch::GOLD, 0.5f));
            // The radio square.
            const ImVec2 box(min.x + 14, min.y + 16);
            draw->AddRect(box, ImVec2(box.x + 18, box.y + 18), IM_COL32(106, 110, 120, 255));
            if (selected) draw->AddRectFilled(ImVec2(box.x + 5, box.y + 5), ImVec2(box.x + 13, box.y + 13), U32_(ch::AMBER));
            // The store's tag, gold.
            float x = min.x + 46;
            const std::string tag = candidate.source;
            const ImVec2 tag_size = ch::Title_Font()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, tag.c_str());
            draw->AddRectFilled(ImVec2(x, min.y + 14), ImVec2(x + tag_size.x + 14, min.y + 34), U32_(ch::GOLD));
            draw->AddText(ch::Title_Font(), 14.0f, ImVec2(x + 7, min.y + 16), IM_COL32(11, 12, 16, 255), tag.c_str());
            x += tag_size.x + 24;
            const bool full = candidate.installer || candidate.found == needed;
            const char* state = candidate.installer ? "Installer" : full ? "Complete"
                                                                         : "Incomplete";
            draw->AddText(ch::Bold_Font(), BODY, ImVec2(x, min.y + 10), U32_(full ? ch::LIGHT : ch::AMBER), state);
            x += ch::Bold_Font()->CalcTextSizeA(BODY, FLT_MAX, 0.0f, state).x + 10;
            const std::string count = candidate.installer ? Megabytes_(candidate.size) + ", in your Downloads: extracted, never run"
                                                          : std::to_string(candidate.found) + " of " + std::to_string(needed) + " files";
            draw->AddText(ch::Body_Font(), SMALL, ImVec2(x, min.y + 12), U32_(full ? ch::DIM : ch::AMBER), count.c_str());
            draw->PushClipRect(min, max, true);
            draw->AddText(ch::Body_Font(), SMALL, ImVec2(min.x + 46, min.y + 41), U32_(ch::DIM, 0.85f), Shown_(candidate.path).c_str());
            draw->PopClipRect();
            ImGui::Dummy(ImVec2(0, 4));
            return pressed;
        }

        // One checklist row: a box with a tick, cross or warning, a title and what it means.
        void Check_Row_(bool ok, bool warning, const char* title, const std::string& text) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float width = ImGui::GetContentRegionAvail().x - 2.0f;
            const float text_width = width - 70.0f;
            const ImVec2 text_size = ch::Body_Font()->CalcTextSizeA(SMALL, FLT_MAX, text_width, text.c_str());
            const float height = 46.0f + text_size.y;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + width, min.y + height);
            Plate_(draw, min, max, false);
            ch::Check_Box(draw, ImVec2(min.x + 14, min.y + 14), 28.0f, ok, warning);
            draw->AddText(ch::Bold_Font(), BODY, ImVec2(min.x + 58, min.y + 8), U32_(ch::LIGHT), title);
            draw->AddText(ch::Body_Font(), SMALL, ImVec2(min.x + 58, min.y + 34), U32_(ch::DIM), text.c_str(), nullptr, text_width);
            ImGui::Dummy(ImVec2(width, height));
            ImGui::Dummy(ImVec2(0, 2));
        }

        // A gold, underlined link. True when clicked.
        bool Link_(const char* label) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 size = ch::Body_Font()->CalcTextSizeA(BODY, FLT_MAX, 0.0f, label);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton(label, size);
            const ImU32 colour = U32_(ch::GOLD, ImGui::IsItemHovered() ? 1.0f : 0.85f);
            draw->AddText(ch::Body_Font(), BODY, at, colour, label);
            draw->AddLine(ImVec2(at.x, at.y + size.y), ImVec2(at.x + size.x, at.y + size.y), colour);
            return pressed;
        }

        // The steps along the top: FIND, CHECK, COPY, SAVES, DONE (`current` -1: none yet).
        void Steps_(ImDrawList* draw, float centre_x, float y, int current) {
            const char* names[] = {"FIND", "CHECK", "COPY", "SAVES", "HD ART", "DONE"};
            constexpr int COUNT = 6;
            const float font = 13.0f, box = 22.0f, gap = 7.0f, bar = 28.0f, bar_gap = 8.0f;
            float total = 0.0f;
            for (int index = 0; index < COUNT; ++index) {
                total += box + gap + ch::Title_Font()->CalcTextSizeA(font, FLT_MAX, 0.0f, names[index]).x;
                if (index < COUNT - 1) total += bar + bar_gap * 2;
            }
            float x = centre_x - total * 0.5f;
            for (int index = 0; index < COUNT; ++index) {
                const bool done = index < current, now = index == current;
                const ImVec4 colour = now ? ch::AMBER : done ? ch::DIM
                                                             : ImVec4(120 / 255.0f, 122 / 255.0f, 130 / 255.0f, 1.0f);
                const ImVec2 min(x, y), max(x + box, y + box);
                draw->AddRectFilled(min, max, IM_COL32(34, 37, 44, 255), 3.0f);
                if (now) draw->AddRect(ImVec2(min.x - 2, min.y - 2), ImVec2(max.x + 2, max.y + 2), U32_(ch::AMBER, 0.3f), 4.0f, 0, 3.0f);
                draw->AddRect(min, max, now ? U32_(ch::AMBER) : IM_COL32(74, 78, 88, 255), 3.0f);
                if (done) {
                    const ImVec2 points[3] = {ImVec2(x + 6, y + 11.5f), ImVec2(x + 9.5f, y + 15), ImVec2(x + 16, y + 7.5f)};
                    draw->AddPolyline(points, 3, U32_(ch::GOLD), ImDrawFlags_None, 1.8f);
                } else {
                    const std::string number = std::to_string(index + 1);
                    const ImVec2 size = ch::Title_Font()->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, number.c_str());
                    draw->AddText(ch::Title_Font(), 11.0f, ImVec2(x + (box - size.x) * 0.5f, y + (box - size.y) * 0.5f), U32_(colour), number.c_str());
                }
                x += box + gap;
                const ImVec2 label = ch::Title_Font()->CalcTextSizeA(font, FLT_MAX, 0.0f, names[index]);
                draw->AddText(ch::Title_Font(), font, ImVec2(x, y + (box - label.y) * 0.5f), U32_(colour), names[index]);
                x += label.x;
                if (index < COUNT - 1) {
                    draw->AddLine(ImVec2(x + bar_gap, y + box * 0.5f), ImVec2(x + bar_gap + bar, y + box * 0.5f), IM_COL32(58, 61, 69, 255), 2.0f);
                    x += bar + bar_gap * 2;
                }
            }
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

#if defined(_WIN32)
        // GOG Galaxy's program, when it is installed (it has no link to open a game's page).
        std::string Galaxy_Client_() {
            const std::string folder = Registry_String_(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\GalaxyClient\\paths", L"client");
            const std::string program = Registry_String_(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\GalaxyClient", L"clientExecutable");
            return folder.empty() || program.empty() ? "" : folder + "\\" + program;
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
        std::vector<std::string> Steam_Libraries_() {
            std::vector<gi::s_root> registry;
#if defined(_WIN32)
            registry = Registry_Roots_();
#endif
            return gi::Steam_Libraries(Home_(), registry);
        }
    } // namespace

    void c_launcher::Open_Setup(const std::string& folder) {
        Setup_Close_();
        setup_.active = true;
        setup_.required = !files_.ok;
        setup_.target = gi::Target_Folder(game_dir_);
        setup_.page = SETUP_WELCOME;
        if (folder == "get") {
            setup_.page = SETUP_GET;
        } else if (folder == "wait-steam" || folder == "wait-gog") {
            Setup_Wait_(folder == "wait-steam" ? "Steam" : "GOG");
        } else if (folder == "hd") {
            setup_.page = SETUP_HD;
        } else if (folder == "find") {
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
        // GOG installers waiting in Downloads (D29 case 2), after the installed copies.
        const std::string downloads = gi::Downloads_Folder();
#if defined(_WIN32)
        const gi::e_installer_kind native = gi::INSTALLER_WINDOWS;
#elif defined(__APPLE__)
        const gi::e_installer_kind native = gi::INSTALLER_MAC;
#else
        const gi::e_installer_kind native = gi::INSTALLER_LINUX;
#endif
        std::vector<gi::s_installer> installers = downloads.empty() ? std::vector<gi::s_installer>{} : gi::Find_Installers(downloads);
        std::stable_partition(installers.begin(), installers.end(), [&](const gi::s_installer& installer) { return installer.kind == native; }); // this system's first
        for (const gi::s_installer& installer : installers) {
            gi::s_candidate candidate;
            candidate.path = installer.path;
            candidate.source = installer.kind == gi::INSTALLER_LINUX ? "GOG for Linux" : installer.kind == gi::INSTALLER_WINDOWS ? "GOG for Windows"
                                                                                                                                 : "GOG for Mac";
            candidate.installer = true;
            candidate.size = installer.size;
            setup_.candidates.push_back(candidate);
        }
        setup_.chosen = setup_.candidates.empty() ? -1 : 0;
        SDL_Log("setup: %zu copies found in %llu ms", setup_.candidates.size(), static_cast<unsigned long long>(SDL_GetTicks() - start));
        setup_.page = SETUP_FIND;
    }

    void c_launcher::Setup_Check_(const std::string& folder) {
        std::error_code type_error;
        setup_.check_error.clear();
        if (std::filesystem::is_regular_file(U8_(folder), type_error)) {
            setup_.check = gi::Check_Installer(folder, &setup_.check_error); // a GOG installer (D29 case 2)
        } else {
            setup_.check = gi::Check_Folder(folder);
        }
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
        setup_.worker = std::thread([check, target, progress]() {
            if (check.installer != gi::INSTALLER_NONE) {
                gi::Extract_Installer(check, target, progress);
            } else {
                gi::Copy_Files(check, target, progress);
            }
        });
    }

    void c_launcher::Setup_After_Copy_() {
        if (setup_.worker.joinable()) setup_.worker.join();
        Check_Files_();
        setup_.saves.clear(); // an installer brings no saves
        if (setup_.check.installer == gi::INSTALLER_NONE) {
            setup_.saves = gi::Find_Saves(setup_.check.folder, gi::Virtual_Store_For(setup_.check.folder), antares::user_folder::Saves_Directory(game_dir_));
        }
        setup_.page = setup_.saves.empty() ? SETUP_HD : SETUP_SAVES;
        SDL_Log("setup: copied to %s; %zu saves found; files %s", setup_.target.c_str(), setup_.saves.size(), files_.ok ? "ok" : "still missing");
    }

    void c_launcher::Setup_Import_() {
        setup_.import_error.clear();
        setup_.imported = gi::Import_Saves(setup_.saves, antares::user_folder::Saves_Directory(game_dir_), setup_.replace_taken, &setup_.import_error);
        saves_dirty_ = true;
        setup_.page = SETUP_HD;
    }

    bool c_launcher::Start_Setup_Copy() {
        if (!setup_.active || setup_.page != SETUP_CHECK || !setup_.check.ok()) return false;
        Setup_Start_Copy_();
        return true;
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
        if (setup_.page == SETUP_HD) setup_.page = SETUP_DONE; // headless: no downloads
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
        if (setup_.page == SETUP_WAIT) Setup_Wait_Poll_();
        Gog_Poll_();
    }

    void c_launcher::Setup_Wait_(const std::string& store) {
        setup_.wait_store = store;
        setup_.wait_next_poll = 0;
        setup_.steam = {};
        setup_.page = SETUP_WAIT;
        SDL_Log("setup: waiting for %s to install the game", store.c_str());
    }

    // Every 2 s: is a complete copy there now (Steam, GOG, Heroic, Lutris, anywhere Find looks)?
    // A Steam copy also waits for Steam to say the install is finished, so a file still being
    // written is never copied.
    void c_launcher::Setup_Wait_Poll_() {
        const uint64_t now = SDL_GetTicks();
        if (now < setup_.wait_next_poll) return;
        setup_.wait_next_poll = now + 2000;
        setup_.steam = gi::Steam_App(Steam_Libraries_(), gi::STEAM_APP_ID);
        std::vector<gi::s_root> registry;
#if defined(_WIN32)
        registry = Registry_Roots_();
#endif
        const size_t needed = antares::game_files::Required_Files().size();
        // A GOG installer that just finished downloading (browsers write to a temporary name and
        // rename at the end, so a file with the installer's name is complete), for this system.
        if (setup_.wait_store == "GOG") {
            const std::string downloads = gi::Downloads_Folder();
            for (const gi::s_installer& installer : downloads.empty() ? std::vector<gi::s_installer>{} : gi::Find_Installers(downloads)) {
                std::string error;
                if (!gi::Check_Installer(installer.path, &error).ok()) continue;
                SDL_Log("setup: GOG installer appeared: %s", installer.path.c_str());
                Setup_Check_(installer.path);
                return;
            }
        }
        for (const gi::s_candidate& candidate : gi::Detect(gi::Detect_Roots(Home_(), registry))) {
            std::error_code error;
            if (candidate.found != needed || std::filesystem::equivalent(U8_(candidate.path), U8_(setup_.target), error)) continue;
            const bool in_steam = !setup_.steam.folder.empty() && candidate.path.rfind(setup_.steam.folder, 0) == 0;
            if (in_steam && !setup_.steam.installed) continue;
            SDL_Log("setup: %s copy appeared at %s", candidate.source.c_str(), candidate.path.c_str());
            Setup_Check_(candidate.path);
            return;
        }
    }

    void c_launcher::Draw_Setup_() {
        // Look B (WP5.11b): the Chrome metal behind everything, the page in the bezel, the theme's
        // buttons below it. The Done page's splash art waits for the owner's new title screen.
        ImDrawList* back = ImGui::GetWindowDrawList();
        const ImVec2 window_min = ImGui::GetWindowPos();
        const ImVec2 window_size = ImGui::GetWindowSize();
        const ImVec2 window_max(window_min.x + window_size.x, window_min.y + window_size.y);
        ch::Background(back, window_min, window_max);

        const float centre = window_min.x + window_size.x * 0.5f;
        const float column = std::min(720.0f, window_size.x - 80.0f);
        const float left = centre - column * 0.5f;
        const char* brand = "BATTLE AT ANTARES";
        const ImVec2 brand_size = ch::Title_Font()->CalcTextSizeA(25.0f, FLT_MAX, 0.0f, brand);
        ch::Shadowed_Text(back, ch::Title_Font(), 25.0f, ImVec2(centre - brand_size.x * 0.5f, window_min.y + 16), ch::GOLD, brand);
        const int step = setup_.page == SETUP_WELCOME                                                                        ? -1
            : setup_.page == SETUP_FIND || setup_.page == SETUP_GET || setup_.page == SETUP_WAIT || setup_.page == SETUP_GOG ? 0
            : setup_.page == SETUP_CHECK                                                                                     ? 1
            : setup_.page == SETUP_COPY                                                                                      ? 2
            : setup_.page == SETUP_SAVES                                                                                     ? 3
            : setup_.page == SETUP_HD                                                                                        ? 4
                                                                                                                             : 5;
        Steps_(back, centre, window_min.y + 58, step);

        const float bezel_top = window_min.y + 98;
        const float bezel_bottom = window_max.y - 104;
        ch::Bezel(back, ImVec2(left, bezel_top), ImVec2(left + column, bezel_bottom));

        // The page, inside the bezel.
        const char* titles[] = {"Welcome", "Where is your Master of Orion II?", "Get Master of Orion II", "Waiting for the store", "GOG", "Checking your copy", "Copying the game files", "Your saved games", "HD art (optional)", "All set"};
        ImGui::SetCursorScreenPos(ImVec2(left + 30, bezel_top + 22));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(10 / 255.0f, 11 / 255.0f, 14 / 255.0f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(40 / 255.0f, 36 / 255.0f, 30 / 255.0f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ch::AMBER);
        ImGui::PushStyleColor(ImGuiCol_Text, ch::LIGHT);
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(10 / 255.0f, 11 / 255.0f, 14 / 255.0f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(18 / 255.0f, 19 / 255.0f, 24 / 255.0f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(34 / 255.0f, 37 / 255.0f, 44 / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(50 / 255.0f, 44 / 255.0f, 34 / 255.0f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, ImVec4(10 / 255.0f, 11 / 255.0f, 14 / 255.0f, 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 0.0f)); // cards and rows reach the page's edges
        ImGui::BeginChild("page", ImVec2(column - 60, bezel_bottom - bezel_top - 44), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const std::string title = Upper_(setup_.page == SETUP_WAIT ? "Waiting for " + setup_.wait_store : std::string(titles[setup_.page]));
        const ImVec2 title_at = ImGui::GetCursorScreenPos();
        ch::Shadowed_Text(draw, ch::Title_Font(), 23.0f, title_at, ch::GOLD, title.c_str());
        ImGui::Dummy(ImVec2(0, 34));
        ImGui::PushFont(ch::Body_Font(), BODY);

        enum e_action { NONE,
            LEFT,
            RIGHT };
        const char* left_label = nullptr;
        const char* right_label = nullptr;
        bool right_enabled = true;
        const size_t needed = antares::game_files::Required_Files().size();

        switch (setup_.page) {
            case SETUP_WELCOME:
                Text_("Battle at Antares plays with the art, sound and text of your own copy of Master of Orion II "
                      "(version 1.31, as sold by GOG and Steam).");
                ImGui::Spacing();
                Text_("This takes a minute: Battle at Antares finds your copy and copies its game files (about 300 MB) into its "
                      "own folder. Your Master of Orion II stays exactly as it is, and you can keep playing it or uninstall it later.");
                ImGui::Spacing();
                Text_("The files go to " + Shown_(setup_.target), ch::DIM, SMALL);
                left_label = setup_.required ? nullptr : "Cancel";
                right_label = "Find my copy";
                break;
            case SETUP_FIND:
                if (setup_.candidates.empty()) {
                    Text_("Battle at Antares didn't find it in the usual places (Steam, GOG, GOG Galaxy, Heroic, Lutris). Choose its "
                          "folder: the one with ORION95.EXE and the .LBX files.");
                } else {
                    Text_(setup_.candidates.size() == 1 ? "Found it. Is this the one?" : "Found these. Pick the one to use.", ch::DIM);
                }
                ImGui::Spacing();
                for (size_t index = 0; index < setup_.candidates.size(); ++index) {
                    if (Card_(static_cast<int>(index), setup_.chosen == static_cast<int>(index), setup_.candidates[index], needed)) {
                        setup_.chosen = static_cast<int>(index);
                    }
                }
                ImGui::Spacing();
                if (Link_("Choose a folder...")) {
                    const SDL_DialogFileCallback callback = [](void* user, const char* const* list, int) {
                        auto* setup = static_cast<s_setup*>(user);
                        if (list == nullptr || list[0] == nullptr) return; // cancelled or failed
                        std::lock_guard<std::mutex> lock(setup->dialog_mutex);
                        setup->dialog_folder = list[0];
                        setup->dialog_answered = true;
                    };
                    SDL_ShowOpenFolderDialog(callback, &setup_, SDL_GetKeyboardFocus(), nullptr, false);
                }
                ImGui::SameLine(0.0f, 24.0f);
                if (Link_("Choose an installer...")) {
                    static const SDL_DialogFileFilter filters[] = {{"GOG installers", "sh;exe;pkg"}};
                    const SDL_DialogFileCallback callback = [](void* user, const char* const* list, int) {
                        auto* setup = static_cast<s_setup*>(user);
                        if (list == nullptr || list[0] == nullptr) return;
                        std::lock_guard<std::mutex> lock(setup->dialog_mutex);
                        setup->dialog_folder = list[0];
                        setup->dialog_answered = true;
                    };
                    const std::string downloads = gi::Downloads_Folder();
                    SDL_ShowOpenFileDialog(callback, &setup_, SDL_GetKeyboardFocus(), filters, 1, downloads.empty() ? nullptr : downloads.c_str(), false);
                }
                ImGui::SameLine(0.0f, 24.0f);
                if (Link_("I don't have it yet")) setup_.page = SETUP_GET;
                left_label = "Back";
                right_label = "Next";
                right_enabled = setup_.chosen >= 0;
                break;
            case SETUP_GET: {
                Text_("Master of Orion II is sold by Steam and GOG. Install it with their own app or website; Battle at Antares "
                      "waits, notices when it's there and carries on by itself.");
                Text_("Battle at Antares never asks for your Steam or GOG password: you sign in to them, not to us.", ch::DIM, SMALL);
                ImGui::Spacing();
                const bool steam = !Steam_Libraries_().empty();
                ImGui::PushFont(ch::Bold_Font(), BODY);
                ImGui::TextColored(ch::GOLD, "STEAM");
                ImGui::PopFont();
                if (steam) {
                    if (Link_("Install it with Steam (if you own it there)")) {
                        (void)SDL_OpenURL(("steam://install/" + std::to_string(gi::STEAM_APP_ID)).c_str());
                        Setup_Wait_("Steam");
                    }
                    ImGui::SameLine(0.0f, 24.0f);
                    if (Link_("Buy it in Steam")) {
                        (void)SDL_OpenURL(("steam://store/" + std::to_string(gi::STEAM_APP_ID)).c_str());
                        Setup_Wait_("Steam");
                    }
                } else if (Link_("Steam's page for Master of Orion 2")) {
                    (void)SDL_OpenURL(("https://store.steampowered.com/app/" + std::to_string(gi::STEAM_APP_ID) + "/").c_str());
                    Setup_Wait_("Steam");
                }
                ImGui::Spacing();
                ImGui::PushFont(ch::Bold_Font(), BODY);
                ImGui::TextColored(ch::GOLD, "GOG");
                ImGui::PopFont();
                if (Gog_Available_()) {
                    if (Link_("Sign in to GOG and download it here")) Gog_Start_();
                    Text_("GOG's own sign-in page opens in a window; Battle at Antares never sees your password, keeps nothing "
                          "after the download, and saves the installer in your Downloads.",
                        ch::DIM,
                        SMALL);
                }
                if (Link_("GOG's page for Master of Orion 1+2")) {
                    (void)SDL_OpenURL("https://www.gog.com/game/master_of_orion_1_2");
                    Setup_Wait_("GOG");
                }
#if defined(_WIN32)
                const std::string galaxy = Galaxy_Client_();
                if (!galaxy.empty()) {
                    ImGui::SameLine(0.0f, 24.0f);
                    if (Link_("Open GOG Galaxy")) {
                        const char* argv[] = {galaxy.c_str(), nullptr};
                        SDL_Process* process = SDL_CreateProcess(argv, false);
                        if (process != nullptr) SDL_DestroyProcess(process); // it keeps running
                        Setup_Wait_("GOG");
                    }
                }
#else
                Text_("On Linux, GOG's games install with Heroic or Lutris; Battle at Antares finds them there.", ch::DIM, SMALL);
#endif
                left_label = "Back";
                right_label = "Find again";
                break;
            }
            case SETUP_WAIT: {
                Text_("Install Master of Orion II in " + setup_.wait_store + " as usual. Battle at Antares looks for it every few seconds "
                                                                             "and carries on by itself when it's there; you can leave this window open.");
                ImGui::Spacing();
                if (setup_.steam.known && !setup_.steam.installed && setup_.wait_store == "Steam") {
                    const uint64_t total = std::max<uint64_t>(setup_.steam.total, 1);
                    const float fraction = static_cast<float>(static_cast<double>(setup_.steam.done) / static_cast<double>(total));
                    const std::string label = "Steam is installing it: " + Megabytes_(setup_.steam.done) + " of " + Megabytes_(setup_.steam.total);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ch::AMBER);
                    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), label.c_str());
                    ImGui::PopStyleColor();
                } else {
                    // A slow pulse so the page is plainly alive.
                    const float phase = static_cast<float>(SDL_GetTicks() % 2000) / 2000.0f;
                    Text_(std::string("Waiting for ") + setup_.wait_store + (phase < 0.33f ? "." : phase < 0.66f ? ".."
                                                                                                                 : "..."),
                        ch::AMBER);
                }
                ImGui::Spacing();
                if (Link_("It's installed somewhere else: choose its folder...")) {
                    const SDL_DialogFileCallback callback = [](void* user, const char* const* list, int) {
                        auto* setup = static_cast<s_setup*>(user);
                        if (list == nullptr || list[0] == nullptr) return;
                        std::lock_guard<std::mutex> lock(setup->dialog_mutex);
                        setup->dialog_folder = list[0];
                        setup->dialog_answered = true;
                    };
                    SDL_ShowOpenFolderDialog(callback, &setup_, SDL_GetKeyboardFocus(), nullptr, false);
                }
                left_label = "Back";
                break;
            }
            case SETUP_GOG: {
                std::string phase, error;
                bool no_engine = false;
                uint64_t total = 0;
                {
                    std::lock_guard<std::mutex> lock(gog_.mutex);
                    phase = gog_.phase;
                    error = gog_.error;
                    no_engine = gog_.no_engine;
                    total = gog_.total;
                }
                if (gog_.busy) {
                    Text_(phase + (phase.rfind("Downloading", 0) == 0 ? "" : "..."));
                    if (phase.rfind("Downloading", 0) == 0) {
                        ImGui::Spacing();
                        const uint64_t done = gog_.progress.done;
                        const float fraction = total > 0 ? std::min(1.0f, static_cast<float>(static_cast<double>(done) / static_cast<double>(total))) : 0.0f;
                        const std::string label = Megabytes_(done) + (total > 0 ? " of about " + Megabytes_(total) : "");
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ch::AMBER);
                        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label.c_str());
                        ImGui::PopStyleColor();
                    }
                    ImGui::Spacing();
                    Text_("Your password goes to GOG's page only. Battle at Antares uses the sign-in for this one download and keeps nothing.", ch::DIM, SMALL);
                    left_label = "Cancel";
                } else {
                    Text_(error.empty() ? "Done." : error, error.empty() ? ch::LIGHT : ch::RED);
                    ImGui::Spacing();
                    Text_(no_engine ? "GOG's website works instead: download Master of Orion II's offline installer there, and the wizard picks it up from your Downloads by itself."
                                    : "You can try again, or use GOG's website: download the offline installer there and the wizard picks it up from your Downloads by itself.",
                        ch::DIM,
                        SMALL);
                    left_label = "Back";
                    right_label = no_engine ? "GOG's website" : "Try again";
                }
                break;
            }
            case SETUP_CHECK: {
                const gi::s_folder_check& check = setup_.check;
                const bool installer = check.installer != gi::INSTALLER_NONE;
                Text_(Shown_(check.folder), ch::DIM, SMALL);
                ImGui::Spacing();
                if (installer && !setup_.check_error.empty()) {
#if defined(_WIN32)
                    const bool native = check.installer == gi::INSTALLER_WINDOWS;
#elif defined(__APPLE__)
                    const bool native = check.installer == gi::INSTALLER_MAC;
#else
                    const bool native = check.installer == gi::INSTALLER_LINUX;
#endif
                    Check_Row_(false, true, "Installer", native ? "Battle at Antares couldn't read this installer: " + setup_.check_error + ". Run it instead: the wizard waits and finds the game when it's installed." : "Battle at Antares couldn't read this installer: " + setup_.check_error + ". GOG's download page for Master of Orion 1+2 has installers for every system; download it again.");
                    left_label = "Back";
                    if (native) {
                        right_label = "Run the installer";
                    } else {
                        right_label = "GOG's page";
                    }
                    break;
                }
                if (!check.exists) {
                    Check_Row_(false, false, "Folder", "This folder doesn't exist any more.");
                } else {
                    Check_Row_(check.missing.empty(), false, "Game files", check.missing.empty() ? "All " + std::to_string(needed) + " game files are there." : std::to_string(check.missing.size()) + " of the " + std::to_string(needed) + " game files aren't in this folder: " + Join_Names_(check.missing) + ". Is it the right folder? It's the one with ORION95.EXE and the .LBX files.");
                    Check_Row_(check.patched, false, "Version", check.patched ? "1.31, as the game needs." : "This copy isn't version 1.31 (PATCH13.LBX is missing). Install the 1.31 patch, or use the GOG "
                                                                                                             "or Steam version.");
                    if (!check.empty.empty()) {
                        Check_Row_(false, false, "Damaged files", "These files are empty, so this copy is broken: " + Join_Names_(check.empty) + ". Reinstall Master of Orion II, or verify it in Steam or GOG Galaxy.");
                    }
                    std::error_code error;
                    if (std::filesystem::equivalent(U8_(check.folder), U8_(setup_.target), error)) {
                        Check_Row_(true, false, "In place", "These files are already where Battle at Antares reads them.");
                    } else {
                        const bool room = setup_.free_bytes == 0 || setup_.free_bytes >= check.bytes;
                        Check_Row_(room, false, "Room on disk", "Copying needs " + Megabytes_(check.bytes) + (setup_.free_bytes != 0 ? "; " + Megabytes_(setup_.free_bytes) + " free." : "."));
                    }
                }
                left_label = "Back";
                right_label = installer ? "Extract the files" : "Copy the files";
                right_enabled = check.ok() && (setup_.free_bytes == 0 || setup_.free_bytes >= check.bytes);
                break;
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
                ImGui::Spacing();
                const float width = ImGui::GetContentRegionAvail().x - 2.0f;
                const ImVec2 min = ImGui::GetCursorScreenPos();
                const ImVec2 max(min.x + width, min.y + 34);
                Plate_(draw, min, max, false);
                draw->AddRectFilled(ImVec2(min.x + 3, min.y + 3), ImVec2(min.x + 3 + (width - 6) * std::clamp(fraction, 0.0f, 1.0f), max.y - 3), U32_(progress.failed ? ch::DIM : ch::AMBER, progress.failed ? 0.5f : 0.9f));
                const std::string label = Megabytes_(progress.bytes_done) + " of " + Megabytes_(progress.bytes_total);
                const ImVec2 label_size = ch::Bold_Font()->CalcTextSizeA(19.0f, FLT_MAX, 0.0f, label.c_str());
                ch::Shadowed_Text(draw, ch::Bold_Font(), 19.0f, ImVec2(min.x + (width - label_size.x) * 0.5f, min.y + (34 - label_size.y) * 0.5f), ch::LIGHT, label.c_str());
                ImGui::Dummy(ImVec2(width, 38));
                if (progress.failed) {
                    if (setup_.worker.joinable()) setup_.worker.join();
                    Text_(error, ch::RED);
                    left_label = "Back";
                    right_label = "Try again";
                } else {
                    Text_(current, ch::DIM, SMALL);
                    left_label = "Cancel";
                }
                break;
            }
            case SETUP_SAVES: {
                std::string taken;
                for (const gi::s_found_save& save : setup_.saves) {
                    if (save.taken) taken += (taken.empty() ? "" : ", ") + std::to_string(save.slot);
                }
                Text_("Your Master of Orion II has " + std::to_string(setup_.saves.size()) + " saved game" + (setup_.saves.size() == 1 ? "" : "s") +
                    ". Battle at Antares can continue them.");
                ImGui::Spacing();
                if (ImGui::BeginTable("saves", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
                    ImGui::TableSetupColumn("SLOT", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("NAME");
                    ImGui::TableSetupColumn("STARDATE", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::PushFont(ch::Title_Font(), 15.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, ch::GOLD);
                    ImGui::TableHeadersRow();
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    for (const gi::s_found_save& save : setup_.saves) {
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", save.slot);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(save.summary.readable ? save.summary.description.c_str() : "(can't be read)");
                        ImGui::TableNextColumn();
                        if (save.summary.readable) ImGui::Text("%d.%d", save.summary.stardate / 10, save.summary.stardate % 10);
                    }
                    ImGui::EndTable();
                }
                {
                    // The choice sits on the title's row, at the right (owner 2026-11-24: it was
                    // under a long list).
                    const char* label = "Bring them into Battle at Antares";
                    const ImVec2 back = ImGui::GetCursorScreenPos();
                    const float width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
                    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - width - 4.0f, title_at.y - 2.0f));
                    ImGui::Checkbox(label, &setup_.import_saves);
                    ImGui::SetCursorScreenPos(back);
                    ImGui::Dummy(ImVec2(0.0f, 0.0f)); // ImGui wants an item after moving the cursor back (owner's error, 2026-11-24)
                }
                if (!taken.empty() && setup_.import_saves) {
                    ImGui::Spacing();
                    Text_("Battle at Antares already has saves in slot" + std::string(taken.find(',') != std::string::npos ? "s " : " ") + taken + ". For those:", ch::DIM);
                    if (ImGui::RadioButton("Keep Battle at Antares's saves", !setup_.replace_taken)) setup_.replace_taken = false;
                    if (ImGui::RadioButton("Use the original game's saves", setup_.replace_taken)) setup_.replace_taken = true;
                }
                right_label = "Next";
                break;
            }
            case SETUP_HD:
                Text_("Battle at Antares can show the whole game in high resolution with these free packs of upscaled art. "
                      "They download from GitHub and switch on by themselves when they're ready. "
                      "They never affect your saves.");
                ImGui::Spacing();
                Text_("Tick the ones you want, or skip: the launcher's HD art button offers them any time.", ch::DIM, SMALL);
                ImGui::Spacing();
                Draw_Hd_List_();
                // The downloads stay on this page (owner 2026-11-24); Next waits for them.
                if (hd_.busy) {
                    ImGui::Spacing();
                    Text_("Next unlocks when the downloads are done. Each pack switches on by itself when it's ready.", ch::DIM, SMALL);
                }
                left_label = hd_.busy || !hd_.session_done.empty() ? nullptr : "Skip";
                right_label = !hd_.busy && Hd_Any_Ticked_() ? "Download" : "Next";
                right_enabled = !hd_.busy;
                break;
            case SETUP_DONE:
                if (files_.ok) {
                    Text_("Battle at Antares has everything it needs.");
                    ImGui::Spacing();
                    Text_("The game files are in " + Shown_(setup_.target), ch::DIM, SMALL);
                } else {
                    Text_("Something is still missing:", ch::RED);
                    Text_(antares::game_files::Describe(files_));
                }
                if (!hd_.session_done.empty()) {
                    ImGui::Spacing();
                    std::string names;
                    for (const std::string& id : hd_.session_done) names += (names.empty() ? "" : ", ") + Package_Name_(id);
                    Text_("HD art switched on: " + names + ".", ch::GREEN);
                }
                if (setup_.imported > 0) {
                    ImGui::Spacing();
                    Text_("Imported " + std::to_string(setup_.imported) + " saved game" + (setup_.imported == 1 ? "." : "s."));
                }
                if (!setup_.import_error.empty()) Text_(setup_.import_error, ch::RED);
                if (files_.ok) {
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::Checkbox("Play in full screen", &setup_.fullscreen);
                    Text_("Alt+Enter switches between full screen and a window while you play; the launcher's Settings "
                          "tab has the window size.",
                        ch::DIM,
                        SMALL);
                    if (menu::Offered(game_dir_) && !menu::Exists(game_dir_)) {
                        ImGui::Spacing();
#ifdef _WIN32
                        ImGui::Checkbox("Add Battle at Antares to the Start menu", &setup_.add_to_menu);
#else
                        ImGui::Checkbox("Add Battle at Antares to the applications menu", &setup_.add_to_menu);
#endif
                    }
                }
                left_label = files_.ok ? "Open the launcher" : "Back";
                right_label = files_.ok ? "Play" : nullptr;
                break;
        }
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(11);

        // The buttons under the bezel: the way back on the left, the way on at the right.
        const ImVec2 button(std::min(290.0f, column * 0.45f), 52.0f);
        const float buttons_y = window_max.y - 82;
        e_action action = NONE;
        if (left_label != nullptr) {
            ImGui::SetCursorScreenPos(ImVec2(left, buttons_y));
            if (ch::Button((std::string(left_label) + "##left").c_str(), button, false)) action = LEFT;
        }
        if (right_label != nullptr) {
            ImGui::SetCursorScreenPos(ImVec2(left + column - button.x, buttons_y));
            if (ch::Button((std::string(right_label) + "##right").c_str(), button, true, right_enabled)) action = RIGHT;
        }
        if (action == NONE) return;
        switch (setup_.page) {
            case SETUP_WELCOME:
                if (action == LEFT) {
                    Setup_Close_();
                } else {
                    Setup_Find_();
                }
                break;
            case SETUP_FIND:
                if (action == LEFT) {
                    setup_.page = SETUP_WELCOME;
                } else {
                    Setup_Check_(setup_.candidates[setup_.chosen].path);
                }
                break;
            case SETUP_GET:
                if (action == LEFT) {
                    setup_.page = SETUP_FIND;
                } else {
                    Setup_Find_();
                }
                break;
            case SETUP_WAIT:
                setup_.page = SETUP_GET;
                break;
            case SETUP_GOG:
                if (gog_.busy) {
                    gog_.progress.cancel = true; // Cancel
                } else if (action == LEFT) {
                    setup_.page = SETUP_GET;
                } else if (gog_.no_engine) {
                    (void)SDL_OpenURL(antares::gog::STORE_URL);
                    Setup_Wait_("GOG");
                } else {
                    Gog_Start_();
                }
                break;
            case SETUP_CHECK:
                if (action == LEFT) {
                    Setup_Find_();
                } else if (!setup_.check_error.empty() && setup_.check.installer != gi::INSTALLER_NONE) {
                    if (setup_.check.installer == gi::INSTALLER_LINUX || std::string(right_label != nullptr ? right_label : "") == "GOG's page") {
                        (void)SDL_OpenURL("https://www.gog.com/account");
                    } else {
                        // Run GOG's own installer, then wait for the game it installs.
                        const char* argv[] = {setup_.check.folder.c_str(), nullptr};
                        SDL_Process* process = SDL_CreateProcess(argv, false);
                        if (process != nullptr) SDL_DestroyProcess(process);
                    }
                    Setup_Wait_("GOG");
                } else {
                    Setup_Start_Copy_();
                }
                break;
            case SETUP_COPY:
                if (setup_.progress && setup_.progress->failed) {
                    if (action == LEFT) {
                        setup_.page = SETUP_CHECK;
                    } else {
                        Setup_Start_Copy_();
                    }
                } else if (setup_.progress) {
                    setup_.progress->cancel = true;
                }
                break;
            case SETUP_SAVES:
                if (setup_.import_saves) {
                    Setup_Import_();
                } else {
                    setup_.page = SETUP_HD;
                }
                break;
            case SETUP_HD:
                if (action == RIGHT && !hd_.busy && Hd_Any_Ticked_()) {
                    Hd_Start_(); // stays here: the bars are on this page
                } else if (!hd_.busy) {
                    setup_.page = SETUP_DONE;
                }
                break;
            case SETUP_DONE:
                if (action == LEFT && !files_.ok) {
                    Setup_Find_();
                } else {
                    Finish_Setup(action == RIGHT);
                }
                break;
        }
    }

    void c_launcher::Finish_Setup(bool play) {
        // Full screen goes into the selected profile, as the Settings tab would write it.
        if (setup_.fullscreen) (void)Apply_Setting("interface.fullscreen=true");
        if (setup_.add_to_menu && menu::Offered(game_dir_) && !menu::Exists(game_dir_)) {
            std::string menu_error;
            if (!menu::Add(game_dir_, &menu_error)) {
                message_ = menu_error;
                message_is_error_ = true;
            }
        }
        Setup_Close_();
        if (play) Launch_(0);
    }
} // namespace launcher
