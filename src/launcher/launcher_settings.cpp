#include "pch.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>

#include "antares/profile_file.h"
#include "launcher/launcher_app.h"
#include "launcher_schema.h"

// The launcher's Settings tab (WP5.2): every scalar setting of the config schema, with the value
// the selected profile would load and where it comes from. Edits are written as setting lines in
// the profile and checked with the game's own dry run; an edit the game would refuse is undone.
namespace launcher {
    namespace {
        const ImVec4 COLOUR_OK(0.45f, 0.85f, 0.45f, 1.0f);
        const ImVec4 COLOUR_ERROR(1.0f, 0.45f, 0.40f, 1.0f);
        const ImVec4 COLOUR_WARN(1.0f, 0.75f, 0.35f, 1.0f);
        const ImVec4 COLOUR_DIM(0.60f, 0.62f, 0.68f, 1.0f);
        const ImVec4 COLOUR_ACCENT(0.55f, 0.75f, 1.0f, 1.0f);

        struct s_common_section {
            const char* title;
            std::vector<const char*> names;
        };

        // The short page players see first.
        const std::vector<s_common_section>& Common_Sections_() {
            static const std::vector<s_common_section> sections = {
                {"Display",
                    {"interface.fullscreen", "interface.window_width", "interface.window_height", "interface.scaler", "interface.vsync_enabled"}},
                {"Interface",
                    {"interface.show_exit_message", "interface.main_menu_mod_list", "interface.screen_fade_speed", "interface.scroll_interval", "combat.bombardment_hit_frames", "interface.center_on_next_ship", "interface.no_outpost_prompt", "interface.warn_blockers", "interface.main_map_overview_markers", "interface.main_screen_tax_rate", "interface.maximum_galaxy_zoom_mode", "interface.history_graph_smoothing", "interface.sort_serious_turn_summary_first", "interface.confirm_battle_message_timeout"}},
                {"Game rules",
                    {"game.starting_ships", "combat.max_turns", "combat.human_fleet_auto_retreat", "combat.interactive_combat_stats", "leaders.leaders_no_limit", "research.fixed_cost", "gameplay.allow_native_annihilation"}},
                {"Multiplayer", {"interface.router"}},
                {"For modders", {"render.art_hot_reload", "interface.debugger_enabled"}},
            };
            return sections;
        }

        std::string Lower_(std::string text) {
            for (char& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return text;
        }

        void Primitive_Range_(config::e_value_type type, int64_t* minimum, int64_t* maximum) {
            switch (type) {
                case config::CONFIG_VALUE_INT8:
                    *minimum = INT8_MIN;
                    *maximum = INT8_MAX;
                    break;
                case config::CONFIG_VALUE_UINT8:
                    *minimum = 0;
                    *maximum = UINT8_MAX;
                    break;
                case config::CONFIG_VALUE_INT16:
                    *minimum = INT16_MIN;
                    *maximum = INT16_MAX;
                    break;
                case config::CONFIG_VALUE_UINT16:
                    *minimum = 0;
                    *maximum = UINT16_MAX;
                    break;
                case config::CONFIG_VALUE_INT32:
                    *minimum = INT32_MIN;
                    *maximum = INT32_MAX;
                    break;
                case config::CONFIG_VALUE_UINT32:
                    *minimum = 0;
                    *maximum = UINT32_MAX;
                    break;
                default:
                    *minimum = INT64_MIN;
                    *maximum = INT64_MAX;
                    break;
            }
        }

        std::string Value_Text_(config::t_variable_id id, const config::s_typed_value& value) {
            std::string text;
            if (antares::profile::Format_Setting_Value(id, value, &text)) return text;
            if (value.type == config::CONFIG_VALUE_BITMAP) return "(a set of flags)";
            return std::to_string(value.integer_value);
        }

        // A string setting's text without the CFG quotes, for editing.
        std::string Unquoted_(const std::string& quoted) {
            if (quoted.size() < 2 || quoted.front() != '"') return quoted;
            std::string text;
            for (size_t index = 1; index + 1 < quoted.size(); ++index) {
                if (quoted[index] == '\\' && index + 2 < quoted.size()) ++index;
                text += quoted[index];
            }
            return text;
        }
    } // namespace

