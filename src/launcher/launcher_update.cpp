#include "pch.h"

// Updates from the launcher (WP5.19): at start, GitHub's latest release against this copy's
// release label (version.txt); when newer, a line on the title row with Update (or the release
// page when this kind of package can't update itself), a progress bar while it downloads, then
// the kind's own way in:
// - the Windows installer runs the new installer silently (it waits for the launcher to close and
//   starts it again);
// - the AppImage's file is replaced by the new one, which is started;
// - a portable folder (the Linux archive, the Windows zip) gets the archive's files: unpacked
//   beside the game, then each file swapped in (the old one kept aside until all are in, put
//   back on a failure); the player's antares.cfg is kept and nothing is deleted.
// Any failure of the check is silence (offline, GitHub's rate limit).

#include <filesystem>
#include <fstream>
#include <vector>

#include "antares/hd_packs.h"
#include "antares/identity.h"
#include "antares/updates.h"
#include "antares/utf8_files.h"
#include "game/vfs.h"
#include "launcher/launcher_app.h"
#include "launcher/launcher_chrome.h"
#include "miniz.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace launcher {
    namespace up = antares::updates;
    namespace hd = antares::hd_packs;
    namespace fs = std::filesystem;

    namespace {
        constexpr char OLD_SUFFIX[] = ".update-old";
        constexpr char STAGING[] = ".update/";

        std::string Release_Url_() {
            const char* override_url = SDL_getenv("ANTARES_UPDATE_URL"); // tests: a local release
            return override_url != nullptr && override_url[0] != '\0' ? override_url : up::RELEASE_API_URL;
        }

        // Files under `root`, as '/'-separated paths relative to it.
        void List_Files_(const fs::path& root, std::vector<std::string>* out) {
            std::error_code error;
            for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
                if (it->is_regular_file(error) || it->is_symlink(error)) {
                    std::string relative = antares::utf8::Text(fs::relative(it->path(), root, error));
                    for (char& c : relative) {
                        if (c == '\\') c = '/';
                    }
                    out->push_back(relative);
                }
            }
        }

        // Unpacks a zip (the Windows zip) into `into`, dropping its top folder.
        bool Unzip_(const std::string& zip_path, const fs::path& into, std::string* error) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) {
                *error = "the download isn't a zip";
                return false;
            }
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            bool ok = true;
            for (mz_uint i = 0; i < count && ok; ++i) {
                char name[1024];
                mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
                std::string entry = name;
                const size_t slash = entry.find('/');
                if (slash == std::string::npos || mz_zip_reader_is_file_a_directory(&zip, i)) continue;
                entry = entry.substr(slash + 1); // battle-at-antares-<v>-windows-x86_64/<path>
                if (entry.empty() || entry.find("..") != std::string::npos) continue;
                const fs::path target = into / antares::utf8::Path(entry);
                std::error_code code;
                fs::create_directories(target.parent_path(), code);
                ok = mz_zip_reader_extract_to_file(&zip, i, antares::utf8::Text(target).c_str(), 0);
                if (!ok) *error = "couldn't unpack " + entry;
            }
            mz_zip_reader_end(&zip);
            return ok;
        }

        // Unpacks a .tar.gz (the Linux archive) into `into` with the system's tar, dropping its
        // top folder.
        bool Untar_(const std::string& archive, const fs::path& into, std::string* error) {
            std::error_code code;
            fs::create_directories(into, code);
            const std::string folder = antares::utf8::Text(into);
            const char* argv[] = {"tar", "-xzf", archive.c_str(), "-C", folder.c_str(), "--strip-components=1", nullptr};
            SDL_Process* process = SDL_CreateProcess(argv, false);
            int status = -1;
            if (process == nullptr || !SDL_WaitProcess(process, true, &status) || status != 0) {
                if (process != nullptr) SDL_DestroyProcess(process);
                *error = "couldn't unpack the archive (tar)";
                return false;
            }
            SDL_DestroyProcess(process);
            return true;
        }

        // Swaps the staged files into `game`: each old file aside (renaming works on a running
        // program, also on Windows), the new one in; on a failure every swapped file goes back.
        bool Swap_In_(const fs::path& staged, const fs::path& game, std::string* error) {
            std::vector<std::string> files;
            List_Files_(staged, &files);
            std::vector<std::pair<fs::path, bool>> done; // target, had an old file
            std::error_code code;
            for (const std::string& relative : files) {
                const fs::path from = staged / antares::utf8::Path(relative);
                const fs::path to = game / antares::utf8::Path(relative);
                if (up::Players_File(relative) && fs::exists(to, code)) continue; // the player's settings stay
                fs::create_directories(to.parent_path(), code);
                const fs::path aside = fs::path(to).concat(OLD_SUFFIX);
                const bool had = fs::exists(to, code);
                if (had) {
                    fs::remove(aside, code);
                    fs::rename(to, aside, code);
                    if (code) {
                        *error = "couldn't move " + relative + " aside: " + code.message();
                        break;
                    }
                }
                fs::rename(from, to, code);
                if (code) {
                    *error = "couldn't put " + relative + " in place: " + code.message();
                    if (had) fs::rename(aside, to, code);
                    break;
                }
                done.emplace_back(to, had);
            }
            if (!error->empty()) {
                for (auto it = done.rbegin(); it != done.rend(); ++it) {
                    const fs::path aside = fs::path(it->first).concat(OLD_SUFFIX);
                    fs::remove(it->first, code);
                    if (it->second) fs::rename(aside, it->first, code);
                }
                return false;
            }
            for (const auto& [target, had] : done) {
                if (had) fs::remove(fs::path(target).concat(OLD_SUFFIX), code); // a running program's stays until next start
            }
            return true;
        }

        bool Start_(const std::vector<std::string>& args) {
            std::vector<const char*> argv;
            for (const std::string& arg : args) argv.push_back(arg.c_str());
            argv.push_back(nullptr);
            SDL_Process* process = SDL_CreateProcess(argv.data(), false);
            if (process == nullptr) return false;
            SDL_DestroyProcess(process); // it keeps running
            return true;
        }
    } // namespace

    void c_launcher::Update_Begin_() {
#if defined(_WIN32)
        // The installer waits for this to go before it replaces the launcher (antares.iss).
        static HANDLE mutex = CreateMutexW(nullptr, FALSE, L"BattleAtAntaresLauncher");
        (void)mutex;
#endif
        // What a swap left aside while it ran (a running program can't be deleted on Windows).
        std::vector<std::string> files;
        List_Files_(antares::utf8::Path(game_dir_), &files);
        std::error_code code;
        for (const std::string& relative : files) {
            if (relative.size() > sizeof(OLD_SUFFIX) && relative.compare(relative.size() - (sizeof(OLD_SUFFIX) - 1), sizeof(OLD_SUFFIX) - 1, OLD_SUFFIX) == 0) {
                fs::remove(antares::utf8::Path(game_dir_ + relative), code);
            }
        }
        fs::remove_all(antares::utf8::Path(game_dir_ + STAGING), code);

        update_.installed = up::Installed_Version(game_dir_);
        update_.kind = up::Package_Kind(game_dir_);
        if (update_.installed.empty() || !update_.check) return; // a development build, or switched off
        update_.busy = true;
        update_.worker = std::thread([this]() {
            std::string text, error;
            up::s_release release;
            const bool ok = hd::Fetch_Text(Release_Url_(), &text, &error) && up::Parse_Release(text, &release, &error);
            std::lock_guard<std::mutex> lock(update_.mutex);
            if (ok && up::Compare_Versions(release.version, update_.installed) > 0) {
                update_.release = release;
                update_.available = true;
                SDL_Log("launcher: %s is out (this is %s)", release.version.c_str(), update_.installed.c_str());
            } else if (!ok) {
                SDL_Log("launcher: no update check: %s", error.c_str());
            }
            update_.busy = false;
        });
    }

    void c_launcher::Update_Stop_() {
        update_.progress.cancel = true;
        if (update_.worker.joinable()) update_.worker.join();
    }

    // Whether Update can work here (else the release page is offered).
    bool c_launcher::Update_Installable_() const {
        if (!up::Can_Install(update_.kind)) return false;
        const up::s_asset* asset = up::Asset_For(update_.release, update_.kind);
        if (asset == nullptr || asset->sha256.empty()) return false;
        std::string folder = game_dir_;
        if (update_.kind == up::KIND_APPIMAGE) {
            const char* appimage = SDL_getenv("APPIMAGE");
            folder = appimage != nullptr ? antares::utf8::Text(antares::utf8::Path(appimage).parent_path()) + "/" : "";
        } else if (update_.kind == up::KIND_WINDOWS_INSTALLER) {
            return true; // the installer has its own rights
        }
        // A folder we may write in.
        const fs::path probe = antares::utf8::Path(folder + ".update-probe");
        {
            std::ofstream file(probe, std::ios::binary);
            if (!file) return false;
        }
        std::error_code code;
        fs::remove(probe, code);
        return true;
    }

    void c_launcher::Update_Start_() {
        const up::s_asset* asset = up::Asset_For(update_.release, update_.kind);
        if (asset == nullptr || update_.downloading || game_ != nullptr) {
            SDL_Log("launcher: no update to start (kind %d, file %s, game %s)", static_cast<int>(update_.kind), asset != nullptr ? asset->name.c_str() : "none", game_ != nullptr ? "running" : "not running");
            return;
        }
        if (update_.worker.joinable()) update_.worker.join();
        // Where it downloads: beside the AppImage, in the game folder's staging folder, or (the
        // installer) the user's cache.
        std::string folder = game_dir_ + STAGING;
        if (update_.kind == up::KIND_APPIMAGE) {
            folder = antares::utf8::Text(antares::utf8::Path(SDL_getenv("APPIMAGE")).parent_path()) + "/";
        } else if (update_.kind == up::KIND_WINDOWS_INSTALLER) {
            folder = config_dir_ + "cache/";
        }
        std::error_code code;
        fs::create_directories(antares::utf8::Path(folder), code);
        update_.download_path = folder + asset->name;
        SDL_Log("launcher: downloading %s", asset->name.c_str());
        update_.downloading = true;
        update_.ready = false;
        update_.error.clear();
        update_.progress.done = 0;
        update_.progress.total = asset->size;
        update_.progress.cancel = false;
        const up::s_asset copy = *asset;
        update_.worker = std::thread([this, copy]() {
            std::string error;
            const std::string part = update_.download_path + ".part";
            bool ok = hd::Fetch(copy.url, part, &update_.progress, &error);
            if (ok) {
                const std::string sha = hd::File_Sha256(part);
                if (sha != copy.sha256) {
                    ok = false;
                    error = "the download doesn't match GitHub's checksum";
                    std::error_code code;
                    fs::remove(antares::utf8::Path(part), code);
                }
            }
            if (ok) {
                std::error_code code;
                fs::rename(antares::utf8::Path(part), antares::utf8::Path(update_.download_path), code);
                if (code) {
                    ok = false;
                    error = code.message();
                }
            }
            std::lock_guard<std::mutex> lock(update_.mutex);
            update_.error = ok ? "" : (error == "cancelled" ? "" : error);
            update_.ready = ok;
            update_.downloading = false;
            SDL_Log("launcher: update download %s", ok ? "checked" : error.c_str());
        });
    }

    void c_launcher::Update_Poll_() {
        if (update_auto_ && update_.available && !update_.busy && !update_.downloading && update_.error.empty()) {
            update_auto_ = false;
            update_.start_requested = true;
        }
        if (update_.start_requested) {
            update_.start_requested = false;
            Update_Start_();
        }
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(update_.mutex);
            ready = update_.ready;
            if (ready) update_.ready = false;
        }
        if (!update_.busy && !update_.downloading && update_.worker.joinable()) update_.worker.join();
        if (!ready) return;
        std::string error;
        if (!Update_Apply_(&error)) {
            update_.error = error;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: update failed: %s", error.c_str());
            std::error_code code;
            fs::remove_all(antares::utf8::Path(game_dir_ + STAGING), code);
        }
    }

    bool c_launcher::Update_Apply_(std::string* error) {
        Save_Settings_();
        const std::string& file = update_.download_path;
        switch (update_.kind) {
            case up::KIND_WINDOWS_INSTALLER: {
                // It waits for this launcher to close (the mutex), installs over this copy and
                // starts the launcher again (/RELAUNCH=1, antares.iss).
                const std::string log = "/LOG=" + config_dir_ + "cache/update-install.log"; // for reports
                vfs::Shutdown();                                                            // the game data released at once (the installer replaces it), not at exit
                if (!Start_({file, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/RELAUNCH=1", log})) {
                    *error = "couldn't start the installer";
                    return false;
                }
                quit_ = true;
                return true;
            }
            case up::KIND_APPIMAGE: {
                const std::string appimage = SDL_getenv("APPIMAGE");
#if !defined(_WIN32)
                chmod(file.c_str(), 0755);
#endif
                std::error_code code;
                fs::rename(antares::utf8::Path(file), antares::utf8::Path(appimage), code); // the running file stays readable until it exits
                if (code) {
                    *error = "couldn't replace the AppImage: " + code.message();
                    return false;
                }
                if (!Start_({appimage})) {
                    *error = "updated, but couldn't start the new version: start it again";
                    return false;
                }
                quit_ = true;
                return true;
            }
            case up::KIND_LINUX_ARCHIVE:
            case up::KIND_WINDOWS_ZIP: {
                const fs::path staged = antares::utf8::Path(game_dir_ + STAGING + "files");
                std::error_code code;
                fs::remove_all(staged, code);
                const bool unpacked = update_.kind == up::KIND_WINDOWS_ZIP ? Unzip_(file, staged, error) : Untar_(file, staged, error);
                if (!unpacked) return false;
                // The launcher's own hold on the game data goes first: Windows won't move a file
                // that is open (base.zip, the packages; Wine test: "Device or resource busy").
                // From here the launcher only restarts, the new copy or (on a failure) the old.
                vfs::Shutdown();
#if defined(_WIN32)
                const std::string launcher = game_dir_ + "antares-launcher.exe";
#else
                const std::string launcher = game_dir_ + "antares-launcher";
#endif
                if (!Swap_In_(staged, antares::utf8::Path(game_dir_), error)) {
                    (void)Start_({launcher}); // the old copy again, everything put back
                    quit_ = true;
                    return false;
                }
                fs::remove_all(antares::utf8::Path(game_dir_ + STAGING), code);
                if (!Start_({launcher})) {
                    *error = "updated, but couldn't start the new version: start it again";
                    return false;
                }
                quit_ = true;
                return true;
            }
            default:
                *error = "this kind of package updates from the release page";
                return false;
        }
    }

    // The title row's notice: "<version> is out", Update (or the release page), the progress.
    void c_launcher::Draw_Update_Notice_(float x, float y) {
        const ImVec2 at(x, y);
        std::lock_guard<std::mutex> lock(update_.mutex);
        if (!update_.available) return;
        const ImVec2 back = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(at);
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(chrome::AMBER, "%s is out", update_.release.version.c_str());
        if (!update_.release.intro.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", update_.release.intro.c_str());
        ImGui::SameLine();
        if (update_.downloading) {
            const float total = static_cast<float>(std::max<uint64_t>(update_.progress.total, 1));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, chrome::AMBER);
            ImGui::ProgressBar(std::min(1.0f, static_cast<float>(update_.progress.done) / total), ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel##update")) update_.progress.cancel = true;
        } else if (Update_Installable_()) {
            ImGui::BeginDisabled(game_ != nullptr || hd_.busy);
            if (ImGui::SmallButton("Update")) update_.start_requested = true; // started by Update_Poll_
            ImGui::EndDisabled();
        } else if (ImGui::SmallButton("Release page")) {
            (void)SDL_OpenURL(update_.release.page_url.c_str());
        }
        if (!update_.error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(chrome::RED, "%s", update_.error.c_str());
        }
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(back);
    }
} // namespace launcher
