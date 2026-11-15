#include "pch.h"

#include "antares/commands_internal.h"

#include <algorithm>
#include <cstring>
#include <vector>

// WP3.7: the Info panel (notes/antares/screens/09-info.md). The queries read what the Info
// screen's tabs show (INFO::Info_Screen and its subscreens); the two commands are the screen's
// only writes, its tab and history-graph choices in the saved `history_btns`.

namespace antares::commands::internal {

    namespace {

        constexpr int32_t k_race_portraits = 32; // RACES.LBX: a race's ruler, 32 + race
        constexpr int32_t k_race_select_portraits = 15; // RACESEL.LBX: the race select screen's, 15 + race
        constexpr int16_t k_history_length = 350;

        std::string Billtext(const char* lbx, int16_t entry) {
            char text[120] = {};
            JIM::Get_Text_Message(lbx, entry, text, sizeof(text));
            return Utf8(text, sizeof(text));
        }

        // The players the graph and the statistics show (BILL::Get_Players_Dead_Or_Alive_Or_
        // Omniscient after the player in slot 0): the living contacted ones, with
        // `previously_contacted` also the living ones met before, then the dead, then (for an
        // omniscient player) everyone. Read here without the screen's shared active_player list.
        std::vector<int16_t> Shown_Players(bool previously_contacted) {
            const int16_t me = MOX::PLAYER_NUM;
            const s_player& player = MOX::player[me];
            std::vector<int16_t> shown{me};
            for (int16_t i = 0; i < MOX::NUM_PLAYERS; ++i) {
                if (i != me && MOX::player[i].eliminated == 0 && player.contact[i] != 0) {
                    shown.push_back(i);
                }
            }
            if (previously_contacted) {
                for (int16_t i = 0; i < MOX::NUM_PLAYERS; ++i) {
                    if (i != me && MOX::player[i].eliminated == 0 && player.contact[i] == 0 &&
                        player.n_times_established_contact[i] != 0) {
                        shown.push_back(i);
                    }
                }
            }
            const bool omniscient = HAROLD::Player_Is_Omniscient(me);
            for (int16_t i = 0; i < MOX::NUM_PLAYERS; ++i) {
                if (i != me && (omniscient || MOX::player[i].eliminated != 0) &&
                    std::find(shown.begin(), shown.end(), i) == shown.end()) {
                    shown.push_back(i);
                }
            }
            return shown;
        }

        // info: the parts every tab shares. The stardate, the tab and graphs the player last
        // chose (history_btns), the tab names (billtext 5-9), the maintenance chart
        // (Initialize_Maintenance_Values, Build_Maint_Chart_Bitmaps: income and the six costs,
        // each a percent of the largest) and the net income line, and the ruler's portrait (the
        // picture when nothing else has one).
        void Query_Info(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const s_player& player = MOX::player[MOX::PLAYER_NUM];
            const int32_t stardate = MOX::stardate;
            char date[24];
            snprintf(date, sizeof(date), MOX::settings.language == 0 ? "%i.%i" : "%i,%i", (int)(stardate / 10), (int)(stardate % 10));
            out.Str("stardate", date);
            out.Int("tab", (player.history_btns & 0x70) >> 4);
            s_out graphs = out.Add_List("graphs");
            for (int i = 0; i < 4; ++i) {
                graphs.Append_Int((player.history_btns >> i) & 1);
            }
            s_out tabs = out.Add_List("tabs");
            for (int16_t i = 5; i <= 9; ++i) {
                tabs.Append_Object().Str("name", Billtext("billtext.lbx", i));
            }
            out.Str("income_label", Billtext("billtext.lbx", 17));
            out.Str("maintenance_label", Billtext("billtext.lbx", 18));
            int32_t values[7];
            values[0] = player.bc_produced;
            for (int i = 0; i < 6; ++i) {
                values[i + 1] = player.maintenance[i];
            }
            int32_t largest = 0;
            for (int32_t v : values) {
                largest = std::max(largest, v);
            }
            s_out bars = out.Add_List("bars");
            for (int i = 0; i < 7; ++i) {
                s_out bar = bars.Append_Object();
                bar.Int("value", values[i]);
                bar.Int("percent", largest > 0 ? ((values[i] * 1000) / largest + 5) / 10 : 0);
                bar.Str("label", Billtext("billtext.lbx", (int16_t)(19 + i)));
            }
            const int32_t net = (int32_t)player.bc_produced - player.total_maintenance;
            char net_text[64] = {};
            JIM::Get_Text_Message("billtext.lbx", 27, net_text, sizeof(net_text));
            JIM::Decode_Text_Stat(net_text, net, sizeof(net_text));
            out.Int("net_income", net);
            out.Str("net_income_text", Utf8(net_text, sizeof(net_text)));
            // The race select screen's portrait (RACESEL.LBX 15 + race, 290x322: owner, 2026-11-15,
            // RACES.LBX's 76x88 ruler came out muddy at this size), its HD art at most 2x (580x644,
            // small enough to get mipmaps for a clean shrink).
            s_out portrait = out.Add_Object("portrait");
            portrait.Str("lbx", "RACESEL.LBX:2");
            portrait.Int("entry", k_race_select_portraits + (uint8_t)player.race);
            out.Int("color", player.color);
        }

