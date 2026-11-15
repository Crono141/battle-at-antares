#include "pch.h"

#include "antares/commands_internal.h"

#include <cstring>

// WP3.7: the Leaders and Races panels (notes/antares/screens/29-officers.md, 06-races.md). The
// queries read what those screens show; the commands are their buttons, with the screens' own
// rules, questions and messages (the officer screen's boxes are GENDRAW boxes, native requests).

namespace antares::commands::internal {

    namespace {

        // SHIPS.LBX: a ship's picture by design picture and owner colour (as the fleet query).
        constexpr int32_t k_ship_pictures_per_colour = 50;

        bool Players_Leader(int64_t leader, s_out& out) {
            if (leader < 0 || leader >= MAX_LEADERS || MOX::leaders[leader].player_index != MOX::PLAYER_NUM ||
                MOX::leaders[leader].status < 0) {
                out.Fail("bad_args", "leader: one of the player's leaders");
                return false;
            }
            return true;
        }

        // The officer screen's tab (OFFICER::Is_Fleet_Officer_Screen), set for one command.
        struct s_officer_tab {
            int8_t saved = MOX::officer_scrn_type;
            explicit s_officer_tab(uint8_t type) { MOX::officer_scrn_type = (int8_t)type; }
            ~s_officer_tab() { MOX::officer_scrn_type = saved; }
        };

        // leaders {type: colony | ship}: the player's leaders of that kind as the Leaders screen
        // lists them (Leader_Is_Displayed: hired or for hire), each with its card, its state line
        // (Draw_Officer_Data: in the pool, in limbo, where it serves, or for hire with the turns
        // the offer lasts and the price), and where it can go: the colonised stars (the colony
        // tab's cycler) or the fleets with warships (the ship tab's).
        void Query_Leaders(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            std::string kind = "colony";
            args.Str("type", &kind);
            const uint8_t type = kind == "ship" ? LEADER_TYPE_SHIP : LEADER_TYPE_COLONY;
            const int16_t me = MOX::PLAYER_NUM;
            out.Str("type", type == LEADER_TYPE_SHIP ? "ship" : "colony");
            out.Int("bc", MOX::player[me].bc);
            s_out list = out.Add_List("leaders");
            for (int16_t i = 0; i < MAX_LEADERS; ++i) {
                const s_leader_data& leader = MOX::leaders[i];
                if (leader.player_index != me || leader.status < 0 || leader.status == 3 || leader.type != type) {
                    continue;
                }
                s_out item = list.Append_Object();
                Leader_Card(i, me, item.Add_Object("card"));
                item.Int("leader", i);
                item.Int("status", leader.status);
                std::string state;
                if (leader.status == 0) {
                    state = Utf8(HAROLD::H_Message(0x10b), 60);
                } else if (leader.status == 2) {
                    state = Utf8(HAROLD::H_Message(0x10c), 60);
                } else if (leader.status == 1 && leader.location > -1) {
                    if (type == LEADER_TYPE_SHIP) {
                        state = Utf8(MOX::ship[leader.location].d.name, sizeof(MOX::ship[0].d.name));
                        item.Int("ship", leader.location);
                    } else {
                        state = Utf8(MOX::star[leader.location].name, sizeof(MOX::star[0].name));
                        item.Int("star", leader.location);
                    }
                    item.Int("eta", leader.eta);
                } else if (leader.status == 4) {
                    char text[80];
                    snprintf(text, sizeof(text), HAROLD::H_Message(0x10d), 0x1e - (int32_t)leader.eta);
                    state = Utf8(text, sizeof(text));
                    item.Bool("for_hire", true);
                    item.Int("cost", OFFICER::Officer_Cost(me, i));
                }
                item.Str("state", state);
            }
            // Where leaders go.
            s_out places = out.Add_List("places");
            if (type == LEADER_TYPE_COLONY) {
                for (int16_t s = 0; s < MOX::NUM_STARS; ++s) {
                    if (HACCESS::Player_Has_Colony_In_System(me, s)) {
                        s_out place = places.Append_Object();
                        place.Int("star", s);
                        place.Str("name", Utf8(MOX::star[s].name, sizeof(MOX::star[0].name)));
                        place.Int("governor", MOX::star[s].officer_index[me]);
                    }
                }
            } else {
                for (int16_t stack = 0; stack < (int16_t)MOX::ship_stack_count; ++stack) {
                    std::vector<int16_t> warships;
                    for (int16_t node = MOX::ship_stack_start[stack]; node != -1; node = MOX::ship_node[node].next_node) {
                        const int16_t s = MOX::ship_node[node].ship_idx;
                        if (MOX::ship[s].owner == me && MOX::ship[s].status < 3 && MOX::ship[s].d.ship_type == SHIP_TYPE_COMBAT) {
                            warships.push_back(s);
                        }
                    }
                    if (warships.empty()) {
                        continue;
                    }
                    s_out place = places.Append_Object();
                    const s_ship_data& lead = MOX::ship[warships.front()];
                    const int16_t star = lead.status == 1 ? -1 : HAROLD::XY_To_Star_Id(lead.x, lead.y);
                    place.Int("star", star);
                    // The stack's first ship: the map's fleet icon and the Fleets panel name a fleet by it.
                    place.Int("fleet", MOX::ship_node[MOX::ship_stack_start[stack]].ship_idx);
                    place.Str("name", star >= 0 ? Utf8(MOX::star[star].name, sizeof(MOX::star[0].name)) : std::string("In space"));
                    s_out ships = place.Add_List("ships");
                    for (int16_t s : warships) {
                        s_out ship = ships.Append_Object();
                        ship.Int("id", s);
                        ship.Str("name", Utf8(MOX::ship[s].d.name, sizeof(MOX::ship[0].d.name)));
                        ship.Int("picture", MOX::ship[s].d.picture_num + (int32_t)MOX::player[me].color * k_ship_pictures_per_colour);
                        ship.Int("officer", MOX::ship[s].officer_index);
                    }
                }
            }
        }

