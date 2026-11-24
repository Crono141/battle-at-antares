#include "pch.h"

#include "antares/game_install.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "antares/game_files.h"
#include "antares/user_folder.h"
#include "game/user_paths.h"

namespace antares::game_install {
    namespace {
        namespace fs = std::filesystem;

        // UTF-8 in and out on every platform (a Windows user name may not be ASCII).
        fs::path Path_(const std::string& text) {
            return fs::path(std::u8string(text.begin(), text.end()));
        }

        std::string Text_(const fs::path& path) {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        std::string Folder_Text_(const fs::path& path) {
            std::string text = Text_(path);
            if (!text.empty() && text.back() != '/' && text.back() != '\\') text += '/';
            return text;
        }

        std::string Upper_(std::string text) {
            for (char& character : text) character = static_cast<char>(SDL_toupper(static_cast<unsigned char>(character)));
            return text;
        }

        // The regular files in `folder` by upper-case name (the game opens them in any case).
        std::map<std::string, fs::directory_entry> Files_(const fs::path& folder) {
            std::map<std::string, fs::directory_entry> files;
            std::error_code error;
            for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
                std::error_code type_error;
                if (entry.is_regular_file(type_error)) {
                    files.emplace(Upper_(Text_(entry.path().filename())), entry);
                }
            }
            return files;
        }

        bool Has_Game_(const fs::path& folder) {
            const auto files = Files_(folder);
            return files.count("DIPLOMAT.LBX") != 0;
        }

        size_t Count_Required_(const fs::path& folder) {
            const auto files = Files_(folder);
            size_t count = 0;
            for (const std::string& name : game_files::Required_Files()) {
                const auto it = files.find(name);
                std::error_code error;
                if (it != files.end() && it->second.file_size(error) > 0) ++count;
            }
            return count;
        }

        std::string Read_File_(const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        // "install_path": "..." values in Heroic's installed.json (no JSON parser needed).
        std::vector<std::string> Heroic_Paths_(const std::string& text) {
            std::vector<std::string> paths;
            size_t at = 0;
            while ((at = text.find("\"install_path\"", at)) != std::string::npos) {
                const size_t open = text.find('"', text.find(':', at) + 1);
                const size_t close = open == std::string::npos ? open : text.find('"', open + 1);
                if (open == std::string::npos || close == std::string::npos) break;
                paths.push_back(text.substr(open + 1, close - open - 1));
                at = close + 1;
            }
            return paths;
        }
    } // namespace

    std::vector<std::string> Parse_Steam_Libraries(const std::string& vdf_text) {
        std::vector<std::string> libraries;
        size_t at = 0;
        while ((at = vdf_text.find("\"path\"", at)) != std::string::npos) {
            const size_t open = vdf_text.find('"', at + 6);
            const size_t close = open == std::string::npos ? open : vdf_text.find('"', open + 1);
            if (open == std::string::npos || close == std::string::npos) break;
            std::string path = vdf_text.substr(open + 1, close - open - 1);
            // VDF escapes backslashes (Windows paths).
            std::string unescaped;
            for (size_t index = 0; index < path.size(); ++index) {
                if (path[index] == '\\' && index + 1 < path.size() && path[index + 1] == '\\') ++index;
                unescaped += path[index];
            }
            libraries.push_back(unescaped);
            at = close + 1;
        }
        return libraries;
    }

