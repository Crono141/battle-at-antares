#include "pch.h"

#include "antares/game_install.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <set>

#include "antares/game_files.h"
#include "LzmaDec.h"
#include "miniz.h"
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
        // The exact places first (the registry, Heroic's list, Steam's libraries), then the broad
        // folders searched deeper.
        std::vector<s_root> roots = registry_roots;
        std::vector<s_root> broad;
        const fs::path home = Path_(home_text);
#if defined(_WIN32)
        broad.push_back({"C:/GOG Games/", "GOG", 2});
        broad.push_back({"C:/Program Files (x86)/GOG Galaxy/Games/", "GOG Galaxy", 2});
        broad.push_back({"C:/Program Files/GOG Galaxy/Games/", "GOG Galaxy", 2});
#else
        if (!home_text.empty()) {
            broad.push_back({Folder_Text_(home / "GOG Games"), "GOG", 2});
            broad.push_back({Folder_Text_(home / "Games"), "Heroic or Lutris", 5});
            broad.push_back({Folder_Text_(home / ".wine/drive_c"), "Wine", 4});
            for (const char* config : {".config/heroic/gog_store/installed.json",
                     ".var/app/com.heroicgameslauncher.hgl/config/heroic/gog_store/installed.json"}) {
                for (const std::string& path : Heroic_Paths_(Read_File_(home / config))) {
                    roots.push_back({path, "Heroic", 2});
                }
            }
#if defined(__APPLE__)
            broad.push_back({"/Applications/", "GOG", 6});
            broad.push_back({Folder_Text_(home / "Applications"), "GOG", 6});
#endif
        }
