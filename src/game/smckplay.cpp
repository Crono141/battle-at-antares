#include "pch.h"

namespace smckplay {
    ::smk smk;
    uint32_t lastkey;

    struct s_smacker_state {
        unsigned long width = 0;
        unsigned long height = 0;
        unsigned long frame_count = 0;
        unsigned long current_frame = 0;
        double microseconds_per_frame = 0.0;
        Uint64 next_frame_tick_ns = 0;
        unsigned char audio_track_mask = 0;
        unsigned char audio_channels[7] = {};
        unsigned char audio_bitdepth[7] = {};
        unsigned long audio_rate[7] = {};
        SDL_AudioStream* audio_streams[7] = {};
        bool last_frame = false;
    };

    s_smacker_state smacker_state;

    namespace {
        ::smk Open_Smacker_File(const char* file_name) {
            s_vfs_file* file = vfs::fopen(file_name, "rb");
            if (file == nullptr) {
                return nullptr;
            }

            const int32_t file_size = vfs::fsize(file);
            if (file_size <= 0) {
                vfs::fclose(file);
                return nullptr;
            }

            std::vector<unsigned char> smacker_file((size_t)file_size);
            const size_t bytes_read = vfs::fread(smacker_file.data(), 1, smacker_file.size(), file);
            vfs::fclose(file);

            if (bytes_read != smacker_file.size()) {
                return nullptr;
            }

            return smk_open_memory(smacker_file.data(), (unsigned long)smacker_file.size());
        }
    } // namespace

    void Reset_Smacker_State() {
        memset(&smacker_state, 0, sizeof(smacker_state));
    }

    void Close_Smacker_Audio_Streams() {
        for (SDL_AudioStream*& stream : smacker_state.audio_streams) {
            sound::Close_Playback_Stream(stream);
        }
    }

    SDL_AudioFormat Get_Smacker_Audio_Format(unsigned char bitdepth) {
        if (bitdepth == 8) {
            return SDL_AUDIO_U8;
        }

        if (bitdepth == 16) {
            return SDL_AUDIO_S16LE;
        }

        return SDL_AUDIO_UNKNOWN;
    }

    void Queue_Smacker_Audio_Frame() {
        for (int track = 0; track < 7; ++track) {
            SDL_AudioStream* stream = smacker_state.audio_streams[track];
            if (stream == nullptr) {
                continue;
            }

            const unsigned char* audio_data = smk_get_audio(smk, (unsigned char)track);
            unsigned long audio_size = smk_get_audio_size(smk, (unsigned char)track);
            if (audio_data == nullptr || audio_size == 0) {
                continue;
            }

            if (!SDL_PutAudioStreamData(stream, audio_data, (int)audio_size)) {
                SDL_Log("smacker: SDL_PutAudioStreamData failed for track %d: %s", track, SDL_GetError());
            }
        }
    }

    bool Init_Smacker_Audio_Streams(char audio_enabled) {
        if ((audio_enabled == 0) || (smacker_state.audio_track_mask == 0)) {
            return true;
        }

        const int32_t volume_percent =
            (MOX::settings.music_on != 0)
            ? MOX::settings.music_level
            : MOX::settings.sound_fx_level;

        for (int track = 0; track < 7; ++track) {
            if ((smacker_state.audio_track_mask & (1u << track)) == 0) {
                continue;
            }

            if (Get_Smacker_Audio_Format(smacker_state.audio_bitdepth[track]) == SDL_AUDIO_UNKNOWN ||
                smacker_state.audio_channels[track] == 0 ||
                smacker_state.audio_rate[track] == 0) {
                continue;
            }

            SDL_AudioStream* stream = sound::Open_Playback_Stream(
                smacker_state.audio_bitdepth[track],
                (uint16_t)smacker_state.audio_channels[track],
                (uint32_t)smacker_state.audio_rate[track],
                volume_percent);
            if (stream == nullptr) {
                continue;
            }

            smk_enable_audio(smk, (unsigned char)track, 1);

            smacker_state.audio_streams[track] = stream;
        }

        return true;
    }

    void Blit_Smacker_Frame(int32_t left, int32_t top) {
        render::twin::Scope twin_scope("Blit_Smacker_Frame", video::current_video_page, 0, 0, 0, 0);
        const unsigned char* frame = smk_get_video(smk);
        if (frame == nullptr || video::g_page_off.pixels == nullptr) {
            return;
        }

        uint8_t* dst = (uint8_t*)video::g_page_off.pixels;
        const int32_t pitch = video::g_screen_res_x;

        for (unsigned long y = 0; y < smacker_state.height; ++y) {
            uint8_t* dst_row = dst + ((top + (int32_t)y) * pitch) + left;
            const unsigned char* src_row = frame + (y * smacker_state.width);

            for (unsigned long x = 0; x < smacker_state.width; ++x) {
                // libsmacker's decoded frame indices are already aligned to the
                // game's active Smacker palette window. The old smackw32.dll path
                // effectively used palette entries 10..245 directly, so applying
                // an extra +10 index remap here corrupts the popup background.
                dst_row[x] = src_row[x];
            }
        }
    }

    void Update_Smacker_Frame_Metadata() {
        smk_info_all(smk, &smacker_state.current_frame, &smacker_state.frame_count, &smacker_state.microseconds_per_frame);
        smacker_state.last_frame = (smacker_state.current_frame + 1 >= smacker_state.frame_count);
    }

