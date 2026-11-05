#include "pch.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>

#include "antares/packages.h"
#include "launcher/launcher_app.h"

// The Saves tab (WP5.3): what each save needs and whether it loads with the selected profile.
// A save brings its own rule packages (the ones it recorded, D18); the profile adds its art
// packages and its display settings. The check is the game's own staging of that set.
namespace launcher {
    namespace {
        const ImVec4 COLOUR_OK(0.45f, 0.85f, 0.45f, 1.0f);
        const ImVec4 COLOUR_ERROR(1.0f, 0.45f, 0.40f, 1.0f);
        const ImVec4 COLOUR_WARN(1.0f, 0.75f, 0.35f, 1.0f);
        const ImVec4 COLOUR_DIM(0.60f, 0.62f, 0.68f, 1.0f);

        std::string Date_(int64_t time) {
            SDL_DateTime date{};
            if (time == 0 || !SDL_TimeToDateTime(time, &date, true)) return "";
            char text[32];
            SDL_snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", date.year, date.month, date.day, date.hour, date.minute);
            return text;
        }

        const char* Game_Type_(int game_type) {
            switch (game_type) {
                case 0: return "single player";
                case 1: return "hot seat";
                default: return "network game";
            }
        }

        std::string Join_(const std::vector<std::string>& items) {
            std::string text;
            for (const std::string& item : items) {
                if (!text.empty()) text += ", ";
                text += item;
            }
            return text;
        }
    } // namespace

    void c_launcher::Open_Saves(int slot) {
        tab_ = 2;
        force_tab_ = true;
        selected_save_ = std::clamp(slot, 0, antares::saves::k_slot_count);
    }

