#pragma once

// The system's applications menu (owner 2026-11-25: "a tick box to add programs to launchers"):
// the setup wizard's Done page offers an entry for the packages that make none themselves, the
// Windows zip (Start menu) and the Linux archive or AppImage (the freedesktop menu KDE, GNOME and
// the others read). The Windows installer has its own tick boxes, Flatpak always adds one, and a
// Mac app shows in Launchpad once it's in Applications, so none of those is offered here.

#include <string>

namespace launcher::menu {
    /** Whether the Done page offers the tick: a portable Windows or Linux build (not the installed
     *  one, not a Flatpak, not macOS). */
    bool Offered(const std::string& game_dir);
    /** Whether an entry already starts this launcher (then the tick isn't shown). */
    bool Exists(const std::string& game_dir);
    /** Adds the entry, pointing at this launcher (the AppImage itself when run from one). */
    bool Add(const std::string& game_dir, std::string* error);
} // namespace launcher::menu
