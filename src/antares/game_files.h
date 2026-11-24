#pragma once

#include <string>
#include <vector>

// The original Master of Orion II 1.31 files the game needs (WP5.10, D28). One list for the game's
// start-up check (MOX2::Set_Mox_Alt_Path), the launcher's check before Play and the setup wizard.
// It holds every LBX the engine names (code and the ESTRINGS table) that a 1.31 install has, plus
// the families it builds names for (TANM_001-213, SR_R0-C_SC/SP/TR, BLDG0-5). Left out: the
// French, German and Spanish text files (an English-only install lacks them; the game asks for one
// only when that language is chosen) and the 14 files nothing opens (COLPUSP, CUSTMSTR, ENGMSG,
// FILEDATA, IFONTS, INSTALL, LOADSAVE, MSGENG, NDATA, NEXTPLYR, OCPOL, PLAYSPEC, SYSDISP; COLREFIT
// is a code namespace, not the file). PATCH13.LBX is on it: a CD install not patched to 1.31 fails.
namespace antares::game_files {
    /** Every required file name, upper case. */
    const std::vector<std::string>& Required_Files();

    struct s_files_check {
        bool ok = false;
        std::vector<std::string> missing;  // in list order
        std::vector<std::string> searched; // the folders looked in, as the game looks
    };

    /** Looks for each required file where the game reads original files: the game folder, the
     *  -game-dir folder and the user folder's game/ (the VFS's raw search paths; case doesn't
     *  matter). A package overriding an LBX doesn't count: these are the original files. Needs
     *  vfs::Init (and antares::user_folder::Mount for an installed build). */
    s_files_check Check_Original_Files();

    /** Plain words for the player: how many are missing, the first few names, where it looked. */
    std::string Describe(const s_files_check& check);
} // namespace antares::game_files