        // POOL: the leader leaves its post (Move_Officer_To_Limbo), and back to the pool at once
        // (the screen's RETURN does it: Move_From_Limbo_To_Pool), the colonies recalculated.
        void Leader_Pool(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t leader = -1;
            if (!args.Int("leader", &leader) || !Players_Leader(leader, out)) {
                return;
            }
            if (MOX::leaders[leader].status != 1) {
                out.Fail("refused", "the leader has no post");
                return;
            }
            s_officer_tab tab(MOX::leaders[leader].type);
            if (MOX::leaders[leader].type == LEADER_TYPE_SHIP && !SHIPMOVE::Confirm_Officer_Change((int16_t)leader, -1, 0)) {
                out.Bool("pooled", false);
                return;
            }
            OFFICER::Move_Officer_To_Limbo((int16_t)leader);
            OFFICER::Move_From_Limbo_To_Pool();
            // Not Check_For_Colony_Calculation_Update: it reads the stars the Leaders screen saves
            // on entry (officers_original_star_id), unset here; every colony is recalculated.
            COLCALC::Do_Player_Colony_Calculations(MOX::PLAYER_NUM);
            out.Bool("pooled", true);
        }

        // DISMISS: the screen's question (HESTR_126), the fleet's question, Dismiss_Officer.
        void Leader_Dismiss(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t leader = -1;
            if (!args.Int("leader", &leader) || !Players_Leader(leader, out)) {
                return;
            }
            if (MOX::leaders[leader].status == 4) {
                out.Fail("refused", "a leader for hire is not hired");
                return;
            }
            s_officer_tab tab(MOX::leaders[leader].type);
            char format[200];
            char text[200];
            strlcpy(format, HAROLD::H_Message(HESTR_126_DISMISS_S), sizeof(format));
            snprintf(text, sizeof(text), format, OFFICER::Leader_Name((int16_t)leader, 1));
            bool dismissed = false;
            if (GENDRAW::Confirmation_Box(text) != 0 && SHIPMOVE::Confirm_Officer_Change((int16_t)leader, -1, 0)) {
                OFFICER::Dismiss_Officer((int16_t)leader);
                COLCALC::Do_Player_Colony_Calculations(MOX::PLAYER_NUM);
                dismissed = true;
            }
            out.Bool("dismissed", dismissed);
        }

        // A ship officer to a ship (OFFICER::Assign_Captain on the ship tab: its questions, the
        // fleet's, the turns to get there).
        void Assign_Officer(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t leader = -1;
            int64_t ship = -1;
            if (!args.Int("leader", &leader) || !Players_Leader(leader, out)) {
                return;
            }
            if (!args.Int("ship", &ship) || ship < 0 || ship >= MOX::NUM_SHIPS || MOX::ship[ship].owner != MOX::PLAYER_NUM ||
                MOX::ship[ship].status >= 3) {
                out.Fail("bad_args", "ship: one of the player's ships");
                return;
            }
            if (MOX::leaders[leader].type != LEADER_TYPE_SHIP || MOX::leaders[leader].status == 4) {
                out.Fail("refused", "leader: a hired ship officer");
                return;
            }
            s_officer_tab tab(LEADER_TYPE_SHIP);
            OFFICER::Assign_Captain((int16_t)leader, (int16_t)ship);
            out.Bool("assigned", MOX::ship[ship].officer_index == leader);
        }

