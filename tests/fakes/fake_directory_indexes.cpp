#include "pch.h"

#include "game/compat.h"

// test_startup_options links startup_directory.cpp for the Antares identity test (Set_Game_Directory);
// it keeps no directory listings (compat.cpp's Resolve_File_Path_Case is not linked), so none to clear.
void Clear_Directory_Indexes() {
}
