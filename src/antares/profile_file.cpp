#include "pch.h"

#include "antares/profile_file.h"

#include <cctype>

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
} // namespace antares::profile