        // history: the History Graph (Draw_Histories, Draw_The_History_Graph). Each shown player's
        // points, the chosen series summed with their weights (bill_savegame 2-5) and smoothed when
        // the option is on; the scale's top; the time axis's eight labels; and how many of the
        // graph's 350 columns a point takes.
        void Query_History(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const uint8_t buttons = MOX::player[MOX::PLAYER_NUM].history_btns;
            bool use[4];
            for (int i = 0; i < 4; ++i) {
                use[i] = ((buttons >> i) & 1) != 0;
            }
            if (!use[0] && !use[1] && !use[2] && !use[3]) {
                use[0] = use[1] = use[2] = use[3] = true; // the screen turns all four on
            }
            out.Str("title", Billtext("billtext.lbx", 5));
            int16_t points = 0;
            const int16_t ring_start = BILL::Get_History_Params(&points);
            points = std::max<int16_t>(0, std::min<int16_t>(points, k_history_length));
            const std::vector<int16_t> shown = Shown_Players(false);
            std::vector<std::vector<int32_t>> data(shown.size(), std::vector<int32_t>((size_t)points, 0));
            int32_t largest = 0;
            for (size_t p = 0; p < shown.size(); ++p) {
                const s_player& plr = MOX::player[shown[p]];
                int16_t at = ring_start;
                for (int16_t t = 0; t < points; ++t) {
                    int32_t value = 0;
                    if (use[0]) {
                        value += (uint8_t)plr.population_history[at] * MOX::bill_savegame[2];
                    }
                    if (use[3]) {
                        value += (uint8_t)plr.tech_history[at] * MOX::bill_savegame[5];
                    }
                    if (use[2]) {
                        value += (uint8_t)plr.fleet_history[at] * MOX::bill_savegame[4];
                    }
                    if (use[1]) {
                        value += (uint8_t)plr.production_history[at] * MOX::bill_savegame[3];
                    }
                    data[p][(size_t)t] = value;
                    at = (int16_t)((at + 1) % k_history_length);
                }
                if (INFO::history_graph_smoothing) {
                    for (int pass = 0; pass < 10; ++pass) {
                        for (int16_t t = 0; t < points; ++t) {
                            INFO::Smooth_History_Data(data[p].data(), t, points);
                        }
                    }
                }
                for (int32_t v : data[p]) {
                    largest = std::max(largest, v);
                }
            }
            int32_t top = 25;
            while (largest >= top) {
                top = top == 25 ? 50 : top == 50 ? 100 : top == 100 ? 250 : top == 250 ? 500 : top == 500 ? 1000 : top == 1000 ? 2000 : top + 2000;
            }
            out.Int("top", top);
            // The time axis (Draw_The_History_Graph): its step by the game's length so far.
            const uint16_t turns = (uint16_t)(MOX::stardate + 30536);
            int32_t label = 35000;
            int32_t step = 50;
            int16_t columns = 1;
            if (turns <= 35) {
                columns = 10;
                step = 5;
            } else if (turns <= 70) {
                columns = 5;
                step = 10;
            } else if (turns <= 175) {
                columns = 2;
                step = 25;
            } else if (turns > 350) {
                label = ((MOX::stardate - 345) / 10) * 10;
            }
            out.Int("columns_per_point", columns);
            s_out axis = out.Add_List("axis");
            for (int i = 0; i < 8; ++i, label += step) {
                char text[16];
                const int32_t whole = label / 10;
                const int32_t tenth = label % 10;
                if (label % 5 == 0 && tenth != 0) {
                    snprintf(text, sizeof(text), MOX::settings.language == 0 ? "%i.%i" : "%i,%i", (int)whole, (int)tenth);
                } else {
                    snprintf(text, sizeof(text), "%i", (int)whole);
                }
                axis.Append_Object().Str("text", text);
            }
            s_out graphs = out.Add_List("graphs");
            for (int i = 0; i < 4; ++i) {
                graphs.Append_Int(use[i] ? 1 : 0);
            }
            s_out players = out.Add_List("players");
            for (size_t p = 0; p < shown.size(); ++p) {
                const s_player& plr = MOX::player[shown[p]];
                s_out item = players.Append_Object();
                item.Int("player", shown[p]);
                item.Str("name", Utf8(plr.race_name, sizeof(plr.race_name)));
                item.Int("color", plr.color);
                item.Bool("eliminated", plr.eliminated != 0);
                s_out values = item.Add_List("points");
                for (int32_t v : data[p]) {
                    values.Append_Int(v);
                }
            }
        }

