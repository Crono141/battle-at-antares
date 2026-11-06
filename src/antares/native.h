#pragma once

// WP3.1b, gate G5 (PLAN section 4, "Phase 3"): native screens and the command bridge.
//
// MOX2::Screen_Control asks Run_Screen before calling a screen's legacy handler. When the frontend
// has claimed that SCREEN_* and the game state allows it, the game thread runs the native loop
// instead: it executes the frontend's commands (each calls the same rules functions the legacy
// screen called), answers its queries and keeps sound, network and exit checks running, until a
// command changes MOX::current_screen. Everything else falls back to the legacy handler, whose
// frames the frontend shows as a texture.
//
// Blocking rules code asks the player through requests: HAROLD::User_Box posts one and waits for
// the frontend's answer when the frontend has claimed requests, else it runs the old popup.
//
// Threading: Run_Screen, User_Box and Update_Packages run on the game thread. The frontend calls
// the rest from its own thread; they only touch queues behind a mutex, never game memory.
//
// Commands and results are JSON text, so any frontend can speak them:
//   {"verb": "end_turn"}
//   {"verb": "open", "screen": 20}                      a legacy screen (SCREEN_COLONY_SUMMARY)
//   {"verb": "close"}                                   back to MOX::return_screen
//   {"verb": "query", "what": "empire" | "command_points"}
// Each result is {"id": n, "ok": true|false, "error": "...", ...fields}.

#include <cstdint>
#include <string>
#include <vector>

namespace antares::native {

    // ---- Frontend side (any thread) ----

    // The frontend says which SCREEN_* values it shows natively.
    void Claim_Screen(int16_t screen, bool claimed);
    // The frontend answers User_Box requests itself.
    void Claim_Requests(bool claimed);

    // Queues a command; returns its id (results carry it back).
    uint64_t Send_Command(const std::string& json);
    bool Take_Result(std::string* json);

    // The SCREEN_* the native loop is running, or -1 while a legacy handler runs.
    int16_t Native_Screen();
    // Counts native screen entries, so a frontend notices a return to the same screen that
    // happened between two of its frames (a quiet turn leaves the galaxy and comes back in
    // under one frame).
    uint64_t Native_Visit();
    // The legacy frame generation (host::Frame_Generation) when the native loop last handed over to
    // a legacy screen. Until a newer frame arrives, the legacy frame still shows what was on screen
    // before the native view took over (the old galaxy), so the frontend keeps its native view up.
    uint64_t Leave_Frame_Generation();

    struct s_request {
        uint64_t id = 0;
        std::string kind; // "message", "confirm", "warning"
        std::string text; // UTF-8
    };
    bool Take_Request(s_request* request);
    // True while the game thread waits for the answer to a request.
    bool Request_Pending();
    // value: 1 for OK or Yes, 0 for No.
    void Answer_Request(uint64_t id, int32_t value);

    // Godot resource packs (or zips) from the enabled packages' godot/ folders, lowest priority
    // first, so loading them in order lets the highest-priority package win. The generation
    // changes when the package set changes.
    std::vector<std::string> Resource_Packs(uint64_t* generation);

    // ---- Game side (game thread) ----

    // Called by Screen_Control before a screen's handler. Returns true when the screen ran
    // natively (MOX::current_screen then names the next screen).
    bool Run_Screen(int16_t screen);

    // True when the frontend wants this screen natively and may have it now. A legacy handler that
    // can be left between inputs (Main_Screen) checks it and returns to the dispatcher.
    bool Wants_Screen(int16_t screen);

    // Called by HAROLD::User_Box. Returns true when the frontend answered (result set).
    bool User_Box(int16_t type, const char* message, int16_t* result);

    // Called by Screen_Control between screens: refreshes Resource_Packs when packages changed.
    void Update_Packages();

    // Test seam: the command interpreter without the loop (tests/unit/native_test.cpp).
    std::string Run_Command_For_Test(int16_t screen, const std::string& json);

} // namespace antares::native
