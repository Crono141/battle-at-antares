#pragma once

// WP3.9a (PLAN.md, "WP3.9 build stages"): tactical space combat as a native scene.
//
// The battle still runs in the legacy code (COMBINIT::Tactical_Combat and the turn loop in
// COMBAT1::Do_Combat_Turn), with its drawing going to the hidden legacy page, so the rules and their
// random draws are the legacy ones. What changes when a frontend claims "combat" requests:
// - Begin/End bracket the battle: the battle state is the scene (native::Set_Scene), so every
//   request carries it as "scene_state", and Frame posts it as the "combat" snapshot whenever the
//   legacy code redraws (each step of a move, each AI action).
// - Native_Action replaces the human player's input: it posts a "combat" request, reads the answer
//   as an action ({"action": "move", "x": 12, "y": 30}, ...), checks it the way the legacy buttons and
//   map clicks did, and applies it through COMBAT1::Apply_Tactical_Action (the network replay's
//   rules path). A refused action is asked again with "error" set.
//
// Game thread only.

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <string>

namespace antares::sprites {
    struct s_sprite;
}

namespace antares::combat {

    // The battle about to start will be native: a frontend claims "combat" and it is single player.
    // Tactical_Combat asks before it holds native::Legacy_Combat_Scope (which hides every claim, so
    // a legacy battle draws its own boxes): a native battle holds no scope.
    bool Will_Be_Native();
    // Tactical_Combat once the battle is set up, and before it returns.
    void Begin(int16_t star_idx);
    void End();
    bool Active();

    // CMBTDRW1::Draw_Main_Combat_Screen: the legacy code redrew, so the state may have changed.
    void Frame();

    // WP3.9c: BEAMS::Beam_SFX is about to animate a volley (beams, and the shots of other weapons
    // drawn as beams): posted as a "combat_event" snapshot {"kind": "beam", from, to, missile,
    // tx, ty, shots: [{weapon, name, type, count, flags}]} for the frontend to animate. Ship deaths,
    // damage, shield hits and missile impacts the frontend reads from successive states.
    // WP3.9 stage 1 adds what the frontend's port of the volley needs: attacker and target {ship,
    // x, y, center, grid, size, facing, owner}, speedx2, shield_frames, per shot special, hits,
    // specials, damage_entry and fire_points [[dx, dy]], and fragments (the non-recipe weapons'
    // 16 colours, 6-bit) when the volley has such a weapon (native-bridge.md).
    // A native battle's Auto button while the AI plays the player's ships (owner 2026-11-22: Auto
    // couldn't be turned off): the frontend asks (any thread), the AI's own button polls
    // (cmbtai.cpp, where the original reads its Auto button) take it once.
    void Request_Auto_Off();
    bool Take_Auto_Off();

    void Beam(int16_t from, int16_t from_x, int16_t from_y, int16_t to, int16_t to_x, int16_t to_y, int16_t non_ship_target, const struct s_beam_fx_entry* entries);

    // WP3.9d (combat-plan.md section 13, foundation 1): the battle's effects as "animate" requests.
    // Each effect is posted when the rules reach it and the rules wait until the frontend has
    // played it, so order and pacing stay legacy's. A move or turn is posted in segments: the
    // steps gather while the rules run them, and any other effect (defensive fire, a missile
    // reaching the ship) first posts the segment travelled so far; Motion_End posts the rest.
    // True while effects go to the frontend: a native battle whose frontend claims "animate",
    // not in super-fast mode. The legacy code then skips its own drawing where that drawing
    // holds no rules (moves and turns, so far).
    bool Animating();
    // The legacy screen draws this battle's effects itself: not super-fast and not animated
    // natively. The rules around each effect run either way (D25: animation never decides results).
    bool Legacy_Draws();
    // Any other effect, with integer fields: Effect("gyro", {{"from", 3}, {"to", 7}}).
    void Effect(const char* kind, std::initializer_list<std::pair<const char*, int64_t>> fields);
    // Rotate_Ship turned `ship` one facing step (`from` to `to`, 0-15).
    void Turn_Step(int16_t ship, int16_t from, int16_t to);
    // Move_Ship moved `ship` one interpolation step (combat pixels, the ship's top-left corner).
    void Move_Step(int16_t ship, int16_t from_x, int16_t from_y, int16_t to_x, int16_t to_y);
    // The move or turn ended (posts the last segment).
    void Motion_End();
    // A sub-space teleporter or phase shifter jump (grid squares).
    void Teleport(int16_t ship, int16_t to_x, int16_t to_y);
    // Destroy_Ship_By: `ship` dies (`death`: 1 normal, 2+ the exploding kinds), before its explosion.
    void Destroyed(int16_t ship, int16_t killer, int16_t death);
    // Fire_Missile: stack `stack` leaves `from` for ship (or stack) `to`, aimed at (tx, ty) pixels.
    void Missile_Launch(int16_t stack, int16_t from, int16_t to, bool at_missile, int16_t tx, int16_t ty);
    // Resolve_Missile: stack `stack` reached its target at (x, y) pixels; hits follow in the state.
    void Missile_Arrive(int16_t stack, int16_t x, int16_t y);