        // The Tech Review's four lists (INFO::review_list: achievements, colony, weapons,
        // equipment), as Init_Tech_Review_List builds them: each group (billtex2) with the
        // applications the player has researched; empty groups are left out.
        void Query_Tech_Review(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const s_player& player = MOX::player[MOX::PLAYER_NUM];
            out.Str("title", Billtext("billtext.lbx", 6));
            static const char* const k_list_names[4] = {"Achievements", "Colony", "Weapons", "Equipment"};
            s_out lists = out.Add_List("lists");
            for (int l = 0; l < 4; ++l) {
                s_out list = lists.Append_Object();
                list.Str("name", k_list_names[l]);
                s_out groups = list.Add_List("groups");
                const s_tech_review_achievement* entry = INFO::review_list[l];
                std::string group_name;
                std::vector<int16_t> apps;
                bool have_group = false;
                auto flush = [&]() {
                    if (have_group && !apps.empty()) {
                        s_out group = groups.Append_Object();
                        group.Str("name", group_name);
                        s_out items = group.Add_List("apps");
                        for (int16_t app : apps) {
                            s_out item = items.Append_Object();
                            item.Int("app", app);
                            item.Str("name", Utf8(TECH::Technology_Applications_Name(app, MOX::PLAYER_NUM), 80));
                        }
                    }
                    apps.clear();
                };
                for (int i = 0; entry != nullptr && i < INFO::TECH_REVIEW_ACHIEVEMENT_COUNT; ++i, ++entry) {
                    if (entry->application == TECH_APP_NO_TECH && entry->group == TECH_REVIEW_GROUP_NONE) {
                        break;
                    }
                    if (entry->group < TECH_REVIEW_GROUP_COUNT) {
                        flush();
                        group_name = Billtext("billtex2.lbx", entry->group);
                        have_group = true;
                    } else if (entry->application < TECH_APP_COUNT && have_group &&
                               player.tech_applications[entry->application] == TECH_RESEARCH_STATUS_RESEARCHED) {
                        apps.push_back((int16_t)entry->application);
                    }
                }
                flush();
            }
        }

