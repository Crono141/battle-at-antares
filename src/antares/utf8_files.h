#pragma once

// Paths are UTF-8 everywhere in Battle at Antares (SDL gives them so). On Windows the C runtime's
// narrow functions (fopen, remove, chdir) and MinGW's std::filesystem::path(std::string) read a
// narrow string in the legacy code page instead, so a user folder under a name like
// "C:\Users\Zoë" can't be opened (WP5.15, found under Wine). These go through the wide functions
// on Windows and are the plain ones elsewhere. A process-wide UTF-8 manifest isn't enough: the
// game runs inside Godot's executable, whose manifest we don't own.

#include <cstdio>
#include <filesystem>
#include <string>

namespace antares::utf8 {
    /** fopen with a UTF-8 path. */
    FILE* Open(const char* path, const char* mode);
    /** freopen with a UTF-8 path. */
    FILE* Reopen(const char* path, const char* mode, FILE* stream);
    /** remove with a UTF-8 path. */
    int Remove(const char* path);
    /** chdir with a UTF-8 path. */
    int Change_Directory(const char* path);
    /** A filesystem path from UTF-8 text. */
    std::filesystem::path Path(const std::string& text);
    /** A filesystem path as UTF-8 text. */
    std::string Text(const std::filesystem::path& path);
} // namespace antares::utf8
