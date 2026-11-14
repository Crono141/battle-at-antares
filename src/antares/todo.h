#pragma once

// WP3.4d: the start-of-turn to-do list (owner, 2026-11-13). When a frontend claims "todo"
// requests, the reports chain (REPORT::Display_Report_Aux) no longer walks the player through
// every item one by one: the rules part of each item still runs where it always ran, and what
// it would have shown or asked is parked here for the frontend's TO DO flyout.
//  - to do: colonies with an empty build queue (computed live), colony and outpost ships that
//    arrived at a system (REPORT::Colonization without a colony base), the leader for hire;
//  - news, read once: systems explored (with the special found and a marooned leader joining or
//    not), leaders gaining a level, the turn summary.
// The rest of the chain (research pick, diplomacy, events, spy reports, the Antarans, a colony
// base's planet, occupation policy) still asks at once.

#include <cstdint>

namespace antares::todo {

    // A frontend answers "todo": the reports chain parks its items here.
    bool Active();

    // From the reports chain (report.cpp, officer.cpp).
    void Note_New_System(int16_t star, int16_t special, int16_t marooned_leader, bool joined);
    void Note_Leader_Level(int16_t leader, int16_t star);
    void Note_Summary();
    void Note_Colonize(int16_t star);
    void Note_Hire(int16_t leader);

    // MAINSCR::Do_Next_Turn: what the turn's end settles (a leader for hire still waiting is
    // declined, as the window's REJECT).
    void Before_Next_Turn();

} // namespace antares::todo
