#include "pch.h"

#include "antares/commands_internal.h"

#include <algorithm>
#include <cstring>

// WP3.5b: the Planets panel (notes/antares/screens/32-planet-summary.md). The query lists what the
// Planets screen can list (unowned planets the player has visited, outposts aside) with each of
// its five filters as a flag, so the panel filters without asking again; the rows' text is the
// screen's own (Summary_Screen_Print_Data). Sending and recalling borrow the screen's functions
// (Build_Ship_Summary, Reset_Ship_To_Star_Distances, Send_Nearest_Ship, Undo_Send_Order) on
// buffers of our own, so the ship that goes is the one the original picks, with its questions.

namespace antares::commands::internal {

    namespace {

        // The PLNTSUM globals the screen's functions read and fill, pointed at buffers of our own
        // for one query or command and put back afterwards (the galaxy owns screen_seg).
        struct s_plntsum_scope {
            s_plntsum_planet* planet_summary = PLNTSUM::planet_summary;
            s_plntsum_ship* ship_summary = PLNTSUM::ship_summary;
            s_plntsum_screen* screen = PLNTSUM::screen;
            s_scroll_bar* scroll_bar = MOX::scroll_bar;
            int16_t n_ships = PLNTSUM::n_ships_in_summary;
            int16_t send_choice = PLNTSUM::send_choice;
            uint8_t found_colony = PLNTSUM::found_colony_ship;
            uint8_t found_outpost = PLNTSUM::found_outpost_ship;
            int16_t last_moved = PLNTSUM::last_ship_moved;

            static s_plntsum_planet s_planets[MAX_PLANETS];
            static s_plntsum_ship s_ships[MAX_SHIPS];
            static s_plntsum_screen s_screen;
            static s_scroll_bar s_bar;

            s_plntsum_scope() {
                memset(s_planets, 0xFF, sizeof(s_planets));
                memset(s_ships, 0xFF, sizeof(s_ships));
                memset(&s_screen, 0, sizeof(s_screen));
                memset(&s_bar, 0, sizeof(s_bar));
                PLNTSUM::planet_summary = s_planets;
                PLNTSUM::ship_summary = s_ships;
                PLNTSUM::screen = &s_screen;
                MOX::scroll_bar = &s_bar;
            }
            ~s_plntsum_scope() {
                PLNTSUM::planet_summary = planet_summary;
                PLNTSUM::ship_summary = ship_summary;
                PLNTSUM::screen = screen;
                MOX::scroll_bar = scroll_bar;
                PLNTSUM::n_ships_in_summary = n_ships;
                PLNTSUM::send_choice = send_choice;
                PLNTSUM::found_colony_ship = found_colony;
                PLNTSUM::found_outpost_ship = found_outpost;
                PLNTSUM::last_ship_moved = last_moved;
            }

            // A row for `planet` as Filter_Explored_Planets records it.
            void Add(int16_t planet) {
                s_plntsum_planet& row = s_planets[s_bar.total_rows++];
                row.max_pop = COLCALC::Planet_Max_Population_For_Player(&MOX::planet[planet], MOX::PLAYER_NUM);
                row.food_per_farmer = (int16_t)MOX::planet[planet].food_per_farmer;
                row.minerals = (int16_t)COLCALC::minerals_per_mine[MOX::planet[planet].mineral_class];
                row.mineral_class = (int16_t)MOX::planet[planet].mineral_class;
                row.climate = (int16_t)MOX::planet[planet].climate;
                row.explored = planet;
            }
        };
        s_plntsum_planet s_plntsum_scope::s_planets[MAX_PLANETS];
        s_plntsum_ship s_plntsum_scope::s_ships[MAX_SHIPS];
        s_plntsum_screen s_plntsum_scope::s_screen;
        s_scroll_bar s_plntsum_scope::s_bar;

        // What the Planets screen lists before its toggles (Filter_Explored_Planets' first tests).
        bool Listed(int16_t p) {
            return MOX::planet[p].planet_type == PLANET_TYPE_PLANET && !PLNTSUM::Planet_Has_Players_Colony(p) &&
                !PLNTSUM::Planet_Is_Outpost_Planet(p) && PLNTSUM::Planet_Has_Been_Visited(p);
        }

