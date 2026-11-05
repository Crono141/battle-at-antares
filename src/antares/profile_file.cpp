#include "pch.h"

#include "antares/profile_file.h"

#include <algorithm>
#include <cctype>

#include "config/cfg_parser.h"
#include "config/config_decode.h"
#include "config/text_encoding.h"

namespace antares::profile {
    namespace {
        struct s_line {
            std::string text; // without the line ending
            std::string ending;
        };

        std::vector<s_line> Split_(const std::string& text) {
            std::vector<s_line> lines;
            size_t start = 0;
            while (start < text.size()) {
                const size_t newline = text.find('\n', start);
                if (newline == std::string::npos) {
                    lines.push_back({text.substr(start), ""});
                    break;
                }
                size_t end = newline;
                std::string ending = "\n";
                if (end > start && text[end - 1] == '\r') {
                    --end;
                    ending = "\r\n";
                }
                lines.push_back({text.substr(start, end - start), ending});
                start = newline + 1;
            }
            return lines;
        }

        bool Is_Space_(char character) {
            return character == ' ' || character == '\t';
        }

        bool Is_Id_Character_(char character) {
            return std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-';
        }

        // "enable <id>;" with optional spaces and a trailing '#' comment.
        bool Parse_Enable_(const std::string& line, std::string* package_id) {
            size_t cursor = 0;
            while (cursor < line.size() && Is_Space_(line[cursor])) ++cursor;
            if (line.compare(cursor, 6, "enable") != 0) return false;
            cursor += 6;
            if (cursor >= line.size() || !Is_Space_(line[cursor])) return false;
            while (cursor < line.size() && Is_Space_(line[cursor])) ++cursor;
            const size_t id_start = cursor;
            while (cursor < line.size() && Is_Id_Character_(line[cursor])) ++cursor;
            if (cursor == id_start) return false;
            const std::string id = line.substr(id_start, cursor - id_start);
            while (cursor < line.size() && Is_Space_(line[cursor])) ++cursor;
            if (cursor >= line.size() || line[cursor] != ';') return false;
            ++cursor;
            while (cursor < line.size() && Is_Space_(line[cursor])) ++cursor;
            if (cursor < line.size() && line[cursor] != '#') return false;
            *package_id = id;
            return true;
        }

        bool Is_Header_Line_(const std::string& line) {
            size_t cursor = 0;
            while (cursor < line.size() && Is_Space_(line[cursor])) ++cursor;
            return cursor == line.size() || line[cursor] == '#' || line.compare(cursor, 8, "include ") == 0;
        }
    } // namespace

    std::vector<std::string> Enabled_Packages(const std::string& text) {
        std::vector<std::string> ids;
        for (const s_line& line : Split_(text)) {
            std::string id;
            if (Parse_Enable_(line.text, &id)) {
                ids.push_back(id);
            }
        }
        return ids;
    }

    std::string With_Enabled_Packages(const std::string& text, const std::vector<std::string>& package_ids) {
        const std::vector<s_line> lines = Split_(text);
        std::string ending = "\n";
        for (const s_line& line : lines) {
            if (!line.ending.empty()) {
                ending = line.ending;
                break;
            }
        }
        std::string block;
        for (const std::string& id : package_ids) {
            block += "enable " + id + ";" + ending;
        }

        // Where the block goes: at the first managed line, else after the leading header lines
        // (comments, blank lines and includes), but before any blank lines that end the header.
        size_t insert_at = lines.size();
        bool found_enable = false;
        for (size_t index = 0; index < lines.size(); ++index) {
            std::string id;
            if (Parse_Enable_(lines[index].text, &id)) {
                insert_at = index;
                found_enable = true;
                break;
            }
        }
        if (!found_enable) {
            size_t header_end = 0;
            while (header_end < lines.size() && Is_Header_Line_(lines[header_end].text)) ++header_end;
            while (header_end > 0 && lines[header_end - 1].text.find_first_not_of(" \t") == std::string::npos) --header_end;
            insert_at = header_end;
        }

        std::string output;
        for (size_t index = 0; index < lines.size(); ++index) {
            if (index == insert_at) {
                output += block;
            }
            std::string id;
            if (Parse_Enable_(lines[index].text, &id)) {
                continue;
            }
            std::string line_ending = lines[index].ending;
            if (line_ending.empty() && index + 1 == lines.size() && insert_at == lines.size() && !block.empty()) {
                line_ending = ending; // the block follows a last line that had no line ending
            }
            output += lines[index].text + line_ending;
        }
        if (insert_at >= lines.size()) {
            output += block;
        }
        return output;
    }

