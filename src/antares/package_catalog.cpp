#include "pch.h"

#include "antares/package_catalog.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

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
    namespace {
        const s_package_info* Find_Info_(const std::vector<s_package_info>& packages, const std::string& id) {
            for (const s_package_info& package : packages) {
                if (package.id == id) return package.valid ? &package : nullptr;
            }
            return nullptr;
        }

        // What the game loads for one enable line: the package and its dependencies, recursively
        // (Resolve_Package in config/mod_manager.cpp; optional ones only when installed).
        std::set<std::string> Loads_(const std::vector<s_package_info>& packages, const std::string& id) {
            std::set<std::string> loads;
            std::vector<std::string> pending = {id};
            while (!pending.empty()) {
                const std::string next = pending.back();
                pending.pop_back();
                if (!loads.insert(next).second) continue;
                const s_package_info* package = Find_Info_(packages, next);
                if (package == nullptr) continue;
                for (const config::s_mod_dependency& dependency : package->manifest.dependencies) pending.push_back(dependency.id);
                for (const config::s_mod_dependency& dependency : package->manifest.optional_dependencies) {
                    if (Find_Info_(packages, dependency.id) != nullptr) pending.push_back(dependency.id);
                }
            }
            return loads;
        }

        bool Declares_Conflict_(const std::vector<s_package_info>& packages, const std::string& id, const std::string& other) {
            const s_package_info* package = Find_Info_(packages, id);
            if (package == nullptr) return false;
            const std::vector<std::string>& conflicts = package->manifest.conflicts;
            return std::find(conflicts.begin(), conflicts.end(), other) != conflicts.end();
        }

        // The first conflict between what `left_id` and `right_id` load, as a reason for switching
        // off `right_id` ("mirror conflicts with mirror_hw", "needs mirror_hw, which conflicts
        // with mirror"), or "".
        std::string Conflict_(const std::vector<s_package_info>& packages, const std::string& left_id, const std::set<std::string>& left,
            const std::string& right_id, const std::set<std::string>& right) {
            for (const std::string& a : left) {
                for (const std::string& b : right) {
                    if (a == b || (!Declares_Conflict_(packages, a, b) && !Declares_Conflict_(packages, b, a))) continue;
                    std::string reason = b == right_id ? b : "needs " + b + ", which";
                    reason += " conflicts with " + a;
                    if (a != left_id) reason += " (needed by " + left_id + ")";
                    return reason;
                }
            }
            return {};
        }
    } // namespace

    s_package_toggle Toggle_Package(const std::vector<s_package_info>& packages, const std::vector<std::string>& enabled, const std::string& package_id, bool on) {
        s_package_toggle result;
        const auto keep = [&](const std::string& id) { result.enabled.push_back(id); };
        if (on) {
            const std::set<std::string> loads = Loads_(packages, package_id);
            for (const std::string& id : enabled) {
                if (id == package_id) continue;
                const std::string conflict = Conflict_(packages, package_id, loads, id, Loads_(packages, id));
                if (conflict.empty() || loads.contains(id)) {
                    keep(id);
                } else {
                    result.removed.push_back(id);
                    result.reasons.push_back(conflict);
                }
            }
            keep(package_id);
        } else {
            for (const std::string& id : enabled) {
                if (id == package_id) continue;
                if (Loads_(packages, id).contains(package_id)) {
                    result.removed.push_back(id);
                    result.reasons.push_back(id + " needs " + package_id);
                } else {
                    keep(id);
                }
            }
        }
        return result;
    }
} // namespace antares
