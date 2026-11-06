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
// Blocking rules code asks the player through requests: the generic dialogs (TEXTBOX, GENDRAW,
// fields::Get_List_Field) post one and wait for the answer when the frontend has claimed that kind,
// else they run the old popup.
//
// Threading: Run_Screen, the request calls and Update_Packages run on the game thread. The frontend calls
// the rest from its own thread; they only touch queues behind a mutex, never game memory.
//
// Commands, queries, results, snapshots and requests are JSON text, so any frontend can speak them.
// The contract is notes/antares/native-bridge.md, "The interface" (WP3.2b); the verbs themselves
// are one table in antares/commands.h, which the native loop, the control channel and the tests
// share. This file adds the bridge verbs "watch" and "unwatch" (snapshots).

#include <cstdint>
#include <string>
#include <vector>

namespace antares::native {

    // ---- Frontend side (any thread) ----

    // The frontend says which SCREEN_* values it shows natively.
    void Claim_Screen(int16_t screen, bool claimed);
    // The request kinds the frontend answers itself ("message", "confirm", "warning", "choose");
    // the others keep the legacy popup. An empty list hands every kind back.
    void Claim_Requests(const std::vector<std::string>& kinds);

    // The control channel (ANTARES_CONTROL, input scripts) acts as the frontend in the SDL build:
    // with it attached, claimed screens run natively and claimed requests wait for its answers,
    // with no Godot host.
    void Attach_Control(bool attached);
    bool Control_Attached();

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
        std::string kind; // "message", "confirm", "warning", "choose"
        std::string text; // UTF-8
        std::string json; // the whole request, as the contract describes it
    };
    bool Take_Request(s_request* request);
    // True while the game thread waits for the answer to a request.
    bool Request_Pending();
    // value: 1 for OK or Yes, 0 for No; for "choose" the option's index, or -1 to cancel.
    void Answer_Request(uint64_t id, int32_t value);

    // Snapshots of watched queries ({"verb": "watch", "what": "empire"}): the game thread re-runs
    // each one while a native screen runs and posts it when its answer changed.
    bool Take_Snapshot(std::string* json);

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

    // Requests from the game's generic dialogs (WP3.2b). Each returns true when the frontend
    // answered, so the legacy popup is skipped; false when no frontend claimed that kind.
    // TEXTBOX::Do_Text_Box (GENDRAW::Message_Box, Text_Box, Timed_Text_Box, User_Box 0, 2, 4, 5):
    bool Message(const char* title, const char* text, uint16_t timeout_ticks);
    // GENDRAW::Confirmation_Box (User_Box 1): *answer is 1 for Yes, 0 for No.
    bool Confirm(const char* text, int16_t* answer);
    // GENDRAW::Message_Box_Exploding (Warning_Box, User_Box 3):
    bool Warning(const char* text);
    // fields::Get_List_Field: `list` holds items of `item_size` bytes up to an empty one; `flags`
    // (may be null) marks enabled items. *answer is the item, or -1; *selected follows it.
    bool Choose(const char* title, const char* list, int32_t item_size, const uint8_t* flags, int8_t* selected, int16_t* answer);

    // Called by Screen_Control between screens: refreshes Resource_Packs when packages changed.
    void Update_Packages();

    // Test seams (tests/unit/native_test.cpp): the command interpreter without the loop, and one
    // snapshot refresh.
    std::string Run_Command_For_Test(int16_t screen, const std::string& json);
    void Refresh_Snapshots_For_Test(int16_t screen);

} // namespace antares::native