    void c_launcher::Init_Settings_() {
        const std::string schema(reinterpret_cast<const char*>(k_launcher_schema_data), k_launcher_schema_size);
        std::string error;
        if (!docs_.Load(schema, &error)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: %s", error.c_str());
        }
        setting_titles_.resize(config::variable_count);
        setting_search_.resize(config::variable_count);
        setting_category_.resize(config::variable_count);
        for (config::t_variable_id id = 0; id < config::variable_count; ++id) {
            std::string name;
            config::Get_Variable_Name(id, &name);
            const settings::s_setting_doc doc = docs_.Describe(name);
            setting_titles_[id] = doc.title;
            setting_search_[id] = Lower_(doc.title + " " + antares::profile::Setting_Path(id) + " " + doc.description);
            setting_category_[id] = docs_.Category_Of(name.substr(0, name.find('.')));
        }
        for (const s_common_section& section : Common_Sections_()) {
            common_settings_.push_back(config::CONFIG_NO_VARIABLE);
            common_sections_.push_back(section.title);
            for (const char* name : section.names) {
                config::t_variable_id id = config::CONFIG_NO_VARIABLE;
                if (config::Find_Variable(name, SDL_strlen(name), &id)) {
                    common_settings_.push_back(id);
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "launcher: no setting '%s'", name);
                }
            }
        }
    }

    void c_launcher::Open_Settings(const std::string& page_or_search) {
        tab_ = 1;
        bool is_page = page_or_search == "common" || page_or_search == "profile" || page_or_search == "other";
        for (const settings::s_category& category : docs_.Categories()) is_page = is_page || category.id == page_or_search;
        if (is_page) {
            settings_page_ = page_or_search;
        } else {
            SDL_strlcpy(settings_search_, page_or_search.c_str(), sizeof(settings_search_));
        }
    }

    bool c_launcher::Profile_Sets_(config::t_variable_id id) const {
        return std::find(profile_settings_.begin(), profile_settings_.end(), id) != profile_settings_.end();
    }

    std::vector<config::t_variable_id> c_launcher::Settings_Rows_() const {
        std::vector<config::t_variable_id> rows;
        const std::string search = Lower_(settings_search_);
        if (!search.empty()) {
            // Ranked: a title that starts with the search, then other title matches, then path or
            // description matches; within each, plain settings before entries of tables.
            std::vector<std::pair<int, config::t_variable_id>> ranked;
            for (config::t_variable_id id = 0; id < config::variable_count; ++id) {
                const size_t found = setting_search_[id].find(search);
                if (found == std::string::npos) continue;
                const bool nested = setting_titles_[id].find("›") != std::string::npos;
                const int tier = found == 0 ? 0 : (found < setting_titles_[id].size() ? 1 : 2);
                ranked.emplace_back(tier * 2 + (nested ? 1 : 0), id);
            }
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
            for (const auto& [rank, id] : ranked) rows.push_back(id);
            return rows;
        }
        if (settings_page_ == "common") return common_settings_;
        if (settings_page_ == "profile") return profile_settings_;
        for (config::t_variable_id id = 0; id < config::variable_count; ++id) {
            if (setting_category_[id] == (settings_page_ == "other" ? std::string() : settings_page_)) rows.push_back(id);
        }
        return rows;
    }

