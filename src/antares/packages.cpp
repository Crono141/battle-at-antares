#include "pch.h"

#include "antares/packages.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#include "game/vfs.h"
#include "game/vfs_detail.h"
#include "yyjson.h"

namespace antares {
    namespace {
        // Where a package lives on disk: data/mods/<id>/ or data/mods/<id>.zip.
        struct s_package_source {
            std::string path; // directory paths end in '/'
            bool archive = false;
        };

        std::string Lower_(std::string text) {
            for (char& character : text) {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            return text;
        }

        bool Find_Source_(const std::string& package_id, s_package_source* source) {
            const vfs_detail::s_vfs_state& state = vfs_detail::g_vfs_state;
            if (!state.initialized) {
                return false;
            }
            const std::string relative = std::string(vfs_detail::VFS_MODS_PATH) + package_id;
            char candidate[vfs_detail::VFS_MAX_PATH];
            char resolved[vfs_detail::VFS_MAX_PATH];
            if (vfs_detail::Build_Candidate_Path_(state.base_path, "", relative.c_str(), candidate, sizeof(candidate)) &&
                vfs_detail::Resolve_Candidate_Path_(candidate, resolved, sizeof(resolved), vfs_detail::VFS_CANDIDATE_DIRECTORY)) {
                *source = {std::string(resolved) + "/", false};
                return true;
            }
            const std::string archive = relative + ".zip";
            if (vfs_detail::Build_Candidate_Path_(state.base_path, "", archive.c_str(), candidate, sizeof(candidate)) &&
                vfs_detail::Resolve_Candidate_Path_(candidate, resolved, sizeof(resolved))) {
                *source = {resolved, true};
                return true;
            }
            return false;
        }

        bool Read_File_(const s_package_source& source, const std::string& relative, std::string* bytes) {
            if (!source.archive) {
                size_t size = 0;
                void* data = SDL_LoadFile((source.path + relative).c_str(), &size);
                if (data == nullptr) {
                    return false;
                }
                bytes->assign(static_cast<const char*>(data), size);
                SDL_free(data);
                return true;
            }
            mz_zip_archive archive{};
            if (!mz_zip_reader_init_file(&archive, source.path.c_str(), 0)) {
                return false;
            }
            size_t size = 0;
            void* data = mz_zip_reader_extract_file_to_heap(&archive, relative.c_str(), &size, 0);
            mz_zip_reader_end(&archive);
            if (data == nullptr) {
                return false;
            }
            bytes->assign(static_cast<const char*>(data), size);
            mz_free(data);
            return true;
        }

        // `relative` is a file path inside the package, '/'-separated.
        bool Is_Art_Path_(const std::string& relative, const std::string& config_path) {
            const std::string path = Lower_(relative);
            return path == "mod.json5" || path == Lower_(config_path) ||
                path.rfind("data/hd/", 0) == 0 || path.rfind("data/art/", 0) == 0;
        }

        // Folders that may hold files: the package root, the config file's folder and data/.
        // data/hd/ and data/art/ themselves are never descended into (they hold thousands of files).
        bool Is_Allowed_Folder_(const std::string& relative, const std::string& config_path) {
            const std::string path = Lower_(relative);
            const std::string config = Lower_(config_path);
            const size_t slash = config.rfind('/');
            return path == "data" || (slash != std::string::npos && path == config.substr(0, slash));
        }

        bool Directory_Is_Art_Only_(const std::string& root, const std::string& relative, const std::string& config_path) {
            std::error_code error;
            const std::filesystem::path folder = std::filesystem::path(relative.empty() ? root : root + relative);
            for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error)) {
                const std::string name = entry->path().filename().string();
                const std::string child = relative.empty() ? name : relative + "/" + name;
                if (entry->is_directory(error)) {
                    const std::string path = Lower_(child);
                    if (path != "data/hd" && path != "data/art" &&
                        !(Is_Allowed_Folder_(child, config_path) && Directory_Is_Art_Only_(root, child, config_path))) {
                        return false;
                    }
                } else if (!Is_Art_Path_(child, config_path)) {
                    return false;
                }
            }
            return !error;
        }

