#include "pch.h"

#include "launcher/launcher_app.h"

#include <imgui.h>

#include <filesystem>
#include <fstream>

#include "antares/game_install.h"
#include "antares/gog.h"
#include "antares/hd_packs.h"
#include "antares/user_folder.h"
#include "antares/utf8_files.h"

// GOG sign-in and download in the launcher (WP5.13b, D29 case 3). The sign-in window is a separate
// program (src/gog_signin/); this file runs the rest on a worker. Logs name the steps only: never
// the code, the token or an address that holds either, so a log is safe to share.
namespace launcher {
    namespace gi = antares::game_install;
    namespace gog = antares::gog;
    namespace hd = antares::hd_packs;

    namespace {
#if defined(_WIN32)
        constexpr gog::e_system NATIVE = gog::SYSTEM_WINDOWS;
        constexpr char SIGNIN_PROGRAM[] = "antares-gog-signin.exe";
#elif defined(__APPLE__)
        constexpr gog::e_system NATIVE = gog::SYSTEM_MAC;
        constexpr char SIGNIN_PROGRAM[] = "antares-gog-signin";
#else
        constexpr gog::e_system NATIVE = gog::SYSTEM_LINUX;
        constexpr char SIGNIN_PROGRAM[] = "antares-gog-signin";
#endif

        std::filesystem::path U8_(const std::string& text) {
            return antares::utf8::Path(text);
        }

        std::string Signin_Path_() {
            const char* base = SDL_GetBasePath();
            return std::string(base != nullptr ? base : "") + SIGNIN_PROGRAM;
        }

        void Wipe_(std::string* secret) {
            secret->assign(secret->size(), '\0');
            secret->clear();
        }
    } // namespace

    bool c_launcher::Gog_Available_() const {
        std::error_code error;
        return std::filesystem::is_regular_file(U8_(Signin_Path_()), error);
    }

    void c_launcher::Start_Gog_Signin(const std::string& dump_folder) {
        Open_Setup();
        gog_.dump_folder = dump_folder;
        Gog_Start_();
    }

    void c_launcher::Gog_Stop_() {
        gog_.progress.cancel = true;
        if (gog_.worker.joinable()) gog_.worker.join();
    }