    int8_t __cdecl Open_Smacker_Animation(const char* file_name, char audio_enabled, uint32_t options) {
        (void)options;

        Reset_Smacker_State();

        smk = Open_Smacker_File(file_name);

        if (smk == nullptr) {
            return 0;
        }

        palette::Save_Palette();

        if (smk_enable_video(smk, 1) < 0 ||
            smk_info_video(smk, &smacker_state.width, &smacker_state.height, nullptr) < 0 ||
            smk_info_all(smk, &smacker_state.current_frame, &smacker_state.frame_count, &smacker_state.microseconds_per_frame) < 0 ||
            smk_info_audio(smk,
                &smacker_state.audio_track_mask,
                smacker_state.audio_channels,
                smacker_state.audio_bitdepth,
                smacker_state.audio_rate) < 0) {
            smk_close(smk);
            smk = nullptr;
            Reset_Smacker_State();
            return 0;
        }

        if (!Init_Smacker_Audio_Streams(audio_enabled)) {
            Close_Smacker_Audio_Streams();
        }

        if ((signed char)smk_first(smk) == SMK_ERROR) {
            Close_Smacker_Audio_Streams();
            smk_close(smk);
            smk = nullptr;
            Reset_Smacker_State();
            return 0;
        }

        Update_Smacker_Frame_Metadata();
        Queue_Smacker_Audio_Frame();

        smacker_state.next_frame_tick_ns = SDL_GetTicksNS();
        render::Video_Open(file_name, (int32_t)smacker_state.width, (int32_t)smacker_state.height, (int32_t)smacker_state.frame_count);

        return (int8_t)(smk != nullptr);
    }

    int32_t __cdecl Get_Smacker_Animation_Width() {
        if (smk == nullptr) {
            return 0;
        }
        return (int32_t)smacker_state.width;
    }

    int32_t __cdecl Get_Smacker_Animation_Height() {
        if (smk == nullptr) {
            return 0;
        }
        return (int32_t)smacker_state.height;
    }

    void __cdecl Draw_Smacker_Animation() {
        Do_Draw_Smacker_Animation(SMACK::smack_x, SMACK::smack_y);
    }

    void __cdecl Do_Draw_Smacker_Animation(int32_t left, int32_t top) {
        if (smckplay::smk != nullptr) {
            render::Video_Between_Frames(smacker_state.next_frame_tick_ns, (Uint64)(smacker_state.microseconds_per_frame * 1000.0));
            const Uint64 now_ns = SDL_GetTicksNS();
            if (smacker_state.next_frame_tick_ns > now_ns) {
                SDL_DelayNS(smacker_state.next_frame_tick_ns - now_ns);
            }

            smckplay::Set_Smacker_Palette();
            Blit_Smacker_Frame(left, top);
            render::Video_Frame(left, top, (int32_t)smacker_state.current_frame);

            setvesa::Add_Square_To_Scan_List(left,
                top,
                left + (int32_t)smacker_state.width,
                top + (int32_t)smacker_state.height);

            lastkey = smacker_state.current_frame + 1;

            if (!smacker_state.last_frame) {
                if ((signed char)smk_next(smk) != SMK_ERROR) {
                    Update_Smacker_Frame_Metadata();
                    Queue_Smacker_Audio_Frame();
                }
            }

            const Uint64 frame_duration_ns = (smacker_state.microseconds_per_frame > 0.0)
                ? (Uint64)(smacker_state.microseconds_per_frame * 1000.0)
                : 0;
            // Advance from the prior presentation target so occasional slow
            // frames do not permanently rebase the schedule and drain audio
            // lead over time.
            smacker_state.next_frame_tick_ns += frame_duration_ns;
        }
    }

    void __cdecl Set_Smacker_Palette() {
        const unsigned char* palette = smk_get_palette(smk);
        if (palette == nullptr) {
            return;
        }

        // The cinematic palette lives in the game's 10..245 window. The legacy
        // smackw32.dll interface exposed a 236-color view that corresponds to
        // libsmacker palette entries 10..245, not 0..235.
        palette += 10 * 3;

        for (s_palette_entry* current_entry = video::current_palette + 10;
             current_entry < video::current_palette + 246;
             ++current_entry, palette += 3) {
            current_entry->changed = 1;
            current_entry->r = (uint8_t)(palette[0] >> 2);
            current_entry->g = (uint8_t)(palette[1] >> 2);
            current_entry->b = (uint8_t)(palette[2] >> 2);
        }
    }

    uint32_t __cdecl Get_Smacker_Animation_Total_Frames() {
        if (smk == nullptr) {
            return 0;
        }
        return (uint32_t)smacker_state.frame_count;
    }

    uint32_t __cdecl Get_Smacker_Animation_Frame() {
        if (smckplay::smk == nullptr) {
            return 0;
        }
        return (uint32_t)smacker_state.current_frame;
    }

    void __cdecl Close_Smacker_Animation(void) {
        if (smk != nullptr) {
            render::Video_Close();
            Close_Smacker_Audio_Streams();
            smk_close(smk);
            smk = nullptr;
            Reset_Smacker_State();
            palette::Restore_Palette();
            palette::Refresh_Palette();
        }
    }
} // namespace smckplay