#endif
        for (const std::string& library : Steam_Libraries(home_text, registry_roots)) {
            roots.push_back({Folder_Text_(Path_(library) / "steamapps/common"), "Steam", 2});
        }
        roots.insert(roots.end(), broad.begin(), broad.end());
        return roots;
    }

    std::vector<std::string> Steam_Libraries(const std::string& home_text, const std::vector<s_root>& registry_roots) {
        std::vector<fs::path> steam_dirs;
        for (const s_root& root : registry_roots) {
            if (root.source == "Steam") steam_dirs.push_back(Path_(root.path));
        }
#if defined(_WIN32)
        (void)home_text;
        steam_dirs.push_back("C:/Program Files (x86)/Steam");
#else
        if (!home_text.empty()) {
            const fs::path home = Path_(home_text);
            steam_dirs.push_back(home / ".local/share/Steam");
            steam_dirs.push_back(home / ".steam/steam");
            steam_dirs.push_back(home / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
            steam_dirs.push_back(home / "Library/Application Support/Steam");
        }
#endif
        std::vector<std::string> libraries;
        std::set<std::string> seen;
        const auto add = [&](const fs::path& path) {
            std::error_code error;
            if (!fs::is_directory(path / "steamapps", error)) return;
            const fs::path canonical = fs::weakly_canonical(path, error);
            const std::string key = Folder_Text_(error ? path : canonical);
            if (seen.insert(key).second) libraries.push_back(key);
        };
        for (const fs::path& steam : steam_dirs) {
            add(steam);
            for (const std::string& library : Parse_Steam_Libraries(Read_File_(steam / "steamapps/libraryfolders.vdf"))) {
                add(Path_(library));
            }
        }
        return libraries;
    }

    std::string Vdf_Value(const std::string& text, const std::string& key) {
        const std::string quoted = "\"" + key + "\"";
        size_t at = 0;
        while ((at = text.find(quoted, at)) != std::string::npos) {
            const size_t open = text.find('"', at + quoted.size());
            const size_t close = open == std::string::npos ? open : text.find('"', open + 1);
            const size_t line_end = text.find('\n', at);
            if (open != std::string::npos && close != std::string::npos && (line_end == std::string::npos || open < line_end)) {
                return text.substr(open + 1, close - open - 1);
            }
            at += quoted.size();
        }
        return "";
    }

    s_steam_app Steam_App(const std::vector<std::string>& libraries, int app_id) {
        s_steam_app app;
        for (const std::string& library : libraries) {
            const fs::path manifest = Path_(library) / "steamapps" / ("appmanifest_" + std::to_string(app_id) + ".acf");
            const std::string text = Read_File_(manifest);
            if (text.empty()) continue;
            app.known = true;
            const unsigned long flags = std::strtoul(Vdf_Value(text, "StateFlags").c_str(), nullptr, 10);
            // 4: fully installed; 2: update required, 1024: update running (Steam's EAppState).
            app.installed = (flags & 4u) != 0 && (flags & (2u | 1024u)) == 0;
            app.done = std::strtoull(Vdf_Value(text, "BytesDownloaded").c_str(), nullptr, 10);
            app.total = std::strtoull(Vdf_Value(text, "BytesToDownload").c_str(), nullptr, 10);
            const std::string folder = Vdf_Value(text, "installdir");
            if (!folder.empty()) app.folder = Folder_Text_(Path_(library) / "steamapps" / "common" / Path_(folder));
            if (app.installed) break; // the copy that is there wins over one being installed elsewhere
        }
        return app;
    }

    std::vector<s_candidate> Detect(const std::vector<s_root>& roots) {
        std::vector<s_candidate> found;
        std::set<std::string> seen;
        constexpr size_t VISIT_LIMIT = 4000; // folders per root: a huge ~/Games can't starve the rest
        for (const s_root& root : roots) {
            size_t visited = 0;
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

    e_installer_kind Installer_Kind(const std::string& file_name) {
        std::string name = file_name;
        for (char& character : name) character = static_cast<char>(SDL_tolower(static_cast<unsigned char>(character)));
        if (name.find("master_of_orion_2") == std::string::npos) return INSTALLER_NONE;
        const auto ends = [&](const char* tail) {
            const size_t length = std::strlen(tail);
            return name.size() > length && name.compare(name.size() - length, length, tail) == 0;
        };
        if (ends(".sh")) return INSTALLER_LINUX;
        if (ends(".exe")) return INSTALLER_WINDOWS;
        if (ends(".pkg")) return INSTALLER_MAC;
        return INSTALLER_NONE;
    }

    std::vector<s_installer> Find_Installers(const std::string& folder_text) {
        std::vector<std::pair<fs::file_time_type, s_installer>> found;
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(Path_(folder_text), error)) {
            std::error_code type_error;
            if (!entry.is_regular_file(type_error)) continue;
            s_installer installer;
            installer.name = Text_(entry.path().filename());
            installer.kind = Installer_Kind(installer.name);
            if (installer.kind == INSTALLER_NONE) continue;
            installer.path = Text_(entry.path());
            installer.size = entry.file_size(type_error);
            found.emplace_back(entry.last_write_time(type_error), installer);
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<s_installer> installers;
        for (auto& [time, installer] : found) installers.push_back(installer);
        return installers;
    }

    std::string Downloads_Folder() {
        const char* folder = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
        if (folder != nullptr) return Folder_Text_(Path_(folder));
        // Linux without xdg-user-dirs set up: SDL knows no Downloads; use ~/Downloads when it is there.
        const char* home = SDL_getenv("HOME");
        std::error_code error;
        if (home != nullptr && fs::is_directory(Path_(home) / "Downloads", error)) return Folder_Text_(Path_(home) / "Downloads");
        return "";
    }

    namespace {
        FILE* Open_Read_(const std::string& path) {
#if defined(_WIN32)
            return _wfopen(Path_(path).wstring().c_str(), L"rb");
#else
            return std::fopen(path.c_str(), "rb");
#endif
        }

        FILE* Open_Write_(const fs::path& path) {
#if defined(_WIN32)
            return _wfopen(path.wstring().c_str(), L"wb");
#else
            return std::fopen(path.c_str(), "wb");
#endif
        }

        bool Seek_(FILE* file, uint64_t offset) {
#if defined(_WIN32)
            return _fseeki64(file, static_cast<long long>(offset), SEEK_SET) == 0;
#else
            return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
        }

        // A Linux installer is a shell script with a zip after it: find where the zip starts from
        // its end-of-central-directory record (the zip's offsets count from that start).
        bool Zip_Start_(FILE* file, uint64_t size, uint64_t* start) {
            const uint64_t tail = std::min<uint64_t>(size, 65536 + 22);
            std::vector<unsigned char> buffer(static_cast<size_t>(tail));
            if (tail < 22 || !Seek_(file, size - tail) || std::fread(buffer.data(), 1, buffer.size(), file) != buffer.size()) return false;
            for (size_t index = buffer.size() - 22 + 1; index-- > 0;) {
                if (buffer[index] == 0x50 && buffer[index + 1] == 0x4B && buffer[index + 2] == 0x05 && buffer[index + 3] == 0x06) {
                    const auto u32 = [&](size_t at) {
                        return uint32_t(buffer[at]) | uint32_t(buffer[at + 1]) << 8 | uint32_t(buffer[at + 2]) << 16 | uint32_t(buffer[at + 3]) << 24;
                    };
                    const uint64_t record = size - tail + index;
                    const uint64_t directory_size = u32(index + 12), directory_offset = u32(index + 16);
                    if (directory_size + directory_offset > record) return false;
                    *start = record - directory_size - directory_offset;
                    return true;
                }
            }
            return false;
        }

        struct s_zip {
            mz_zip_archive archive{};
            FILE* file = nullptr;
            bool open = false;
            ~s_zip() {
                if (open) mz_zip_reader_end(&archive);
                if (file != nullptr) std::fclose(file);
            }
        };

        bool Open_Installer_Zip_(const std::string& path, s_zip* zip, uint64_t* start, std::string* error) {
            zip->file = Open_Read_(path);
            std::error_code code;
            const uint64_t size = fs::file_size(Path_(path), code);
            if (zip->file == nullptr || code) {
                *error = "can't read " + path;
                return false;
            }
            if (!Zip_Start_(zip->file, size, start) || !Seek_(zip->file, *start) || !mz_zip_reader_init_cfile(&zip->archive, zip->file, size - *start, 0)) {
                *error = "this doesn't look like a GOG Linux installer (no archive inside)";
                return false;
            }
            zip->open = true;
            return true;
        }

        uint64_t Size_Or_None_(const fs::path& path) {
            std::error_code error;
            const uint64_t size = fs::file_size(path, error);
            return error ? UINT64_MAX : size;
        }
    } // namespace

    namespace {
        // A Mac installer (GOG 1.40b, 2026-11-24): a xar archive whose Scripts entry (stored, i.e.
        // octet-stream) is a gzip stream of an odc cpio archive; the game is in
        // ./payload/Contents/Resources/game/. Read as a stream, never whole.
        uint64_t Big_Endian_(const unsigned char* bytes, int count) {
            uint64_t value = 0;
            for (int index = 0; index < count; ++index) value = (value << 8) | bytes[index];
            return value;
        }

        std::string Xml_Tag_(const std::string& text, size_t from, size_t to, const std::string& tag) {
            const size_t open = text.find("<" + tag + ">", from);
            if (open == std::string::npos || open > to) return "";
            const size_t start = open + tag.size() + 2;
            const size_t close = text.find("</" + tag + ">", start);
            return close == std::string::npos || close > to ? "" : text.substr(start, close - start);
        }

        // The Scripts entry's place in the heap (offset from the heap's start, length, encoding).
        bool Xar_Scripts_(FILE* file, uint64_t* data_start, uint64_t* data_length, std::string* error) {
            unsigned char header[28];
            if (std::fread(header, 1, 28, file) != 28 || std::memcmp(header, "xar!", 4) != 0) {
                *error = "this doesn't look like a GOG Mac installer (not a xar package)";
                return false;
            }
            const uint64_t header_size = Big_Endian_(header + 4, 2);
            const uint64_t toc_compressed = Big_Endian_(header + 8, 8);
            const uint64_t toc_size = Big_Endian_(header + 16, 8);
            if (toc_compressed > (64u << 20) || toc_size > (64u << 20) || !Seek_(file, header_size)) {
                *error = "the Mac installer's table of contents is damaged";
                return false;
            }
            std::vector<unsigned char> compressed(static_cast<size_t>(toc_compressed));
            std::string toc(static_cast<size_t>(toc_size), '\0');
            mz_ulong out_size = static_cast<mz_ulong>(toc_size);
            if (std::fread(compressed.data(), 1, compressed.size(), file) != compressed.size() ||
                mz_uncompress(reinterpret_cast<unsigned char*>(toc.data()), &out_size, compressed.data(), static_cast<mz_ulong>(compressed.size())) != MZ_OK) {
                *error = "the Mac installer's table of contents is damaged";
                return false;
            }
            toc.resize(out_size);
            const size_t name = toc.find("<name>Scripts</name>");
            const size_t data = name == std::string::npos ? std::string::npos : toc.rfind("<data>", name);
            const size_t data_end = data == std::string::npos ? std::string::npos : toc.find("</data>", data);
            if (data_end == std::string::npos || data_end > name) {
                *error = "this Mac installer has no Scripts payload (not one of GOG's)";
                return false;
            }
            if (toc.find("application/octet-stream", data) > data_end) {
                *error = "this Mac installer's payload is packed in a way Battle at Antares doesn't read";
                return false;
            }
            *data_start = header_size + toc_compressed + std::strtoull(Xml_Tag_(toc, data, data_end, "offset").c_str(), nullptr, 10);
            *data_length = std::strtoull(Xml_Tag_(toc, data, data_end, "length").c_str(), nullptr, 10);
            return *data_length > 0;
        }

        // Reads a gzip stream out of a file region, inflating as it goes.
        class c_gzip_reader {
          public:
            c_gzip_reader(FILE* file, uint64_t length) : file_(file), left_(length) {
                std::memset(&stream_, 0, sizeof(stream_));
            }
            ~c_gzip_reader() {
                if (started_) mz_inflateEnd(&stream_);
            }
            bool Start(std::string* error) {
                unsigned char header[10];
                if (!Raw_(header, 10) || header[0] != 0x1F || header[1] != 0x8B || header[2] != 8) {
                    *error = "the Mac installer's payload isn't gzip";
                    return false;
                }
                const int flags = header[3];
                unsigned char byte[2];
                if (flags & 4) { // FEXTRA
                    if (!Raw_(byte, 2)) return false;
                    std::vector<unsigned char> skip(byte[0] | (byte[1] << 8));
                    if (!Raw_(skip.data(), skip.size())) return false;
                }
                for (int mask : {8, 16}) { // FNAME, FCOMMENT: zero-terminated
                    if (!(flags & mask)) continue;
                    do {
                        if (!Raw_(byte, 1)) return false;
                    } while (byte[0] != 0);
                }
                if ((flags & 2) && !Raw_(byte, 2)) return false; // FHCRC
                if (mz_inflateInit2(&stream_, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
                started_ = true;
                return true;
            }
            // Exactly `size` bytes into `out` (nullptr: skip them). False at the end or on damage.
            bool Read(unsigned char* out, size_t size) {
                unsigned char scratch[65536];
                while (size > 0) {
                    const size_t take = std::min(size, sizeof(scratch));
                    stream_.next_out = out != nullptr ? out : scratch;
                    stream_.avail_out = static_cast<unsigned int>(take);
                    while (stream_.avail_out > 0) {
                        if (stream_.avail_in == 0) {
                            const size_t got = static_cast<size_t>(std::min<uint64_t>(left_, sizeof(input_)));
                            if (got == 0 || std::fread(input_, 1, got, file_) != got) return false;
                            left_ -= got;
                            stream_.next_in = input_;
                            stream_.avail_in = static_cast<unsigned int>(got);
                        }
                        const int status = mz_inflate(&stream_, MZ_NO_FLUSH);
                        if (status == MZ_STREAM_END && stream_.avail_out > 0) return false;
                        if (status != MZ_OK && status != MZ_STREAM_END) return false;
                    }
                    if (out != nullptr) out += take;
                    size -= take;
                }
                return true;
            }

          private:
            bool Raw_(unsigned char* out, size_t size) {
                if (size > left_ || std::fread(out, 1, size, file_) != size) return false;
                left_ -= size;
                return true;
            }
            FILE* file_;
            uint64_t left_;
            mz_stream stream_;
            bool started_ = false;
            unsigned char input_[1 << 16];
        };

        uint64_t Octal_(const unsigned char* text, int count) {
            uint64_t value = 0;
            for (int index = 0; index < count; ++index) {
                if (text[index] < '0' || text[index] > '7') return UINT64_MAX;
                value = value * 8 + (text[index] - '0');
            }
            return value;
        }

        // Walks an odc cpio archive: `visit` gets each file's name and size and returns where its
        // bytes go (a FILE*, or nullptr to skip them). False on damage or when `visit` stops it.
        bool Walk_Cpio_(c_gzip_reader* reader, const std::function<bool(const std::string&, uint64_t, FILE**)>& visit, std::string* error) {
            for (;;) {
                unsigned char header[76];
                if (!reader->Read(header, 76) || std::memcmp(header, "070707", 6) != 0) {
                    *error = "the Mac installer's archive is damaged";
                    return false;
                }
                const uint64_t name_size = Octal_(header + 59, 6), file_size = Octal_(header + 65, 11);
                if (name_size == UINT64_MAX || file_size == UINT64_MAX || name_size == 0 || name_size > 4096) {
                    *error = "the Mac installer's archive is damaged";
                    return false;
                }
                std::string name(static_cast<size_t>(name_size), '\0');
                if (!reader->Read(reinterpret_cast<unsigned char*>(name.data()), name.size())) return false;
                name.pop_back(); // the terminating zero
                if (name == "TRAILER!!!") return true;
                FILE* out = nullptr;
                if (!visit(name, file_size, &out)) return false;
                if (out == nullptr) {
                    if (!reader->Read(nullptr, static_cast<size_t>(file_size))) return false;
                    continue;
                }
                unsigned char buffer[65536];
                for (uint64_t left = file_size; left > 0;) {
                    const size_t take = static_cast<size_t>(std::min<uint64_t>(left, sizeof(buffer)));
                    if (!reader->Read(buffer, take) || std::fwrite(buffer, 1, take, out) != take) return false;
                    left -= take;
                }
            }
        }
    } // namespace

    namespace {
        // GOG's Windows installer (1.40b, 2026-11-24): Inno Setup 5.6 (Unicode). Layout worked out
        // from innoextract's source and proven on the owner's file (notes/antares/launcher.md, "GOG
        // installers"). The game files are GOG Galaxy parts: zlib streams in stored chunks, named
        // by the before_install(...) script string of the first part's file entry.
        const unsigned char INNO_MAGIC[12] = {0x72, 0x44, 0x6C, 0x50, 0x74, 0x53, 0xCD, 0xE6, 0xD7, 0x7B, 0x0B, 0x2A};

        uint32_t Le32_(const unsigned char* bytes) {
            return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
        }
        uint64_t Le64_(const unsigned char* bytes) {
            return uint64_t(Le32_(bytes)) | uint64_t(Le32_(bytes + 4)) << 32;
        }

        // Reads through a decompressed setup block.
        class c_cursor {
          public:
            explicit c_cursor(const std::vector<unsigned char>& data) : data_(data) {}
            bool Ok() const {
                return ok_;
            }
            bool Skip(size_t count) {
                if (!ok_ || count > data_.size() - at_) return ok_ = false;
                at_ += count;
                return true;
            }
            uint32_t U32() {
                if (!Skip(4)) return 0;
                return Le32_(data_.data() + at_ - 4);
            }
            uint64_t U64() {
                if (!Skip(8)) return 0;
                return Le64_(data_.data() + at_ - 8);
            }
            // A length-prefixed string's bytes (wstr: UTF-16LE).
            std::string Bytes() {
                const uint32_t length = U32();
                if (!Skip(length)) return "";
                return std::string(reinterpret_cast<const char*>(data_.data() + at_ - length), length);
            }
            void Skip_Strings(int count) {
                for (int index = 0; index < count && ok_; ++index) (void)Bytes();
            }

          private:
            const std::vector<unsigned char>& data_;
            size_t at_ = 0;
            bool ok_ = true;
        };

        std::string Utf16_To_Utf8_(const std::string& bytes) {
            std::string text;
            for (size_t index = 0; index + 1 < bytes.size(); index += 2) {
                uint32_t code = uint8_t(bytes[index]) | uint32_t(uint8_t(bytes[index + 1])) << 8;
                if (code >= 0xD800 && code <= 0xDBFF && index + 3 < bytes.size()) {
                    const uint32_t low = uint8_t(bytes[index + 2]) | uint32_t(uint8_t(bytes[index + 3])) << 8;
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    index += 2;
                }
                if (code < 0x80) {
                    text += static_cast<char>(code);
                } else if (code < 0x800) {
                    text += static_cast<char>(0xC0 | (code >> 6));
                    text += static_cast<char>(0x80 | (code & 0x3F));
                } else if (code < 0x10000) {
                    text += static_cast<char>(0xE0 | (code >> 12));
                    text += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    text += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    text += static_cast<char>(0xF0 | (code >> 18));
                    text += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                    text += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    text += static_cast<char>(0x80 | (code & 0x3F));
                }
            }
            return text;
        }

        void* Lzma_Alloc_(ISzAllocPtr, size_t size) {
            return std::malloc(size);
        }
        void Lzma_Free_(ISzAllocPtr, void* address) {
            std::free(address);
        }

        // A raw LZMA1 stream: 5 property bytes, then data ending with the end marker.
        bool Lzma1_(const std::vector<unsigned char>& input, std::vector<unsigned char>* output) {
            if (input.size() < LZMA_PROPS_SIZE) return false;
            const ISzAlloc alloc = {Lzma_Alloc_, Lzma_Free_};
            CLzmaDec decoder;
            LzmaDec_CONSTRUCT(&decoder);
            if (LzmaDec_Allocate(&decoder, input.data(), LZMA_PROPS_SIZE, &alloc) != SZ_OK) return false;
            LzmaDec_Init(&decoder);
            size_t in_at = LZMA_PROPS_SIZE;
            bool ok = false;
            output->clear();
            for (;;) {
                const size_t before = output->size();
                if (before > (64u << 20)) break; // far beyond any setup header
                output->resize(before + (1 << 20));
                SizeT out_size = 1 << 20, in_size = input.size() - in_at;
                ELzmaStatus status;
                const SRes result = LzmaDec_DecodeToBuf(&decoder, output->data() + before, &out_size, input.data() + in_at, &in_size, LZMA_FINISH_ANY, &status);
                output->resize(before + out_size);
                in_at += in_size;
                if (result != SZ_OK) break;
                if (status == LZMA_STATUS_FINISHED_WITH_MARK) {
                    ok = true;
                    break;
                }
                if (out_size == 0 && in_size == 0) break; // stuck: damaged
            }
            LzmaDec_Free(&decoder, &alloc);
            return ok;
        }

        // One setup-0 block: a 9-byte header (CRC of the next 5, stored size, compressed), then
        // 4096-byte chunks each led by its CRC, then (when compressed) LZMA1.
        bool Inno_Block_(FILE* file, std::vector<unsigned char>* data) {
            unsigned char header[9];
            if (std::fread(header, 1, 9, file) != 9) return false;
            if (static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, header + 4, 5)) != Le32_(header)) return false;
            const uint32_t stored = Le32_(header + 4);
            if (stored > (64u << 20)) return false;
            std::vector<unsigned char> raw(stored);
            if (std::fread(raw.data(), 1, stored, file) != stored) return false;
            std::vector<unsigned char> joined;
            for (size_t at = 0; at < raw.size();) {
                if (raw.size() - at < 5) return false;
                const size_t length = std::min<size_t>(4096, raw.size() - at - 4);
                if (static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, raw.data() + at + 4, length)) != Le32_(raw.data() + at)) return false;
                joined.insert(joined.end(), raw.begin() + static_cast<std::ptrdiff_t>(at + 4), raw.begin() + static_cast<std::ptrdiff_t>(at + 4 + length));
                at += 4 + length;
            }
            if (header[8] == 0) {
                *data = std::move(joined);
                return true;
            }
            return Lzma1_(joined, data);
        }

        // The arguments of a script call: 'quoted' strings ('' is a quote) and bare numbers.
        std::vector<std::string> Script_Arguments_(const std::string& call) {
            std::vector<std::string> arguments;
            const size_t open = call.find('(');
            if (open == std::string::npos) return arguments;
            std::string current;
            bool quoted = false, any = false;
            for (size_t index = open + 1; index < call.size(); ++index) {
                const char character = call[index];
                if (quoted) {
                    if (character == '\'' && index + 1 < call.size() && call[index + 1] == '\'') {
                        current += '\'';
                        ++index;
                    } else if (character == '\'') {
                        quoted = false;
                    } else {
                        current += character;
                    }
                } else if (character == '\'') {
                    quoted = any = true;
                } else if (character == ',' || character == ')') {
                    arguments.push_back(current);
                    current.clear();
                    any = false;
                    if (character == ')') break;
                } else if (character != ' ') {
                    current += character;
                    any = true;
                }
            }
            (void)any;
            return arguments;
        }

        struct s_inno_part {
            uint64_t chunk_offset = 0; // from data_offset; the chunk starts with "zlb\x1a"
            uint64_t file_offset = 0;  // inside the chunk
            uint64_t stored = 0;       // a zlib stream of this many bytes
            uint64_t inflated = 0;
            uint16_t flags = 0;
        };
        struct s_inno_file {
            std::string name; // the file's name (basename)
            std::vector<s_inno_part> parts;
            uint64_t size = 0;
        };

        // Lists the GOG Galaxy files in an Inno installer. False with `error` when the file isn't
        // a GOG Inno 5.6 Unicode installer.
        bool Inno_Files_(FILE* file, uint64_t size, uint64_t* data_offset, std::vector<s_inno_file>* files, std::string* error) {
            const auto refuse = [&](const char* why) {
                *error = why;
                return false;
            };
            // The loader table: its magic within the first 16 MB (it's a PE resource near the start).
            std::vector<unsigned char> window(static_cast<size_t>(std::min<uint64_t>(size, 16u << 20)));
            if (!Seek_(file, 0) || std::fread(window.data(), 1, window.size(), file) != window.size()) return refuse("can't read the installer");
            size_t table = std::string::npos;
            for (size_t at = 0; at + 44 <= window.size(); ++at) {
                if (window[at] == INNO_MAGIC[0] && std::memcmp(window.data() + at, INNO_MAGIC, 12) == 0 &&
                    static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, window.data() + at, 40)) == Le32_(window.data() + at + 40)) {
                    table = at;
                    break;
                }
            }
            if (table == std::string::npos || Le32_(window.data() + table + 12) != 1) return refuse("this isn't an Inno Setup installer Battle at Antares can read");
            const uint64_t header_offset = Le32_(window.data() + table + 32);
            *data_offset = Le32_(window.data() + table + 36);
            char version[65] = {};
            if (!Seek_(file, header_offset) || std::fread(version, 1, 64, file) != 64) return refuse("can't read the installer's header");
            const std::string version_text = version;
            if (version_text.find("Inno Setup Setup Data (5.6.") == std::string::npos || version_text.find("(u)") == std::string::npos) {
                *error = "this installer's version (" + version_text + ") isn't one Battle at Antares can read; GOG's 1.40b is";
                return false;
            }
            std::vector<unsigned char> block1, block2;
            if (!Inno_Block_(file, &block1) || !Inno_Block_(file, &block2)) return refuse("the installer's header is damaged");

            c_cursor cursor(block1);
            cursor.Skip_Strings(30 + 4); // the setup header's strings, then license, info and code
            uint32_t counts[16];
            for (uint32_t& count : counts) count = cursor.U32();
            cursor.Skip(93);                                                      // the rest of the setup header (fixed size in 5.6 Unicode)
            for (uint32_t index = 0; index < counts[0] && cursor.Ok(); ++index) { // languages
                cursor.Skip_Strings(10);
                cursor.Skip(21);
            }
            for (uint32_t index = 0; index < counts[1] && cursor.Ok(); ++index) { // messages
                cursor.Skip_Strings(2);
                cursor.Skip(4);
            }
            for (uint32_t index = 0; index < counts[2] && cursor.Ok(); ++index) cursor.Skip_Strings(1); // permissions
            for (uint32_t index = 0; index < counts[3] && cursor.Ok(); ++index) {                       // types
                cursor.Skip_Strings(4);
                cursor.Skip(30);
            }
            for (uint32_t index = 0; index < counts[4] && cursor.Ok(); ++index) { // components
                cursor.Skip_Strings(5);
                cursor.Skip(42);
            }
            for (uint32_t index = 0; index < counts[5] && cursor.Ok(); ++index) { // tasks
                cursor.Skip_Strings(6);
                cursor.Skip(26);
            }
            for (uint32_t index = 0; index < counts[6] && cursor.Ok(); ++index) { // directories
                cursor.Skip_Strings(7);
                cursor.Skip(27);
            }
            struct s_entry {
                std::string after_install, before_install;
                uint32_t location = 0;
            };
            std::vector<s_entry> entries;
            for (uint32_t index = 0; index < counts[7] && cursor.Ok(); ++index) { // files
                s_entry entry;
                cursor.Skip_Strings(8); // source, destination, font, assembly, components, tasks, languages, check
                entry.after_install = Utf16_To_Utf8_(cursor.Bytes());
                entry.before_install = Utf16_To_Utf8_(cursor.Bytes());
                cursor.Skip(20); // windows versions
                entry.location = cursor.U32();
                cursor.Skip(4 + 8 + 2 + 4 + 1); // attributes, external size, permission, flags, type
                entries.push_back(entry);
            }
            if (!cursor.Ok() || block2.size() < uint64_t(counts[8]) * 74) return refuse("the installer's file list couldn't be read");
            const auto part_of = [&](uint32_t location, s_inno_part* part) {
                if (location >= counts[8]) return false;
                const unsigned char* data = block2.data() + size_t(location) * 74;
                part->chunk_offset = Le32_(data + 8);
                part->file_offset = Le64_(data + 12);
                part->stored = Le64_(data + 20);
                part->flags = static_cast<uint16_t>(data[72] | data[73] << 8);
                return true;
            };
            // GOG Galaxy: before_install('md5', 'name', parts) starts a file; it and the next
            // parts - 1 entries each say after_install('md5', stored, inflated).
            files->clear();
            for (size_t index = 0; index < entries.size(); ++index) {
                const std::string& call = entries[index].before_install;
                if (call.rfind("before_install", 0) != 0) continue;
                const std::vector<std::string> arguments = Script_Arguments_(call);
                if (arguments.size() < 3) continue;
                const size_t count = static_cast<size_t>(std::strtoul(arguments[2].c_str(), nullptr, 10));
                if (count == 0 || index + count > entries.size()) continue;
                s_inno_file logical;
                std::string name = arguments[1];
                const size_t slash = name.find_last_of("\\/");
                logical.name = slash == std::string::npos ? name : name.substr(slash + 1);
                bool ok = true;
                for (size_t part_index = 0; part_index < count && ok; ++part_index) {
                    const s_entry& entry = entries[index + part_index];
                    const std::vector<std::string> sizes = Script_Arguments_(entry.after_install);
                    s_inno_part part;
                    ok = entry.after_install.rfind("after_install", 0) == 0 && sizes.size() >= 3 && part_of(entry.location, &part);
                    if (!ok) break;
                    part.inflated = std::strtoull(sizes[2].c_str(), nullptr, 10);
                    logical.size += part.inflated;
                    logical.parts.push_back(part);
                }
                if (ok) files->push_back(logical);
            }
            if (files->empty()) return refuse("this installer holds no GOG Galaxy files Battle at Antares can read");
            return true;
        }
    } // namespace

    s_folder_check Check_Installer(const std::string& path, std::string* error) {
        s_folder_check check;
        check.folder = path;
        check.installer = Installer_Kind(Text_(Path_(path).filename()));
        if (check.installer == INSTALLER_NONE) check.installer = INSTALLER_LINUX; // a renamed file: try it
        check.missing = game_files::Required_Files();
        if (check.installer == INSTALLER_WINDOWS) {
            FILE* file = Open_Read_(path);
            std::error_code code;
            const uint64_t size = fs::file_size(Path_(path), code);
            std::vector<s_inno_file> inno;
            uint64_t data_offset = 0;
            const bool ok = file != nullptr && !code && Inno_Files_(file, size, &data_offset, &inno, error);
            if (file != nullptr) std::fclose(file);
            if (!ok) {
                if (error->empty()) *error = "can't read " + path;
                return check;
            }
            check.exists = true;
            check.archive_offset = data_offset;
            std::map<std::string, uint64_t> found; // upper-case name -> size
            for (const s_inno_file& entry : inno) {
                if (Upper_(entry.name).size() > 4 && Upper_(entry.name).compare(Upper_(entry.name).size() - 4, 4, ".LBX") == 0) found[Upper_(entry.name)] = entry.size;
            }
            check.missing.clear();
            for (const std::string& name : game_files::Required_Files()) {
                const auto it = found.find(name);
                if (it == found.end()) {
                    check.missing.push_back(name);
                } else if (it->second == 0) {
                    check.empty.push_back(name);
                } else {
                    ++check.found;
                }
            }
            check.patched = found.count("PATCH13.LBX") != 0;
            for (const auto& [name, file_size] : found) {
                check.to_copy.push_back(name);
                check.bytes += file_size;
            }
            return check;
        }
        std::map<std::string, std::pair<std::string, uint64_t>> files; // upper-case name -> entry, size
        if (check.installer == INSTALLER_MAC) {
            FILE* file = Open_Read_(path);
            if (file == nullptr) {
                *error = "can't read " + path;
                return check;
            }
            uint64_t start = 0, length = 0;
            bool ok = Xar_Scripts_(file, &start, &length, error) && Seek_(file, start);
            if (ok) {
                c_gzip_reader reader(file, length);
                std::vector<std::pair<std::string, uint64_t>> entries;
                ok = reader.Start(error) && Walk_Cpio_(&reader, [&](const std::string& name, uint64_t size, FILE**) {
                    entries.emplace_back(name, size);
                    return true; }, error);
                // The game is the folder holding DIPLOMAT.LBX.
                std::string game_folder;
                for (const auto& [name, size] : entries) {
                    const size_t slash = name.rfind('/');
                    if (Upper_(name.substr(slash + 1)) == "DIPLOMAT.LBX") game_folder = name.substr(0, slash + 1);
                }
                for (const auto& [name, size] : entries) {
                    if (game_folder.empty() || name.compare(0, game_folder.size(), game_folder) != 0 || name.find('/', game_folder.size()) != std::string::npos) continue;
                    files[Upper_(name.substr(game_folder.size()))] = {name, size};
                }
            }
            std::fclose(file);
            if (!ok) return check;
            check.exists = true;
            check.archive_offset = start;
            check.archive_length = length;
            check.missing.clear();
            for (const std::string& name : game_files::Required_Files()) {
                const auto it = files.find(name);
                if (it == files.end()) {
                    check.missing.push_back(name);
                } else if (it->second.second == 0) {
                    check.empty.push_back(name);
                } else {
                    ++check.found;
                }
            }
            check.patched = files.count("PATCH13.LBX") != 0;
            for (const auto& [name, entry] : files) {
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".LBX") == 0) {
                    check.to_copy.push_back(entry.first);
                    check.bytes += entry.second;
                }
            }
            return check;
        }
        s_zip zip;
        if (!Open_Installer_Zip_(path, &zip, &check.archive_offset, error)) return check;
        check.exists = true;
        // The game's files sit in one folder of the archive (data/noarch/data/ in GOG's 2.0.0.6):
        // the folder that holds DIPLOMAT.LBX.
        const mz_uint count = mz_zip_reader_get_num_files(&zip.archive);
        std::vector<mz_zip_archive_file_stat> stats(count);
        std::vector<bool> valid(count, false);
        std::string game_folder;
        bool have_folder = false;
        for (mz_uint index = 0; index < count; ++index) {
            valid[index] = mz_zip_reader_file_stat(&zip.archive, index, &stats[index]) && !stats[index].m_is_directory;
            if (!valid[index]) continue;
            const std::string entry = stats[index].m_filename;
            const size_t slash = entry.rfind('/');
            const std::string name = slash == std::string::npos ? entry : entry.substr(slash + 1);
            if (Upper_(name) == "DIPLOMAT.LBX") {
                game_folder = slash == std::string::npos ? "" : entry.substr(0, slash + 1);
                have_folder = true;
            }
        }
        for (mz_uint index = 0; have_folder && index < count; ++index) {
            if (!valid[index]) continue;
            const std::string entry = stats[index].m_filename;
            if (entry.size() <= game_folder.size() || entry.compare(0, game_folder.size(), game_folder) != 0) continue;
            const std::string name = entry.substr(game_folder.size());
            if (name.find('/') != std::string::npos) continue;
            files[Upper_(name)] = {entry, stats[index].m_uncomp_size};
        }
        check.missing.clear();
        for (const std::string& name : game_files::Required_Files()) {
            const auto it = files.find(name);
            if (it == files.end()) {
                check.missing.push_back(name);
            } else if (it->second.second == 0) {
                check.empty.push_back(name);
            } else {
                ++check.found;
            }
        }
        check.patched = files.count("PATCH13.LBX") != 0;
        for (const auto& [name, entry] : files) {
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".LBX") == 0) {
                check.to_copy.push_back(entry.first);
                check.bytes += entry.second;
            }
        }
        return check;
    }

    bool Extract_Installer(const s_folder_check& check, const std::string& target_text, s_copy_progress* progress) {
        progress->bytes_total = check.bytes;
        const auto fail = [&](const std::string& message) {
            std::lock_guard<std::mutex> lock(progress->mutex);
            progress->error = message;
            progress->failed = true;
            progress->finished = true;
            return false;
        };
        const char* stopped = "Stopped. The files extracted so far are kept: Try again carries on from there.";
        std::string error;
        if (check.installer == INSTALLER_WINDOWS) {
            const fs::path target = Path_(target_text);
            std::error_code code;
            fs::create_directories(target, code);
            if (code) return fail("Can't create " + Text_(target) + ": " + code.message());
            FILE* file = Open_Read_(check.folder);
            const uint64_t size = fs::file_size(Path_(check.folder), code);
            std::vector<s_inno_file> inno;
            uint64_t data_offset = 0;
            if (file == nullptr || code || !Inno_Files_(file, size, &data_offset, &inno, &error)) {
                if (file != nullptr) std::fclose(file);
                return fail(error.empty() ? "Can't read " + check.folder + "." : error + ".");
            }
            const std::set<std::string> wanted(check.to_copy.begin(), check.to_copy.end());
            std::string failure;
            for (const s_inno_file& entry : inno) {
                if (wanted.count(Upper_(entry.name)) == 0) continue;
                if (progress->cancel) {
                    failure = stopped;
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(progress->mutex);
                    progress->current = entry.name;
                }
                const fs::path final_path = target / Path_(entry.name);
                if (Size_Or_None_(final_path) == entry.size) { // already there from an earlier try
                    progress->bytes_done += entry.size;
                    continue;
                }
                const fs::path temporary = target / Path_(entry.name + ".part");
                FILE* out = Open_Write_(temporary);
                if (out == nullptr) {
                    failure = "Can't write " + entry.name + " to " + Text_(target) + ".";
                    break;
                }
                for (const s_inno_part& part : entry.parts) {
                    // Galaxy parts sit in stored chunks (no LZMA2, no encryption) as zlib streams.
                    unsigned char magic[4];
                    if ((part.flags & 0xC0) != 0 || part.stored > (64u << 20) || part.inflated > (64u << 20) || !Seek_(file, data_offset + part.chunk_offset) ||
                        std::fread(magic, 1, 4, file) != 4 || std::memcmp(magic, "zlb\x1a", 4) != 0 || !Seek_(file, data_offset + part.chunk_offset + 4 + part.file_offset)) {
                        failure = "The installer's data for " + entry.name + " is damaged or packed differently.";
                        break;
                    }
                    std::vector<unsigned char> stored(static_cast<size_t>(part.stored)), inflated(static_cast<size_t>(part.inflated));
                    mz_ulong inflated_size = static_cast<mz_ulong>(part.inflated);
                    if (std::fread(stored.data(), 1, stored.size(), file) != stored.size() ||
                        mz_uncompress(inflated.data(), &inflated_size, stored.data(), static_cast<mz_ulong>(stored.size())) != MZ_OK || inflated_size != part.inflated) {
                        failure = "The installer's data for " + entry.name + " is damaged.";
                        break;
                    }
                    if (std::fwrite(inflated.data(), 1, inflated.size(), out) != inflated.size()) {
                        failure = "Writing " + entry.name + " failed (is the disk full?).";
                        break;
                    }
                    progress->bytes_done += part.inflated;
                    if (progress->cancel) {
                        failure = stopped;
                        break;
                    }
                }
                const bool closed = std::fclose(out) == 0;
                if (failure.empty() && !closed) failure = "Writing " + entry.name + " failed (is the disk full?).";
                if (!failure.empty()) {
                    fs::remove(temporary, code);
                    break;
                }
                fs::rename(temporary, final_path, code);
                if (code) {
                    failure = "Can't finish " + entry.name + ": " + code.message();
                    break;
                }
            }
            std::fclose(file);
            if (!failure.empty()) return fail(failure);
            progress->finished = true;
            return true;
        }
        if (check.installer == INSTALLER_MAC) {
            const fs::path target = Path_(target_text);
            std::error_code code;
            fs::create_directories(target, code);
            if (code) return fail("Can't create " + Text_(target) + ": " + code.message());
            FILE* file = Open_Read_(check.folder);
            if (file == nullptr || !Seek_(file, check.archive_offset)) {
                if (file != nullptr) std::fclose(file);
                return fail("Can't read " + check.folder + ".");
            }
            const std::set<std::string> wanted(check.to_copy.begin(), check.to_copy.end());
            c_gzip_reader reader(file, check.archive_length);
            FILE* out = nullptr;
            fs::path pending, final_path;
            bool cancelled = false;
            // Finishes the file written last: rename the .part into place.
            const auto finish = [&]() {
                if (out == nullptr) return true;
                const bool closed = std::fclose(out) == 0;
                out = nullptr;
                std::error_code rename_error;
                if (closed) fs::rename(pending, final_path, rename_error);
                return closed && !rename_error;
            };
            bool ok = reader.Start(&error) && Walk_Cpio_(&reader, [&](const std::string& name, uint64_t size, FILE** sink) {
                if (!finish()) return false;
                if (progress->cancel) {
                    cancelled = true;
                    return false;
                }
                if (wanted.count(name) == 0) return true;
                const std::string file_name = name.substr(name.rfind('/') + 1);
                {
                    std::lock_guard<std::mutex> lock(progress->mutex);
                    progress->current = file_name;
                }
                final_path = target / Path_(file_name);
                if (Size_Or_None_(final_path) == size) { // already there from an earlier try
                    progress->bytes_done += size;
                    return true;
                }
                pending = target / Path_(file_name + ".part");
                out = Open_Write_(pending);
                if (out == nullptr) return false;
                progress->bytes_done += size; // counted as it starts; the bar moves per file
                *sink = out;
                return true; }, &error);
            ok = finish() && ok;
            if (out != nullptr) std::fclose(out);
            std::fclose(file);
            if (cancelled) {
                fs::remove(pending, code);
                return fail(stopped);
            }
            if (!ok) {
                fs::remove(pending, code);
                return fail(error.empty() ? "Extracting failed (is the disk full, or the installer damaged?)." : error + ".");
            }
            progress->finished = true;
            return true;
        }
        s_zip zip;
        uint64_t start = 0;
        if (!Open_Installer_Zip_(check.folder, &zip, &start, &error)) return fail(error);
        const fs::path target = Path_(target_text);
        std::error_code code;
        fs::create_directories(target, code);
        if (code) return fail("Can't create " + Text_(target) + ": " + code.message());
        for (const std::string& entry : check.to_copy) {
            if (progress->cancel) return fail(stopped);
            const std::string name = entry.substr(entry.rfind('/') + 1);
            {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->current = name;
            }
            const int index = mz_zip_reader_locate_file(&zip.archive, entry.c_str(), nullptr, 0);
            mz_zip_archive_file_stat stat{};
            if (index < 0 || !mz_zip_reader_file_stat(&zip.archive, static_cast<mz_uint>(index), &stat)) return fail("The installer is missing " + name + ".");
            const fs::path final_path = target / Path_(name);
            if (Size_Or_None_(final_path) == stat.m_uncomp_size) { // already there from an earlier try
                progress->bytes_done += stat.m_uncomp_size;
                continue;
            }
            const fs::path temporary = target / Path_(name + ".part");
            FILE* out = Open_Write_(temporary);
            if (out == nullptr) return fail("Can't write " + name + " to " + Text_(target) + ".");
            struct s_sink {
                FILE* out;
                s_copy_progress* progress;
            } sink{out, progress};
            const auto write = [](void* opaque, mz_uint64, const void* data, size_t size) -> size_t {
                auto* state = static_cast<s_sink*>(opaque);
                if (state->progress->cancel) return 0;
                const size_t written = std::fwrite(data, 1, size, state->out);
                state->progress->bytes_done += written;
                return written;
            };
            const bool ok = mz_zip_reader_extract_to_callback(&zip.archive, static_cast<mz_uint>(index), write, &sink, 0);
            const bool closed = std::fclose(out) == 0;
            if (!ok || !closed) {
                fs::remove(temporary, code);
                if (progress->cancel) return fail(stopped);
                return fail("Extracting " + name + " failed (is the disk full, or the installer damaged?).");
            }
            fs::rename(temporary, final_path, code);
            if (code) return fail("Can't finish " + name + ": " + code.message());
        }
        progress->finished = true;
        return true;
    }

    std::string Target_Folder(const std::string& game_dir) {
        if (user_paths::Is_Enabled()) {
            return std::string(user_paths::Get_Directory(user_paths::USER_DIRECTORY_DATA)) + "game/";
        }
        return Folder_Text_(Path_(game_dir)) + user_folder::GAME_FILES_FOLDER;
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
        // Tests only: slow the copy down so its page can be seen (milliseconds per file).
        const char* delay_text = SDL_getenv("ANTARES_SETUP_COPY_DELAY_MS");
        const int delay_ms = delay_text != nullptr ? SDL_atoi(delay_text) : 0;
        for (const std::string& name : check.to_copy) {
            if (delay_ms > 0) SDL_Delay(static_cast<Uint32>(delay_ms));
            if (progress->cancel) {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->error = "Stopped. The files copied so far are kept: Try again carries on from there.";
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
            progress->error = "Stopped. The files copied so far are kept: Try again carries on from there.";
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
            std::error_code same_error;
            if (fs::equivalent(folder, Path_(saves_dir), same_error)) continue; // Antares's own saves

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
