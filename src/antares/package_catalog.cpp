#include "pch.h"

#include "antares/package_catalog.h"

#include <algorithm>
#include <filesystem>
#include <map>

#include "antares/packages.h"
#include "config/config_loader.h"
#include "game/vfs_detail.h"

namespace antares {
    namespace {
        // A file inside a package folder or zip, by its '/'-separated path.
        bool Read_Package_File_(const std::string& source_path, bool archive, const char* relative, std::string* bytes) {
            if (!archive) {
                size_t size = 0;
                void* data = SDL_LoadFile((source_path + relative).c_str(), &size);
                if (data == nullptr) {
                    return false;
                }
                bytes->assign(static_cast<const char*>(data), size);
                SDL_free(data);
                return true;
            }
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, source_path.c_str(), 0)) {
                return false;
            }
            size_t size = 0;
            void* data = mz_zip_reader_extract_file_to_heap(&zip, relative, &size, 0);
            mz_zip_reader_end(&zip);
            if (data == nullptr) {
                return false;
            }
            bytes->assign(static_cast<const char*>(data), size);
            mz_free(data);
            return true;
        }

        bool Has_Package_File_(const std::string& source_path, bool archive, const char* relative) {
            if (!archive) {
                std::error_code error;
                return std::filesystem::is_regular_file(std::filesystem::path(source_path + relative), error);
            }
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, source_path.c_str(), 0)) {
                return false;
            }
            const bool found = mz_zip_reader_locate_file(&zip, relative, nullptr, 0) >= 0;
            mz_zip_reader_end(&zip);
            return found;
        }

        void Describe_(s_package_info* package) {
            std::string text;
            if (!Read_Package_File_(package->source_path, package->archive, "mod.json5", &text)) {
                package->error = "mods/" + package->id + "/mod.json5: mod manifest not found";
                return;
            }
            config::s_cfg_diagnostic diagnostic;
            if (!config::Parse_Mod_Manifest(package->id, text, &package->manifest, &diagnostic)) {
                package->error = diagnostic.message.empty() ? "mods/" + package->id + "/mod.json5: invalid package ID" : Format_Diagnostic(diagnostic);
                return;
            }
            package->valid = true;
            package->art_only = Is_Art_Only_Package(package->id);
            // The game turns HD drawing on when an enabled package has an art pack (render.cpp).
            for (const char* marker : {"data/art/pack.json", "data/hd/pack.json", "data/art/index.json", "data/hd/index.json"}) {
                package->hd_art = package->hd_art || Has_Package_File_(package->source_path, package->archive, marker);
            }
            Read_Package_File_(package->source_path, package->archive, "README.md", &package->readme);
        }
    } // namespace

    std::vector<s_package_info> Discover_Packages() {
        std::vector<s_package_info> packages;
        const vfs_detail::s_vfs_state& state = vfs_detail::g_vfs_state;
        if (!state.initialized) {
            return packages;
        }
        const std::filesystem::path root = std::filesystem::path(state.base_path) / vfs_detail::VFS_MODS_PATH;
        std::error_code error;
        std::map<std::string, s_package_info> found; // sorted by id
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, error)) {
            const std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') {
                continue;
            }
            std::error_code type_error;
            s_package_info package;
            if (entry.is_directory(type_error)) {
                package.id = name;
                package.source_path = entry.path().string() + "/";
            } else if (entry.is_regular_file(type_error) && name.size() > 4 &&
                SDL_strcasecmp(name.c_str() + name.size() - 4, ".zip") == 0) {
                package.id = name.substr(0, name.size() - 4);
                package.source_path = entry.path().string();
                package.archive = true;
            } else {
                continue;
            }
            const auto existing = found.find(package.id);
            if (existing != found.end()) {
                existing->second.valid = false;
                existing->second.error = "package '" + package.id + "' has both directory and ZIP forms";
                continue;
            }
            found.emplace(package.id, std::move(package));
        }
        for (auto& [id, package] : found) {
            if (package.error.empty()) {
                Describe_(&package);
            }
            packages.push_back(std::move(package));
        }
        return packages;
    }

    s_config_check Check_Config(const std::string& entry_path) {
        s_config_check check;
        config::s_cfg_diagnostic diagnostic;
        auto effective = std::make_shared<config::s_effective_config>();
        check.ok = config::Check_Config_File(entry_path.c_str(), "base_game", &check.package_order, &diagnostic, effective.get());
        if (check.ok) {
            check.effective = std::move(effective);
        }
        if (!check.ok) {
            check.error = Format_Diagnostic(diagnostic);
            if (check.error.empty()) {
                check.error = entry_path + ": unable to load the configuration";
            }
        }
        return check;
    }

    s_config_check Check_Package(const std::string& package_id) {
        s_config_check check;
        config::s_cfg_diagnostic diagnostic;
        auto effective = std::make_shared<config::s_effective_config>();
        const std::string text = "enable " + package_id + ";\n";
        check.ok = config::Check_Config_Text("package preview", text, "base_game", &check.package_order, &diagnostic, effective.get());
        if (check.ok) {
            check.effective = std::move(effective);
        } else {
            check.error = Format_Diagnostic(diagnostic);
            if (check.error.empty()) check.error = "unable to load package " + package_id;
        }
        return check;
    }

    s_config_check Check_Save(const std::string& entry_path, const std::vector<config::s_mod_requirement>* required_mods, const config::s_config_state* state) {
        s_config_check check;
        config::s_cfg_diagnostic diagnostic;
        check.ok = config::Check_Save_Config_File(entry_path.c_str(), "base_game", required_mods, state, &check.package_order, &diagnostic);
        if (!check.ok) {
            check.error = Format_Diagnostic(diagnostic);
            if (check.error.empty()) {
                check.error = "unable to load the save's configuration";
            }
        }
        return check;
    }

    std::string Format_Diagnostic(const config::s_cfg_diagnostic& diagnostic) {
        const config::s_source_location& location = diagnostic.location;
        std::string text;
        if (!location.source.empty()) {
            text = location.source;
            if (location.offset > 0 || location.line > 1 || location.column > 1) {
                text += ":" + std::to_string(location.line) + ":" + std::to_string(location.column);
            }
            text += ": ";
        }
        return text + diagnostic.message;
    }
} // namespace antares
