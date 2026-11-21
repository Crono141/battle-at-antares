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
    void Beam(int16_t from, int16_t to, int16_t non_ship_target, const struct s_beam_fx_entry* entries);

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
