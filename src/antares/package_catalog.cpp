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
        bool Read_Manifest_(const std::string& source_path, bool archive, std::string* bytes) {
            if (!archive) {
                size_t size = 0;
                void* data = SDL_LoadFile((source_path + "mod.json5").c_str(), &size);
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
            void* data = mz_zip_reader_extract_file_to_heap(&zip, "mod.json5", &size, 0);
            mz_zip_reader_end(&zip);
            if (data == nullptr) {
                return false;
            }
            bytes->assign(static_cast<const char*>(data), size);
            mz_free(data);
            return true;
        }

        void Describe_(s_package_info* package) {
            std::string text;
            if (!Read_Manifest_(package->source_path, package->archive, &text)) {
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
