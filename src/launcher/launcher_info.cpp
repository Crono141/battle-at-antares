#include "pch.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>

#include "antares/profile_file.h"
#include "launcher/launcher_app.h"

// Package information (WP5.5): a package's README.md, the settings it changes
// (a dry run that enables only it), and the manual's keyboard page.
namespace launcher {
    namespace {
        const ImVec4 COLOUR_DIM(0.60f, 0.62f, 0.68f, 1.0f);
        const ImVec4 COLOUR_ACCENT(0.55f, 0.75f, 1.0f, 1.0f);
        const ImVec4 COLOUR_ERROR(1.0f, 0.45f, 0.40f, 1.0f);

        enum e_block_kind { BLOCK_H1, BLOCK_H2, BLOCK_H3, BLOCK_PARAGRAPH, BLOCK_BULLET, BLOCK_TABLE };

        struct s_block {
            e_block_kind kind = BLOCK_PARAGRAPH;
            std::string text;
            std::vector<std::vector<std::string>> rows; // tables: the header row first
        };

        std::string Lower_(std::string text) {
            for (char& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return text;
        }

        std::string Trim_(const std::string& text) {
            const size_t first = text.find_first_not_of(" \t\r");
            if (first == std::string::npos) return "";
            return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
        }

        // Drops the inline markers this renderer can't style.
        std::string Plain_(const std::string& text) {
            std::string plain;
            for (size_t index = 0; index < text.size(); ++index) {
                if (text[index] == '`') continue;
                if (text.compare(index, 2, "**") == 0) {
                    ++index;
                    continue;
                }
                plain += text[index];
            }
            return plain;
        }

        std::vector<std::string> Cells_(const std::string& line) {
            std::vector<std::string> cells;
            std::string cell;
            bool in_code = false;
            for (size_t index = 0; index < line.size(); ++index) {
                const char character = line[index];
                if (character == '`') in_code = !in_code;
                if (character == '|' && !in_code) {
                    cells.push_back(Plain_(Trim_(cell)));
                    cell.clear();
                } else {
                    cell += character;
                }
            }
            cells.push_back(Plain_(Trim_(cell)));
            // "| a | b |" leaves an empty cell at each end.
            if (!cells.empty() && cells.front().empty()) cells.erase(cells.begin());
            if (!cells.empty() && cells.back().empty()) cells.pop_back();
            return cells;
        }

        bool Is_Separator_Row_(const std::string& line) {
            return line.find_first_not_of("|-: \t") == std::string::npos && line.find('-') != std::string::npos;
        }

        std::vector<s_block> Parse_(const std::string& text) {
            std::vector<s_block> blocks;
            size_t start = 0;
            bool open_paragraph = false;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string::npos) end = text.size();
                const std::string line = Trim_(text.substr(start, end - start));
                start = end + 1;
                if (line.empty()) {
                    open_paragraph = false;
                    if (end == text.size()) break;
                    continue;
                }
                if (line[0] == '#') {
                    const size_t level = line.find_first_not_of('#');
                    s_block block;
                    block.kind = level == 1 ? BLOCK_H1 : (level == 2 ? BLOCK_H2 : BLOCK_H3);
                    block.text = Plain_(Trim_(line.substr(level == std::string::npos ? line.size() : level)));
                    blocks.push_back(block);
                    open_paragraph = false;
                } else if (line[0] == '|') {
                    if (blocks.empty() || blocks.back().kind != BLOCK_TABLE || !open_paragraph) {
                        blocks.push_back({BLOCK_TABLE, "", {}});
                    }
                    if (!Is_Separator_Row_(line)) blocks.back().rows.push_back(Cells_(line));
                    open_paragraph = true; // the table continues on the next line
                } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
                    blocks.push_back({BLOCK_BULLET, Plain_(Trim_(line.substr(2))), {}});
                    open_paragraph = true; // a wrapped bullet continues on the next line
                } else if (open_paragraph && !blocks.empty() && (blocks.back().kind == BLOCK_PARAGRAPH || blocks.back().kind == BLOCK_BULLET)) {
                    blocks.back().text += " " + Plain_(line);
                } else {
                    blocks.push_back({BLOCK_PARAGRAPH, Plain_(line), {}});
                    open_paragraph = true;
                }
                if (end == text.size()) break;
            }
            return blocks;
        }

        bool Row_Matches_(const std::vector<std::string>& row, const std::string& filter) {
            for (const std::string& cell : row) {
                if (Lower_(cell).find(filter) != std::string::npos) return true;
            }
            return false;
        }

        void Draw_Table_(const s_block& block, const std::string& filter, int* drawn, int table_index) {
            if (block.rows.empty()) return;
            const int columns = static_cast<int>(block.rows.front().size());
            if (columns == 0) return;
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
            if (!ImGui::BeginTable(("md-table-" + std::to_string(table_index)).c_str(), columns, flags)) return;
            // Every column but the last as wide as its widest cell (up to a third of the space);
            // the last one takes the rest and wraps.
            const float limit = ImGui::GetContentRegionAvail().x / 3.0f;
            for (int column = 0; column < columns; ++column) {
                const char* header = block.rows.front()[static_cast<size_t>(column)].c_str();
                if (column == columns - 1) {
                    ImGui::TableSetupColumn(header, ImGuiTableColumnFlags_WidthStretch);
                    continue;
                }
                float width = 0.0f;
                for (const std::vector<std::string>& row : block.rows) {
                    if (static_cast<size_t>(column) < row.size()) width = std::max(width, ImGui::CalcTextSize(row[static_cast<size_t>(column)].c_str()).x);
                }
                ImGui::TableSetupColumn(header, ImGuiTableColumnFlags_WidthFixed, std::min(width, limit));
            }
            ImGui::TableHeadersRow();
            for (size_t row = 1; row < block.rows.size(); ++row) {
                if (!filter.empty() && !Row_Matches_(block.rows[row], filter)) continue;
                ImGui::TableNextRow();
                for (int column = 0; column < columns; ++column) {
                    ImGui::TableNextColumn();
                    if (static_cast<size_t>(column) < block.rows[row].size()) ImGui::TextWrapped("%s", block.rows[row][static_cast<size_t>(column)].c_str());
                }
                ++*drawn;
            }
            ImGui::EndTable();
        }

        void Draw_Heading_(const s_block& block) {
            if (block.kind != BLOCK_H1) ImGui::Spacing();
            ImGui::TextColored(block.kind == BLOCK_H3 ? COLOUR_DIM : COLOUR_ACCENT, "%s", block.text.c_str());
            if (block.kind == BLOCK_H2) ImGui::Separator();
        }
    } // namespace

    int Draw_Markdown(const std::string& text, const std::string& filter_text) {
        const std::string filter = Lower_(filter_text);
        const std::vector<s_block> blocks = Parse_(text);
        int drawn = 0;
        // With a filter: only tables with matching rows, each under the headings above it.
        const s_block* pending[3] = {nullptr, nullptr, nullptr};
        int table_index = 0;
        ImGui::PushTextWrapPos(0.0f);
        for (const s_block& block : blocks) {
            switch (block.kind) {
                case BLOCK_H1:
                case BLOCK_H2:
                case BLOCK_H3: {
                    const int level = block.kind == BLOCK_H1 ? 0 : (block.kind == BLOCK_H2 ? 1 : 2);
                    if (filter.empty()) {
                        Draw_Heading_(block);
                    } else {
                        pending[level] = &block;
                        for (int deeper = level + 1; deeper < 3; ++deeper) pending[deeper] = nullptr;
                    }
                    break;
                }
                case BLOCK_PARAGRAPH:
                    if (filter.empty()) ImGui::TextUnformatted(block.text.c_str());
                    break;
                case BLOCK_BULLET:
                    if (filter.empty()) {
                        ImGui::Bullet();
                        ImGui::TextWrapped("%s", block.text.c_str());
                    }
                    break;
                case BLOCK_TABLE: {
                    const int index = table_index++;
                    if (!filter.empty()) {
                        bool any = false;
                        for (size_t row = 1; row < block.rows.size() && !any; ++row) any = Row_Matches_(block.rows[row], filter);
                        if (!any) break;
                        for (int level = 1; level < 3; ++level) { // the page title (H1) isn't repeated
                            if (pending[level] != nullptr) Draw_Heading_(*pending[level]);
                            pending[level] = nullptr;
                        }
                    }
                    Draw_Table_(block, filter, &drawn, index);
                    break;
                }
            }
        }
        ImGui::PopTextWrapPos();
        return drawn;
    }

    const s_package_preview& c_launcher::Package_Preview_(const std::string& id) {
        const auto found = previews_.find(id);
        if (found != previews_.end()) return found->second;
        s_package_preview preview;
        const antares::s_config_check check = antares::Check_Package(id);
        preview.ok = check.ok;
        preview.error = check.error;
        if (check.ok) {
            preview.effective = check.effective;
            for (config::t_variable_id variable = 0; variable < config::variable_count; ++variable) {
                const config::s_value_provenance& from = check.effective->provenance[variable];
                if (!from.has_override || from.package_id.empty() || from.package_id == "base_game") continue;
                // Big packages assign whole tables; only the values that differ from the
                // default are changes.
                config::s_typed_value value;
                config::s_typed_value default_value;
                if (config::Read_State_Value(&check.effective->state, variable, &value) && config::generated::Get_Default_Value(variable, &default_value) &&
                    Setting_Value_Text(variable, value) == Setting_Value_Text(variable, default_value)) {
                    continue;
                }
                if (from.package_id == id) {
                    preview.own.push_back(variable);
                } else {
                    ++preview.from_dependencies[from.package_id];
                }
            }
        }
        return previews_.emplace(id, std::move(preview)).first->second;
    }

    void c_launcher::Draw_Package_Changes_(const antares::s_package_info& package) {
        const config::s_mod_manifest& manifest = package.manifest;
        if (package.hd_art) {
            ImGui::Bullet();
            ImGui::TextWrapped("HD art pack: with it enabled the game draws its screens in high resolution, at the window's size. "
                               "The presentation scaler has no effect then.");
        }
        if (manifest.has_lua_runtime) {
            ImGui::Bullet();
            ImGui::TextWrapped("Runs Lua scripts (main.lua). Scripts can change the galaxy, technologies or other game state "
                               "beyond the settings listed here. In multiplayer every player needs the same copy.");
        }
        const s_package_preview& preview = Package_Preview_(package.id);
        if (!preview.ok) {
            ImGui::TextColored(COLOUR_ERROR, "Can't be loaded on its own: %s", preview.error.c_str());
            return;
        }
        if (preview.own.empty()) {
            ImGui::Bullet();
            ImGui::TextWrapped("Changes no settings itself.");
        } else {
            ImGui::Bullet();
            ImGui::TextWrapped("Changes %zu setting%s from the original game's values. Expand a group to see them:", preview.own.size(), preview.own.size() == 1 ? "" : "s");
        }
        if (!preview.from_dependencies.empty()) {
            std::string text;
            for (const auto& [dependency, count] : preview.from_dependencies) {
                text += (text.empty() ? "" : ", ") + dependency + " (" + std::to_string(count) + ")";
            }
            ImGui::Bullet();
            ImGui::TextWrapped("Its dependencies change more (shown under each): %s.", text.c_str());
        }
        if (preview.own.empty()) return;

        // Grouped by the schema's documentation categories, in their order.
        std::map<std::string, std::vector<config::t_variable_id>> by_category;
        for (const config::t_variable_id variable : preview.own) by_category[setting_category_[variable]].push_back(variable);
        std::vector<std::pair<std::string, std::string>> groups; // category id, title
        for (const settings::s_category& category : docs_.Categories()) {
            if (by_category.count(category.id) != 0) groups.emplace_back(category.id, category.title);
        }
        if (by_category.count("") != 0) groups.emplace_back("", "Other");
        for (const auto& [category, title] : groups) {
            const std::vector<config::t_variable_id>& variables = by_category[category];
            const std::string label = title + " (" + std::to_string(variables.size()) + ")##" + category;
            if (!ImGui::TreeNode(label.c_str())) continue;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(variables.size()));
            while (clipper.Step()) {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    const config::t_variable_id variable = variables[static_cast<size_t>(index)];
                    config::s_typed_value value;
                    const std::string value_text = config::Read_State_Value(&preview.effective->state, variable, &value) ? Setting_Value_Text(variable, value) : "";
                    ImGui::TextUnformatted(setting_titles_[variable].c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(COLOUR_DIM, "= %s", value_text.c_str());
                    if (ImGui::BeginItemTooltip()) {
                        ImGui::TextUnformatted(antares::profile::Setting_Path(variable).c_str());
                        ImGui::EndTooltip();
                    }
                }
            }
            ImGui::TreePop();
        }
    }
} // namespace launcher