    void c_launcher::Set_Setting_(config::t_variable_id id, const std::string& value_text) {
        const std::string before = profile_text_;
        const bool checked_before = check_.ok;
        if (!Write_Profile_Text_(antares::profile::With_Setting(profile_text_, id, value_text))) {
            return;
        }
        Run_Check_();
        const std::string path = antares::profile::Setting_Path(id);
        if (checked_before && !check_.ok) {
            const std::string error = check_.error;
            Write_Profile_Text_(before);
            Run_Check_();
            message_ = "Not saved: " + error;
            message_is_error_ = true;
            return;
        }
        message_ = value_text.empty() ? "Removed " + path + " from the profile." : "Saved " + path + " = " + value_text + ";";
        message_is_error_ = false;
    }

    void c_launcher::Draw_Settings_(float height) {
        const float pages_width = ImGui::GetFontSize() * 15.0f;
        ImGui::BeginChild("pages", ImVec2(pages_width, height), ImGuiChildFlags_Borders);
        Draw_Settings_Pages_();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("settings", ImVec2(0.0f, height));
        if (!check_.ok) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(COLOUR_WARN,
                "The game would refuse this profile (see below), so the values it would load aren't known. "
                "Fix the packages first. The profile's own setting lines are listed here; Reset removes one.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            for (const config::t_variable_id id : profile_settings_) {
                ImGui::PushID(static_cast<int>(id));
                if (ImGui::SmallButton("Reset")) {
                    Set_Setting_(id, "");
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(antares::profile::Setting_Path(id).c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
            return;
        }
        const float details_height = ImGui::GetTextLineHeightWithSpacing() * 5.0f;
        const std::vector<config::t_variable_id> rows = Settings_Rows_();
        Draw_Settings_Table_(rows, std::max(80.0f, ImGui::GetContentRegionAvail().y - details_height));
        ImGui::BeginChild("setting-details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        Draw_Setting_Details_();
        ImGui::EndChild();
        ImGui::EndChild();
    }

    void c_launcher::Draw_Settings_Pages_() {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", "Search all settings", settings_search_, sizeof(settings_search_));
        ImGui::Spacing();
        const bool searching = settings_search_[0] != '\0';
        const auto page = [&](const char* id, const std::string& label) {
            if (ImGui::Selectable(label.c_str(), !searching && settings_page_ == id)) {
                settings_page_ = id;
                settings_search_[0] = '\0';
            }
        };
        page("common", "Common");
        page("profile", "Set in this profile (" + std::to_string(profile_settings_.size()) + ")");
        ImGui::Separator();
        for (const settings::s_category& category : docs_.Categories()) {
            page(category.id.c_str(), category.title);
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(category.description.c_str());
                ImGui::EndTooltip();
            }
        }
        page("other", "Other");
    }

    void c_launcher::Draw_Settings_Table_(const std::vector<config::t_variable_id>& rows, float height) {
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("settings-table", 4, flags, ImVec2(0.0f, height))) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch, 0.15f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2.0f);
        ImGui::TableHeadersRow();
        if (rows.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(COLOUR_DIM, settings_page_ == "profile" ? "This profile sets nothing itself." : "No settings match.");
        }

        const bool on_common = settings_search_[0] == '\0' && settings_page_ == "common";
        size_t section = 0;
        std::vector<size_t> section_of(rows.size(), 0);
        for (size_t index = 0; index < rows.size(); ++index) {
            if (on_common && rows[index] == config::CONFIG_NO_VARIABLE) section_of[index] = section++;
        }

        std::string pending_text;
        config::t_variable_id pending_id = config::CONFIG_NO_VARIABLE;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const config::t_variable_id id = rows[static_cast<size_t>(row)];
                ImGui::TableNextRow();
                if (id == config::CONFIG_NO_VARIABLE) {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(COLOUR_ACCENT, "%s", common_sections_[section_of[static_cast<size_t>(row)]].c_str());
                    continue;
                }
                ImGui::PushID(static_cast<int>(id));
                config::s_typed_value value;
                const bool have_value = config::Read_State_Value(&check_.effective->state, id, &value);
                const config::s_value_provenance& from = check_.effective->provenance[id];
                const bool profile_sets = Profile_Sets_(id);

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (ImGui::Selectable(setting_titles_[id].c_str(), selected_setting_ == id, ImGuiSelectableFlags_AllowOverlap)) {
                    selected_setting_ = id;
                }
                if (ImGui::IsItemHovered() && !ImGui::IsAnyItemActive()) selected_setting_ = id;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                std::string text;
                if (have_value && antares::profile::Is_Editable_Setting(id)) {
                    if (Draw_Setting_Editor_(id, value, &text)) {
                        pending_id = id;
                        pending_text = text;
                    }
                } else {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(COLOUR_DIM, "%s", have_value ? "a list: edit in the file" : "unavailable");
                }
                if (ImGui::IsItemHovered() && !ImGui::IsAnyItemActive()) selected_setting_ = id;

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const bool from_package = from.has_override && !from.package_id.empty() && from.package_id != "base_game";
                if (from_package) {
                    ImGui::TextColored(profile_sets ? COLOUR_WARN : COLOUR_DIM, "%s", from.package_id.c_str());
                } else if (from.has_override && from.location.source == profiles_[profile_index_].vfs_path) {
                    ImGui::TextColored(COLOUR_ACCENT, "this profile");
                } else if (from.has_override) {
                    ImGui::TextColored(COLOUR_DIM, "%s", from.location.source.c_str());
                } else {
                    ImGui::TextColored(COLOUR_DIM, "default");
                }

                ImGui::TableNextColumn();
                if (profile_sets && ImGui::SmallButton("Reset")) {
                    pending_id = id;
                    pending_text.clear();
                }
                if (profile_sets && ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted("Remove this line from the profile.");
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        if (pending_id != config::CONFIG_NO_VARIABLE) {
            Set_Setting_(pending_id, pending_text);
        }
    }

    bool c_launcher::Draw_Setting_Editor_(config::t_variable_id id, const config::s_typed_value& value, std::string* value_text) {
        const config::s_type_descriptor* type = config::Get_Variable_Type(id);
        const std::string current = Value_Text_(id, value);
        switch (value.type) {
            case config::CONFIG_VALUE_BOOL: {
                bool checked = value.bool_value;
                if (ImGui::Checkbox("##value", &checked)) {
                    *value_text = checked ? "true" : "false";
                    return true;
                }
                return false;
            }
            case config::CONFIG_VALUE_STRING:
            case config::CONFIG_VALUE_RATIO: {
                const bool is_string = value.type == config::CONFIG_VALUE_STRING;
                char buffer[256];
                SDL_strlcpy(buffer, (is_string ? Unquoted_(current) : current).c_str(), sizeof(buffer));
                ImGui::InputText("##value", buffer, sizeof(buffer));
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    *value_text = is_string ? antares::profile::Quote_Cfg_String(buffer) : std::string(buffer);
                    return *value_text != current;
                }
                return false;
            }
            case config::CONFIG_VALUE_BITMAP:
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(COLOUR_DIM, "%s", current.c_str());
                return false;
            default: break;
        }
        if (type->scalar_kind == config::CONFIG_SCALAR_ENUM) {
            bool chosen = false;
            if (ImGui::BeginCombo("##value", current.c_str())) {
                for (size_t index = 0; index < type->value_count; ++index) {
                    const bool selected = type->values[index].value == value.integer_value;
                    if (ImGui::Selectable(type->values[index].name, selected) && !selected) {
                        *value_text = type->values[index].name;
                        chosen = true;
                    }
                }
                ImGui::EndCombo();
            }
            return chosen;
        }
        if (type->scalar_kind == config::CONFIG_SCALAR_MASK) {
            bool changed = false;
            if (ImGui::BeginCombo("##value", current.c_str())) {
                for (size_t index = 0; index < type->value_count; ++index) {
                    if (ImGui::Selectable(type->values[index].name, type->values[index].value == value.integer_value)) {
                        *value_text = type->values[index].name;
                        changed = true;
                    }
                }
                if (type->value_count > 0) ImGui::Separator();
                for (size_t index = 0; index < type->flag_count; ++index) {
                    const uint64_t bits = static_cast<uint64_t>(type->flags[index].value);
                    bool on = bits != 0 && (static_cast<uint64_t>(value.integer_value) & bits) == bits;
                    if (ImGui::Checkbox(type->flags[index].name, &on)) {
                        config::s_typed_value next = value;
                        next.integer_value = static_cast<int64_t>(on ? (static_cast<uint64_t>(value.integer_value) | bits)
                                                                     : (static_cast<uint64_t>(value.integer_value) & ~bits));
                        changed = antares::profile::Format_Setting_Value(id, next, value_text);
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
        int64_t minimum = 0;
        int64_t maximum = 0;
        Primitive_Range_(type->value_type, &minimum, &maximum);
        if (type->has_min) minimum = type->min;
        if (type->has_max) maximum = type->max;
        int64_t edited = value.integer_value;
        if (maximum - minimum <= 100) {
            ImGui::SliderScalar("##value", ImGuiDataType_S64, &edited, &minimum, &maximum, nullptr, ImGuiSliderFlags_AlwaysClamp);
        } else {
            ImGui::InputScalar("##value", ImGuiDataType_S64, &edited);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            edited = std::clamp(edited, minimum, maximum);
            if (edited != value.integer_value) {
                *value_text = std::to_string(edited);
                return true;
            }
        }
        return false;
    }

    void c_launcher::Draw_Setting_Details_() {
        const config::t_variable_id id = selected_setting_;
        if (id == config::CONFIG_NO_VARIABLE || id >= config::variable_count) {
            ImGui::TextColored(COLOUR_DIM, "Point at a setting to read about it.");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(COLOUR_DIM,
                "Changes are written to the profile as setting lines and checked with the game's own "
                "rules. Reset removes the profile's line, so the value comes from the base config, a "
                "package or the default again.");
            ImGui::PopTextWrapPos();
            return;
        }
        std::string name;
        config::Get_Variable_Name(id, &name);
        const settings::s_setting_doc doc = docs_.Describe(name);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(doc.title.c_str());
        ImGui::SameLine();
        ImGui::TextColored(COLOUR_DIM, "%s", antares::profile::Setting_Path(id).c_str());
        if (!doc.description.empty()) ImGui::TextUnformatted(doc.description.c_str());
        if (!doc.remarks.empty()) ImGui::TextColored(COLOUR_DIM, "%s", doc.remarks.c_str());

        std::string line;
        config::s_typed_value default_value;
        if (config::generated::Get_Default_Value(id, &default_value)) {
            line = "Default " + Value_Text_(id, default_value) + ".";
        }
        const config::s_config_group_descriptor* group = config::Get_Config_Group(config::Get_Variable_Group(id));
        if (group != nullptr && group->synchronize) line += " In multiplayer the host's value is used.";
        if (group != nullptr && group->save) line += " Saved with each game: a loaded save keeps its own value.";
        const config::s_value_provenance& from = check_.effective->provenance[id];
        if (from.has_override) {
            line += " Set by " + (from.package_id.empty() || from.package_id == "base_game" ? from.location.source : "package " + from.package_id) +
                (from.location.line > 0 && !from.location.source.empty() ? " (" + from.location.source + ":" + std::to_string(from.location.line) + ")" : "") + ".";
        }
        if (Profile_Sets_(id) && from.has_override && !from.package_id.empty() && from.package_id != "base_game") {
            ImGui::TextColored(COLOUR_WARN, "The profile sets this too, but package %s wins: the game applies packages after the profile.", from.package_id.c_str());
        }
        ImGui::TextColored(COLOUR_DIM, "%s", line.c_str());
        ImGui::PopTextWrapPos();
    }
} // namespace launcher
