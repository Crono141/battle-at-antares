#include "pch.h"

#include "launcher/launcher_menu.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "antares/utf8_files.h"
#include "k_launcher_icon.h"

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace launcher::menu {
    namespace {
        std::string With_Separator_(std::string folder) {
            if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
            return folder;
        }

#if defined(_WIN32)
        std::wstring Wide_(const std::string& text) {
            const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            std::wstring wide(static_cast<size_t>(count > 0 ? count : 1), L'\0');
            if (count > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), count);
            wide.resize(wide.size() - 1);
            return wide;
        }

        // %APPDATA%\Microsoft\Windows\Start Menu\Programs\Battle at Antares.lnk
        std::filesystem::path Shortcut_() {
            PWSTR folder = nullptr;
            std::filesystem::path path;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &folder))) {
                path = std::filesystem::path(folder) / (std::wstring(L"Battle at Antares") + L".lnk");
            }
            CoTaskMemFree(folder);
            return path;
        }
#elif !defined(__APPLE__)
        std::string Data_Home_() {
            const char* data = SDL_getenv("XDG_DATA_HOME");
            if (data != nullptr && data[0] == '/') return With_Separator_(data);
            const char* home = SDL_getenv("HOME");
            return home != nullptr && home[0] != '\0' ? With_Separator_(home) + ".local/share/" : std::string();
        }

        // The same file install-desktop-entry.sh writes, so the two never make two entries.
        std::string Entry_() {
            const std::string data = Data_Home_();
            return data.empty() ? std::string() : data + "applications/battle-at-antares.desktop";
        }

        // What the entry starts: the AppImage itself when run from one (its files are mounted
        // somewhere else each time), otherwise the launcher beside the game.
        std::string Program_(const std::string& game_dir) {
            const char* appimage = SDL_getenv("APPIMAGE");
            if (appimage != nullptr && appimage[0] == '/') return appimage;
            return With_Separator_(game_dir) + "antares-launcher";
        }

        // Another menu entry (not ours) that starts `program`.
        bool Other_Entry_For_(const std::string& program) {
            const std::string folder = Data_Home_() + "applications";
            std::error_code error;
            for (std::filesystem::directory_iterator entry(antares::utf8::Path(folder), error), end; !error && entry != end; entry.increment(error)) {
                const std::string path = antares::utf8::Text(entry->path());
                if (path.size() < 8 || path.compare(path.size() - 8, 8, ".desktop") != 0 || path == Entry_()) continue;
                std::ifstream file(entry->path(), std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (text.find(program) != std::string::npos) return true;
            }
            return false;
        }

        // Desktop entry Exec quoting: the path in double quotes, with \ " ` $ escaped.
        std::string Quote_(const std::string& path) {
            std::string quoted = "\"";
            for (char c : path) {
                if (c == '\\' || c == '"' || c == '`' || c == '$') quoted += '\\';
                quoted += c;
            }
            return quoted + "\"";
        }
#endif
    } // namespace

    bool Offered(const std::string& game_dir) {
#if defined(_WIN32)
        // The installer asks for itself (and its uninstaller removes what it made).
        std::error_code error;
        return !std::filesystem::exists(antares::utf8::Path(With_Separator_(game_dir) + "unins000.exe"), error);
#elif defined(__APPLE__)
        (void)game_dir;
        return false;
#else
        (void)game_dir;
        if (SDL_getenv("FLATPAK_ID") != nullptr || Entry_().empty()) return false;
        // An AppImage that an integrator (Gear Lever, AppImageLauncher, appimaged) put in the menu:
        // they start it with DESKTOPINTEGRATION set, and their entry starts the file (owner
        // 2026-11-25: two Battle at Antares entries after Gear Lever).
        if (SDL_getenv("APPIMAGE") != nullptr) {
            if (SDL_getenv("DESKTOPINTEGRATION") != nullptr) return false;
            if (Other_Entry_For_(SDL_getenv("APPIMAGE"))) return false;
        }
        return true;
#endif
    }

    bool Exists(const std::string& game_dir) {
#if defined(_WIN32)
        (void)game_dir;
        std::error_code error;
        const std::filesystem::path shortcut = Shortcut_();
        return !shortcut.empty() && std::filesystem::exists(shortcut, error);
#elif defined(__APPLE__)
        (void)game_dir;
        return true;
#else
        // An entry for this copy (another copy's entry is replaced when the player ticks the box).
        std::ifstream file(antares::utf8::Path(Entry_()), std::ios::binary);
        if (!file) return false;
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return text.find("Exec=" + Quote_(Program_(game_dir))) != std::string::npos;
#endif
    }

    bool Add(const std::string& game_dir, std::string* error) {
#if defined(_WIN32)
        const std::filesystem::path shortcut = Shortcut_();
        if (shortcut.empty()) {
            *error = "Windows didn't say where the Start menu is.";
            return false;
        }
        const std::wstring program = Wide_(With_Separator_(game_dir) + "antares-launcher.exe");
        const std::wstring folder = Wide_(game_dir);
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IShellLinkW* link = nullptr;
        bool ok = SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)));
        if (ok) {
            link->SetPath(program.c_str());
            link->SetWorkingDirectory(folder.c_str());
            link->SetIconLocation(program.c_str(), 0);
            link->SetDescription(L"Battle at Antares");
            IPersistFile* file = nullptr;
            ok = SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)));
            if (ok) {
                ok = SUCCEEDED(file->Save(shortcut.c_str(), TRUE));
                file->Release();
            }
            link->Release();
        }
        if (SUCCEEDED(init)) CoUninitialize();
        if (!ok) *error = "Couldn't make the Start menu entry.";
        return ok;
#elif defined(__APPLE__)
        (void)game_dir;
        *error = "On a Mac, move Battle at Antares into Applications.";
        return false;
#else
        const std::string entry = Entry_();
        const std::string data = Data_Home_();
        std::error_code code;
        // The icon in the user's icon theme folder, so the entry needs no path into this copy.
        const std::filesystem::path icon_dir = antares::utf8::Path(data + "icons/hicolor/128x128/apps");
        std::filesystem::create_directories(icon_dir, code);
        {
            std::ofstream icon(icon_dir / "battle-at-antares.png", std::ios::binary | std::ios::trunc);
            icon.write(reinterpret_cast<const char*>(k_launcher_icon_data), static_cast<std::streamsize>(k_launcher_icon_size));
        }
        std::filesystem::create_directories(antares::utf8::Path(data + "applications"), code);
        std::string text = "[Desktop Entry]\nType=Application\nVersion=1.5\nName=Battle at Antares\n";
        text += "GenericName=Space strategy game\n";
        text += "Comment=Choose packages and settings, then start the game\n";
        text += "Exec=" + Quote_(Program_(game_dir)) + "\n";
        // No working folder for an AppImage: its files are mounted somewhere new each run (the
        // folder would be gone when the entry is used).
        if (SDL_getenv("APPIMAGE") == nullptr) text += "Path=" + game_dir + "\n";
        text += "Icon=battle-at-antares\nTerminal=false\nCategories=Game;StrategyGame;\nKeywords=Master of Orion;MOO2;4X;\n";
        std::ofstream file(antares::utf8::Path(entry), std::ios::binary | std::ios::trunc);
        if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
            *error = "Couldn't write " + entry + ".";
            return false;
        }
        SDL_Log("launcher: applications menu entry %s", entry.c_str());
        return true;
#endif
    }
} // namespace launcher::menu
