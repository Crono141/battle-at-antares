#pragma once
// WP3.9 stage 1 (native beams): the reference bench. A debug tool, never on in play: with
// ANTARES_BEAM_BENCH=<dir>, the first beam volley of a legacy battle (the legacy screen drawing it)
// first plays a fixed list of made-up volleys through the game's own BEAMS::Beam_SFX, every
// weapon with its modifiers and hit kinds, between the same two ships, and writes what the game
// drew on each tick to <dir>/beam_bench.jsonl: the fire calls (fire point, aim point), each
// Draw_Generic_Beam segment, the muzzle bursts, hit explosions, shield hits, Draw_Beam2 calls,
// strands and sounds. Then the game exits. The Godot port (views/beam_volley.gd) is compared
// against this trace (frontend/godot/project/tests/beam_bench_compare.gd).

#include <cstdint>

struct s_beam_fx_entry;

namespace antares::beam_bench {
    // At the top of BEAMS::Beam_SFX. Runs the bench the first time (when asked), then exits.
    void Run(int16_t attacker, int16_t attacker_x, int16_t attacker_y, int16_t target, int16_t target_x, int16_t target_y, int16_t non_ship_target);
    bool Tracing();
    void Tick();
    void Fire(int16_t from_x, int16_t from_y, int16_t to_x, int16_t to_y, int16_t weapon, int16_t frame, int16_t slot, int16_t fire_point, int16_t attacker_facing, int16_t target_facing);
    void Segment(int16_t frame, int16_t max_frames, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t hit, int16_t slot, int16_t step, int16_t length);
    void Burst(int16_t x, int16_t y, int16_t dir, int16_t variant, int16_t frame, int16_t slot);
    void Explosion(int16_t x, int16_t y, int16_t type, int16_t frame, int16_t size);
    void Shield(int16_t x, int16_t y, int16_t ship, int16_t special, int16_t heavy, int16_t rotation, int16_t frame, int16_t status);
    void Beam2(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t frame, int32_t palette_base, int32_t style);
    void Strand(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t* colors, int32_t count);
    void Sound(int16_t sound_id);
} // namespace antares::beam_bench
