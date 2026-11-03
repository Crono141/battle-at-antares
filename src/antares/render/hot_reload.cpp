#include "pch.h"

#include "antares/render/internal.h"

// Hot reload (PLAN.md section 5, WP1.7): the developer setting `render art_hot_reload = true;`,
// for art on a local disk. Loose art folders are looked at even when the pack has an index, entries
// that had no art are looked up again now and then (so new folders show up), and art files whose
// modification time changed since they were decoded are decoded again: the new art shows on the
// next draw of that sprite. Polled on the game thread at publish, for up to 3 ms at a time,
// so no other thread reads the asset table while the game adds to it.

namespace render {

    namespace {

        constexpr uint64_t POLL_NS = 500 * SDL_NS_PER_MS;
        constexpr uint64_t FORGET_NS = 2000 * SDL_NS_PER_MS;
        constexpr int32_t FILES_PER_POLL = 20000;
        // Per poll, for each step (moved folders, changed files), however many files that is.
        constexpr uint64_t POLL_BUDGET_NS = 3 * SDL_NS_PER_MS;
        constexpr int32_t RELOCATE_PER_POLL = 64;

        uint64_t last_poll;
        uint64_t last_forget;
        int32_t next_asset;
        int32_t next_relocate;
        bool announced;

    } // namespace

    int64_t Art_Modified(const s_hd_frame* frame) {
        if (frame->disk_path.empty()) {
            return -1;
        }
        SDL_PathInfo info;
        int64_t modified = SDL_GetPathInfo(frame->disk_path.c_str(), &info) ? (int64_t)info.modify_time : -1;
        if (!frame->mask_disk_path.empty() && SDL_GetPathInfo(frame->mask_disk_path.c_str(), &info)) {
            modified = SDL_max(modified, (int64_t)info.modify_time); // a new or changed mask counts too
        }
        return modified;
    }

    void Poll_Hot_Reload() {
        if (!art_hot_reload || !enabled) {
            return;
        }
        bool full_pass = false;
        if (!announced) {
            // Sprites made before the setting was read (startup screens) get one full check now.
            announced = true;
            full_pass = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: art hot reload is on (local art folders only)");
        }
        const uint64_t now = SDL_GetTicksNS();
        if (now - last_poll < POLL_NS && !full_pass) {
            return;
        }
        last_poll = now;
        if (now - last_forget >= FORGET_NS) {
            // Entries without art are looked up again when the game next loads them.
            last_forget = now;
            for (auto it = asset_by_key.begin(); it != asset_by_key.end();) {
                it = it->second < 0 ? asset_by_key.erase(it) : std::next(it);
            }
        }
        // Assets whose art has moved (a new folder in art/ over the pack's hd/ one, or sprites
        // made before hot reload was on): reset, so their art is read from the new place. The old
        // frames are left allocated (pending late draws may point at them; this is a dev mode).
        const int32_t relocate_checks = full_pass ? asset_count : RELOCATE_PER_POLL;
        const uint64_t relocate_start = SDL_GetTicksNS();
        for (int32_t moved_checks = 0; moved_checks < relocate_checks && asset_count > 0 &&
             (full_pass || SDL_GetTicksNS() - relocate_start < POLL_BUDGET_NS);
             ++moved_checks) {
            next_relocate = next_relocate >= asset_count ? 0 : next_relocate;
            s_hd_asset* asset = assets[next_relocate++];
            s_hd_asset probe;
            if (!Find_Loose_Art(asset->key, &probe) || (probe.directory == asset->directory && probe.inferred == asset->inferred)) {
                continue;
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: %s now from %s", asset->key.c_str(), probe.directory.c_str());
            Free_Asset_Frames(asset);
            asset->directory = probe.directory;
            asset->inferred = probe.inferred;
            asset->extension = probe.extension;
            asset->frames = nullptr;
            asset->frame_count = 0;
            asset->loaded = false;
        }
        int32_t checked = 0;
        const uint64_t files_start = SDL_GetTicksNS();
        for (int32_t visited = 0; visited < asset_count && checked < FILES_PER_POLL && SDL_GetTicksNS() - files_start < POLL_BUDGET_NS; ++visited) {
            next_asset = next_asset >= asset_count ? 0 : next_asset;
            s_hd_asset* asset = assets[next_asset++];
            if (!asset->loaded || asset->frames == nullptr) {
                continue;
            }
            for (int32_t i = 0; i < asset->frame_count; ++i) {
                s_hd_frame& frame = asset->frames[i];
                const uint8_t state = frame.load.load(std::memory_order_acquire);
                if (frame.state != FRAME_ART || (state != LOAD_READY && state != LOAD_FAILED)) {
                    continue; // never decoded (nothing to refresh) or being decoded
                }
                ++checked;
                const int64_t modified = Art_Modified(&frame);
                if (modified < 0 || modified == frame.modified) {
                    continue;
                }
                if (frame.modified == 0) {
                    frame.modified = modified; // decoded before hot reload was on: note it only
                    continue;
                }
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: reloading %s", frame.vfs_path.c_str());
                Free_Frame(frame);
                frame.load.store(LOAD_NONE, std::memory_order_release);
                Request_Frame(&frame, PRIORITY_SCREEN);
            }
        }
    }

} // namespace render
