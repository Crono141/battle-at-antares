#include "pch.h"

#include "doctest/doctest.h"

namespace {
    Uint64 fake_ticks;
    Uint32 fake_delay;
}

int16_t g_cached_mouse_x;
int16_t g_cached_mouse_y;
int32_t g_window_focus_state;
uint32_t g_last_scan_code;

extern "C" Uint64 SDLCALL SDL_GetTicks(void) {
    return fake_ticks;
}

extern "C" void SDLCALL SDL_Delay(Uint32 milliseconds) {
    fake_delay = milliseconds;
}

// timer.cpp logs speed changes; the test links no SDL.
extern "C" void SDLCALL SDL_LogInfo(int, SDL_PRINTF_FORMAT_STRING const char*, ...) {
}

namespace platform {
    void __cdecl Pump_Game_Input_Queue() {
    }
}

DOCTEST_TEST_CASE("animation speed keys adjust global release timing within bounds") {
    fake_ticks = 1000;
    key::Push_Key_Code(key::KEY_PAGE_UP, 0);

    DOCTEST_CHECK(timer::Get_Animation_Speedup() == 1);
    DOCTEST_CHECK(key::Read_Key() == key::KEY_PAGE_UP);
    DOCTEST_CHECK(timer::Animation_Speed_Change_Is_Visible());

    fake_ticks = 4024;
    DOCTEST_CHECK(timer::Animation_Speed_Change_Is_Visible());
    fake_ticks = 4025;
    DOCTEST_CHECK_FALSE(timer::Animation_Speed_Change_Is_Visible());

    fake_ticks = 5000;
    for (int i = 0; i < 3; ++i) {
        key::Push_Key_Code(key::KEY_PAGE_UP, 0);
        DOCTEST_CHECK(key::Read_Key() == key::KEY_PAGE_UP);
    }
    DOCTEST_CHECK(timer::Get_Animation_Speedup() == 3);

    fake_ticks = 8024;
    DOCTEST_CHECK(timer::Animation_Speed_Change_Is_Visible());

    fake_ticks = 8500;
    timer::Mark_Time();
    fake_delay = UINT32_MAX;
    timer::Release_Time(1);
    DOCTEST_CHECK(fake_delay == 0);

    fake_ticks = 9000;
    for (int i = 0; i < 6; ++i) {
        key::Push_Key_Code(key::KEY_PAGE_DOWN, 0);
        DOCTEST_CHECK(key::Read_Key() == key::KEY_PAGE_DOWN);
    }
    DOCTEST_CHECK(timer::Get_Animation_Speedup() == -3);

    timer::Mark_Time();
    fake_delay = 0;
    timer::Release_Time(1);
    DOCTEST_CHECK(fake_delay == 220);

    key::Push_Key_Code(0x1234, 0);
    DOCTEST_CHECK(key::Read_Key() == 0x1234);
    DOCTEST_CHECK(timer::Get_Animation_Speedup() == -3);
}