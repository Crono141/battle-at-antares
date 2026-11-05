#pragma once

// WP3.0 trial path A: a Control that runs the game and shows its legacy 4x frame, with input
// forwarded in 640x480 game coordinates. Galaxy state is read from the snapshot the game thread
// publishes with each frame (src/antares/host.h).

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/typed_array.hpp>

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

    protected:
        static void _bind_methods();

    private:
        Rect2 Frame_Rect() const;
        bool To_Game(const Vector2& local, Vector2* game) const;
        void Upload_Rows(int32_t row_first, int32_t row_last);

        String game_dir_;
        PackedStringArray args_;
        bool autostart_ = true;
        std::thread thread_;
        bool started_ = false;

        std::vector<uint32_t> frame_;
        int32_t width_ = 0;
        int32_t height_ = 0;
        uint64_t frame_generation_ = 0;
        int64_t galaxy_generation_ = -1;

        // The frame in horizontal strips, so a frame that changed in a few rows uploads only those
        // strips.
        static constexpr int32_t k_strip_rows = 120;
        std::vector<Ref<ImageTexture>> strips_;
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