        // ---- Races ----

        std::string Billtext(int16_t entry) {
            char text[120] = {};
            JIM::Get_Text_Message("billtext.lbx", entry, text, sizeof(text));
            return Utf8(text, sizeof(text));
        }

        // races: the Races screen's slots (every other empire), its spy bonuses and the agents
        // at home.
        void Query_Races(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const int16_t me = MOX::PLAYER_NUM;
            s_player& player = MOX::player[me];
            int16_t spy_bonus = 0;
            int16_t agent_bonus = 0;
            spy::Compute_Spy_Bonuses(&player, &spy_bonus, &agent_bonus);
            out.Int("spy_bonus", spy_bonus);
            out.Int("agent_bonus", agent_bonus);
            out.Str("spy_bonus_label", Billtext(0x34));
            out.Str("agent_bonus_label", Billtext(0x35));
            out.Int("agents", BILL::Get_My_Agent_Number());
            out.Int("spy_icon", (uint8_t)player.race + 0x2E); // races.lbx: the player's spy
            out.Str("no_contact", Billtext(0x33));
            out.Str("ignoring_label", Billtext(0x32));
            static const uint8_t k_relation_limits[17] = {9, 21, 33, 45, 57, 69, 81, 93, 107, 119, 131, 143, 155, 167, 179, 191, 255};
            s_out list = out.Add_List("races");
            for (int16_t p = 0; p < MOX::NUM_PLAYERS; ++p) {
                if (p == me) {
                    continue;
                }
                const s_player& other = MOX::player[p];
                s_out item = list.Append_Object();
                item.Int("player", p);
                item.Str("ruler", Utf8(other.name, sizeof(other.name)));
                item.Str("race", Utf8(other.race_name, sizeof(other.race_name)));
                item.Int("race_id", (uint8_t)other.race);
                item.Int("portrait", (uint8_t)other.race + 32); // races.lbx
                item.Int("color", other.color);
                item.Bool("eliminated", other.eliminated != 0);
                item.Bool("contact", player.contact[p] != 0);
                const int16_t relation = (int16_t)player.relations[p] + 100;
                int relation_idx = 0;
                while (relation_idx < 16 && (int16_t)k_relation_limits[relation_idx] < relation) {
                    ++relation_idx;
                }
                item.Int("relations", player.relations[p]);
                item.Str("relation", Billtext((int16_t)(0x21 + relation_idx)));
                // The treaty summary (Draw_Race_Displays): the treaty, research and trade
                // agreements, tribute either way.
                const int16_t treaty = player.treaty[p];
                item.Int("treaty_id", treaty);
                std::string treaty_text = treaty > 0 ? Utf8(MOX::treaty_labels[treaty > 5 ? 5 : treaty], 40) : std::string();
                if (player.research_treaty[p] != 0) {
                    const int16_t level = player.current_research_agreement_level[p];
                    treaty_text += (treaty_text.empty() ? "" : "\n") + Billtext(0x39) + std::to_string(level) + (level >= 0 ? "RP" : "BC");
                }
                if (player.trade_treaty[p] != 0) {
                    treaty_text += (treaty_text.empty() ? "" : "\n") + Billtext(0x3A) + std::to_string(player.current_trade_agreement_level[p]) + "BC";
                }
                const uint8_t giving = player.tribute_treaty[p];
                const uint8_t receiving = other.tribute_treaty[me];
                if (giving != 0 || receiving != 0) {
                    char text[120] = {};
                    JIM::Get_Text_Message("billtext.lbx", giving != 0 ? 0x3B : 0x3C, text, sizeof(text));
                    JIM::Decode_Text_Stat(text, (giving != 0 ? giving : receiving) == 1 ? 5 : 10, sizeof(text));
                    treaty_text += (treaty_text.empty() ? "" : "\n") + Utf8(text, sizeof(text));
                }
                if (treaty_text.empty()) {
                    treaty_text = Utf8(MOX::treaty_labels[0], 40);
                }
                item.Str("treaty", treaty_text);
                item.Bool("at_war", treaty >= 4);
                item.Bool("war_declared", player.delayed_diplomacy_orders[p] == 0);
                item.Int("spies", BILL::Get_My_Spy_Number(p));
                item.Int("mission", BILL::Get_My_Spy_Mission(p));
                item.Int("their_spies", BILL::Get_Their_Spy_Number(p, me));
                item.Bool("ignoring", ((player.ignoring >> p) & 1) != 0);
            }
        }

        bool Other_Player(int64_t p, s_out& out) {
            if (p < 0 || p >= MOX::NUM_PLAYERS || p == MOX::PLAYER_NUM || MOX::player[p].eliminated != 0 ||
                MOX::player[MOX::PLAYER_NUM].contact[p] == 0) {
                out.Fail("bad_args", "player: an empire the player has met");
                return false;
            }
            return true;
        }

