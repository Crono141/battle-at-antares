// The to-do list (antares/todo.h, WP3.4d) for tests of the game code that calls it: no frontend
// claims "todo", so the reports chain behaves as the original.
#include "antares/todo.h"

namespace antares::todo {
    bool Active() { return false; }
    void Note_New_System(int16_t, int16_t, int16_t, bool) {}
    void Note_Leader_Level(int16_t, int16_t) {}
    void Note_Summary() {}
    void Note_Colonize(int16_t) {}
    void Note_Hire(int16_t) {}
    int32_t Open_Count() {
        return 0;
    }
    void Before_Next_Turn() {}
} // namespace antares::todo