    std::string New_Profile_Text() {
        return "# Battle at Antares launcher profile. The launcher manages the enable lines;\n"
               "# anything else in this file is kept as written.\n"
               "include ../antares.cfg;\n";
    }

    bool Is_Valid_Profile_Name(const std::string& name) {
        if (name.empty() || name.size() > 64 || name[0] == '.' || name[0] == ' ' || name.back() == ' ') {
            return false;
        }
        for (const char character : name) {
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != ' ' && character != '-' &&
                character != '_' && character != '.') {
                return false;
            }
        }
        return true;
    }
    bool Is_Editable_Setting(config::t_variable_id variable_id) {
        if (variable_id >= config::variable_count) return false;
        const config::s_runtime_variable_descriptor& variable = config::runtime_variables[variable_id];
        if (variable.aggregate || variable.vector || config::Get_Variable_Type(variable_id) == nullptr) return false;
        // Entries of implicit lists (a list ended by a terminator) can only be assigned as the
        // whole list, not one by one.
        const config::s_runtime_schema* schema = config::Get_Runtime_Schema();
        const config::s_schema_node_descriptor* node = config::Get_Variable_Node(schema, variable_id);
        while (node != nullptr) {
            if (node->kind == config::CONFIG_NODE_ARRAY && node->implicit_terminator) return false;
            node = node->parent == config::CONFIG_NO_NODE || node->parent >= schema->node_count ? nullptr : &schema->nodes[node->parent];
        }
        return true;
    }

    namespace {
        // Flags whose bits make up `value` exactly, joined with ','; false if any bit is left over.
        template <typename t_flag, typename t_test>
        bool Join_Flags_(const t_flag* flags, size_t count, t_test contains, std::string* text) {
            std::string joined;
            for (size_t index = 0; index < count; ++index) {
                if (contains(flags[index])) {
                    if (!joined.empty()) joined += ',';
                    joined += flags[index].name;
                }
            }
            *text = joined;
            return !joined.empty();
        }
    } // namespace

    std::string Quote_Cfg_String(const std::string& utf8) {
        std::string quoted = "\"";
        for (const char character : utf8) {
            if (character == '"' || character == '\\') {
                quoted += '\\';
                quoted += character;
            } else if (static_cast<unsigned char>(character) < 0x20) {
                char escape[8];
                SDL_snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned char>(character));
                quoted += escape;
            } else {
                quoted += character;
            }
        }
        return quoted + "\"";
    }

    bool Format_Setting_Value(config::t_variable_id variable_id, const config::s_typed_value& value, std::string* text) {
        const config::s_type_descriptor* type = config::Get_Variable_Type(variable_id);
        if (type == nullptr || text == nullptr || !Is_Editable_Setting(variable_id)) return false;
        switch (value.type) {
            case config::CONFIG_VALUE_BOOL: *text = value.bool_value ? "true" : "false"; return true;
            case config::CONFIG_VALUE_RATIO:
                *text = std::to_string(value.ratio_numerator);
                if (value.ratio_denominator != 1) *text += "/" + std::to_string(value.ratio_denominator);
                return true;
            case config::CONFIG_VALUE_STRING: {
                std::string utf8;
                config::Decode_Cp850_To_Utf8(value.string_value.data(), value.string_value.size(), &utf8);
                *text = Quote_Cfg_String(utf8);
                return true;
            }
            case config::CONFIG_VALUE_BITMAP: {
                const size_t bytes = value.bitmap_value.size();
                for (size_t index = 0; index < type->bitmap_value_count; ++index) {
                    if (std::equal(value.bitmap_value.begin(), value.bitmap_value.end(), type->bitmap_values[index].value)) {
                        *text = type->bitmap_values[index].name;
                        return true;
                    }
                }
                std::vector<uint8_t> covered(bytes, 0);
                const bool joined = Join_Flags_(type->bitmap_flags, type->bitmap_flag_count, [&](const config::s_bitmap_symbol_descriptor& flag) {
                        for (size_t byte = 0; byte < bytes; ++byte) {
                            if ((flag.value[byte] & ~value.bitmap_value[byte]) != 0) return false;
                        }
                        for (size_t byte = 0; byte < bytes; ++byte) covered[byte] |= flag.value[byte];
                        return true; }, text);
                return joined && std::equal(covered.begin(), covered.end(), value.bitmap_value.begin());
            }
            default: break;
        }
        const int64_t integer = value.integer_value;
        if (type->scalar_kind == config::CONFIG_SCALAR_ENUM || type->scalar_kind == config::CONFIG_SCALAR_MASK) {
            for (size_t index = 0; index < type->value_count; ++index) {
                if (type->values[index].value == integer) {
                    *text = type->values[index].name;
                    return true;
                }
            }
        }
        if (type->scalar_kind == config::CONFIG_SCALAR_MASK && integer != 0) {
            uint64_t covered = 0;
            const bool joined = Join_Flags_(type->flags, type->flag_count, [&](const config::s_symbol_descriptor& flag) {
                    const uint64_t bits = static_cast<uint64_t>(flag.value);
                    if (bits == 0 || (bits & static_cast<uint64_t>(integer)) != bits) return false;
                    covered |= bits;
                    return true; }, text);
            if (joined && covered == static_cast<uint64_t>(integer)) return true;
        }
        if (type->scalar_kind != config::CONFIG_SCALAR_PRIMITIVE && !type->allow_numeric) {
            return false;
        }
        *text = std::to_string(integer);
        return true;
    }

    std::string Setting_Path(config::t_variable_id variable_id) {
        std::string name;
        if (!config::Get_Variable_Name(variable_id, &name)) return "";
        for (char& character : name) {
            if (character == '.') character = ' ';
        }
        return name;
    }

    config::t_variable_id Setting_Line_Variable(const std::string& line) {
        config::s_cfg_document document;
        config::s_cfg_diagnostic diagnostic;
        if (line.find('=') == std::string::npos ||
            !config::Parse_Cfg("profile", line.data(), line.size(), &document, &diagnostic) ||
            document.statements.size() != 1 || document.statements[0].kind != config::CFG_STATEMENT_ASSIGNMENT) {
            return config::CONFIG_NO_VARIABLE;
        }
        config::s_config_write_set writes;
        if (!config::Decode_Assignment(&document.statements[0], &writes, &diagnostic) || writes.writes.size() != 1 ||
            writes.writes[0].leaf_index != 0 || !Is_Editable_Setting(writes.writes[0].variable_id)) {
            return config::CONFIG_NO_VARIABLE;
        }
        return writes.writes[0].variable_id;
    }

    std::vector<config::t_variable_id> Profile_Settings(const std::string& text) {
        std::vector<config::t_variable_id> settings;
        for (const s_line& line : Split_(text)) {
            const config::t_variable_id variable_id = Setting_Line_Variable(line.text);
            if (variable_id != config::CONFIG_NO_VARIABLE &&
                std::find(settings.begin(), settings.end(), variable_id) == settings.end()) {
                settings.push_back(variable_id);
            }
        }
        return settings;
    }

    std::string With_Setting(const std::string& text, config::t_variable_id variable_id, const std::string& value_text) {
        const std::vector<s_line> lines = Split_(text);
        std::string ending = "\n";
        for (const s_line& line : lines) {
            if (!line.ending.empty()) {
                ending = line.ending;
                break;
            }
        }
        const std::string statement = value_text.empty() ? "" : Setting_Path(variable_id) + " = " + value_text + ";";
        std::string output;
        bool written = false;
        for (size_t index = 0; index < lines.size(); ++index) {
            const s_line& line = lines[index];
            if (Setting_Line_Variable(line.text) != variable_id) {
                output += line.text + (line.ending.empty() && index + 1 == lines.size() && !written && !statement.empty() ? ending : line.ending);
                continue;
            }
            if (written || statement.empty()) {
                continue;
            }
            // Keep the line's comment: the text after the last ';' when it starts with '#'.
            std::string comment;
            const size_t semicolon = line.text.rfind(';');
            if (semicolon != std::string::npos) {
                const size_t start = line.text.find_first_not_of(" \t", semicolon + 1);
                if (start != std::string::npos && line.text[start] == '#') {
                    comment = line.text.substr(semicolon + 1);
                }
            }
            output += statement + comment + (line.ending.empty() ? ending : line.ending);
            written = true;
        }
        if (!written && !statement.empty()) {
            output += statement + ending;
        }
        return output;
    }
} // namespace antares::profile
