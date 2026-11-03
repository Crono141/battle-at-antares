#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
#include "antares/render/art_format.h"

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

        // Decodes PNG or WebP bytes to RGBA.
        bool Decode_Rgba(const uint8_t* bytes, size_t size, const std::string& name, std::vector<uint8_t>* rgba, int32_t* width, int32_t* height) {
            if (size >= 12 && memcmp(bytes, "RIFF", 4) == 0 && memcmp(bytes + 8, "WEBP", 4) == 0) {
                int w = 0;
                int h = 0;
                uint8_t* decoded = WebPDecodeRGBA(bytes, size, &w, &h);
                if (decoded == nullptr) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: unable to decode %s", name.c_str());
                    return false;
                }
                rgba->assign(decoded, decoded + (size_t)w * h * 4);
                WebPFree(decoded);
                *width = w;
                *height = h;
                return true;
            }
            SDL_IOStream* stream = SDL_IOFromConstMem(bytes, size);
            SDL_Surface* loaded = stream != nullptr ? SDL_LoadPNG_IO(stream, true) : nullptr;
            if (loaded == nullptr) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: unable to decode %s: %s", name.c_str(), SDL_GetError());
                return false;
            }
            SDL_Surface* converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(loaded);
            if (converted == nullptr) {
                return false;
            }
            rgba->resize((size_t)converted->w * converted->h * 4);
            for (int32_t y = 0; y < converted->h; ++y) {
                memcpy(rgba->data() + (size_t)y * converted->w * 4, (const uint8_t*)converted->pixels + (size_t)y * converted->pitch, (size_t)converted->w * 4);
            }
            *width = converted->w;
            *height = converted->h;
            SDL_DestroySurface(converted);
            return true;
        }

    } // namespace

    // A frame file's bytes (WebP by signature, or PNG), and its mask image's when it has one
    // (PLAN.md section 5). The format comes from the file: grey PNGs are indexed (grey value =
    // palette index), the rest full colour; sprite.json "format": "rgba" forces full colour.
    // The scale comes from sprite.json, or from the 1x frame size when inferred; art is
    // resampled to scale S here, so drawing only ever sees S.
    bool Decode_Frame_Bytes(const uint8_t* bytes, size_t size, const std::string& name, s_hd_frame* frame, const uint8_t* mask_bytes, size_t mask_size) {
        std::vector<uint8_t> rgba;
        int32_t width = 0;
        int32_t height = 0;
        if (!Decode_Rgba(bytes, size, name, &rgba, &width, &height)) {
            return false;
        }
        const bool full_colour = frame->full_colour || art::Is_Full_Colour(bytes, size);
        int32_t scale = frame->file_scale;
        if (frame->file_width > 0) {
            if (width != frame->file_width || height != frame->file_height) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s is %dx%d, sprite.json says %dx%d", name.c_str(), width, height, frame->file_width, frame->file_height);
                return false;
            }
        } else if (scale <= 0) {
            if (frame->base_width <= 0) {
                scale = S; // cursors and glyphs: drawn at S, whatever their size
            } else {
                scale = art::Infer_Scale(width, height, frame->base_width, frame->base_height, full_colour);
                if (scale == 0) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s is %dx%d over a %dx%d frame: not 1, 2 or 4 times (3 for full colour); padded art needs a sprite.json", name.c_str(), width, height, frame->base_width, frame->base_height);
                    return false;
                }
            }
        }
        if (scale <= 0 || (scale == 3 && !full_colour)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s: scale %d is not supported for %s art", name.c_str(), scale, full_colour ? "full-colour" : "indexed");
            return false;
        }
        const int32_t to_width = art::Scaled_Size(width, scale, S);
        const int32_t to_height = art::Scaled_Size(height, scale, S);
        if (scale != S) {
            // Indexed art and whole-number enlargements keep their pixels (nearest); full-colour
            // 3x art is resampled smoothly.
            rgba = S % scale == 0 || !full_colour ? art::Resample_Nearest(rgba.data(), width, height, 4, to_width, to_height)
                                                  : art::Resample_Bilinear_Rgba(rgba.data(), width, height, to_width, to_height);
        }
        std::vector<uint8_t> mask;
        if (full_colour && mask_bytes != nullptr && mask_size > 0) {
            std::vector<uint8_t> mask_rgba;
            int32_t mask_width = 0;
            int32_t mask_height = 0;
            if (Decode_Rgba(mask_bytes, mask_size, name + " (mask)", &mask_rgba, &mask_width, &mask_height)) {
                if (mask_width == width && mask_height == height) {
                    mask.resize((size_t)width * height);
                    for (size_t i = 0; i < mask.size(); ++i) {
                        mask[i] = mask_rgba[i * 4]; // grey value
                    }
                    if (scale != S) {
                        mask = art::Resample_Nearest(mask.data(), width, height, 1, to_width, to_height);
                    }
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: the mask of %s is %dx%d, the art %dx%d: ignored", name.c_str(), mask_width, mask_height, width, height);
                }
            }
        }
        const size_t count = (size_t)to_width * to_height;
        uint8_t* colour = (uint8_t*)malloc(full_colour ? count * 3 : count);
        uint8_t* alpha = (uint8_t*)malloc(count);
        uint8_t* mask_plane = mask.empty() ? nullptr : (uint8_t*)malloc(count);
        if (colour == nullptr || alpha == nullptr || (!mask.empty() && mask_plane == nullptr)) {
            free(colour);
            free(alpha);
            free(mask_plane);
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            if (full_colour) {
                colour[i * 3 + 0] = rgba[i * 4 + 0];
                colour[i * 3 + 1] = rgba[i * 4 + 1];
                colour[i * 3 + 2] = rgba[i * 4 + 2];
            } else {
                colour[i] = rgba[i * 4]; // grey PNGs expand to R = G = B = palette index
            }
            alpha[i] = rgba[i * 4 + 3];
        }
        if (mask_plane != nullptr) {
            memcpy(mask_plane, mask.data(), count);
        }
        frame->width = to_width;
        frame->height = to_height;
        frame->full_colour = full_colour;
        frame->rgb = full_colour ? colour : nullptr;
        frame->index = full_colour ? nullptr : colour;
        frame->alpha = alpha;
        frame->mask = mask_plane;
        return true;
    }

    // Synchronous load through the VFS (game thread; glyphs).
    bool Load_Frame_Png(const std::string& path, s_hd_frame* frame) {
        std::vector<uint8_t> bytes;
        if (!Read_Vfs_File(path.c_str(), &bytes)) {
            return false;
        }
        return Decode_Frame_Bytes(bytes.data(), bytes.size(), path, frame);
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
        // <art|hd>/index.json (optional, generated by Moo2Mods tools/hdindex.py): every sprite.json
        // of the pack in one file, keyed "<LBX>/<EEE>". When present it is authoritative: an entry
        // has HD art only if it is listed, and nothing else is read per asset (except in hot
        // reload mode, which also looks at loose folders). Without it each new entry costs VFS
        // existence checks plus a sprite.json read on the game thread, each a round trip on a
        // network drive (combat slowed to ~15 fps on one).
        // One index per art folder (an art/ pack and an hd/ pack can both have one; the VFS gives
        // each folder's highest-priority file). An entry is looked up in art/'s first.
        struct s_index_entry {
            yyjson_val* meta;
            int32_t root; // into ART_ROOTS
        };
        std::vector<yyjson_doc*> index_documents;
        std::unordered_map<std::string, s_index_entry> index_entries;
        std::string index_disk_roots[2]; // each index's folder on disk, when not inside a zip

        // The art folders, in the order looked at (PLAN.md section 5: hd/ is an alias of art/).
        const char* const ART_ROOTS[] = {"art", "hd"};

    } // namespace

    void Load_Index() {
        for (int32_t root = 0; root < 2; ++root) {
            const std::string path = std::string(ART_ROOTS[root]) + "/index.json";
            std::vector<uint8_t> json;
            if (!Read_Vfs_File(path.c_str(), &json)) {
                continue;
            }
            yyjson_doc* document = yyjson_read((const char*)json.data(), json.size(), 0);
            yyjson_val* top = document != nullptr ? yyjson_doc_get_root(document) : nullptr;
            yyjson_val* sprites = top != nullptr ? yyjson_obj_get(top, "sprites") : nullptr;
            if (!yyjson_is_obj(sprites)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: invalid %s", path.c_str());
                yyjson_doc_free(document);
                continue;
            }
            index_documents.push_back(document);
            size_t index = 0;
            size_t max = 0;
            yyjson_val* key = nullptr;
            yyjson_val* value = nullptr;
            size_t added = 0;
            yyjson_obj_foreach(sprites, index, max, key, value) {
                added += index_entries.emplace(yyjson_get_str(key), s_index_entry{value, root}).second ? 1 : 0;
            }
            char disk[1024];
            if (vfs::Resolve_Read_Path_No_Zip(path.c_str(), disk, sizeof(disk)) != 0) {
                std::string directory = disk;
                const size_t slash = directory.find_last_of("/\\");
                index_disk_roots[root] = slash == std::string::npos ? std::string() : directory.substr(0, slash);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: %s lists %zu sprites", path.c_str(), added);
        }
    }

    namespace {

        void Load_Asset_Meta(s_hd_asset* asset, const uint8_t* header);

    } // namespace

    void Load_Asset(s_hd_asset* asset, const uint8_t* header) {
        Load_Asset_Meta(asset, header);
    }

    namespace {

        // The folder of `vfs_file` on disk, or empty when it is inside a zip (or missing).
        std::string Disk_Directory(const std::string& vfs_file) {
            char disk[1024];
            if (vfs::Resolve_Read_Path_No_Zip(vfs_file.c_str(), disk, sizeof(disk)) == 0) {
                return std::string();
            }
            std::string directory = disk;
            const size_t slash = directory.find_last_of("/\\");
            return slash == std::string::npos ? std::string() : directory.substr(0, slash);
        }

        void Set_Frame_Paths(s_hd_frame* frame, const std::string& vfs_directory, const std::string& disk_directory, const char* name, bool mask) {
            frame->vfs_path = vfs_directory + name;
            if (!disk_directory.empty()) {
                frame->disk_path = disk_directory + name;
            }
            if (mask) {
                std::string mask_name = name;
                mask_name = mask_name.substr(0, mask_name.find_last_of('.')) + ".mask.png";
                frame->mask_vfs_path = vfs_directory + mask_name;
                if (!disk_directory.empty()) {
                    frame->mask_disk_path = disk_directory + mask_name;
                }
            }
        }

        // No sprite.json: a frame file per frame of the entry, scale inferred at decode, origin
        // 0,0, masks looked for beside each (full-colour art only uses them). Needs the entry's
        // header; without it the asset stays unloaded until the game loads the entry.
        void Load_Inferred_Asset(s_hd_asset* asset, const uint8_t* header) {
            if (header == nullptr) {
                return;
            }
            int16_t width = 0;
            int16_t height = 0;
            int16_t frame_count = 0;
            memcpy(&width, header, 2);
            memcpy(&height, header + 2, 2);
            memcpy(&frame_count, header + 6, 2);
            if (width <= 0 || height <= 0 || frame_count <= 0 || frame_count > 4096) {
                return;
            }
            asset->loaded = true;
            asset->base_width = width;
            asset->base_height = height;
            const std::string disk_directory = Disk_Directory(asset->directory + "/000" + asset->extension);
            s_hd_frame* loaded = new s_hd_frame[(size_t)frame_count]();
            for (int32_t i = 0; i < frame_count; ++i) {
                s_hd_frame* frame = &loaded[i];
                char name[24];
                snprintf(name, sizeof(name), "/%03d%s", i, asset->extension.c_str());
                Set_Frame_Paths(frame, asset->directory, disk_directory, name, true);
                frame->base_width = width;
                frame->base_height = height;
                frame->state = FRAME_ART;
            }
            asset->frames = loaded;
            asset->frame_count = frame_count;
        }

        void Load_Asset_Meta(s_hd_asset* asset, const uint8_t* header) {
            if (asset->inferred) {
                Load_Inferred_Asset(asset, header);
                return;
            }
            asset->loaded = true;
            const std::string meta_path = asset->directory + "/sprite.json";
            yyjson_doc* document = nullptr; // owned here when read from sprite.json
            yyjson_val* root = nullptr;
            const auto listed = index_entries.find(asset->key);
            const bool from_index = listed != index_entries.end() && asset->directory == std::string(ART_ROOTS[listed->second.root]) + "/" + asset->key;
            if (from_index) {
                root = listed->second.meta;
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
            const char* outline = yyjson_get_str(yyjson_obj_get(root, "outline"));
            asset->art_outline = outline != nullptr && strcmp(outline, "art") == 0;
            const char* format = yyjson_get_str(yyjson_obj_get(root, "format"));
            // Frame files are <FFF>.png, or <FFF>.webp when sprite.json says "image": "webp".
            const char* image = yyjson_get_str(yyjson_obj_get(root, "image"));
            const bool webp = image != nullptr && strcmp(image, "webp") == 0;
            const bool full_colour = format != nullptr && strcmp(format, "rgba") == 0;
            // "mask": true: the frames have mask images (D20). Not looked for otherwise, so packs
            // without masks cost no extra file opens.
            const bool masks = yyjson_get_bool(yyjson_obj_get(root, "mask"));
            yyjson_val* source_hash = yyjson_obj_get(root, "source_hash");
            asset->has_source_hash = yyjson_is_uint(source_hash) || yyjson_is_int(source_hash);
            asset->source_hash = asset->has_source_hash ? (uint32_t)yyjson_get_uint(source_hash) : 0;
            if (asset->scale != 1 && asset->scale != 2 && asset->scale != 3 && asset->scale != 4) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s has scale %d (1, 2 or 4; 3 for full colour)", meta_path.c_str(), asset->scale);
                yyjson_doc_free(document);
                return;
            }

            // Frames live next to sprite.json: resolve the directory on disk once instead of every
            // frame file (each lookup touches the file system, slow on network drives).
            std::string disk_directory;
            if (from_index) {
                if (!index_disk_roots[listed->second.root].empty()) {
                    disk_directory = index_disk_roots[listed->second.root] + "/" + asset->key;
                }
            } else {
                disk_directory = Disk_Directory(meta_path);
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
                    !Read_Int_Pair(yyjson_obj_get(item, "size"), &frame->file_width, &frame->file_height)) {
                    continue;
                }
                frame->file_scale = asset->scale;
                frame->width = art::Scaled_Size(frame->file_width, asset->scale, S);
                frame->height = art::Scaled_Size(frame->file_height, asset->scale, S);
                char name[16];
                snprintf(name, sizeof(name), webp ? "/%03d.webp" : "/%03d.png", frame_number);
                Set_Frame_Paths(frame, asset->directory, disk_directory, name, masks);
                frame->full_colour = full_colour;
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

    namespace {

        int32_t Add_Asset(s_hd_asset* asset, const std::string& key) {
            if (asset_count >= HD_MAX_ASSETS) {
                delete asset;
                asset_by_key.emplace(key, -1);
                return -1;
            }
            asset->key = key;
            assets[asset_count] = asset;
            asset_by_key.emplace(key, asset_count);
            return asset_count++;
        }

    } // namespace

    // Where an entry's art is, looking at loose folders: art/ then its alias hd/; in each a
    // sprite.json first, then a bare first frame (inferred, PNG or WebP). False when there is none.
    bool Find_Loose_Art(const std::string& key, s_hd_asset* asset) {
        for (const char* root : ART_ROOTS) {
            const std::string directory = std::string(root) + "/" + key;
            if (vfs::File_Exists((directory + "/sprite.json").c_str()) != 0) {
                asset->directory = directory;
                asset->inferred = false;
                return true;
            }
            for (const char* extension : {".png", ".webp"}) {
                if (vfs::File_Exists((directory + "/000" + extension).c_str()) != 0) {
                    asset->directory = directory;
                    asset->inferred = true;
                    asset->extension = extension;
                    return true;
                }
            }
        }
        return false;
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
        s_hd_asset* asset = new s_hd_asset();
        const auto listed = index_entries.find(key);
        if (art_hot_reload && Find_Loose_Art(key, asset)) {
            // hot reload: loose folders first, so art being worked on wins over the index
        } else if (listed != index_entries.end()) {
            asset->directory = std::string(ART_ROOTS[listed->second.root]) + "/" + key;
        } else if (index_documents.empty() && Find_Loose_Art(key, asset)) {
            // loose folders: no index, or hot reload (which also sees art added since)
        } else {
            delete asset;
            asset_by_key.emplace(key, -1);
            return -1;
        }
        return Add_Asset(asset, key);
    }

    int32_t Find_Or_Create_Known_Asset(const char* key) {
        const auto found = asset_by_key.find(key);
        if (found != asset_by_key.end()) {
            return found->second;
        }
        s_hd_asset* asset = new s_hd_asset();
        const auto listed = index_entries.find(key);
        if (!(art_hot_reload && Find_Loose_Art(key, asset))) {
            asset->directory = std::string(ART_ROOTS[listed != index_entries.end() ? listed->second.root : 1]) + "/" + key;
        }
        return Add_Asset(asset, key);
    }

    void Register_Load(const void* data, int32_t size, const char* lbx_name, int32_t entry) {
        if (!initialized) {
            Initialize();
        }
        if (!enabled || data == nullptr || lbx_name == nullptr || size < (int32_t)sizeof(s_registration::header)) {
            return;
        }
        // A load wins over any image plane in its memory (PLAN.md section 3.1, lifetime).
        twin::Drop_Images(data, (const uint8_t*)data + size);
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
            Load_Asset(asset, registration.header);
        }
        if (trace_enabled) {
            int32_t ready = 0;
            for (int32_t i = 0; i < asset->frame_count; ++i) {
                ready += Frame_Ready(&asset->frames[i]) ? 1 : 0;
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd trace %llu load %s#%03d frames %d ready %d from %s%s", (unsigned long long)SDL_GetTicks(), lbx_name, (int)entry, asset->frame_count, ready, asset->directory.c_str(), asset->inferred ? " (inferred)" : "");
        }
        if (!asset->has_source_hash || asset->source_hash == registration.data_hash) {
            Prefetch_Asset(asset);
        }
    }

    void Forget_Loads(const void* begin, const void* end) {
        if (!enabled) {
            return;
        }
        twin::Drop_Images(begin, end);
        for (auto it = registrations.begin(); it != registrations.end();) {
            it = it->first >= begin && it->first < end ? registrations.erase(it) : std::next(it);
        }
        for (auto it = stats_names.begin(); it != stats_names.end();) {
            it = it->first >= begin && it->first < end ? stats_names.erase(it) : std::next(it);
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
