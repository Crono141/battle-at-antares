#include "pch.h"

#include "antares/render/internal.h"

#include "webp/decode.h"
#include "yyjson.h"

namespace render {

    // Assets are appended and never moved or freed.
    s_hd_asset* assets[HD_MAX_ASSETS];
    int32_t asset_count;
    std::unordered_map<std::string, int32_t> asset_by_key;
    std::unordered_map<const void*, s_registration> registrations;

    bool Read_Vfs_File(const char* path, std::vector<uint8_t>* bytes) {
        s_vfs_file* file = vfs::fopen(path, "rb");
        if (file == nullptr) {
            return false;
        }
        const int32_t size = vfs::fsize(file);
        bool ok = size > 0;
        if (ok) {
            bytes->resize((size_t)size);
            ok = vfs::fread(bytes->data(), 1, bytes->size(), file) == bytes->size();
        }
        vfs::fclose(file);
        return ok;
    }

    namespace {

        // Decodes PNG bytes (LA or grey) into a frame's index/alpha planes.
        bool Decode_Png(SDL_IOStream* stream, const std::string& name, s_hd_frame* frame) {
            SDL_Surface* loaded = stream != nullptr ? SDL_LoadPNG_IO(stream, true) : nullptr;
            if (loaded == nullptr) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: unable to decode %s: %s", name.c_str(), SDL_GetError());
                return false;
            }
            SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(loaded);
            if (rgba == nullptr) {
                return false;
            }
            if (frame->width == 0 && frame->height == 0) {
                frame->width = rgba->w;
                frame->height = rgba->h;
            }
            if (rgba->w != frame->width || rgba->h != frame->height) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s is %dx%d, sprite.json says %dx%d", name.c_str(), rgba->w, rgba->h, frame->width, frame->height);
                SDL_DestroySurface(rgba);
                return false;
            }
            const size_t count = (size_t)frame->width * (size_t)frame->height;
            uint8_t* index = (uint8_t*)malloc(frame->full_colour ? count * 3 : count);
            uint8_t* alpha = (uint8_t*)malloc(count);
            if (index == nullptr || alpha == nullptr) {
                free(index);
                free(alpha);
                SDL_DestroySurface(rgba);
                return false;
            }
            for (int32_t y = 0; y < frame->height; ++y) {
                const uint8_t* row = (const uint8_t*)rgba->pixels + (size_t)y * (size_t)rgba->pitch;
                for (int32_t x = 0; x < frame->width; ++x) {
                    const size_t i = (size_t)y * frame->width + x;
                    if (frame->full_colour) {
                        index[i * 3 + 0] = row[x * 4 + 0];
                        index[i * 3 + 1] = row[x * 4 + 1];
                        index[i * 3 + 2] = row[x * 4 + 2];
                    } else {
                        // Grey PNGs expand to R = G = B = palette index.
                        index[i] = row[x * 4 + 0];
                    }
                    alpha[i] = row[x * 4 + 3];
                }
            }
            SDL_DestroySurface(rgba);
            if (frame->full_colour) {
                frame->rgb = index;
            } else {
                frame->index = index;
            }
            frame->alpha = alpha;
            return true;
        }

        // Decodes a WebP frame (full-colour art) into the frame's planes.
        bool Decode_Webp(const uint8_t* bytes, size_t size, const std::string& name, s_hd_frame* frame) {
            int width = 0;
            int height = 0;
            uint8_t* rgba = WebPDecodeRGBA(bytes, size, &width, &height);
            if (rgba == nullptr) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: unable to decode %s", name.c_str());
                return false;
            }
            if (width != frame->width || height != frame->height) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s is %dx%d, sprite.json says %dx%d", name.c_str(), width, height, frame->width, frame->height);
                WebPFree(rgba);
                return false;
            }
            const size_t count = (size_t)width * (size_t)height;
            uint8_t* rgb = (uint8_t*)malloc(count * 3);
            uint8_t* alpha = (uint8_t*)malloc(count);
            if (rgb == nullptr || alpha == nullptr) {
                free(rgb);
                free(alpha);
                WebPFree(rgba);
                return false;
            }
            for (size_t i = 0; i < count; ++i) {
                rgb[i * 3 + 0] = rgba[i * 4 + 0];
                rgb[i * 3 + 1] = rgba[i * 4 + 1];
                rgb[i * 3 + 2] = rgba[i * 4 + 2];
                alpha[i] = rgba[i * 4 + 3];
            }
            WebPFree(rgba);
            frame->full_colour = true;
            frame->rgb = rgb;
            frame->alpha = alpha;
            return true;
        }

    } // namespace

    // A frame file's bytes: WebP (by signature) or PNG.
    bool Decode_Frame_Bytes(const uint8_t* bytes, size_t size, const std::string& name, s_hd_frame* frame) {
        if (size >= 12 && memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0) {
            return Decode_Webp(bytes, size, name, frame);
        }
        return Decode_Png(SDL_IOFromConstMem(bytes, size), name, frame);
    }

    // Synchronous load through the VFS (game thread; glyphs).
    bool Load_Frame_Png(const std::string& path, s_hd_frame* frame) {
        std::vector<uint8_t> bytes;
        if (!Read_Vfs_File(path.c_str(), &bytes)) {
            return false;
        }
        return Decode_Png(SDL_IOFromConstMem(bytes.data(), bytes.size()), path, frame);
    }

    namespace {

        bool Read_Int_Pair(yyjson_val* array, int32_t* a, int32_t* b) {
            if (!yyjson_is_arr(array) || yyjson_arr_size(array) != 2) {
                return false;
            }
            *a = (int32_t)yyjson_get_int(yyjson_arr_get(array, 0));
            *b = (int32_t)yyjson_get_int(yyjson_arr_get(array, 1));
            return true;
        }

        // Reads sprite.json; frame pixels are decoded later by the workers.
        // hd/index.json (optional, generated by Moo2Mods tools/hdindex.py): every sprite.json of
        // the pack in one file, keyed "<LBX>/<EEE>". When present it is authoritative: an entry
        // has HD art only if it is listed, and nothing else is read per asset. Without it each
        // new entry costs a VFS existence check plus a sprite.json read on the game thread, each
        // a round trip on a network drive (combat slowed to ~15 fps on one).
        yyjson_doc* index_document = nullptr;
        std::unordered_map<std::string, yyjson_val*> index_entries;
        std::string index_disk_root; // hd/ on disk, when the index is not inside a zip

    } // namespace

    void Load_Index() {
        std::vector<uint8_t> json;
        if (!Read_Vfs_File("hd/index.json", &json)) {
            return;
        }
        yyjson_doc* document = yyjson_read((const char*)json.data(), json.size(), 0);
        yyjson_val* root = document != nullptr ? yyjson_doc_get_root(document) : nullptr;
        yyjson_val* sprites = root != nullptr ? yyjson_obj_get(root, "sprites") : nullptr;
        if (!yyjson_is_obj(sprites)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: invalid hd/index.json");
            yyjson_doc_free(document);
            return;
        }
        index_document = document;
        size_t index = 0;
        size_t max = 0;
        yyjson_val* key = nullptr;
        yyjson_val* value = nullptr;
        yyjson_obj_foreach(sprites, index, max, key, value) {
            index_entries.emplace(yyjson_get_str(key), value);
        }
        char disk[1024];
        if (vfs::Resolve_Read_Path_No_Zip("hd/index.json", disk, sizeof(disk)) != 0) {
            index_disk_root = disk;
            const size_t slash = index_disk_root.find_last_of("/\\");
            index_disk_root = slash == std::string::npos ? std::string() : index_disk_root.substr(0, slash);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: index lists %zu sprites", index_entries.size());
    }

    namespace {

        void Load_Asset_Meta(s_hd_asset* asset);

    } // namespace

    void Load_Asset(s_hd_asset* asset) {
        const uint64_t start = SDL_GetTicksNS();
        Load_Asset_Meta(asset);
        trace_meta_ns += SDL_GetTicksNS() - start;
    }

    namespace {

        void Load_Asset_Meta(s_hd_asset* asset) {
            asset->loaded = true;
            const std::string meta_path = asset->directory + "/sprite.json";
            yyjson_doc* document = nullptr; // owned here when read from sprite.json
            yyjson_val* root = nullptr;
            if (index_document != nullptr) {
                const auto found = index_entries.find(asset->directory.substr(3)); // after "hd/"
                if (found == index_entries.end()) {
                    return;
                }
                root = found->second;
            } else {
                std::vector<uint8_t> json;
                if (!Read_Vfs_File(meta_path.c_str(), &json)) {
                    return;
                }
                document = yyjson_read((const char*)json.data(), json.size(), 0);
                root = document != nullptr ? yyjson_doc_get_root(document) : nullptr;
            }
            yyjson_val* frames = root != nullptr ? yyjson_obj_get(root, "frames") : nullptr;
            if (!yyjson_is_arr(frames)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: invalid %s", meta_path.c_str());
                yyjson_doc_free(document);
                return;
            }
            asset->scale = (int32_t)yyjson_get_int(yyjson_obj_get(root, "scale"));
            asset->glassed = yyjson_get_bool(yyjson_obj_get(root, "glassed"));
            asset->redrawn = yyjson_obj_get(root, "redraw") != nullptr;
            const char* format = yyjson_get_str(yyjson_obj_get(root, "format"));
            // Frame files are <FFF>.png, or <FFF>.webp when sprite.json says "image": "webp".
            const char* image = yyjson_get_str(yyjson_obj_get(root, "image"));
            const bool webp = image != nullptr && strcmp(image, "webp") == 0;
            const bool full_colour = format != nullptr && strcmp(format, "rgba") == 0;
            yyjson_val* source_hash = yyjson_obj_get(root, "source_hash");
            asset->has_source_hash = yyjson_is_uint(source_hash) || yyjson_is_int(source_hash);
            asset->source_hash = asset->has_source_hash ? (uint32_t)yyjson_get_uint(source_hash) : 0;
            if (asset->scale != SCALE) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s has scale %d, only %d is supported", meta_path.c_str(), asset->scale, SCALE);
                yyjson_doc_free(document);
                return;
            }

            // Frames live next to sprite.json: resolve the directory on disk once instead of every
            // frame file (each lookup touches the file system, slow on network drives).
            std::string disk_directory;
            char disk_meta[1024];
            if (index_document != nullptr) {
                if (!index_disk_root.empty()) {
                    disk_directory = index_disk_root + asset->directory.substr(2); // "/<LBX>/<EEE>"
                }
            } else if (vfs::Resolve_Read_Path_No_Zip(meta_path.c_str(), disk_meta, sizeof(disk_meta)) != 0) {
                disk_directory = disk_meta;
                const size_t slash = disk_directory.find_last_of("/\\");
                disk_directory = slash == std::string::npos ? std::string() : disk_directory.substr(0, slash);
            }

            const int32_t count = (int32_t)yyjson_arr_size(frames);
            s_hd_frame* loaded = new s_hd_frame[(size_t)count]();
            size_t index = 0;
            size_t max = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(frames, index, max, item) {
                const int32_t frame_number = (int32_t)yyjson_get_int(yyjson_obj_get(item, "frame"));
                if (frame_number < 0 || frame_number >= count) {
                    continue;
                }
                s_hd_frame* frame = &loaded[frame_number];
                yyjson_val* same_as = yyjson_obj_get(item, "same_as");
                if (same_as != nullptr) {
                    frame->state = FRAME_SAME_AS;
                    frame->same_as = (int32_t)yyjson_get_int(same_as);
                    continue;
                }
                if (!Read_Int_Pair(yyjson_obj_get(item, "origin"), &frame->origin_x, &frame->origin_y) ||
                    !Read_Int_Pair(yyjson_obj_get(item, "size"), &frame->width, &frame->height)) {
                    continue;
                }
                char name[16];
                snprintf(name, sizeof(name), webp ? "/%03d.webp" : "/%03d.png", frame_number);
                frame->vfs_path = asset->directory + name;
                frame->full_colour = full_colour;
                if (!disk_directory.empty()) {
                    frame->disk_path = disk_directory + name;
                }
                frame->state = FRAME_ART;
            }
            yyjson_doc_free(document);
            asset->frames = loaded;
            asset->frame_count = count;
        }

    } // namespace

    const s_hd_frame* Resolve_Frame(const s_hd_asset* asset, int32_t frame_number) {
        for (int32_t hops = 0; hops < 4; ++hops) {
            if (asset->frames == nullptr || frame_number < 0 || frame_number >= asset->frame_count) {
                return nullptr;
            }
            const s_hd_frame* frame = &asset->frames[frame_number];
            if (frame->state == FRAME_ART) {
                return frame;
            }
            if (frame->state != FRAME_SAME_AS) {
                return nullptr;
            }
            frame_number = frame->same_as;
        }
        return nullptr;
    }

    int32_t Find_Or_Create_Asset(const char* lbx_name, int32_t entry) {
        char key[64];
        snprintf(key, sizeof(key), "%s/%03d", lbx_name, (int)entry);
        for (char* c = key; *c != '\0'; ++c) {
            if (*c >= 'a' && *c <= 'z') {
                *c = (char)(*c - 'a' + 'A');
            }
        }
        const auto found = asset_by_key.find(key);
        if (found != asset_by_key.end()) {
            return found->second;
        }
        int32_t asset_index = -1;
        const std::string directory = std::string("hd/") + key;
        const bool listed = index_document != nullptr ? index_entries.count(key) != 0
                                                      : vfs::File_Exists((directory + "/sprite.json").c_str()) != 0;
        if (asset_count < HD_MAX_ASSETS && listed) {
            s_hd_asset* asset = new s_hd_asset();
            asset->directory = directory;
            assets[asset_count] = asset;
            asset_index = asset_count++;
        }
        asset_by_key.emplace(key, asset_index);
        return asset_index;
    }

    int32_t Find_Or_Create_Known_Asset(const char* key) {
        const auto found = asset_by_key.find(key);
        if (found != asset_by_key.end()) {
            return found->second;
        }
        int32_t asset_index = -1;
        if (asset_count < HD_MAX_ASSETS) {
            s_hd_asset* asset = new s_hd_asset();
            asset->directory = std::string("hd/") + key;
            assets[asset_count] = asset;
            asset_index = asset_count++;
        }
        asset_by_key.emplace(key, asset_index);
        return asset_index;
    }

    void Register_Load(const void* data, int32_t size, const char* lbx_name, int32_t entry) {
        if (!initialized) {
            Initialize();
        }
        if (!enabled || data == nullptr || lbx_name == nullptr || size < (int32_t)sizeof(s_registration::header)) {
            return;
        }
        if (stats_enabled || !capture_dir.empty() || probe_x >= 0) {
            char key[64];
            snprintf(key, sizeof(key), "%s#%03d", lbx_name, (int)entry);
            for (char* c = key; *c != '\0'; ++c) {
                if (*c >= 'a' && *c <= 'z') {
                    *c = (char)(*c - 'a' + 'A');
                }
            }
            stats_names[data] = key;
        }
        Enter_Screen_Of(lbx_name);
        s_registration registration = {};
        registration.asset = Find_Or_Create_Asset(lbx_name, entry);
        registration.size = size;
        // Fingerprint of the loaded bytes: a mod that replaces this entry with different art
        // must not get HD art made from the original.
        uint32_t hash = 2166136261u;
        const uint8_t* bytes = (const uint8_t*)data;
        for (int32_t i = 0; i < size; ++i) {
            hash = (hash ^ bytes[i]) * 16777619u;
        }
        registration.data_hash = hash;
        memcpy(registration.header, data, sizeof(registration.header));
        if (registration.asset < 0) {
            registrations.erase(data);
            return;
        }
        registrations[data] = registration;
        // The entry is about to be drawn: start decoding its art (unless a mod changed it).
        s_hd_asset* asset = assets[registration.asset];
        if (!asset->loaded) {
            Load_Asset(asset);
        }
        if (trace_enabled) {
            int32_t ready = 0;
            for (int32_t i = 0; i < asset->frame_count; ++i) {
                ready += Frame_Ready(&asset->frames[i]) ? 1 : 0;
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd trace %llu load %s#%03d frames %d ready %d", (unsigned long long)SDL_GetTicks(), lbx_name, (int)entry, asset->frame_count, ready);
        }
        if (!asset->has_source_hash || asset->source_hash == registration.data_hash) {
            Prefetch_Asset(asset);
        }
    }

    void Forget_Loads(const void* begin, const void* end) {
        if (!enabled) {
            return;
        }
        for (auto it = registrations.begin(); it != registrations.end();) {
            it = it->first >= begin && it->first < end ? registrations.erase(it) : std::next(it);
        }
    }

    void Move_Loads(const void* begin, const void* end, ptrdiff_t delta) {
        if (!enabled || delta == 0) {
            return;
        }
        std::vector<std::pair<const void*, s_registration>> moved;
        for (auto it = registrations.begin(); it != registrations.end();) {
            if (it->first >= begin && it->first < end) {
                moved.emplace_back((const uint8_t*)it->first + delta, it->second);
                it = registrations.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& item : moved) {
            registrations[item.first] = item.second;
        }
        if (!stats_names.empty()) {
            std::vector<std::pair<const void*, std::string>> names;
            for (auto it = stats_names.begin(); it != stats_names.end();) {
                if (it->first >= begin && it->first < end) {
                    names.emplace_back((const uint8_t*)it->first + delta, it->second);
                    it = stats_names.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto& item : names) {
                stats_names[item.first] = item.second;
            }
        }
    }

    void Note_Recolor(const void* anim, int32_t first, int32_t count, const uint8_t* map) {
        if (!enabled || anim == nullptr || map == nullptr || count <= 0) {
            return;
        }
        const auto found = registrations.find(anim);
        if (found == registrations.end()) {
            return;
        }
        s_registration& registration = found->second;
        if (!registration.recolored) {
            for (int32_t i = 0; i < 256; ++i) {
                registration.recolor[i] = (uint8_t)i;
            }
            registration.recolored = true;
        }
        // Applied to what each original index has become so far; index 0 (transparent) is never
        // rewritten, as in the engine's recolour loops.
        for (int32_t i = 1; i < 256; ++i) {
            const int32_t current = registration.recolor[i];
            if (current != 0 && current >= first && current < first + count) {
                registration.recolor[i] = map[current - first];
            }
        }
    }

} // namespace render