        // set_spies {player, count, mission}: spies move between home (agents) and an empire, as
        // the screen's drag moves them (Update_Spy_Stuff), never more than 63 there; and that
        // empire's mission (0 espionage, 1 sabotage, 2 hide).
        void Set_Spies(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t p = -1;
            int64_t count = -1;
            int64_t mission = -1;
            if (!args.Int("player", &p) || !Other_Player(p, out)) {
                return;
            }
            const int16_t now = BILL::Get_My_Spy_Number((int16_t)p);
            const int16_t home = BILL::Get_My_Agent_Number();
            if (args.Int("count", &count)) {
                const int64_t wanted = std::min<int64_t>(std::max<int64_t>(count, 0), std::min<int64_t>(63, now + home));
                BILL::Set_My_Spy_Number((int16_t)p, (uint8_t)wanted);
                BILL::Set_My_Agent_Number((uint8_t)(home + now - wanted));
            }
            if (args.Int("mission", &mission) && mission >= 0 && mission <= 2) {
                BILL::Set_My_Spy_Mission((int16_t)p, (int8_t)mission);
            }
            out.Int("spies", BILL::Get_My_Spy_Number((int16_t)p));
            out.Int("agents", BILL::Get_My_Agent_Number());
            out.Int("mission", BILL::Get_My_Spy_Mission((int16_t)p));
        }

        void Toggle_Ignore(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t p = -1;
            if (!args.Int("player", &p) || !Other_Player(p, out)) {
                return;
            }
            MOX::player[MOX::PLAYER_NUM].ignoring ^= (uint8_t)(1 << p);
            out.Bool("ignoring", ((MOX::player[MOX::PLAYER_NUM].ignoring >> p) & 1) != 0);
        }

        // DECLARE WAR: the screen's question (billtex2 0x1A with the race), then the order that
        // the next turn carries out (delayed_diplomacy_orders, Resolve_Delayed_Diplomacy_Orders).
        void Declare_War(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t p = -1;
            if (!args.Int("player", &p) || !Other_Player(p, out)) {
                return;
            }
            if (MOX::player[MOX::PLAYER_NUM].treaty[p] >= 4) {
                out.Fail("refused", "already at war");
                return;
            }
            char text[200] = {};
            JIM::Get_Text_Message("billtex2.lbx", 0x1A, text, sizeof(text));
            JIM::Decode_Text_Race(text, (int16_t)p, sizeof(text));
            const bool declared = GENDRAW::Do_Confirmation_Box(text) == 1;
            if (declared) {
                MOX::player[MOX::PLAYER_NUM].delayed_diplomacy_orders[p] = 0;
            }
            out.Bool("declared", declared);
        }

        // AUDIENCE: the ignore bit cleared, then the diplomacy screen (the native diplomacy window
        // when a frontend shows popups), as the Races screen opens it.
        void Audience(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t p = -1;
            if (!args.Int("player", &p) || !Other_Player(p, out)) {
                return;
            }
            if (MOX::game_type == 2 || MOX::game_type == 3) {
                out.Fail("refused", "network games keep the original Races screen's audience");
                return;
            }
            MOX::player[MOX::PLAYER_NUM].ignoring &= (uint8_t) ~(1 << p);
            MOX2::action_func_t saved_auto = nullptr;
            MOX2::Get_Auto_Function(&saved_auto);
            DIP_SCRN::Diplomacy_Screen((int)p);
            MOX2::Put_Auto_Function(&saved_auto);
            out.Int("player", p);
        }

    } // namespace

    void Add_Empire_Panel_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"leaders", e_kind::query, ANY_SCREEN, 1, "type: colony or ship", Query_Leaders});
        table->push_back({"leader_pool", e_kind::command, galaxy, 0, "leader", Leader_Pool});
        table->push_back({"leader_dismiss", e_kind::command, galaxy, 0, "leader", Leader_Dismiss});
        table->push_back({"assign_officer", e_kind::command, galaxy, 0, "leader, ship", Assign_Officer});
        table->push_back({"races", e_kind::query, ANY_SCREEN, 1, "", Query_Races});
        table->push_back({"set_spies", e_kind::command, galaxy, 0, "player, count, mission (0-2)", Set_Spies});
        table->push_back({"toggle_ignore", e_kind::command, galaxy, 0, "player", Toggle_Ignore});
        table->push_back({"declare_war", e_kind::command, galaxy, 0, "player", Declare_War});
        table->push_back({"audience", e_kind::command, galaxy, 0, "player", Audience});
    }

} // namespace antares::commands::internal
