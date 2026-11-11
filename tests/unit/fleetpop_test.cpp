#include "pch.h"

#include "doctest/doctest.h"
#include "game/fleetpop.h"
#include "game/shipstak.h"

namespace TEST_FLEETPOP {
    void Reset_();
}

DOCTEST_TEST_CASE("fleet at last active star exposes build outpost button") {
    TEST_FLEETPOP::Reset_();
    MOX::ship[0].location = static_cast<int16_t>(MOX::NUM_STARS - 1);

    DOCTEST_CHECK(FLEETPOP::Set_Build_Outpost_Button_Flag(MOX::fleet_box_ship_stack, 1) == 1);
}

DOCTEST_TEST_CASE("build outpost button rejects invalid direct-star boundaries") {
    TEST_FLEETPOP::Reset_();

    const int16_t invalid_locations[] = {
        MOX::NUM_STARS,
        static_cast<int16_t>(MAX_STARS),
        static_cast<int16_t>(SHIP_LOCATION_MOVING_OFFSET - 1),
    };

    for (int16_t location : invalid_locations) {
        TEST_FLEETPOP::Reset_();
        MOX::ship[0].location = location;

        DOCTEST_CAPTURE(location);
        DOCTEST_CHECK(FLEETPOP::Set_Build_Outpost_Button_Flag(MOX::fleet_box_ship_stack, 1) == 0);
    }
}

DOCTEST_TEST_CASE("fleet en route does not expose build outpost button") {
    TEST_FLEETPOP::Reset_();
    MOX::ship[0].location =
        static_cast<int16_t>(SHIP_LOCATION_WORMHOLE_OFFSET + 1);

    DOCTEST_CHECK(FLEETPOP::Set_Build_Outpost_Button_Flag(MOX::fleet_box_ship_stack, 1) == 0);
}

DOCTEST_TEST_CASE("fleet navigation is a no-op without ship stacks") {
    TEST_FLEETPOP::Reset_();
    MOX::ship_stack_count = 0;
    MOX::small_ship_stack_ptr = -1;

    FLT1::Next_Ship_Icon();
    DOCTEST_CHECK(MOX::small_ship_stack_ptr == -1);

    FLT1::Previous_Ship_Icon();
    DOCTEST_CHECK(MOX::small_ship_stack_ptr == -1);
}

DOCTEST_TEST_CASE("fleet summary counts only ships linked into the visible stack") {
    TEST_FLEETPOP::Reset_();

    MOX::ship_node[0].next_node = 1;
    MOX::ship_node[1].ship_idx = 1;
    MOX::ship_node[1].next_node = -1;

    MOX::ship[0].d.ship_type = SHIP_TYPE_COMBAT;
    MOX::ship[0].d.size = SHIP_SIZE_SMALL;
    MOX::ship[1].d.ship_type = SHIP_TYPE_OUTPOST;
    MOX::ship[2].d.ship_type = SHIP_TYPE_COLONY;

    int16_t total_count;
    int16_t colony_ships_count;
    int16_t outposts_count;
    int16_t transports_count;
    int16_t ship_sizes[SHIP_SIZE_DOOM_STAR + 1];

    SYS::N_Ships_By_Type_In_Stack(
        0,
        &total_count,
        &colony_ships_count,
        &outposts_count,
        &transports_count,
        ship_sizes);

    DOCTEST_CHECK(total_count == 2);
    DOCTEST_CHECK(ship_sizes[SHIP_SIZE_SMALL] == 1);
    DOCTEST_CHECK(colony_ships_count == 0);
    DOCTEST_CHECK(outposts_count == 1);
    DOCTEST_CHECK(transports_count == 0);
}

DOCTEST_TEST_CASE("Space Scanner uses its independent strategic range") {
    TEST_FLEETPOP::Reset_();
    MOX::ship[0].owner = 1;
    MOX::ship[0].x = 90;
    MOX::ship[1].owner = 0;
    MOX::ship[1].status = 0;
    MOX::ship[1].x = 0;
    MOX::player[0].tech_applications[TECH_APP_SPACE_SCANNER] =
        TECH_RESEARCH_STATUS_RESEARCHED;
    SHIPSTAK::scan_bonus.no_scanner = 2;
    SHIPSTAK::scan_bonus.space_scanner = 3;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 1);
    DOCTEST_CHECK(MOX::ship_stack_start[0] == 0);
}

DOCTEST_TEST_CASE("maximum strategic scan bonuses do not wrap negative") {
    TEST_FLEETPOP::Reset_();
    MOX::ship[0].owner = 1;
    MOX::ship[0].x = 90;
    MOX::ship[1].owner = 0;
    MOX::ship[1].status = 0;
    MOX::ship[1].x = 0;
    struct_::Set_Bit_Field(
        MOX::ship[1].d.special_device_flags, SPECIAL_BATTLE_SCANNER);
    SHIPSTAK::scan_bonus.no_scanner = 127;
    SHIPSTAK::scan_bonus.battle_scanner = 32767;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 1);
    DOCTEST_CHECK(MOX::ship_stack_start[0] == 0);
}