        // The row's special line (Summary_Screen_Print_Data): the system's or the planet's special,
        // the monster's name for a space monster.
        std::string Special(const s_planet_data& planet) {
            const int16_t star_idx = planet.star_index;
            int16_t special_id = 0;
            const char system_special = MOX::star[star_idx].system_special;
            if (system_special > SYSTEM_SPECIAL_NO_SPECIAL && system_special != SYSTEM_SPECIAL_ORION_STAR) {
                special_id = system_special;
            }
            if (planet.planet_special == SYSTEM_SPECIAL_ANCIENT_ARTIFACTS) {
                special_id = SYSTEM_SPECIAL_ANCIENT_ARTIFACTS;
            }
            if (special_id <= SYSTEM_SPECIAL_NO_SPECIAL || !HACCESS::Special_Is_Planet_Special(special_id)) {
                return {};
            }
            if (system_special == SYSTEM_SPECIAL_SPACE_MONSTER) {
                const int16_t ship_idx = HAROLD::First_Ship_Orbiting_Star(&MOX::star[star_idx]);
                if (ship_idx != -1) {
                    return Utf8(MOX::ship[ship_idx].d.name, sizeof(MOX::ship[ship_idx].d.name));
                }
                if (special_id == SYSTEM_SPECIAL_ANCIENT_ARTIFACTS) {
                    return Utf8(HAROLD::H_Message(140), 80);
                }
                return Utf8(MOX::planet_special_string[special_id], 80);
            }
            return planet.planet_special == special_id ? Utf8(MOX::planet_special_string[special_id], 80) : std::string();
        }

        // ETA without the screen's bias for ships already moving (+1000 / +2000: orbiting ships win).
        int32_t Eta(int16_t value) {
            return value < 0 || value >= 9999 ? -1 : value % 1000;
        }

