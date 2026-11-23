#include "pch.h"

#include "antares/commands_internal.h"

#include <algorithm>
#include <string>

// WP3.4c: `preview_popup` shows one of the legacy windows over the galaxy with made-up but valid
// data, so a frontend's native popup can be seen without staging the game situation (smoke runs,
// the owner's review). It is a developer verb: the window's answer is reported and thrown away
// (nothing is colonised, attacked or paid), and what a window reads or consumes on the way (a
// just-visited system's special) is put back.

namespace antares::commands::internal {
    namespace {

        bool Star_Arg(const s_args& args, s_out& out, int16_t* star) {
            int64_t value = -1;
            if (!args.Int("star", &value) || value < 0 || value >= MOX::NUM_STARS) {
                out.Fail("bad_args", "star: a star id");
                return false;
            }
            *star = (int16_t)value;
            return true;
        }

        void Preview_Popup(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            std::string popup;
            if (!args.Str("popup", &popup)) {
                out.Fail("bad_args", "popup: new_system, planet_pick, outpost_pick, attack_target, monster_tribute or colony_combat");
                return;
            }
            const int16_t player = MOX::PLAYER_NUM;
            int16_t star = -1;
            int64_t answer = -1;
            if (popup == "diplomacy") {
                // The diplomacy window with a computer player (`player`, default the first one
                // met), as the Races screen opens it. Unlike the other previews its lists act:
                // a smoke run only leaves (EXIT).
                int64_t other = -1;
                args.Int("player", &other);
                if (other < 0) {
                    for (int16_t p = 0; p < MOX::NUM_PLAYERS; ++p) {
                        if (p != player && MOX::player[p].eliminated == 0 && MOX::player[p].contact[player] != 0) {
                            other = p;
                            break;
                        }
                    }
                }
                if (other < 0 || other >= MOX::NUM_PLAYERS || other == player) {
                    out.Fail("bad_args", "player: a computer player the player has met");
                    return;
                }
                // The screen leaves its redraw as the auto function (the Races screen sets its own
                // afterwards); a preview over the galaxy puts the galaxy's back.
                MOX2::action_func_t saved_auto = nullptr;
                MOX2::Get_Auto_Function(&saved_auto);
                DIP_SCRN::Diplomacy_Screen((int)other);
                MOX2::Put_Auto_Function(&saved_auto);
                out.Int("player", other);
                answer = 0;
            } else if (popup == "research") {
                // The research window: two discoveries, then the picker; the player's research is
                // put back afterwards (a preview chooses nothing).
                s_player& p = MOX::player[player];
                const int8_t field = p.current_research_field;
                const int8_t app = p.current_research_application;
                const uint8_t breakthrough = p.research_breakthrough;
                int16_t techs[2] = {1, 50};
                uint16_t tech_count = 2;
                int64_t hyper = 0;
                if (args.Int("hyper", &hyper) && hyper != 0) {
                    techs[0] = TECH::Get_Hyper_Tech_App_ID(TECH_FIELD_BIOLOGY); // a hyper-advanced level's page
                    tech_count = 1;
                }
                char room[32];
                snprintf(room, sizeof(room), "SR_R%x_SC.LBX", (unsigned int)(uint8_t)p.race);
                MOX2::action_func_t saved_auto = nullptr;
                MOX2::Get_Auto_Function(&saved_auto);
                SCIENCE::Native_Tech_Select_With(room, tech_count, techs, "Your scientists have completed their research!");
                MOX2::Put_Auto_Function(&saved_auto);
                out.Int("chosen_field", (uint8_t)p.current_research_field);
                out.Int("chosen_app", (uint8_t)p.current_research_application);
                p.current_research_field = field;
                p.current_research_application = app;
                p.research_breakthrough = breakthrough;
                JIM::Play_Background_Music();
                answer = 0;
            } else if (popup == "council") {
                // WP3.11d: a council meeting, for tests on a scratch save only: it runs the rules
                // (relations change; a ruling accepted ends the game at the next turn).
                council::Main_Council_Screen();
                answer = 0;
            } else if (popup == "occupation") {
                // The occupation question for the player's home colony; its policy is put back.
                const int16_t home = MOX::player[player].home_planet_id;
                const int16_t colony = MOX::planet[home].colony_index;
                const char saved_policy = MOX::colony[colony].occupation_policy;
                MOX::screen_data = MOX::planet[home].star_index;
                COLONY::orbit_temp = (int16_t)MOX::planet[home].orbit;
                OCPOLPUP::Occupation_Policy_Popup();
                MOX::colony[colony].occupation_policy = saved_policy;
                answer = 0;
            } else if (popup == "antarans") {
                // WP3.11c: the Antarans' message (the attack's message set).
                ANTAROOM::Main_Antaran_Room_Screen(0);
                answer = 0;
            } else if (popup == "film") {
                // WP3.11b: a film in its box (the amoeba's: no rules run in it).
                ERIC::Play_Amoeba_Flic();
                answer = 0;
            } else if (popup == "gnn") {
                // The GNN window: the intro and one story (`event`, default 0) about the player's
                // home planet.
                int64_t event = 0;
                args.Int("event", &event);
                const int16_t home = MOX::player[player].home_planet_id;
                EVENTS::Preview_Native_Gnn((int16_t)std::clamp<int64_t>(event, 0, EVENT_COUNT - 1), home, MOX::planet[home].star_index);
                answer = 0;
            } else if (popup == "new_system") {
                if (!Star_Arg(args, out, &star)) {
                    return;
                }
                // An optional special (the window's description of it, e.g. a pirate cache's BC).
                int64_t special = SYSTEM_SPECIAL_NO_SPECIAL;
                if (args.Int("special", &special) && (special < 0 || special >= SYSTEM_SPECIAL_COUNT)) {
                    special = SYSTEM_SPECIAL_NO_SPECIAL;
                }
                MAINPUPS::New_System_Discovery_Popup(star, (uint8_t)special);
                answer = 0;
            } else if (popup == "planet_pick" || popup == "outpost_pick") {
                if (!Star_Arg(args, out, &star)) {
                    return;
                }
                const uint8_t visited = MOX::star[star].just_visited;
                const auto special = MOX::star[star].system_special;
                answer = popup == "planet_pick" ? MAINPUPS::New_Colony_Selection_Popup(star, 1) : MAINPUPS::New_Outpost_Selection_Popup(star);
                MOX::star[star].just_visited = visited;
                MOX::star[star].system_special = special;
            } else if (popup == "attack_target") {
                if (!Star_Arg(args, out, &star)) {
                    return;
                }
                // The targets the turn's combat check would find: the other empires' colonies there.
                const s_combfind_target saved = COMBFIND::target;
                COMBFIND::target = s_combfind_target{};
                for (const int16_t p : MOX::star[star].planet_index) {
                    if (p < 0 || p >= MOX::planet_count) {
                        continue;
                    }
                    const int16_t colony = MOX::planet[p].colony_index;
                    if (colony >= 0 && MOX::colony[colony].owner != player && COMBFIND::target.num_colony_targets < 5) {
                        COMBFIND::target.colony_targets[COMBFIND::target.num_colony_targets++] = colony;
                    }
                }
                answer = MAINPUPS::Defense_Colony_Selection_Popup(star, player);
                COMBFIND::target = saved;
            } else if (popup == "monster_tribute") {
                if (!Star_Arg(args, out, &star)) {
                    return;
                }
                answer = MAINPUPS::Dragon_New_Star_Popup(player, star);
            } else if (popup == "player_attacked") {
                // The message before an attack on the player (combfind.cpp), by `attacker` (an
                // empire id, default the first other one) at the star.
                if (!Star_Arg(args, out, &star)) {
                    return;
                }
                int64_t attacker = player == 0 ? 1 : 0;
                args.Int("attacker", &attacker);
                MAINPUPS::Player_Attacked_Popup((int16_t)attacker, player, star, -1);
                answer = 0;
            } else if (popup == "colony_combat") {
                int64_t colony = -1;
                if (!args.Int("colony", &colony) || colony < 0 || colony >= MOX::NUM_COLONIES || MOX::colony[colony].owner < 0) {
                    out.Fail("bad_args", "colony: a colony id");
                    return;
                }
                s_invasion_side_info attacker{};
                s_invasion_side_info defender{};
                attacker.player_id = player;
                attacker.unit_counts[UNIT_MARINES] = 8;
                defender.player_id = MOX::colony[colony].owner;
                defender.unit_counts[UNIT_MARINES] = MOX::colony[colony].military[0];
                defender.unit_counts[0] = MOX::colony[colony].military[1];
                answer = MAINPUPS::Colony_Selection_Popup(&attacker, &defender, 0, (int16_t)colony, 3, 0);
                // `then_bomb` hits: BOMB goes on to the bombing, as combfind.cpp does (the one
                // window throughout, owner 2026-11-20). The hits are real, as colony_bombing's.
                int64_t then_bomb = 0;
                if (answer == 2 && args.Int("then_bomb", &then_bomb) && then_bomb > 0) {
                    s_combat_bomb_result bomb{};
                    bomb.attacker_idx = (int8_t)player;
                    bomb.colony_idx = (int16_t)colony;
                    bomb.bomb_hits_remaining = (int16_t)then_bomb;
                    s_invasion_side_info bomb_attacker{};
                    s_invasion_side_info bomb_defender{};
                    bomb_attacker.player_id = player;
                    bomb_attacker.unit_counts[UNIT_MARINES] = 8; // the choice's attacker, as combfind.cpp passes it on
                    bomb_defender.player_id = MOX::colony[colony].owner;
                    MAINPUPS::Colony_Bombing_Popup(&bomb, &bomb_attacker, &bomb_defender, game_random::Get_Random_Seed(), (int16_t)colony,
                        (int16_t)then_bomb, 0);
                    out.Int("population_killed", bomb.population_killed);
                }
            } else if (popup == "colony_bombing") {
                // The bombing after BOMB (combfind.cpp: COMBAT::Bombardment, then
                // Colony_Bombing_Popup), with `hits` bomb hits (default 12) on the colony. The
                // hits are real: the colony loses what they destroy (a developer verb).
                int64_t colony = -1;
                if (!args.Int("colony", &colony) || colony < 0 || colony >= MOX::NUM_COLONIES || MOX::colony[colony].owner < 0) {
                    out.Fail("bad_args", "colony: a colony id");
                    return;
                }
                int64_t hits = 12;
                args.Int("hits", &hits);
                s_invasion_side_info attacker{};
                s_invasion_side_info defender{};
                attacker.player_id = player;
                defender.player_id = MOX::colony[colony].owner;
                s_combat_bomb_result bomb{};
                bomb.attacker_idx = (int8_t)player;
                bomb.colony_idx = (int16_t)colony;
                bomb.bomb_hits_remaining = (int16_t)hits;
                answer = MAINPUPS::Colony_Bombing_Popup(&bomb, &attacker, &defender, game_random::Get_Random_Seed(), (int16_t)colony, (int16_t)hits, 0);
                out.Int("population_killed", bomb.population_killed);
                out.Int("hits_left", bomb.bomb_hits_remaining);
            } else if (popup == "choose") {
                // The diplomacy screen's lists (fields::Get_List_Field): three items, the last one off.
                char list[4][50] = {"Propose Treaty", "Form Alliance", "Exchange Technology", ""};
                uint8_t flags[4] = {1, 1, 0, 0};
                int8_t selected = 0;
                answer = fields::Get_List_Field(10, 102, 245, "Diplomacy", &list[0][0], 50, &selected, flags, 17, nullptr, 0, nullptr, 0, nullptr);
            } else {
                out.Fail("bad_args", "popup: new_system, planet_pick, outpost_pick, attack_target, player_attacked, monster_tribute, colony_combat, colony_bombing, choose, gnn, diplomacy or research");
                return;
            }
            MOX::PLAYER_NUM = player;
            out.Int("answer", answer);
        }
    } // namespace

    void Add_Preview_Entries(std::vector<s_entry>* table) {
        table->push_back({"preview_popup", e_kind::command, ANY_SCREEN, 0,
            "popup (new_system, planet_pick, outpost_pick, attack_target, player_attacked, monster_tribute: star; colony_combat: colony; colony_bombing: colony, hits; choose; gnn: event; diplomacy: player; research); developer verb", Preview_Popup});
    }

} // namespace antares::commands::internal