    void c_launcher::Refresh_Saves_() {
        const auto start = std::chrono::steady_clock::now();
        const s_profile_entry& entry = profiles_[profile_index_];
        saves_.clear();
        for (int slot = 1; slot <= antares::saves::k_slot_count; ++slot) {
            s_save_row row;
            row.summary = antares::saves::Load_Save_Summary(slot);
            if (row.summary.readable && !antares::saves::Is_Network_Game(row.summary)) {
                row.check = antares::Check_Save(entry.vfs_path, row.summary.records_packages ? &row.summary.packages : nullptr, row.summary.state.get());
            }
            saves_.push_back(std::move(row));
        }
        saves_dirty_ = false;
        saves_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    const s_save_row* c_launcher::Find_Save_(int slot) const {
        if (slot < 1 || slot > static_cast<int>(saves_.size())) return nullptr;
        return &saves_[static_cast<size_t>(slot - 1)];
    }

    std::string c_launcher::Save_Problem_(const s_save_row& row) const {
        const antares::saves::s_save_summary& summary = row.summary;
        if (!summary.exists) return "there is no SAVE" + std::to_string(summary.slot) + ".GAM";
        if (!summary.readable) return summary.error;
        if (antares::saves::Is_Network_Game(summary)) return "a network game: load it from the game's Multiplayer menu";
        if (!row.check.ok) return row.check.error;
        return "";
    }

    std::string c_launcher::Save_Label_(int slot) const {
        const s_save_row* row = Find_Save_(slot);
        if (row == nullptr || !row->summary.exists) return "";
        std::string label = std::to_string(slot) + "  ";
        if (row->summary.readable) {
            label += (row->summary.description.empty() ? std::string("(no name)") : row->summary.description) + "  ·  " +
                antares::saves::Format_Stardate(row->summary.stardate);
        } else {
            label += "(unreadable)";
        }
        const std::string date = Date_(row->summary.modified);
        if (!date.empty()) label += "  ·  " + date;
        return label;
    }

    void c_launcher::Draw_Continue_Combo_() {
        if (saves_dirty_) Refresh_Saves_();
        std::string current = "Main menu";
        if (load_save_ > 0) {
            current = Save_Label_(load_save_);
            if (current.empty()) current = std::to_string(load_save_) + "  (missing)";
        }
        if (ImGui::BeginCombo("##continue", current.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("Main menu", load_save_ == 0)) {
                load_save_ = 0;
                Save_Settings_();
            }
            for (int slot = 1; slot <= antares::saves::k_slot_count; ++slot) {
                const s_save_row* row = Find_Save_(slot);
                if (row == nullptr || !row->summary.exists) continue;
                const bool loads = Save_Problem_(*row).empty();
                std::string label = Save_Label_(slot);
                if (!loads) label += "  (won't load)";
                if (!loads) ImGui::PushStyleColor(ImGuiCol_Text, COLOUR_DIM);
                if (ImGui::Selectable(label.c_str(), load_save_ == slot)) {
                    load_save_ = slot;
                    Save_Settings_();
                }
                if (!loads) ImGui::PopStyleColor();
            }
            ImGui::EndCombo();
        }
    }

    // The table's one-line reason: the first package that is missing or a different version, else
    // the game's message.
    std::string c_launcher::Short_Problem_(const s_save_row& row, const std::string& problem) const {
        if (!row.summary.readable || row.check.ok) return problem;
        for (const config::s_mod_requirement& required : row.summary.packages) {
            if (antares::Is_Retired_Art_Package(required.id)) continue;
            const antares::s_package_info* package = Find_Package_(required.id);
            if (package == nullptr) return "needs " + required.id + " (not installed)";
            if (!package->valid) return "needs " + required.id + " (installed copy is invalid)";
            if (!required.version.empty() && package->manifest.version_text != required.version) {
                return "needs " + required.id + " " + required.version + " (" + package->manifest.version_text + " installed)";
            }
        }
        return problem;
    }

    void c_launcher::Draw_Saves_(float height) {
        if (saves_dirty_) Refresh_Saves_();
        const float row_height = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
        const float table_height = row_height * (antares::saves::k_slot_count + 1) + ImGui::GetStyle().CellPadding.y * 2.0f;
        const float details_height = std::max(ImGui::GetTextLineHeightWithSpacing() * 6.0f, height - table_height - ImGui::GetStyle().ItemSpacing.y);
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("saves", 6, flags, ImVec2(0.0f, height - details_height - ImGui::GetStyle().ItemSpacing.y))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("Stardate", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
            ImGui::TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
            ImGui::TableSetupColumn("Packages", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("With this profile", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (const s_save_row& row : saves_) {
                const antares::saves::s_save_summary& summary = row.summary;
                ImGui::PushID(summary.slot);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string slot = summary.slot == antares::saves::k_slot_count ? "10 (auto)" : std::to_string(summary.slot);
                if (ImGui::Selectable(slot.c_str(), selected_save_ == summary.slot, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_save_ = summary.slot;
                }
                if (summary.exists && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                    Save_Problem_(row).empty() && check_.ok && game_ == nullptr) {
                    Launch_(summary.slot);
                }
                ImGui::TableNextColumn();
                if (!summary.exists) {
                    ImGui::TextColored(COLOUR_DIM, "empty");
                    ImGui::PopID();
                    continue;
                }
                if (summary.readable) {
                    ImGui::TextUnformatted(summary.description.empty() ? "(no name)" : summary.description.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(antares::saves::Format_Stardate(summary.stardate).c_str());
                } else {
                    ImGui::TextColored(COLOUR_ERROR, "unreadable");
                    ImGui::TableNextColumn();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Date_(summary.modified).c_str());
                ImGui::TableNextColumn();
                if (summary.readable) {
                    if (!summary.records_packages) {
                        ImGui::TextColored(COLOUR_DIM, "the profile's");
                    } else if (summary.packages.empty()) {
                        ImGui::TextColored(COLOUR_DIM, "none");
                    } else {
                        std::vector<std::string> ids;
                        for (const config::s_mod_requirement& required : summary.packages) {
                            if (!antares::Is_Retired_Art_Package(required.id)) ids.push_back(required.id);
                        }
                        if (ids.empty()) {
                            ImGui::TextColored(COLOUR_DIM, "none");
                        } else {
                            ImGui::TextUnformatted(Join_(ids).c_str());
                        }
                    }
                }
                ImGui::TableNextColumn();
                const std::string problem = Save_Problem_(row);
                if (problem.empty()) {
                    ImGui::TextColored(COLOUR_OK, "loads");
                } else {
                    ImGui::TextColored(antares::saves::Is_Network_Game(summary) ? COLOUR_DIM : COLOUR_ERROR, "%s", Short_Problem_(row, problem).c_str());
                    ImGui::SetItemTooltip("%s", problem.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::BeginChild("save details", ImVec2(0.0f, details_height), ImGuiChildFlags_Borders);
        const s_save_row* row = Find_Save_(selected_save_);
        if (row == nullptr || !row->summary.exists) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(COLOUR_DIM,
                "Select a save to see the packages it needs. A save records every package that changes "
                "the rules, and loading it brings exactly those back, whatever this profile enables; "
                "the profile adds its art packages and display settings. Double-click a save to "
                "continue it. Slot 10 is the autosave.");
            ImGui::TextColored(COLOUR_DIM, "Read and checked %d saves in %.0f ms.", static_cast<int>(saves_.size()), saves_ms_);
            ImGui::PopTextWrapPos();
        } else {
            Draw_Save_Details_(*row);
        }
        ImGui::EndChild();
    }

    void c_launcher::Draw_Save_Needs_(const std::vector<config::s_mod_requirement>& needs, bool loads) {
        ImGui::PushTextWrapPos(-1.0f); // no wrapping inside the table's narrow columns
        if (ImGui::BeginTable("needs", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Needs");
            ImGui::TableSetupColumn("Saved with");
            ImGui::TableSetupColumn("Installed");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const config::s_mod_requirement& required : needs) {
                const antares::s_package_info* package = Find_Package_(required.id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(required.id.c_str());
                if (package != nullptr && package->valid && package->manifest.name != required.id) {
                    ImGui::SameLine();
                    ImGui::TextColored(COLOUR_DIM, "%s", package->manifest.name.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(required.version.empty() ? "any" : required.version.c_str());
                ImGui::TableNextColumn();
                if (package == nullptr) {
                    ImGui::TextColored(COLOUR_ERROR, "not installed");
                } else if (!package->valid) {
                    ImGui::TextColored(COLOUR_ERROR, "invalid");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(COLOUR_ERROR, "%s", package->error.c_str());
                    continue;
                } else {
                    ImGui::TextUnformatted(package->manifest.version_text.c_str());
                }
                ImGui::TableNextColumn();
                if (package == nullptr) {
                    ImGui::TextColored(COLOUR_ERROR, "install %s %s to load this save", required.id.c_str(), required.version.c_str());
                } else if (!required.version.empty() && package->manifest.version_text != required.version) {
                    if (loads) {
                        ImGui::TextColored(COLOUR_WARN, "a different version, which declares itself compatible");
                    } else {
                        ImGui::TextColored(COLOUR_ERROR, "a different version");
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopTextWrapPos();
    }

    void c_launcher::Draw_Save_Details_(const s_save_row& row) {
        const antares::saves::s_save_summary& summary = row.summary;
        ImGui::PushTextWrapPos(0.0f);
        const std::string problem = Save_Problem_(row);
        const float button_width = ImGui::GetFontSize() * 9.0f;
        if (summary.readable) {
            ImGui::TextUnformatted(summary.description.empty() ? "(no name)" : summary.description.c_str());
            ImGui::SameLine();
            ImGui::TextColored(COLOUR_DIM, "slot %d  ·  stardate %s  ·  saved %s  ·  %s  ·  save format %u", summary.slot,
                antares::saves::Format_Stardate(summary.stardate).c_str(), Date_(summary.modified).c_str(), Game_Type_(summary.game_type),
                static_cast<unsigned>(summary.version));
        } else {
            ImGui::Text("SAVE%d.GAM", summary.slot);
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - button_width);
        ImGui::BeginDisabled(!problem.empty() || !check_.ok || game_ != nullptr);
        if (ImGui::Button("Continue this save", ImVec2(button_width, 0.0f))) {
            Launch_(summary.slot);
        }
        ImGui::EndDisabled();
        if (problem.empty() && !check_.ok) ImGui::SetItemTooltip("Fix the profile first: the game would refuse it.");

        if (!summary.readable || antares::saves::Is_Network_Game(summary)) {
            ImGui::TextColored(antares::saves::Is_Network_Game(summary) ? COLOUR_DIM : COLOUR_ERROR, "This save is %s.", problem.c_str());
            ImGui::PopTextWrapPos();
            return;
        }
        if (problem.empty()) {
            ImGui::TextColored(COLOUR_OK, "Loads with this profile.");
        } else {
            ImGui::TextColored(COLOUR_ERROR, "The game would refuse it:");
            ImGui::SameLine();
            ImGui::TextUnformatted(problem.c_str());
        }
        ImGui::Spacing();

        std::vector<std::string> recorded;
        std::vector<config::s_mod_requirement> needs;
        for (const config::s_mod_requirement& required : summary.packages) {
            // Saves from before D18 list the old art pack; the game skips it, and so does this.
            if (antares::Is_Retired_Art_Package(required.id)) {
                recorded.push_back(required.id);
            } else {
                needs.push_back(required);
            }
        }
        if (!summary.records_packages) {
            ImGui::TextColored(COLOUR_DIM, "This save doesn't list its packages (an older save format): it loads with this profile's packages.");
        } else if (needs.empty()) {
            ImGui::TextColored(COLOUR_DIM, "It needs no packages: the original rules.");
        } else {
            Draw_Save_Needs_(needs, problem.empty());
            for (const config::s_mod_requirement& required : needs) recorded.push_back(required.id);
        }
        if (row.check.ok) {
            std::vector<std::string> added;
            for (const std::string& id : row.check.package_order) {
                if (std::find(recorded.begin(), recorded.end(), id) == recorded.end()) added.push_back(id);
            }
            if (summary.records_packages && !added.empty()) {
                ImGui::TextColored(COLOUR_DIM, "Plus this profile's art packages: %s", Join_(added).c_str());
            }
        }
        ImGui::PopTextWrapPos();
    }
} // namespace launcher