        void Query_Planets(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            s_plntsum_scope scope;
            std::vector<int16_t> listed;
            for (int16_t p = 0; p < (int16_t)MOX::planet_count; ++p) {
                if (Listed(p)) {
                    listed.push_back(p);
                    scope.Add(p);
                }
            }
            // The free ships and each planet's nearest, per kind (Determine_Button_Status, then
            // Reset_Ship_To_Star_Distances as the screen runs them for its armed button).
            PLNTSUM::Build_Ship_Summary();
            const bool colony_ships = PLNTSUM::found_colony_ship != 0;
            const bool outpost_ships = PLNTSUM::found_outpost_ship != 0;
            std::vector<int32_t> best_colony(listed.size(), -1);
            std::vector<int32_t> best_outpost(listed.size(), -1);
            std::vector<bool> blocked(listed.size(), false);
            for (int16_t choice = 0; choice < 2; ++choice) {
                if ((choice == 0 && !colony_ships) || (choice == 1 && !outpost_ships)) {
                    continue;
                }
                PLNTSUM::send_choice = choice;
                PLNTSUM::Reset_Ship_To_Star_Distances();
                for (size_t i = 0; i < listed.size(); ++i) {
                    const s_plntsum_planet& row = PLNTSUM::planet_summary[i];
                    (choice == 0 ? best_colony : best_outpost)[i] = Eta(choice == 0 ? row.best_colony_eta : row.best_outpost_eta);
                    blocked[i] = blocked[i] || row.blocked == 1;
                }
            }
            out.Bool("colony_ships", colony_ships);
            out.Bool("outpost_ships", outpost_ships);
            out.Bool("flux", EVENTS::Event_Check_Hyperspace_Flux() != 0);
            s_out rows = out.Add_List("planets");
            for (size_t i = 0; i < listed.size(); ++i) {
                const int16_t p = listed[i];
                const s_planet_data& planet = MOX::planet[p];
                const s_plntsum_planet& summary = PLNTSUM::planet_summary[i];
                s_out row = rows.Append_Object();
                row.Int("planet", p);
                row.Int("star", planet.star_index);
                row.Str("name", Utf8(HACCESS::Get_Planet_Name_No_Prime(p), 32));
                // Who is there: an empire's colony, or the monster guarding the star.
                int16_t monster = -1;
                if (planet.colony_index != -1) {
                    const int16_t owner = (int16_t)MOX::colony[planet.colony_index].owner;
                    if (owner >= 0 && owner < MAX_PLAYERS) {
                        row.Str("owner", Utf8(MOX::player[owner].race_name, sizeof(MOX::player[owner].race_name)));
                    }
                } else if (HAROLD::Star_Guarded_By_Monster(planet.star_index, &monster)) {
                    char race[20];
                    HACCESS::Race_Name(monster, race, sizeof(race));
                    row.Str("owner", Utf8(race, sizeof(race)));
                    row.Bool("guarded", true);
                }
                row.Str("special", Special(planet));
                row.Int("climate_id", planet.climate);
                row.Str("climate", Utf8(MOX::planet_climate_string[planet.climate], 40));
                char text[100];
                char food[16];
                COLCALC::Format_Food2((uint16_t)summary.food_per_farmer, food, sizeof(food));
                snprintf(text, sizeof(text), "%s Food", food);
                row.Str("food", Utf8(text, sizeof(text)));
                row.Int("food_per_farmer", summary.food_per_farmer);
                row.Int("gravity_id", planet.gravity_class);
                row.Str("gravity", Utf8(MOX::planet_gravity_string[planet.gravity_class], 40));
                const int16_t penalty = PLNTSUM::Productivity_Penalty(MOX::PLAYER_NUM, planet.gravity_class);
                row.Int("gravity_penalty", penalty);
                if (penalty != 0) {
                    snprintf(text, sizeof(text), HAROLD::H_Message(0x142), (int)penalty);
                    row.Str("gravity_note", Utf8(text, sizeof(text)));
                }
                row.Int("mineral_id", planet.mineral_class);
                row.Str("minerals", Utf8(MOX::mineral_class_string[planet.mineral_class], 40));
                row.Int("mineral_value", summary.minerals);
                snprintf(text, sizeof(text), HAROLD::H_Message(0x152), (int)summary.minerals);
                row.Str("minerals_note", Utf8(text, sizeof(text)));
                row.Int("size_id", planet.size);
                row.Str("size", Utf8(MOX::planet_size_string[planet.size], 40));
                row.Int("max_pop", summary.max_pop);
                snprintf(text, sizeof(text), HAROLD::H_Message(0x143), (int)summary.max_pop);
                row.Str("size_note", Utf8(text, sizeof(text)));
                // The screen's rotating planet (PLNTSUM.LBX 26 + climate * sizes + size).
                s_out picture = row.Add_Object("picture");
                picture.Str("lbx", "PLNTSUM.LBX");
                picture.Int("entry", 26 + planet.climate * PLANET_SIZE_COUNT + planet.size);
                // The five toggles (Filter_Explored_Planets): true means the toggle hides it.
                s_out hide = row.Add_Object("hidden_by");
                hide.Bool("out_of_range", !PLNTSUM::Planet_In_Range_Of_A_Colony(p));
                hide.Bool("adverse_gravity", PLNTSUM::Planet_Has_Adverse_Gravity(p, MOX::PLAYER_NUM));
                hide.Bool("enemy", PLNTSUM::Planet_Is_Enemy_Controlled(p, MOX::PLAYER_NUM));
                hide.Bool("hostile", PLNTSUM::Planet_Has_Hostile_Environment(p));
                hide.Bool("mineral_poor", PLNTSUM::Planet_Is_Mineral_Scarce(p));
                // Ships on their way (Update_Ship_Etas: the en-route icons) and the nearest free ones.
                row.Int("colony_eta", summary.colony_eta);
                row.Int("outpost_eta", summary.outpost_eta);
                row.Int("best_colony_eta", best_colony[i]);
                row.Int("best_outpost_eta", best_outpost[i]);
                row.Bool("blocked", blocked[i]);
            }
        }

