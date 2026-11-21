#include "pch.h"

#include "doctest/doctest.h"
#include "game/flt1.h"
#include "game/maintain.h"

DOCTEST_TEST_CASE("scrap value sums the listed ships (D25 argument form)") {
    // It used to sum the fleet screen's selected big icons; the native scrap command has no
    // screen, so it takes the same ships Scrap_Ships was given. Ships at one of the owner's
    // colonies are worth their refund share (maintain::Ship_Scrap_Value).
    s_ship_data ships[4] = {};
    s_star_data stars[1] = {};
    stars[0].has_colony = 1; // player 0
    for (int i = 0; i < 4; ++i) {
        ships[i].d.cost = (int16_t)(1 << i);
        ships[i].owner = 0;
        ships[i].status = 0;
        ships[i].location = 0;
    }
    s_ship_data* const saved_ships = MOX::ship;
    s_star_data* const saved_stars = MOX::star;
    const uint8_t saved_refund = maintain::ship_scrap_refund_percent;
    MOX::ship = ships;
    MOX::star = stars;
    maintain::ship_scrap_refund_percent = 100;
    const int16_t list[] = {3, 1};
    DOCTEST_CHECK(FLT1::Get_Scrap_Ship_Value(list, 2) == 10);
    DOCTEST_CHECK(FLT1::Get_Scrap_Ship_Value(list, 0) == 0);
    maintain::ship_scrap_refund_percent = saved_refund;
    MOX::star = saved_stars;
    MOX::ship = saved_ships;
}
