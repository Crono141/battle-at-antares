#include "pch.h"

#include "antares/hd_packs.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "antares/user_folder.h"
#include "game/user_paths.h"
#include "yyjson.h"

namespace antares::hd_packs {
    namespace {
        namespace fs = std::filesystem;

        fs::path Path_(const std::string& text) {
            return fs::path(std::u8string(text.begin(), text.end()));
        }

        // What each pack is, for people; the release only knows file names and sizes.
        struct s_known {
            const char* id;
            const char* title;
            const char* description;
            bool recommended;
        };
        const s_known KNOWN[] = {
            {"antares-hd", "HD art", "The whole game in high resolution, except diplomacy and the cutscenes.", true},
            {"antares-hd-diplomacy", "HD diplomacy rooms", "The animated diplomacy rooms and their ambassadors.", false},
            {"antares-hd-cinematics", "HD cutscenes", "The intro and the endings.", false},
        };

        // A streaming SHA-256 (FIPS 180-4); orion2re::crypto only hashes whole buffers, and the
        // packs are over a gigabyte.
        class c_sha256 {
          public:
            void Update(const uint8_t* data, size_t size) {
                length_ += size;
                while (size > 0) {
                    const size_t take = std::min(size, sizeof(block_) - used_);
                    std::memcpy(block_ + used_, data, take);
                    used_ += take;
                    data += take;
                    size -= take;
                    if (used_ == sizeof(block_)) {
                        Transform_();
                        used_ = 0;
                    }
                }
            }
            std::string Hex() {
                const uint64_t bits = length_ * 8;
                const uint8_t one = 0x80, zero = 0;
                Update(&one, 1);
                while (used_ != 56) Update(&zero, 1);
                uint8_t tail[8];
                for (int index = 0; index < 8; ++index) tail[index] = static_cast<uint8_t>(bits >> (56 - index * 8));
                Update(tail, 8);
                std::string hex;
                static const char* digits = "0123456789abcdef";
                for (uint32_t word : state_) {
                    for (int shift = 28; shift >= 0; shift -= 4) hex += digits[(word >> shift) & 0xF];
                }
                return hex;
            }

          private:
            static uint32_t Rotate_(uint32_t value, int bits) {
                return (value >> bits) | (value << (32 - bits));
            }
            void Transform_() {
                static const uint32_t K[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
                uint32_t w[64];
                for (int index = 0; index < 16; ++index) {
                    w[index] = (uint32_t(block_[index * 4]) << 24) | (uint32_t(block_[index * 4 + 1]) << 16) | (uint32_t(block_[index * 4 + 2]) << 8) |
                        uint32_t(block_[index * 4 + 3]);
                }
                for (int index = 16; index < 64; ++index) {
                    const uint32_t s0 = Rotate_(w[index - 15], 7) ^ Rotate_(w[index - 15], 18) ^ (w[index - 15] >> 3);
                    const uint32_t s1 = Rotate_(w[index - 2], 17) ^ Rotate_(w[index - 2], 19) ^ (w[index - 2] >> 10);
                    w[index] = w[index - 16] + s0 + w[index - 7] + s1;
                }
                uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6], h = state_[7];
                for (int index = 0; index < 64; ++index) {
                    const uint32_t s1 = Rotate_(e, 6) ^ Rotate_(e, 11) ^ Rotate_(e, 25);
                    const uint32_t choose = (e & f) ^ (~e & g);
                    const uint32_t t1 = h + s1 + choose + K[index] + w[index];
                    const uint32_t s0 = Rotate_(a, 2) ^ Rotate_(a, 13) ^ Rotate_(a, 22);
                    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                    const uint32_t t2 = s0 + majority;
                    h = g;
                    g = f;
                    f = e;
                    e = d + t1;
                    d = c;
                    c = b;
                    b = a;
                    a = t1 + t2;
                }
                state_[0] += a;
                state_[1] += b;
                state_[2] += c;
                state_[3] += d;
                state_[4] += e;
                state_[5] += f;
                state_[6] += g;
                state_[7] += h;
            }
            uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
            uint8_t block_[64] = {};
            size_t used_ = 0;
            uint64_t length_ = 0;
        };

        std::string Read_File_(const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        // The state file: { "antares-hd": "<sha256>", ... }.
        std::vector<std::pair<std::string, std::string>> Read_State_(const std::string& mods_folder) {
            std::vector<std::pair<std::string, std::string>> entries;
            const std::string text = Read_File_(Path_(mods_folder) / STATE_FILE);
            yyjson_doc* document = text.empty() ? nullptr : yyjson_read(text.data(), text.size(), 0);
            if (document == nullptr) return entries;
            yyjson_val* root = yyjson_doc_get_root(document);
            size_t index, max;
            yyjson_val *key, *value;
            yyjson_obj_foreach(root, index, max, key, value) {
                if (yyjson_is_str(value)) entries.emplace_back(yyjson_get_str(key), yyjson_get_str(value));
            }
            yyjson_doc_free(document);
            return entries;
        }
    } // namespace