DOCTEST_TEST_CASE("configured planet proximity determines same-location detection") {
    TEST_FLEETPOP::Reset_();
    MOX::NUM_SHIPS = 1;
    MOX::NUM_COLONIES = 1;
    MOX::ship[0].owner = 1;
    MOX::colony[0].owner = 0;
    MOX::colony[0].planet = 0;
    MOX::planet[0].star_index = 0;
    MOX::star[0].x = 0;
    MOX::star[0].y = 0;
    MOX::player[1].traits[TRAIT_STEALTHY_SHIPS] = 1;
    SHIPSTAK::scan_bonus.planet = 9;
    SHIPSTAK::stealth_bonus.stealthy_ships = 10;
    SHIPSTAK::stealth_bonus.stealth_field = 0;
    SHIPSTAK::stealth_bonus.cloaking_device = 0;
    SHIPSTAK::stealth_bonus.phasing_cloak = 0;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 0);

    TEST_FLEETPOP::Reset_();
    MOX::NUM_SHIPS = 1;
    MOX::NUM_COLONIES = 1;
    MOX::ship[0].owner = 1;
    MOX::colony[0].owner = 0;
    MOX::colony[0].planet = 0;
    MOX::planet[0].star_index = 0;
    MOX::star[0].x = 0;
    MOX::star[0].y = 0;
    MOX::player[1].traits[TRAIT_STEALTHY_SHIPS] = 1;
    SHIPSTAK::scan_bonus.planet = 10;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 1);
}

DOCTEST_TEST_CASE("configured fleet proximity determines same-location detection") {
    TEST_FLEETPOP::Reset_();
    MOX::ship[0].owner = 1;
    MOX::ship[1].owner = 0;
    MOX::ship[1].status = 0;
    MOX::player[1].traits[TRAIT_STEALTHY_SHIPS] = 1;
    SHIPSTAK::scan_bonus.fleet = 9;
    SHIPSTAK::stealth_bonus.stealthy_ships = 10;
    SHIPSTAK::stealth_bonus.stealth_field = 0;
    SHIPSTAK::stealth_bonus.cloaking_device = 0;
    SHIPSTAK::stealth_bonus.phasing_cloak = 0;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 0);

    TEST_FLEETPOP::Reset_();
    MOX::ship[0].owner = 1;
    MOX::ship[1].owner = 0;
    MOX::ship[1].status = 0;
    MOX::player[1].traits[TRAIT_STEALTHY_SHIPS] = 1;
    SHIPSTAK::scan_bonus.fleet = 10;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 1);
}

DOCTEST_TEST_CASE("race_tech retains size penalty for zero-value stealth device") {
    TEST_FLEETPOP::Reset_();
    MOX::ship[0].owner = 1;
    MOX::ship[0].status = 1;
    MOX::ship[0].x = 90;
    MOX::ship[0].d.size = SHIP_SIZE_MEDIUM;
    struct_::Set_Bit_Field(
        MOX::ship[0].d.special_device_flags, SPECIAL_STEALTH_FIELD);
    MOX::ship[1].owner = 0;
    MOX::ship[1].status = 0;
    MOX::ship[1].x = 0;
    SHIPSTAK::scan_bonus.no_scanner = 2;
    SHIPSTAK::stealth_bonus.stealth_field = 0;
    SHIPSTAK::stealth_bonus.cloaking_device = 0;
    SHIPSTAK::stealth_bonus.phasing_cloak = 0;
    SHIPSTAK::stealth_size_penalty =
        SHIPSTAK::e_stealth_size_penalty::race_tech;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 1);
    DOCTEST_CHECK(MOX::ship_stack_start[0] == 0);
}

DOCTEST_TEST_CASE("race_tech retains size penalty when device bonuses cancel") {
    TEST_FLEETPOP::Reset_();
    MOX::ship[0].owner = 1;
    MOX::ship[0].status = 1;
    MOX::ship[0].x = 90;
    MOX::ship[0].d.size = SHIP_SIZE_MEDIUM;
    struct_::Set_Bit_Field(
        MOX::ship[0].d.special_device_flags, SPECIAL_STEALTH_FIELD);
    struct_::Set_Bit_Field(
        MOX::ship[0].d.special_device_flags, SPECIAL_CLOAKING_DEVICE);
    MOX::ship[1].owner = 0;
    MOX::ship[1].status = 0;
    MOX::ship[1].x = 0;
    SHIPSTAK::scan_bonus.no_scanner = 2;
    SHIPSTAK::stealth_bonus.stealth_field = 25;
    SHIPSTAK::stealth_bonus.cloaking_device = -25;
    SHIPSTAK::stealth_bonus.phasing_cloak = 0;
    SHIPSTAK::stealth_size_penalty =
        SHIPSTAK::e_stealth_size_penalty::race_tech;

    SHIPSTAK::Remove_Non_Detected_Ships();

    DOCTEST_CHECK(MOX::ship_stack_count == 1);
    DOCTEST_CHECK(MOX::ship_stack_start[0] == 0);
}