        // The Race Statistics (Race_Stats_Subscreen): each shown player's race name, its traits
        // and government as Print_Player_Specials_To_Bitmap prints them, each with its help
        // record (Race_Statistics_Specials_Help), and whether it was eliminated.
        void Query_Race_Stats(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            out.Str("title", Billtext("billtext.lbx", 7));
            out.Str("eliminated_label", Billtext("billtext.lbx", 14));
            s_out players = out.Add_List("players");
            for (int16_t p : Shown_Players(true)) {
                const s_player& plr = MOX::player[p];
                s_out item = players.Append_Object();
                item.Int("player", p);
                std::string name = Utf8(plr.race_name, sizeof(plr.race_name));
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::toupper(c); });
                item.Str("name", name);
                item.Int("color", plr.color);
                item.Int("portrait", k_race_portraits + (uint8_t)plr.race);
                item.Bool("eliminated", plr.eliminated != 0);
                s_out lines = item.Add_List("traits");
                for (int i = 0; i < TRAIT_COUNT; ++i) {
                    const int8_t value = plr.traits[i];
                    if (value == 0 && i != TRAIT_CURRENT_GOVERNMENT) {
                        continue;
                    }
                    std::string text;
                    int16_t help = 0;
                    if (i == TRAIT_CURRENT_GOVERNMENT) {
                        text = Utf8(MOX::government_labels[value], 40);
                        help = (int16_t)(value / 2 + 474);
                    } else if (i == TRAIT_RICH_HOME_WORLD) {
                        text = Utf8(MOX::special_ability_names[value < 0 ? 31 : 15], 40);
                        help = value < 0 ? 458 : 457;
                    } else {
                        text = Utf8(MOX::special_ability_names[i], 40);
                        help = (int16_t)(i < TRAIT_RICH_HOME_WORLD ? i + 442 : i + 443);
                    }
                    if (i > TRAIT_CURRENT_GOVERNMENT && i < TRAIT_LOW_G_WORLD) {
                        char number[16] = {};
                        raceopt::Format_Numeric_Trait(number, sizeof(number), (int16_t)i, value);
                        text += Utf8(number, sizeof(number));
                    }
                    s_out line = lines.Append_Object();
                    line.Str("text", text);
                    line.Int("help", help);
                }
            }
        }

        // The Reference (Reference_Main_Subscreen): the help categories (help_category_labels
        // whose first byte is past '9') and the How To topics (help LBX record set 15).
        void Query_Reference(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            out.Str("title", Billtext("billtext.lbx", 9));
            out.Str("categories_label", Billtext("billtext.lbx", 10));
            out.Str("how_to_label", Billtext("billtext.lbx", 11));
            s_out categories = out.Add_List("categories");
            for (int i = 1; i < 17; ++i) {
                const char* label = MOX::help_category_labels[i];
                if (label != nullptr && (uint8_t)label[0] > '9') {
                    s_out item = categories.Append_Object();
                    item.Int("category", i);
                    item.Str("name", Utf8(label, 48));
                }
            }
            char lbx[20];
            TEXTBOX::Get_Help_Lbx_Name(lbx, sizeof(lbx));
            const uint16_t count = std::min<uint16_t>(Lbx_Record_Count(lbx, 0xF), 16);
            s_out how_to = out.Add_List("how_to");
            for (uint16_t i = 0; i < count; ++i) {
                s_help_topic_record record = {};
                if (!Lbx_Record_Bytes(lbx, 0xF, i, &record, sizeof(record))) {
                    continue;
                }
                s_out item = how_to.Append_Object();
                item.Str("name", Utf8(record.text, sizeof(record.text)));
                item.Int("topic", record.id);
            }
        }

        int Compare_Unsigned(const std::string& a, const std::string& b) {
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) {
                if ((uint8_t)a[i] != (uint8_t)b[i]) {
                    return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
                }
            }
            return (int)a.size() - (int)b.size();
        }

        // reference_category {category}: one category's topics (help LBX record set
        // `category`) in the screen's order, sorted by their names' bytes
        // (Reference_Category_Subscreen), each with its help record.
        void Query_Reference_Category(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t category = -1;
            if (!args.Int("category", &category) || category < 1 || category > 16 ||
                MOX::help_category_labels[category] == nullptr || (uint8_t)MOX::help_category_labels[category][0] <= '9') {
                out.Fail("bad_args", "category: one of the reference's categories");
                return;
            }
            out.Int("category", category);
            out.Str("title", Billtext("billtext.lbx", 12) + Utf8(MOX::help_category_labels[category], 48));
            char lbx[20];
            TEXTBOX::Get_Help_Lbx_Name(lbx, sizeof(lbx));
            const uint16_t count = std::min<uint16_t>(Lbx_Record_Count(lbx, (int32_t)category), 60);
            struct s_topic {
                std::string raw;
                std::string name;
                uint32_t id;
            };
            std::vector<s_topic> topics;
            for (uint16_t i = 0; i < count; ++i) {
                s_help_topic_record record = {};
                if (Lbx_Record_Bytes(lbx, (int32_t)category, i, &record, sizeof(record))) {
                    record.text[sizeof(record.text) - 1] = '\0';
                    topics.push_back({record.text, Utf8(record.text, sizeof(record.text)), record.id});
                }
            }
            std::stable_sort(topics.begin(), topics.end(), [](const s_topic& a, const s_topic& b) { return Compare_Unsigned(a.raw, b.raw) < 0; });
            s_out list = out.Add_List("topics");
            for (const s_topic& topic : topics) {
                s_out item = list.Append_Object();
                item.Str("name", topic.name);
                item.Int("topic", topic.id);
            }
        }

        // info_set_tab {tab}: the tab the screen opens on next time (Set_Plyr_Info_Btns, which
        // the screen calls on leaving).
        void Info_Set_Tab(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t tab = -1;
            if (!args.Int("tab", &tab) || tab < 0 || tab > 4) {
                out.Fail("bad_args", "tab: 0-4");
                return;
            }
            BILL::Set_Plyr_Info_Btns((int16_t)tab);
            out.Int("tab", tab);
        }

        // info_set_graphs {pop, prod, fleet, tech}: the History Graph's toggles
        // (Set_Plyr_Hist_Btns); all four off turns them all on, as the screen does.
        void Info_Set_Graphs(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            static const char* const k_names[4] = {"pop", "prod", "fleet", "tech"};
            int16_t buttons[4];
            INFO::Get_Plyr_Hist_Btns(buttons);
            for (int i = 0; i < 4; ++i) {
                int64_t on = 0;
                if (args.Int(k_names[i], &on)) {
                    buttons[i] = on != 0 ? 1 : 0;
                }
            }
            if (buttons[0] == 0 && buttons[1] == 0 && buttons[2] == 0 && buttons[3] == 0) {
                buttons[0] = buttons[1] = buttons[2] = buttons[3] = 1;
            }
            INFO::Set_Plyr_Hist_Btns(buttons);
            s_out graphs = out.Add_List("graphs");
            for (int i = 0; i < 4; ++i) {
                graphs.Append_Int(buttons[i] != 0 ? 1 : 0);
            }
        }
    } // namespace

    void Add_Info_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"info", e_kind::query, ANY_SCREEN, 1, "", Query_Info});
        table->push_back({"history", e_kind::query, ANY_SCREEN, 1, "", Query_History});
        table->push_back({"tech_review", e_kind::query, ANY_SCREEN, 1, "", Query_Tech_Review});
        table->push_back({"race_stats", e_kind::query, ANY_SCREEN, 1, "", Query_Race_Stats});
        table->push_back({"reference", e_kind::query, ANY_SCREEN, 1, "", Query_Reference});
        table->push_back({"reference_category", e_kind::query, ANY_SCREEN, 1, "category: 1-16", Query_Reference_Category});
        table->push_back({"info_set_tab", e_kind::command, galaxy, 0, "tab: 0-4", Info_Set_Tab});
        table->push_back({"info_set_graphs", e_kind::command, galaxy, 0, "pop, prod, fleet, tech: 0 or 1", Info_Set_Graphs});
    }

} // namespace antares::commands::internal