    bool Parse_Release(const std::string& json, std::vector<s_pack>* packs, std::string* tag, std::string* error) {
        packs->clear();
        yyjson_doc* document = yyjson_read(json.data(), json.size(), 0);
        yyjson_val* root = document != nullptr ? yyjson_doc_get_root(document) : nullptr;
        yyjson_val* assets = root != nullptr ? yyjson_obj_get(root, "assets") : nullptr;
        if (!yyjson_is_arr(assets)) {
            *error = "GitHub's answer isn't a release";
            if (yyjson_is_str(yyjson_obj_get(root, "message"))) *error += std::string(": ") + yyjson_get_str(yyjson_obj_get(root, "message"));
            yyjson_doc_free(document);
            return false;
        }
        if (tag != nullptr) tag->clear();
        if (tag != nullptr && yyjson_is_str(yyjson_obj_get(root, "tag_name"))) *tag = yyjson_get_str(yyjson_obj_get(root, "tag_name"));
        std::vector<s_pack> found;
        size_t index, max;
        yyjson_val* asset;
        yyjson_arr_foreach(assets, index, max, asset) {
            yyjson_val* name = yyjson_obj_get(asset, "name");
            yyjson_val* url = yyjson_obj_get(asset, "browser_download_url");
            if (!yyjson_is_str(name) || !yyjson_is_str(url)) continue;
            s_pack pack;
            pack.file = yyjson_get_str(name);
            if (pack.file.size() <= 4 || pack.file.compare(pack.file.size() - 4, 4, ".zip") != 0) continue;
            pack.id = pack.file.substr(0, pack.file.size() - 4);
            pack.url = yyjson_get_str(url);
            pack.size = yyjson_get_uint(yyjson_obj_get(asset, "size"));
            yyjson_val* digest = yyjson_obj_get(asset, "digest");
            if (yyjson_is_str(digest) && std::strncmp(yyjson_get_str(digest), "sha256:", 7) == 0) pack.sha256 = yyjson_get_str(digest) + 7;
            pack.title = pack.id;
            for (const s_known& known : KNOWN) {
                if (pack.id == known.id) {
                    pack.title = known.title;
                    pack.description = known.description;
                    pack.recommended = known.recommended;
                }
            }
            found.push_back(pack);
        }
        yyjson_doc_free(document);
        // The known packs in their order, then anything new.
        for (const s_known& known : KNOWN) {
            for (const s_pack& pack : found) {
                if (pack.id == known.id) packs->push_back(pack);
            }
        }
        for (const s_pack& pack : found) {
            bool known_pack = false;
            for (const s_known& known : KNOWN) known_pack = known_pack || pack.id == known.id;
            if (!known_pack) packs->push_back(pack);
        }
        return true;
    }

    std::string Mods_Folder(const std::string& game_dir) {
        if (user_paths::Is_Enabled()) {
            return std::string(user_paths::Get_Directory(user_paths::USER_DIRECTORY_DATA)) + "mods/";
        }
        std::string folder = game_dir;
        if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
        return folder + "data/mods/";
    }

    e_state State(const s_pack& pack, const std::string& mods_folder) {
        std::error_code error;
        const fs::path zip = Path_(mods_folder) / Path_(pack.file);
        const bool zip_there = fs::is_regular_file(zip, error);
        const bool folder_there = fs::is_directory(Path_(mods_folder) / Path_(pack.id), error);
        std::string recorded;
        for (const auto& [id, sha] : Read_State_(mods_folder)) {
            if (id == pack.id) recorded = sha;
        }
        if (!zip_there && !folder_there) return NOT_INSTALLED;
        if (folder_there || recorded.empty()) return OTHER_COPY;
        return recorded == pack.sha256 ? INSTALLED : UPDATE;
    }

    bool Record_Installed(const std::string& mods_folder, const s_pack& pack, std::string* error) {
        auto entries = Read_State_(mods_folder);
        bool replaced = false;
        for (auto& [id, sha] : entries) {
            if (id == pack.id) {
                sha = pack.sha256;
                replaced = true;
            }
        }
        if (!replaced) entries.emplace_back(pack.id, pack.sha256);
        yyjson_mut_doc* document = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* root = yyjson_mut_obj(document);
        yyjson_mut_doc_set_root(document, root);
        for (const auto& [id, sha] : entries) yyjson_mut_obj_add_strcpy(document, root, id.c_str(), sha.c_str());
        size_t length = 0;
        char* json = yyjson_mut_write(document, YYJSON_WRITE_PRETTY, &length);
        yyjson_mut_doc_free(document);
        const fs::path path = Path_(mods_folder) / STATE_FILE;
        const fs::path temporary = Path_(mods_folder) / (std::string(STATE_FILE) + ".tmp");
        bool ok = json != nullptr;
        if (ok) {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            ok = file.write(json, static_cast<std::streamsize>(length)).good();
            free(json);
        }
        std::error_code code;
        if (ok) fs::rename(temporary, path, code);
        if (!ok || code) {
            *error = "can't write " + path.string();
            return false;
        }
        return true;
    }

    std::string File_Sha256(const std::string& path, const std::function<bool(uint64_t)>& progress) {
        std::ifstream file(Path_(path), std::ios::binary);
        if (!file) return "";
        c_sha256 hash;
        std::vector<char> buffer(1 << 20);
        uint64_t done = 0;
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = file.gcount();
            if (got <= 0) break;
            hash.Update(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(got));
            done += static_cast<uint64_t>(got);
            if (progress && !progress(done)) return "";
        }
        return hash.Hex();
    }
} // namespace antares::hd_packs
