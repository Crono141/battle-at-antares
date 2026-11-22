// WP3.9d: as fake_combat_native.cpp, for unit tests that link no MOX globals: the legacy screen
// always draws, and no native battle posts anything.
#include "antares/combat_native.h"

namespace antares::combat {
    bool Animating() {
        return false;
    }
    bool Legacy_Draws() {
        return true;
    }
    void Effect(const char*, std::initializer_list<std::pair<const char*, int64_t>>) {}
    void Turn_Step(int16_t, int16_t, int16_t) {}
    void Move_Step(int16_t, int16_t, int16_t, int16_t, int16_t) {}
    void Motion_End() {}
    void Teleport(int16_t, int16_t, int16_t) {}
    void Destroyed(int16_t, int16_t, int16_t) {}
    void Missile_Launch(int16_t, int16_t, int16_t, bool, int16_t, int16_t) {}
    void Missile_Arrive(int16_t, int16_t, int16_t) {}
    bool Capture_Sound(int16_t) {
        return false; // spike 3.9: no capture in unit tests
    }
} // namespace antares::combat
