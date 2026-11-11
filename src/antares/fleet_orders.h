#pragma once

// WP3.3c: the fleet box and fleet orders as commands and queries (fleet_orders.cpp registers them
// in the command table; the contract is notes/antares/native-bridge.md), and the rules steps that
// the legacy screens share with them.

#include <cstdint>

namespace antares::fleet {

    // The shared consume step for a colony or outpost ship that founds a colony (owner,
    // 2026-11-06): the ship is used up, and an officer aboard (or on the way to it) goes back to the
    // pool. The original only marked the ship used and left its officer assigned to a dead ship.
    // Called by the native commands and by the legacy galaxy's Colonize and Outpost buttons
    // (mainscr2.cpp); the reports-phase prompt (REPORT::Colonization) follows in WP3.8.
    void Consume_Ship(int16_t ship_idx);

} // namespace antares::fleet
