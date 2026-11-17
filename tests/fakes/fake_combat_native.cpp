// WP3.9d: the battle's effect hooks (antares/combat_native.h) for unit tests that link the combat
// rules without the native bridge: no native battle, so nothing is posted.
#include "pch.h"

#include "antares/combat_native.h"

namespace antares::combat {
    bool Animating() {
        return false;
    }
    bool Legacy_Draws() {
        return MOX::super_fast_flag == 0;
    }
    void Effect(const char*, std::initializer_list<std::pair<const char*, int64_t>>) {}
    void Turn_Step(int16_t, int16_t, int16_t) {}
    void Move_Step(int16_t, int16_t, int16_t, int16_t, int16_t) {}
    void Motion_End() {}
    void Teleport(int16_t, int16_t, int16_t) {}
    void Destroyed(int16_t, int16_t, int16_t) {}
    void Missile_Launch(int16_t, int16_t, int16_t, bool, int16_t, int16_t) {}
    void Missile_Arrive(int16_t, int16_t, int16_t) {}
} // namespace antares::combat