        bool Archive_Is_Art_Only_(const std::string& path, const std::string& config_path) {
            mz_zip_archive archive{};
            if (!mz_zip_reader_init_file(&archive, path.c_str(), 0)) {
                return false;
            }
            bool art_only = true;
            const mz_uint file_count = mz_zip_reader_get_num_files(&archive);
            for (mz_uint index = 0; art_only && index < file_count; ++index) {
                mz_zip_archive_file_stat stat;
                if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
                    art_only = false;
                } else if (!stat.m_is_directory) {
                    std::string entry = stat.m_filename;
                    for (char& character : entry) {
                        if (character == '\\') character = '/';
                    }
                    art_only = Is_Art_Path_(entry, config_path);
                }
            }
            mz_zip_reader_end(&archive);
            return art_only;
        }

        bool Files_Are_Art_Only_(const std::string& package_id, const std::string& config_path) {
            // Scanning costs a few directory reads; the answer only changes when the package is
            // replaced, which its source stamp tracks (available once the package is mounted).
            static std::map<std::string, std::pair<std::string, bool>> cache;
            std::string stamp;
            const bool stamped = vfs::Get_Package_Source_Stamp(package_id.c_str(), &stamp);
            if (stamped) {
                const auto found = cache.find(package_id);
                if (found != cache.end() && found->second.first == stamp + config_path) {
                    return found->second.second;
                }
            }
            s_package_source source;
            const bool art_only = Find_Source_(package_id, &source) &&
                (source.archive ? Archive_Is_Art_Only_(source.path, config_path) : Directory_Is_Art_Only_(source.path, "", config_path));
            if (stamped) {
                cache[package_id] = {stamp + config_path, art_only};
            }
            return art_only;
        }

        bool Is_Empty_Array_Or_Absent_(yyjson_val* value) {
            return value == nullptr || (yyjson_is_arr(value) && yyjson_arr_size(value) == 0);
        }
    } // namespace

    bool Is_Art_Only_Package(const config::s_mod_manifest& manifest, const config::s_cfg_document& document) {
        const bool art_only = document.statements.empty() && !manifest.has_lua_runtime && manifest.dependencies.empty() &&
            manifest.optional_dependencies.empty() && Files_Are_Art_Only_(manifest.id, manifest.config);
        static std::map<std::string, bool> logged;
        const auto found = logged.find(manifest.id);
        if (found == logged.end() || found->second != art_only) {
            logged[manifest.id] = art_only;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "packages: '%s' is %s", manifest.id.c_str(), art_only ? "art-only (not recorded in saves)" : "recorded in saves");
        }
        return art_only;
    }

    bool Is_Art_Only_Package(const std::string& package_id) {
        s_package_source source;
        std::string manifest_text;
        if (package_id.empty() || !Find_Source_(package_id, &source) || !Read_File_(source, "mod.json5", &manifest_text)) {
            return false;
        }
        // A light read of the fields that matter; the resolver validates the whole manifest.
        const yyjson_read_flag flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS |
            YYJSON_READ_ALLOW_EXT_NUMBER | YYJSON_READ_ALLOW_SINGLE_QUOTED_STR | YYJSON_READ_ALLOW_UNQUOTED_KEY;
        yyjson_doc* json = yyjson_read_opts(manifest_text.data(), manifest_text.size(), flags, nullptr, nullptr);
        if (json == nullptr) {
            return false;
        }
        yyjson_val* root = yyjson_doc_get_root(json);
        yyjson_val* config = yyjson_obj_get(root, "config");
        const bool rules_free = yyjson_is_str(config) && yyjson_obj_get(root, "lua") == nullptr &&
            Is_Empty_Array_Or_Absent_(yyjson_obj_get(root, "dependencies")) &&
            Is_Empty_Array_Or_Absent_(yyjson_obj_get(root, "optional_dependencies"));
        const std::string config_path = rules_free ? yyjson_get_str(config) : "";
        yyjson_doc_free(json);
        if (!rules_free) {
            return false;
        }
        std::string config_text;
        config::s_cfg_document document;
        config::s_cfg_diagnostic diagnostic;
        if (!Read_File_(source, config_path, &config_text) ||
            !config::Parse_Cfg(config_path.c_str(), config_text.data(), config_text.size(), &document, &diagnostic) ||
            !document.statements.empty()) {
            return false;
        }
        return Files_Are_Art_Only_(package_id, config_path);
    }

    const orion2re::lua::s_lua_save_snapshot* Without_Art_Packages(const orion2re::lua::s_lua_save_snapshot* snapshot, const orion2re::lua::s_lua_registry& active, orion2re::lua::s_lua_save_snapshot* storage) {
        if (snapshot == nullptr) {
            return nullptr;
        }
        const auto is_dropped = [&](const orion2re::lua::s_lua_package_record& package) {
            for (const orion2re::lua::s_lua_package_record& kept : active.packages_high_to_low_priority) {
                if (kept.id == package.id) return false;
            }
            for (const orion2re::lua::s_lua_runtime_package_record& runtime : snapshot->registry.runtime_packages) {
                if (runtime.package_id == package.id) return false;
            }
            return Is_Art_Only_Package(package.id);
        };
        const auto& packages = snapshot->registry.packages_high_to_low_priority;
        if (std::none_of(packages.begin(), packages.end(), is_dropped)) {
            return snapshot;
        }
        *storage = *snapshot;
        auto& kept = storage->registry.packages_high_to_low_priority;
        kept.erase(std::remove_if(kept.begin(), kept.end(), is_dropped), kept.end());
        return storage;
    }
} // namespace antares
