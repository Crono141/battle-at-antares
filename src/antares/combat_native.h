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
#include <string>

namespace antares::combat {

    // Tactical_Combat once the battle is set up, and before it returns.
    void Begin(int16_t star_idx);
    void End();
    bool Active();

    // CMBTDRW1::Draw_Main_Combat_Screen: the legacy code redrew, so the state may have changed.
    void Frame();

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
