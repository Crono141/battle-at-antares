#include "pch.h"

#include "antares/save_summary.h"

#include <filesystem>

#include "config/config_snapshot.h"
#include "config/text_encoding.h"
#include "game/platform.h"
#include "game/save_sections.h"
#include "game/savegame.h"
#include "game/serialization.h"
#include "game/vfs_detail.h"

namespace antares::saves {
    namespace {
        constexpr size_t k_description_size = sizeof(SAVEGAME::s_metadata::description);
        constexpr size_t k_versioned_header_size = 4 + k_description_size + 4 + 1 + 2;
        constexpr uint16_t k_config_snapshot_version = 3; // savegame.cpp CONFIG_SNAPSHOT_VERSION
        constexpr uint16_t k_lua_snapshot_version = 5;    // savegame.cpp LUA_SAVE_SNAPSHOT_VERSION

        uint32_t Load_U32_(const uint8_t* bytes) {
            return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
                static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
        }

        // The save name as the game draws it: CP850, with font colour codes (bytes below 0x20).
        std::string Description_(const char* raw) {
            std::string visible;
            for (size_t index = 0; index < k_description_size && raw[index] != '\0'; ++index) {
                if (static_cast<uint8_t>(raw[index]) >= 0x20) visible.push_back(raw[index]);
            }
            std::string utf8;
            config::Decode_Cp850_To_Utf8(visible.data(), visible.size(), &utf8);
            return utf8;
        }

        // Reads the config snapshot at `data`. Its parser exits the process on a truncated record
        // (SERIAL::Reader_ fails through exitm); the engine turns that into an exception, which is
        // caught here so a damaged save can't close the launcher.
        bool Read_Snapshot_(const uint8_t* data, size_t size, config::s_config_snapshot* snapshot) {
            try {
                SERIAL::Reader_ reader(data, static_cast<uint32_t>(size), "damaged configuration snapshot");
                return config::Read_Config_Snapshot(&reader, &config::saved_config_snapshot, snapshot) && reader.Remaining() == 0;
            } catch (const platform::process_exit_requested&) {
                platform::Reset_Exit();
                return false;
            } catch (const std::exception&) { // the unit tests' exitm throws this instead
                return false;
            }
        }

        // Saves from version 5 end with an extension block (save_sections.h): a header, then
        // sections. The game finds it by reading the whole game state first; the launcher finds it
        // by its magic number, accepting only a block that ends exactly at the end of the file.
        bool Find_Extension_(const uint8_t* data, size_t size, SAVEGAME::s_section_view* sections, uint32_t* count) {
            if (size < k_versioned_header_size + SAVEGAME::EXTENSION_HEADER_SIZE) return false;
            for (size_t offset = size - SAVEGAME::EXTENSION_HEADER_SIZE + 1; offset-- > k_versioned_header_size;) {
                if (Load_U32_(data + offset) != SAVEGAME::EXTENSION_MAGIC) continue;
                const uint32_t payload = Load_U32_(data + offset + 8);
                if (static_cast<uint64_t>(offset) + SAVEGAME::EXTENSION_HEADER_SIZE + payload != size) continue;
                SERIAL::Reader_ reader(data + offset, static_cast<uint32_t>(size - offset), "damaged save extension");
                std::string diagnostic;
                if (SAVEGAME::Read_Extension(&reader, sections, SAVEGAME::MAX_EXTENSION_SECTIONS, count, &diagnostic) &&
                    reader.Remaining() == 0) {
                    return true;
                }
            }
            return false;
        }

        // Versions 3 and 4 end with the config snapshot itself (no extension block).
        bool Find_Trailing_Snapshot_(const uint8_t* data, size_t size, config::s_config_snapshot* snapshot) {
            constexpr uint32_t k_snapshot_magic = 0x4746434f; // config_snapshot.cpp CONFIG_SNAPSHOT_MAGIC
            if (size < k_versioned_header_size + 16) return false;
            for (size_t offset = size - 16 + 1; offset-- > k_versioned_header_size;) {
                if (Load_U32_(data + offset) != k_snapshot_magic) continue;
                const uint32_t header_size = static_cast<uint32_t>(data[offset + 6]) | static_cast<uint32_t>(data[offset + 7]) << 8;
                const uint32_t payload = Load_U32_(data + offset + 8);
                if (static_cast<uint64_t>(offset) + header_size + payload != size) continue;
                if (Read_Snapshot_(data + offset, size - offset, snapshot)) return true;
            }
            return false;
        }
    } // namespace