    // Spike 3.9 (notes/antares/combat-capture-spike.md): a beam volley painted by the legacy code,
    // frame by frame, onto the hidden page cleared to transparent (render/capture.h), and handed
    // to the frontend as the sprite ("capture", id) with an "animate" event "beam" carrying
    // "capture": {entry, x, y, frames, sounds}. Legacy's 32 x 18-square window is moved over the
    // shooter and target in tiles, one pass of the loop per tile, stitched by world position.
    // ANTARES_COMBAT_CAPTURE=0 turns it off; ANTARES_CAPTURE_COMPARE=N also runs, for the first N
    // volleys, a legacy reference pass, a comparison and a forced-seam test (logged, with PNGs in
    // ANTARES_CAPTURE_DUMP).
    bool Capturing();
    class Beam_Capture {
      public:
        Beam_Capture(int16_t from, int16_t ax, int16_t ay, int16_t to, int16_t tx, int16_t ty, const struct s_beam_fx_entry* entries, int16_t non_ship_target);
        ~Beam_Capture();
        Beam_Capture(const Beam_Capture&) = delete;
        Beam_Capture& operator=(const Beam_Capture&) = delete;
        bool Active() const;
        // This pass draws as legacy does (the comparison's reference): background, ships, waits.
        bool Reference_Pass() const;
        // Before each pass: legacy's window for it, the screen positions recomputed, the volley's
        // entries restored (the loop uses them up).
        void Start_Pass(int16_t* ax, int16_t* ay, int16_t* tx, int16_t* ty, struct s_beam_fx_entry* entries);
        void End_Frame(); // after each frame of the loop
        bool Next_Pass(); // true: run the loop again
        void Finish();    // posts the volley and waits until the frontend has played it
      private:
        struct s_impl;
        s_impl* impl_;
    };
    // BEAMS::Draw_Beam2 during a capture (spike 3.9, native beams test): the call recorded with
    // its frame, in world pixels, and the weapon's 16-colour ramp as the palette holds it now.
    void Record_Beam2(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t frame_index, int32_t palette_base, int32_t style, int32_t sfx_sprite_id, int32_t wide_bolt_size_bonus);
    // BOLT::Bolt_Segment during a capture: the strand it is about to draw (spike 3.9).
    void Record_Strand(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t* colors, int32_t count);
    // ERICNET::Mox_Play_Sound during a capture: recorded at its frame instead of played (true).
    bool Capture_Sound(int16_t sound_id);
    // sprites::Export for lbx "capture": the frames of capture `entry`.
    bool Export_Capture(int32_t entry, sprites::s_sprite* out);

    // The human player's input for MOX::cur_ship. False when the battle isn't native (the legacy
    // fields then read input); true once one action was applied (*next_ship_flag as the turn loop
    // expects).
    bool Native_Action(int16_t active_player_id, int16_t* next_ship_flag);

    // The battle as JSON (the "combat" snapshot and the requests' scene_state).
    std::string State_Json();

    // Checks and applies one action (JSON text). Returns "" when applied, else why not (the
    // legacy refusal text where there is one). Used by Native_Action and the unit tests.
    std::string Apply_Action(const std::string& json, int16_t active_player_id, int16_t* next_ship_flag);

} // namespace antares::combat
