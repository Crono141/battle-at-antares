// WP3.13: lobby.cpp (title screen, new game, Hall of Fame) needs most of the game's new-game code;
// the bridge tests run without it, so its entries and entry steps are empty here.
#include "antares/commands_internal.h"
#include "antares/lobby.h"

namespace antares::lobby {
    void Enter(int16_t) {}
    int16_t Flash_Entry() {
        return -1;
    }
    void Set_Flash_Entry(int16_t) {}
} // namespace antares::lobby

namespace antares::commands::internal {
    void Add_Lobby_Entries(std::vector<s_entry>*) {}
} // namespace antares::commands::internal