    bool Read_Save_Summary(const uint8_t* data, size_t size, s_save_summary* summary) {
        summary->readable = false;
        summary->error.clear();
        summary->packages.clear();
        summary->state.reset();
        summary->records_packages = false;
        if (data == nullptr || size < k_versioned_header_size - 2) {
            summary->error = "not a save (too short)";
            return false;
        }
        const uint32_t header = Load_U32_(data);
        if (header != SAVEGAME::LEGACY_HEADER && header != SAVEGAME::VERSIONED_HEADER) {
            summary->error = "not a save (unknown header)";
            return false;
        }
        char description[k_description_size];
        memcpy(description, data + 4, sizeof(description));
        summary->description = Description_(description);
        summary->stardate = static_cast<int32_t>(Load_U32_(data + 4 + k_description_size));
        summary->game_type = static_cast<int8_t>(data[4 + k_description_size + 4]);
        summary->version = header == SAVEGAME::LEGACY_HEADER
            ? 0
            : static_cast<uint16_t>(data[k_versioned_header_size - 2] | data[k_versioned_header_size - 1] << 8);
        if (summary->version > SAVEGAME::CURRENT_VERSION) {
            summary->error = "made by a newer version of the game (save format " + std::to_string(summary->version) + ")";
            return false;
        }

        config::s_config_snapshot snapshot;
        if (summary->version >= k_lua_snapshot_version) {
            SAVEGAME::s_section_view sections[SAVEGAME::MAX_EXTENSION_SECTIONS];
            uint32_t count = 0;
            if (!Find_Extension_(data, size, sections, &count)) {
                summary->error = "damaged (the package list can't be found)";
                return false;
            }
            bool found = false;
            for (uint32_t index = 0; index < count && !found; ++index) {
                if (sections[index].type == SAVEGAME::SECTION_TYPE_CONFIG && sections[index].version == SAVEGAME::SECTION_FORMAT_VERSION) {
                    found = Read_Snapshot_(sections[index].data, sections[index].size, &snapshot);
                    if (!found) {
                        summary->error = "damaged (its settings can't be read)";
                        return false;
                    }
                }
            }
            if (!found || !snapshot.has_required_mods) {
                summary->error = "damaged (required config or Lua save metadata is missing)";
                return false;
            }
        } else if (summary->version >= k_config_snapshot_version) {
            Find_Trailing_Snapshot_(data, size, &snapshot); // optional in these versions
        }

        if (summary->version < k_config_snapshot_version || snapshot.state.values.size() == 0) {
            // No saved settings: the game loads it with the default rules and no packages.
            summary->records_packages = true;
        } else {
            summary->records_packages = snapshot.has_required_mods;
            summary->packages = std::move(snapshot.required_mods);
            summary->state = std::make_shared<const config::s_config_state>(std::move(snapshot.state));
        }
        summary->readable = true;
        return true;
    }

    s_save_summary Load_Save_Summary(int slot) {
        s_save_summary summary;
        summary.slot = slot;
        const vfs_detail::s_vfs_state& state = vfs_detail::g_vfs_state;
        if (!state.initialized || slot < 1 || slot > k_slot_count) return summary;
        // The game opens saves case-insensitively in its working folder, the game folder.
        const std::filesystem::path folder(state.base_path);
        const std::string wanted = "save" + std::to_string(slot) + ".gam";
        std::error_code error;
        std::filesystem::path path;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error)) {
            std::string name = entry.path().filename().string();
            for (char& character : name) character = static_cast<char>(SDL_tolower(static_cast<unsigned char>(character)));
            if (name == wanted && entry.is_regular_file(error)) {
                path = entry.path();
                if (entry.path().filename().string() == "SAVE" + std::to_string(slot) + ".GAM") break; // exact name wins
            }
        }
        if (path.empty()) return summary;
        summary.exists = true;
        SDL_PathInfo info{};
        if (SDL_GetPathInfo(path.string().c_str(), &info)) summary.modified = info.modify_time;
        size_t size = 0;
        void* data = SDL_LoadFile(path.string().c_str(), &size);
        if (data == nullptr) {
            summary.error = std::string("can't be read: ") + SDL_GetError();
            return summary;
        }
        if (size > SAVEGAME::BUFFER_CAPACITY) {
            summary.error = "damaged (larger than any save)";
        } else {
            Read_Save_Summary(static_cast<const uint8_t*>(data), size, &summary);
        }
        SDL_free(data);
        return summary;
    }

    std::string Format_Stardate(int32_t stardate) {
        const int32_t whole = stardate / 10;
        const int32_t tenth = stardate % 10 < 0 ? -(stardate % 10) : stardate % 10;
        return std::to_string(whole) + "." + std::to_string(tenth);
    }
} // namespace antares::saves