    void c_launcher::Gog_Start_() {
        if (gog_.busy) return;
        if (gog_.worker.joinable()) gog_.worker.join();
        {
            std::lock_guard<std::mutex> lock(gog_.mutex);
            gog_.phase = "Opening GOG's sign-in window";
            gog_.error.clear();
            gog_.result_path.clear();
            gog_.finished = false;
            gog_.no_engine = false;
            gog_.total = 0;
        }
        gog_.progress.done = 0;
        gog_.progress.cancel = false;
        gog_.busy = true;
        setup_.page = SETUP_GOG;
        SDL_Log("gog: sign-in started");
        const std::string downloads = gi::Downloads_Folder();
        const std::string fallback_folder = antares::user_folder::Saves_Directory(game_dir_);
        gog_.worker = std::thread([this, downloads, fallback_folder]() {
            const auto phase = [&](const std::string& text) {
                std::lock_guard<std::mutex> lock(gog_.mutex);
                gog_.phase = text;
                SDL_Log("gog: %s", text.c_str());
            };
            const auto stop = [&](const std::string& why, bool no_engine = false) {
                std::lock_guard<std::mutex> lock(gog_.mutex);
                gog_.error = why;
                gog_.no_engine = no_engine;
                gog_.finished = true;
                SDL_Log("gog: stopped: %s", why.c_str()); // never holds the code or the token
            };
            const auto dump = [&](const char* name, const std::string& json) {
                if (gog_.dump_folder.empty()) return;
                std::error_code error;
                std::filesystem::create_directories(U8_(gog_.dump_folder), error);
                std::ofstream(U8_(gog_.dump_folder) / name, std::ios::binary) << json;
            };

            // 1. The sign-in window; its standard output is the one-time code.
            const std::string program = Signin_Path_();
            const char* argv[] = {program.c_str(), nullptr};
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
            SDL_Process* window = SDL_CreateProcessWithProperties(properties);
            SDL_DestroyProperties(properties);
            if (window == nullptr) {
                stop("GOG's sign-in window couldn't open on this system", true);
                gog_.busy = false;
                return;
            }
            phase("Sign in to GOG in the window that opened");
            std::string code;
            SDL_IOStream* output = SDL_GetProcessOutput(window);
            int exit_code = 0;
            for (bool exited = false;;) {
                char buffer[512];
                const size_t got = output != nullptr ? SDL_ReadIO(output, buffer, sizeof(buffer)) : 0;
                if (got > 0) {
                    code.append(buffer, got);
                    continue;
                }
                if (exited) break;
                if (SDL_WaitProcess(window, false, &exit_code)) {
                    exited = true; // one more read for what it wrote last
                    continue;
                }
                if (gog_.progress.cancel) {
                    SDL_KillProcess(window, true);
                    SDL_WaitProcess(window, true, &exit_code);
                    break;
                }
                SDL_Delay(100);
            }
            SDL_DestroyProcess(window);
            while (!code.empty() && (code.back() == '\n' || code.back() == '\r')) code.pop_back();
            if (gog_.progress.cancel) {
                Wipe_(&code);
                stop("Cancelled.");
                gog_.busy = false;
                return;
            }
            if (exit_code == 127 || (exit_code != 0 && exit_code != 1 && code.empty())) {
                stop("GOG's sign-in window couldn't open on this system (it needs WebKitGTK)", true);
                gog_.busy = false;
                return;
            }
            if (exit_code != 0 || code.empty()) {
                stop("The sign-in window was closed before signing in.");
                gog_.busy = false;
                return;
            }

            // 2. The code for a token. 3. The game in the library. 4. Its installer.
            phase("Signing in");
            std::string body, error, token, final_url;
            const bool signed_in = hd::Gog_Request(gog::Token_Url(code), "", "", &body, nullptr, nullptr, &error) && gog::Parse_Token(body, &token, &error);
            Wipe_(&code);
            Wipe_(&body);
            gog::s_product product;
            gog::s_download download;
            bool ok = signed_in;
            if (ok) {
                phase("Looking for Master of Orion II in your GOG library");
                ok = hd::Gog_Request(gog::LIBRARY_URL, token, "", &body, nullptr, nullptr, &error);
                if (ok) dump("library.json", body);
                ok = ok && gog::Choose_Product(body, &product, &error);
            }
            if (ok) {
                ok = hd::Gog_Request(gog::Details_Url(product.id), token, "", &body, nullptr, nullptr, &error);
                if (ok) dump("game-details.json", body);
                ok = ok && gog::Choose_Download(body, NATIVE, &download, &error);
            }
            // 5. The download, into Downloads (D29: a reinstall then needs no sign-in).
            const std::string folder = downloads.empty() ? fallback_folder : downloads;
            const std::string part = folder + "antares-gog-download.part";
            if (ok) {
                {
                    std::lock_guard<std::mutex> lock(gog_.mutex);
                    gog_.total = download.size;
                }
                phase("Downloading " + (download.name.empty() ? std::string("the installer") : download.name) + " into " + folder);
                ok = hd::Gog_Request("https://embed.gog.com" + download.path, token, part, nullptr, &final_url, &gog_.progress, &error);
            }
            Wipe_(&token); // the token's work is done: it is never kept (owner: "sign in fresh")
            std::error_code code_error;
            if (!ok) {
                std::filesystem::remove(U8_(part), code_error);
                stop(error == "cancelled" ? "Cancelled." : (signed_in ? "" : "Signing in failed: ") + error + ".");
                gog_.busy = false;
                return;
            }
            // 6. Its real name, then a check that it is a readable installer.
            const std::string name = gog::Installer_File_Name(final_url, download.system);
            Wipe_(&final_url); // a signed address
            const std::string path = folder + name;
            std::filesystem::rename(U8_(part), U8_(path), code_error);
            if (code_error) {
                std::filesystem::remove(U8_(part), code_error);
                stop("The download couldn't be saved in " + folder + ".");
                gog_.busy = false;
                return;
            }
            phase("Checking the installer");
            std::string check_error;
            if (!gi::Check_Installer(path, &check_error).ok()) {
                std::filesystem::remove(U8_(path), code_error);
                stop("The download isn't a readable installer (" + check_error + "); try again.");
                gog_.busy = false;
                return;
            }
            {
                std::lock_guard<std::mutex> lock(gog_.mutex);
                gog_.result_path = path;
                gog_.finished = true;
            }
            SDL_Log("gog: installer saved as %s", name.c_str());
            gog_.busy = false;
        });
    }

    void c_launcher::Gog_Poll_() {
        if (!gog_.finished || gog_.busy) return;
        if (gog_.worker.joinable()) gog_.worker.join();
        std::string path;
        {
            std::lock_guard<std::mutex> lock(gog_.mutex);
            path = gog_.result_path;
            if (!path.empty()) gog_.finished = false; // handled
        }
        if (!path.empty() && setup_.active && setup_.page == SETUP_GOG) Setup_Check_(path); // the checklist, then case 2
    }
} // namespace launcher
