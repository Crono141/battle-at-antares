#pragma once

#include <string>
#include <vector>

#include "config/config_runtime.h"

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

    // Settings (WP5.2). A managed setting line holds one assignment to one scalar setting, such as
    // `interface fullscreen = true;`, optionally followed by a comment.

    /** True for settings the launcher edits: one scalar value, not a table or a list. */
    bool Is_Editable_Setting(config::t_variable_id variable_id);

    /** `value` in CFG syntax (`true`, `640`, `mmpx4`, `a,b`, `3/2`, `"text"`). False when the
     *  value can't be written as one token (the launcher then leaves the setting alone). */
    bool Format_Setting_Value(config::t_variable_id variable_id, const config::s_typed_value& value, std::string* text);

    /** `utf8` as a CFG string literal, with quotes and escapes. */
    std::string Quote_Cfg_String(const std::string& utf8);

    /** The setting's path as a CFG statement writes it: "interface window_width". */
    std::string Setting_Path(config::t_variable_id variable_id);

    /** The setting a managed line assigns, or CONFIG_NO_VARIABLE. */
    config::t_variable_id Setting_Line_Variable(const std::string& line);

    /** The settings that managed lines assign, in file order, each once. */
    std::vector<config::t_variable_id> Profile_Settings(const std::string& text);

    /** `text` with the setting assigned `value_text` (CFG syntax): the first managed line for it is
     *  rewritten (keeping its comment), any others are removed, and with none a line is added at
     *  the end. An empty `value_text` removes the setting's lines. */
    std::string With_Setting(const std::string& text, config::t_variable_id variable_id, const std::string& value_text);
} // namespace antares::profile