        bool Kind_Arg(const s_args& args, s_out& out, int16_t* choice) {
            std::string kind;
            if (!args.Str("kind", &kind) || (kind != "colony" && kind != "outpost")) {
                out.Fail("bad_args", "kind: colony or outpost");
                return false;
            }
            *choice = kind == "colony" ? 0 : 1;
            return true;
        }

        bool Planet_Arg(const s_args& args, s_out& out, int16_t* planet) {
            int64_t p = -1;
            if (!args.Int("planet", &p) || p < 0 || p >= MOX::planet_count || !Listed((int16_t)p)) {
                out.Fail("bad_args", "planet: one the Planets screen lists");
                return false;
            }
            *planet = (int16_t)p;
            return true;
        }

        // SEND COLONY SHIP / SEND OUTPOST SHIP armed and the planet clicked (Send_Nearest_Ship):
        // the nearest free ship of that kind goes, after the screen's questions; flux refuses.
        void Planets_Send(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t planet = -1;
            int16_t choice = -1;
            if (!Planet_Arg(args, out, &planet) || !Kind_Arg(args, out, &choice)) {
                return;
            }
            if (EVENTS::Event_Check_Hyperspace_Flux() != 0) {
                GENDRAW::Warning_Box(HAROLD::H_Message(0x13F)); // as the screen's buttons refuse
                out.Bool("sent", false);
                return;
            }
            s_plntsum_scope scope;
            scope.Add(planet);
            PLNTSUM::send_choice = choice;
            PLNTSUM::Build_Ship_Summary();
            if ((choice == 0 ? PLNTSUM::found_colony_ship : PLNTSUM::found_outpost_ship) == 0) {
                out.Fail("refused", choice == 0 ? "no free colony ship" : "no free outpost ship");
                return;
            }
            PLNTSUM::Reset_Ship_To_Star_Distances();
            PLNTSUM::last_ship_moved = -1;
            PLNTSUM::Send_Nearest_Ship(planet);
            const int16_t ship = PLNTSUM::last_ship_moved;
            out.Bool("sent", ship >= 0);
            out.Int("ship", ship);
            if (ship >= 0) {
                SHIPS::Build_Ship_Icons(); // the galaxy's fleet icons, as after a fleet order
                SHIPS::Set_Ship_Icon_XYs();
            }
        }

        // The en-route icon clicked (Undo_Send_Order): the ship sent there turns back to the
        // nearest star, after the screen's question, and loses its orders.
        void Planets_Recall(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t planet = -1;
            int16_t choice = -1;
            if (!Planet_Arg(args, out, &planet) || !Kind_Arg(args, out, &choice)) {
                return;
            }
            s_plntsum_scope scope;
            scope.Add(planet);
            PLNTSUM::Update_Ship_Etas();
            const int8_t before = choice == 0 ? PLNTSUM::planet_summary[0].colony_eta : PLNTSUM::planet_summary[0].outpost_eta;
            if (before < 0) {
                out.Fail("refused", "no ship of that kind is on its way there");
                return;
            }
            PLNTSUM::Undo_Send_Order(planet, choice == 0 ? SHIP_TYPE_COLONY : SHIP_TYPE_OUTPOST);
            PLNTSUM::Update_Ship_Etas();
            const int8_t after = choice == 0 ? PLNTSUM::planet_summary[0].colony_eta : PLNTSUM::planet_summary[0].outpost_eta;
            out.Bool("recalled", after < 0);
            if (after < 0) {
                SHIPS::Build_Ship_Icons();
                SHIPS::Set_Ship_Icon_XYs();
            }
        }

    } // namespace

    void Add_Planet_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"planets", e_kind::query, ANY_SCREEN, 1, "", Query_Planets});
        table->push_back({"planets_send", e_kind::command, galaxy, 0, "planet, kind: colony or outpost", Planets_Send});
        table->push_back({"planets_recall", e_kind::command, galaxy, 0, "planet, kind: colony or outpost", Planets_Recall});
    }

} // namespace antares::commands::internal
