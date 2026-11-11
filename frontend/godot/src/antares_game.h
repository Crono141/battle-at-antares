#pragma once

// WP3.0 trial path A: a Control that runs the game and shows its legacy 4x frame, with input
// forwarded in 640x480 game coordinates. Galaxy state is read from the snapshot the game thread
// publishes with each frame (src/antares/host.h).

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace godot {

    class AntaresGame : public Control {
        GDCLASS(AntaresGame, Control)

      public:
        AntaresGame() = default;
        ~AntaresGame() override;

        void _ready() override;
        void _process(double delta) override;
        void _draw() override;
        void _gui_input(const Ref<InputEvent>& event) override;
        void _notification(int what);

        bool start();
        void stop();
        bool is_running() const;

        void set_game_dir(const String& dir);
        String get_game_dir() const;
        void set_args(const PackedStringArray& args);
        PackedStringArray get_args() const;
        void set_autostart(bool value);
        bool get_autostart() const;

        TypedArray<Dictionary> get_stars() const;
        int64_t get_galaxy_generation() const;
        Dictionary get_stats() const;
        // Sends a key as the game's SDL path would see it (for buttons that stand for hotkeys).
        void send_key(int64_t godot_keycode, bool shift, bool alt, bool ctrl);
        // Asks the game to exit as its own quit does (game_exited follows).
        void request_game_quit();

        // WP3.1b native screens (src/antares/native.h).
        void claim_screen(int64_t screen, bool claimed);
        void claim_requests(const PackedStringArray& kinds);
        // Queues a command (a Dictionary with "verb"); command_result(id, result) follows.
        int64_t send_command(const Dictionary& command);
        int64_t get_native_screen() const;
        int64_t get_native_visit() const;
        void answer_request(int64_t id, int64_t value);
        PackedStringArray get_resource_packs() const;
        PackedStringArray get_packages() const;
        // WP3.3a: the game's art (an LBX entry's frames) as images; sprite_ready(id, sprite) follows.
        int64_t request_sprite(const String& lbx, int64_t entry);
        // The last legacy frame as an image (tests: works headless).
        Ref<Image> get_frame_image() const;

      protected:
        static void _bind_methods();

      private:
        Rect2 Frame_Rect() const;
        bool To_Game(const Vector2& local, Vector2* game) const;
        void Upload_Rows(int32_t row_first, int32_t row_last);
        void Apply_Cursor(bool force);
        void Apply_Window_Settings();
        void Poll_Native();

        String game_dir_;
        PackedStringArray args_;
        bool autostart_ = true;
        std::thread thread_;
        bool started_ = false;
        std::atomic<bool> game_finished_{false};
        bool exit_signalled_ = false;
        bool window_applied_ = false;

        // The game's cursor, shown as Godot's hardware cursor over this control.
        std::vector<uint32_t> cursor_argb_;
        int32_t cursor_size_ = -1;
        uint64_t cursor_generation_ = 0;
        float cursor_scale_ = 0.0f;
        bool cursor_custom_ = false;

        std::vector<uint32_t> frame_;
        int32_t width_ = 0;
        int32_t height_ = 0;
        uint64_t frame_generation_ = 0;
        int64_t galaxy_generation_ = -1;
        int64_t native_screen_ = -1;
        uint64_t native_visit_ = 0;
        // Leaving a native view for a legacy screen waits for that screen's first frame (or
        // kLegacyWaitMs), so the stale frame from before the native view never shows.
        int64_t legacy_wait_start_ms_ = -1;
        uint64_t legacy_ready_generation_ = 0; // the frame to wait for once the legacy screen has drawn
        uint64_t packs_generation_ = 0;

        // The frame in horizontal strips, so a frame that changed in a few rows uploads only those
        // strips.
        static constexpr int32_t k_strip_rows = 120;
        // Two textures per strip, written alternately, so an update never waits for the GPU to
        // finish drawing the copy shown last frame.
        std::vector<Ref<ImageTexture>> strips_[2];
        std::vector<uint8_t> strip_current_;
        PackedByteArray strip_bytes_;

        // Measurements for frontend-trial.md.
        uint64_t frames_taken_ = 0;
        uint64_t strips_uploaded_ = 0;
        double upload_ms_total_ = 0.0;
        double upload_ms_max_ = 0.0;
        uint64_t full_uploads_ = 0; // frames where every strip changed (not counting the first)
        double full_upload_ms_total_ = 0.0;
    };

} // namespace godot
