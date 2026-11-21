#include "pch.h"

#include "antares/host.h"

// Tests run unhosted: the game folder comes from the executable (startup::Set_Game_Directory).
namespace antares::host {
    bool Active() {
        return false;
    }
} // namespace antares::host
