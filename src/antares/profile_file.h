#pragma once

#include <string>
#include <vector>

// Launcher profiles (WP5.1, D23) are ordinary config files, data/profiles/<name>.cfg, that the
// game reads with -config. The launcher manages only the `enable <id>;` lines; every other line
// (includes, settings, comments) is kept as written. A managed line holds one enable statement,
// optionally followed by a comment; the first enable is the highest priority.
namespace antares::profile {
    /** The packages enabled by managed lines, in file order (highest priority first). */
    std::vector<std::string> Enabled_Packages(const std::string& text);

    /** `text` with its managed enable lines replaced by one line per id, in order. The new lines go
     *  where the first old one was; with none, after the leading includes and comments. */
    std::string With_Enabled_Packages(const std::string& text, const std::vector<std::string>& package_ids);

    /** The text of a new profile: it includes the base config, so its settings still apply. */
    std::string New_Profile_Text();

    /** Profile names are file names: letters, digits, space, '-', '_' and '.', not starting with
     *  '.' or a space, at most 64 characters. */
    bool Is_Valid_Profile_Name(const std::string& name);
} // namespace antares::profile
