#include "pch.h"

#include "antares/user_folder.h"

#include "game/user_paths.h"
#include "game/vfs.h"

// Kept apart from user_folder.cpp so the user_paths tests don't need the VFS.
namespace antares::user_folder {
    bool Mount(const std::string& game_directory) {
        if (!user_paths::Is_Enabled()) {
            // Portable: the setup wizard puts the original files in game/ (owner 2026-11-24: keep
            // the folder tidy); files beside the program, as before 0.8, are found first.
            std::string folder = game_directory;
            if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
            return vfs::Add_Raw_Search_Path((folder + GAME_FILES_FOLDER).c_str()) != 0;
        }
        const std::string user_mods_path = std::string(user_paths::Get_Directory(user_paths::USER_DIRECTORY_DATA)) + "mods/";
        bool valid = vfs::Set_User_Config_Path(user_paths::Get_Directory(user_paths::USER_DIRECTORY_CONFIG)) != 0 &&
            vfs::Set_User_Mods_Path(user_mods_path.c_str()) != 0 && vfs::Add_Raw_Search_Path(game_directory.c_str()) != 0;
        for (const std::string& directory : user_paths::Get_Game_Asset_Directories()) {
            valid = vfs::Add_Raw_Search_Path(directory.c_str()) != 0 && valid;
        }
        return valid;
    }
} // namespace antares::user_folder
