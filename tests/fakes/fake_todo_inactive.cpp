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
    void Note_Report(const char*, bool) {}
    int32_t Open_Count() {
        return 0;
    }
    void Before_Next_Turn() {}
} // namespace antares::todo

// REPORT::Colonization_At_Star's native landing branch (owner, 2026-11-13): no native galaxy in tests.
#include "antares/native.h"
namespace antares::native {
    bool Galaxy_Will_Be_Native() {
        return false;
    }
    // The GNN's native scene window (events.cpp): no frontend claims popups in tests.
    bool Popups_Native() {
        return false;
    }
    bool Popup(const char*, const char*, const std::string&, int32_t*) {
        return false;
    }
    void Set_Scene(std::function<std::string()>, std::function<void()>) {}
    void Clear_Scene() {}
} // namespace antares::native