    std::vector<s_root> Detect_Roots(const std::string& home_text, const std::vector<s_root>& registry_roots) {
        std::vector<s_root> roots = registry_roots;
        const fs::path home = Path_(home_text);
        std::vector<fs::path> steam_dirs;
#if defined(_WIN32)
        for (const s_root& root : registry_roots) {
            if (root.source == "Steam") steam_dirs.push_back(Path_(root.path));
        }
        roots.push_back({"C:/GOG Games/", "GOG", 2});
        roots.push_back({"C:/Program Files (x86)/GOG Galaxy/Games/", "GOG Galaxy", 2});
        roots.push_back({"C:/Program Files/GOG Galaxy/Games/", "GOG Galaxy", 2});
        steam_dirs.push_back("C:/Program Files (x86)/Steam");
#else
        if (!home_text.empty()) {
            steam_dirs.push_back(home / ".local/share/Steam");
            steam_dirs.push_back(home / ".steam/steam");
            steam_dirs.push_back(home / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
            roots.push_back({Folder_Text_(home / "GOG Games"), "GOG", 2});
            roots.push_back({Folder_Text_(home / "Games"), "Heroic or Lutris", 5});
            roots.push_back({Folder_Text_(home / ".wine/drive_c"), "Wine", 4});
            for (const char* config : {".config/heroic/gog_store/installed.json",
                     ".var/app/com.heroicgameslauncher.hgl/config/heroic/gog_store/installed.json"}) {
                for (const std::string& path : Heroic_Paths_(Read_File_(home / config))) {
                    roots.push_back({path, "Heroic", 2});
                }
            }
#if defined(__APPLE__)
            roots.push_back({Folder_Text_(home / "Library/Application Support/Steam/steamapps/common"), "Steam", 2});
            roots.push_back({"/Applications/", "GOG", 6});
            roots.push_back({Folder_Text_(home / "Applications"), "GOG", 6});
#endif
        }
#endif
        std::set<std::string> libraries;
        for (const fs::path& steam : steam_dirs) {
            libraries.insert(Folder_Text_(steam));
            for (const std::string& library : Parse_Steam_Libraries(Read_File_(steam / "steamapps/libraryfolders.vdf"))) {
                libraries.insert(Folder_Text_(Path_(library)));
            }
        }
        for (const std::string& library : libraries) {
            roots.push_back({Folder_Text_(Path_(library) / "steamapps/common"), "Steam", 2});
        }
        return roots;
    }

    std::vector<s_candidate> Detect(const std::vector<s_root>& roots) {
        std::vector<s_candidate> found;
        std::set<std::string> seen;
        size_t visited = 0;
        constexpr size_t VISIT_LIMIT = 20000;
        for (const s_root& root : roots) {
            struct s_item {
                fs::path path;
                int depth;
            };
            std::vector<s_item> queue = {{Path_(root.path), 0}};
            for (size_t index = 0; index < queue.size() && visited < VISIT_LIMIT; ++index) {
                const s_item item = queue[index];
                ++visited;
                std::error_code error;
                if (!fs::is_directory(item.path, error)) continue;
                if (Has_Game_(item.path)) {
                    const fs::path canonical = fs::weakly_canonical(item.path, error);
                    const std::string key = Folder_Text_(error ? item.path : canonical);
                    if (seen.insert(key).second) {
                        found.push_back({key, root.source, Count_Required_(item.path)});
                    }
                    continue; // the game's own folder: nothing more below it
                }
                if (item.depth >= root.depth) continue;
                for (const fs::directory_entry& entry : fs::directory_iterator(item.path, fs::directory_options::skip_permission_denied, error)) {
                    std::error_code type_error;
                    if (!entry.is_directory(type_error) || entry.is_symlink(type_error)) continue;
                    const std::string name = Text_(entry.path().filename());
                    if (name.empty() || name[0] == '.' || Upper_(name) == "DOSBOX") continue;
                    queue.push_back({entry.path(), item.depth + 1});
                }
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const s_candidate& a, const s_candidate& b) { return a.found > b.found; });
        return found;
    }

    s_folder_check Check_Folder(const std::string& folder_text) {
        s_folder_check check;
        fs::path folder = Path_(folder_text);
        std::error_code error;
        check.exists = fs::is_directory(folder, error);
        if (check.exists && !Has_Game_(folder)) {
            // A bundle (GOG's Master of Orion 1+2, Steam's): the game one folder down.
            for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
                std::error_code type_error;
                if (entry.is_directory(type_error) && Has_Game_(entry.path())) {
                    folder = entry.path();
                    break;
                }
            }
        }
        check.folder = Folder_Text_(folder);
        if (!check.exists) {
            check.missing = game_files::Required_Files();
            return check;
        }
        const auto files = Files_(folder);
        for (const std::string& name : game_files::Required_Files()) {
            const auto it = files.find(name);
            std::error_code size_error;
            if (it == files.end()) {
                check.missing.push_back(name);
            } else if (it->second.file_size(size_error) == 0) {
                check.empty.push_back(name);
            } else {
                ++check.found;
            }
        }
        check.patched = files.count("PATCH13.LBX") != 0;
        for (const auto& [name, entry] : files) {
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".LBX") == 0) {
                std::error_code size_error;
                check.to_copy.push_back(Text_(entry.path().filename()));
                check.bytes += entry.file_size(size_error);
            }
        }
        return check;
    }

    std::string Target_Folder(const std::string& game_dir) {
        if (user_paths::Is_Enabled()) {
            return std::string(user_paths::Get_Directory(user_paths::USER_DIRECTORY_DATA)) + "game/";
        }
        return Folder_Text_(Path_(game_dir));
    }

    bool Copy_Files(const s_folder_check& check, const std::string& target_text, s_copy_progress* progress) {
        const fs::path source = Path_(check.folder);
        const fs::path target = Path_(target_text);
        progress->bytes_total = check.bytes;
        const auto fail = [&](const std::string& message) {
            std::lock_guard<std::mutex> lock(progress->mutex);
            progress->error = message;
            progress->failed = true;
            progress->finished = true;
            return false;
        };
        std::error_code error;
        fs::create_directories(target, error);
        if (error) return fail("Can't create " + Text_(target) + ": " + error.message());
        if (fs::equivalent(source, target, error)) {
            progress->bytes_done = check.bytes;
            progress->finished = true;
            return true;
        }
        const auto existing = Files_(target);
        for (const std::string& name : check.to_copy) {
            if (progress->cancel) {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->error = "Cancelled.";
                progress->failed = true;
                progress->finished = true;
                return false;
            }
            {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->current = name;
            }
            const fs::path from = source / Path_(name);
            const uint64_t size = fs::file_size(from, error);
            if (error) return fail("Can't read " + Text_(from) + ": " + error.message());
            // A file already there in another case (fonts.lbx) is replaced under the new name.
            const auto there = existing.find(Upper_(name));
            if (there != existing.end()) {
                std::error_code size_error;
                if (there->second.file_size(size_error) == size && Text_(there->second.path().filename()) == name) {
                    progress->bytes_done += size;
                    continue;
                }
                fs::remove(there->second.path(), size_error);
            }
            const fs::path temporary = target / Path_(name + ".part");
            {
                std::ifstream in(from, std::ios::binary);
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!in || !out) return fail("Can't copy " + name + " to " + Text_(target) + ".");
                std::vector<char> buffer(1 << 20);
                while (in) {
                    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    const std::streamsize got = in.gcount();
                    if (got <= 0) break;
                    out.write(buffer.data(), got);
                    if (!out) {
                        out.close();
                        fs::remove(temporary, error);
                        return fail("Writing " + name + " failed (is the disk full?).");
                    }
                    progress->bytes_done += static_cast<uint64_t>(got);
                    if (progress->cancel) break;
                }
            }
            if (progress->cancel) {
                fs::remove(temporary, error);
                continue; // reported at the top of the loop
            }
            fs::rename(temporary, target / Path_(name), error);
            if (error) return fail("Can't finish " + name + ": " + error.message());
        }
        if (progress->cancel) {
            std::lock_guard<std::mutex> lock(progress->mutex);
            progress->error = "Cancelled.";
            progress->failed = true;
        }
        progress->finished = true;
        return !progress->failed;
    }

    std::vector<s_found_save> Find_Saves(const std::string& folder_text, const std::string& virtual_store, const std::string& saves_dir) {
        std::map<int, s_found_save> by_slot;
        std::map<int, fs::file_time_type> times;
        std::vector<fs::path> folders = {Path_(folder_text), Path_(folder_text) / "cloud_saves"};
        if (!virtual_store.empty()) folders.push_back(Path_(virtual_store));
        for (const fs::path& folder : folders) {
            for (const auto& [name, entry] : Files_(folder)) {
                int slot = 0;
                if (sscanf(name.c_str(), "SAVE%d.GAM", &slot) != 1 || slot < 1 || slot > 10 ||
                    name != "SAVE" + std::to_string(slot) + ".GAM") {
                    continue;
                }
                std::error_code error;
                const fs::file_time_type time = entry.last_write_time(error);
                if (by_slot.count(slot) != 0 && times[slot] >= time) continue;
                s_found_save save;
                save.slot = slot;
                save.path = Text_(entry.path());
                size_t size = 0;
                void* data = SDL_LoadFile(save.path.c_str(), &size);
                if (data != nullptr) {
                    save.summary.slot = slot;
                    save.summary.exists = true;
                    saves::Read_Save_Summary(static_cast<const uint8_t*>(data), size, &save.summary);
                    SDL_free(data);
                }
                SDL_PathInfo info{};
                if (SDL_GetPathInfo(save.path.c_str(), &info)) save.summary.modified = info.modify_time;
                by_slot[slot] = save;
                times[slot] = time;
            }
        }
        const auto taken = Files_(Path_(saves_dir));
        std::vector<s_found_save> saves;
        for (auto& [slot, save] : by_slot) {
            save.taken = taken.count("SAVE" + std::to_string(slot) + ".GAM") != 0;
            saves.push_back(save);
        }
        return saves;
    }

    int Import_Saves(const std::vector<s_found_save>& saves, const std::string& saves_dir, bool replace_taken, std::string* error) {
        int imported = 0;
        const fs::path folder = Path_(saves_dir);
        std::error_code code;
        fs::create_directories(folder, code);
        const auto existing = Files_(folder);
        for (const s_found_save& save : saves) {
            if (save.taken && !replace_taken) continue;
            const std::string name = "SAVE" + std::to_string(save.slot) + ".GAM";
            const auto there = existing.find(name);
            if (there != existing.end() && Text_(there->second.path().filename()) != name) {
                fs::remove(there->second.path(), code); // save3.gam: one copy per slot
            }
            fs::copy_file(Path_(save.path), folder / name, fs::copy_options::overwrite_existing, code);
            if (code) {
                *error = "Can't import " + name + ": " + code.message();
                continue;
            }
            ++imported;
        }
        return imported;
    }

    std::string Virtual_Store_For(const std::string& folder) {
#if defined(_WIN32)
        const char* local = SDL_getenv("LOCALAPPDATA");
        if (local == nullptr || folder.size() < 3 || folder[1] != ':') return "";
        return Folder_Text_(Path_(local) / "VirtualStore" / Path_(folder.substr(3)));
#else
        (void)folder;
        return "";
#endif
    }
} // namespace antares::game_install
