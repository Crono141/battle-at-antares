#include "pch.h"

#include "antares/commands_internal.h"
#include "antares/native.h"
#include "game/legacy_ids.h"
#include "yyjson.h"

// WP3.5c: the Fleets panel (notes/antares/screens/04-fleet.md). The fleet's ships come from the
// `fleet` query (the galaxy's fleet box); this adds what the Fleets screen shows beside them, a
// ship's detail box (FLT2::Print_Scanned_Ship_Data), and its relocation clicks
// (FLT1::Star_Relocation: a star to relocate from, then the star to send to; the same star twice
// cancels), with the screen's own checks and messages.

namespace antares::commands::internal {

    namespace {

        // OFFICER.LBX: the leaders' portraits (as the colony leaders' cards).
        constexpr int32_t k_officer_portraits = 0;

        // The relocation in progress (FLT1::Star_Relocation's two stars), across commands.
        int16_t s_relocate_from = -1;
        int16_t s_relocate_to = -1;

        // A ship's card, the battle scan's facts outside a battle (owner 2026-11-23: the fleets
        // panel's ship info and a right click on a ship, in the scan's look): its parts with their
        // help records, what its damage leaves (armour, structure, systems), combat values, crew,
        // officer, weapons with their arcs, specials, and its picture.
        void Ship_Card(int16_t s, int16_t attack, int16_t defense, int16_t missile_defense, s_out card) {
            s_ship_data& ship = MOX::ship[s];
            s_ship_design& d = ship.d;
            card.Str("hull", Utf8(TECHDATA::hull_data[SDL_clamp((int)d.size, 0, 5)].name, 40));
            card.Str("drive", Utf8(TECHDATA::drives[d.ftl_type].name, 40));
            card.Int("speed", d.combat_speed);
            card.Str("crew", Utf8(FLT2::Crew_Description_String(s), 60));
            card.Str("shield", d.shield_type == SHIELD_NO_SHIELD ? Utf8(HAROLD::H_Message(HESTR_036_NO_SHIELD), 40) : Utf8(TECHDATA::shields[d.shield_type].name, 40));
            card.Int("shield_class", d.shield_type == SHIELD_NO_SHIELD ? 0 : TECHDATA::shields[d.shield_type].shield_class);
            card.Str("computer", Utf8(TECHDATA::computers[d.computer_type].name, 40));
            card.Str("armor_name", Utf8(TECHDATA::armor[d.armor_type].name, 40));
            s_out help = card.Add_Object("help");
            help.Int("shield", d.shield_type == SHIELD_NO_SHIELD ? -1 : TECHDATA::shields[d.shield_type].tech_app_id);
            help.Int("computer", TECHDATA::computers[d.computer_type].tech_app_id);
            help.Int("armor", TECHDATA::armor[d.armor_type].tech_app_id);
            help.Int("drive", TECHDATA::drives[d.ftl_type].tech_app_id);
            card.Int("attack", attack);
            card.Int("defense", defense);
            card.Int("evade", missile_defense);
            card.Str("attack_label", Utf8(HAROLD::H_Message(0x99), 40));
            card.Str("defense_label", Utf8(HAROLD::H_Message(0x9A), 40));
            if (ship.officer_index >= 0 && ship.officer_index < MAX_LEADERS) {
                card.Str("officer", Utf8(OFFICER::Leader_Name(ship.officer_index, 1), 80));
            }
            const int16_t armor_max = INITSHIP::Get_Ship_Armor_Hits(s);
            const int16_t structure_max = INITSHIP::Get_Ship_Structure(s);
            s_out armor = card.Add_Object("armor");
            armor.Int("now", std::max(0, armor_max - ship.armor_damage));
            armor.Int("max", armor_max);
            s_out structure = card.Add_Object("structure");
            structure.Int("now", std::max(0, structure_max - ship.structural_damage));
            structure.Int("max", structure_max);
            // The systems' share still working: shield and drive by their damage, the computer whole or not.
            const int systems_hurt = (int)ship.shield_damage_percent + (int)ship.drive_damage_percent + (ship.computer_damage > 0 ? 100 : 0);
            s_out systems = card.Add_Object("systems");
            systems.Int("now", std::max(0, 300 - systems_hurt));
            systems.Int("max", 300);
            s_out damaged = card.Add_Object("damaged");
            damaged.Int("shield", ship.shield_damage_percent);
            damaged.Int("drive", ship.drive_damage_percent);
            damaged.Bool("computer", ship.computer_damage > 0);
            const int16_t color = MOX::player[ship.owner].color;
            card.Str("lbx", "SHIPS.LBX");
            card.Int("picture", d.picture_num + color * 50); // as the fleet query's
            s_out weapons = card.Add_List("weapons");
            for (s_ship_weapons& weapon : d.ship_weapon) {
                if (weapon.type == 0) {
                    continue;
                }
                char text[200];
                COLBLDG::Build_Weapon_String(&weapon, text, sizeof(text), 0, (uint8_t)weapon.count);
                s_out item = weapons.Append_Object();
                item.Str("name", Utf8(text, sizeof(text)));
                item.Int("count", weapon.count);
                item.Int("arc", weapon.firing_arc);
                item.Int("ammo", weapon.ammo);
                item.Int("help", TECHDATA::weapons[weapon.type].tech_app_id);
            }
            s_out specials = card.Add_List("specials");
            for (int k = 0; k < 39; ++k) {
                if (struct_::Test_Bit_Field(d.special_device_flags, k)) {
                    s_out special = specials.Append_Object();
                    special.Str("name", Utf8(TECHDATA::specials[k].name, 60));
                    special.Bool("damaged", struct_::Test_Bit_Field(ship.special_device_damage_flags, k) != 0);
                    special.Int("help", TECHDATA::specials[k].tech_app_id);
                }
            }
        }

        void Query_Ship_Detail(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t id = -1;
            if (!args.Int("ship", &id) || id < 0 || id >= MOX::NUM_SHIPS || MOX::ship[id].owner != MOX::PLAYER_NUM ||
                MOX::ship[id].status >= 3) {
                out.Fail("bad_args", "ship: one of the player's ships");
                return;
            }
            const int16_t s = (int16_t)id;
            s_ship_data& ship = MOX::ship[s];
            const uint8_t type = ship.d.ship_type;
            out.Int("ship", s);
            out.Str("name", Utf8(ship.d.name, sizeof(ship.d.name)));
            out.Bool("combat", type == SHIP_TYPE_COMBAT);
            // Colony, outpost and transport ships: their help text, as the screen prints it.
            if (type == SHIP_TYPE_COLONY || type == SHIP_TYPE_TRANSPORT || type == SHIP_TYPE_OUTPOST) {
                out.Str("help", Application_Help(type == SHIP_TYPE_COLONY ? 0x29 : (type == SHIP_TYPE_TRANSPORT ? 0xBD : 0x6D)));
                return;
            }
            int16_t defense = 0;
            int16_t missile_defense = 0;
            int16_t attack = 0;
            INITSHIP::Get_Ship_Combat_Bonuses(s, &defense, &missile_defense, &attack);
            char text[120];
            snprintf(text, sizeof(text), "%s (%u EP)", FLT2::Crew_Description_String(s), (unsigned)ship.crew_experience);
            out.Str("crew", Utf8(text, sizeof(text)));
            out.Str("shield", Utf8(TECHDATA::shields[ship.d.shield_type].name, 40));
            out.Int("attack", attack);
            out.Int("defense", defense);
            out.Int("missile_defense", missile_defense);
            out.Str("attack_label", Utf8(HAROLD::H_Message(0x99), 40));
            out.Str("defense_label", Utf8(HAROLD::H_Message(0x9A), 40));
            if (ship.officer_index >= 0 && ship.officer_index < MAX_LEADERS) {
                s_out officer = out.Add_Object("officer");
                officer.Int("leader", ship.officer_index);
                officer.Str("name", Utf8(OFFICER::Leader_Name(ship.officer_index, 1), 80));
                officer.Str("lbx", "OFFICER.LBX");
                officer.Int("entry", k_officer_portraits + MOX::leaders[ship.officer_index].pict_num);
            }
            Design_Parts(ship.d, out.Add_Object("design"));
            Ship_Card(s, attack, defense, missile_defense, out.Add_Object("card"));
        }

        // The select-combat window's sides (owner, 2026-11-20): the player's ships in orbit there
        // (as COMBFIND gathers a side: location the star, status 0), each with its picture, its
        // damage (what persists between combats: armour, structure, shield, drive, computer and
        // special devices), its officer and its design; and, of every other side there, only what
        // the player can see from orbit: its ships by hull size. With Battle Scanner researched,
        // a rough rating of each side against the player's (the AI's own Ship_Strength_Vs_Ships,
        // read-only), deliberately banded: outmatched, even or superior.
        void Query_Combat_Fleet(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t star = -1;
            if (!args.Int("star", &star) || star < 0 || star > MOX::NUM_STARS) {
                out.Fail("bad_args", "star: a star id");
                return;
            }
            const int16_t player = MOX::PLAYER_NUM;
            out.Int("star", star);
            // `sides` (optional): only these others, the ones in the fight (an ally or a bystander
            // in orbit is not an opposing force).
            bool only[15] = {};
            bool filtered = false;
            if (yyjson_val* list = args.Get("sides"); list != nullptr && yyjson_is_arr(list)) {
                filtered = true;
                size_t at = 0;
                size_t count = 0;
                yyjson_val* side = nullptr;
                yyjson_arr_foreach(list, at, count, side) {
                    const int64_t p = yyjson_get_int(side);
                    if (p >= 0 && p < 15) {
                        only[p] = true;
                    }
                }
            }
            const bool scanner = MOX::player[player].tech_applications[TECH_APP_BATTLE_SCANNER] == TECH_RESEARCH_STATUS_RESEARCHED;
            out.Bool("scanner", scanner);
            uint32_t ours[15] = {};
            uint32_t theirs[15] = {};
            int32_t hulls[15][6] = {};
            bool present[15] = {};
            s_out ships = out.Add_List("ships");
            for (int16_t i = 0; i < MOX::NUM_SHIPS; ++i) {
                s_ship_data& ship = MOX::ship[i];
                if (ship.location != star || ship.status != 0 || ship.owner < 0 || ship.owner >= 15) {
                    continue;
                }
                const int16_t owner = ship.owner;
                if (owner != player) {
                    if (filtered && !only[owner]) {
                        continue;
                    }
                    present[owner] = true;
                    if (ship.d.size >= 0 && ship.d.size < 6) {
                        ++hulls[owner][ship.d.size];
                    }
                    theirs[owner] += AIPOWER::Ship_Strength_Vs_Ships(&game::current, legacy_world::Ship_Id(i), legacy_world::Player_Id(player));
                    continue;
                }
                if (ship.d.ship_type != SHIP_TYPE_COMBAT) {
                    continue; // colony, outpost and transport ships stay out of the fight's list
                }
                s_out item = ships.Append_Object();
                item.Int("ship", i);
                item.Str("name", Utf8(ship.d.name, sizeof(ship.d.name)));
                item.Int("hull", (int8_t)ship.d.size);
                item.Str("hull_name", Utf8(TECHDATA::hull_data[ship.d.size].name, 40));
                const int16_t builder = (int8_t)ship.d.previous_owner;
                const int32_t color = builder >= 0 && builder < MAX_PLAYERS ? MOX::player[builder].color : MAX_PLAYERS;
                item.Int("picture", ship.d.picture_num + color * 50); // SHIPS.LBX, as the fleet query's
                const int16_t armor_max = INITSHIP::Get_Ship_Armor_Hits(i);
                const int16_t structure_max = INITSHIP::Get_Ship_Structure(i);
                item.Int("armor", std::max(0, armor_max - ship.armor_damage));
                item.Int("armor_max", armor_max);
                item.Int("structure", std::max(0, structure_max - ship.structural_damage));
                item.Int("structure_max", structure_max);
                s_out damaged = item.Add_List("damaged");
                if (ship.shield_damage_percent > 0) {
                    damaged.Append_Object().Str("text", "Shield " + std::to_string((int)ship.shield_damage_percent) + "% damaged");
                }
                if (ship.drive_damage_percent > 0) {
                    damaged.Append_Object().Str("text", "Drive " + std::to_string((int)ship.drive_damage_percent) + "% damaged");
                }
                if (ship.computer_damage > 0) {
                    damaged.Append_Object().Str("text", "Computer damaged");
                }
                for (int16_t sp = 1; sp < SPECIAL_COUNT; ++sp) {
                    if (struct_::Test_Bit_Field(ship.special_device_damage_flags, sp) != 0 &&
                        struct_::Test_Bit_Field((uint8_t*)ship.d.special_device_flags, sp) != 0) {
                        damaged.Append_Object().Str("text", Utf8(TECHDATA::specials[sp].name, 60) + " damaged");
                    }
                }
                if (ship.officer_index >= 0 && ship.officer_index < MAX_LEADERS) {
                    Leader_Card(ship.officer_index, player, item.Add_Object("officer"));
                }
                Design_Parts(ship.d, item.Add_Object("design"));
                // Each weapon as the combat panel lists it: count, name with its mods, arc.
                s_out weapons = item.Add_List("weapons");
                for (s_ship_weapons& weapon : ship.d.ship_weapon) {
                    if (weapon.type == 0) {
                        continue;
                    }
                    char text[200];
                    char arc[40];
                    COLBLDG::Build_Weapon_String(&weapon, text, sizeof(text), 1, (uint8_t)weapon.count);
                    DESIGN::Weapon_Arc_String((uint8_t)weapon.firing_arc, arc, sizeof(arc));
                    s_out w = weapons.Append_Object();
                    w.Int("count", weapon.count);
                    w.Str("text", Utf8(text, sizeof(text)));
                    w.Str("arc", Utf8(arc, sizeof(arc)));
                }
                for (int16_t p = 0; p < 15; ++p) {
                    if (p != player) {
                        ours[p] += AIPOWER::Ship_Strength_Vs_Ships(&game::current, legacy_world::Ship_Id(i), legacy_world::Player_Id(p));
                    }
                }
            }
            // The player's own colonies here and their defences (owner, 2026-11-21: "you are
            // attacked" showed the fleet but not the planets' defences).
            s_out colonies = out.Add_List("colonies");
            const size_t orbits = sizeof(MOX::star[star].planet_index) / sizeof(MOX::star[star].planet_index[0]);
            for (size_t orbit = 0; orbit < orbits; ++orbit) {
                const int16_t p = MOX::star[star].planet_index[orbit]; // a copy: the packed star's fields are unaligned
                if (p < 0 || p >= MOX::planet_count) {
                    continue;
                }
                const int16_t colony = MOX::planet[p].colony_index;
                if (colony < 0 || colony >= MOX::NUM_COLONIES || MOX::colony[colony].owner != player) {
                    continue;
                }
                s_out item = colonies.Append_Object();
                item.Int("colony", colony);
                item.Int("planet", p);
                item.Str("name", Utf8(HACCESS::Get_Planet_Name(p), 40));
                s_out defences = item.Add_List("defences");
                for (const std::string& name : antares::native::Colony_Defences(colony)) {
                    defences.Append_Object().Str("name", Utf8(name.c_str(), name.size()));
                }
            }
            s_out sides = out.Add_List("enemies");
            for (int16_t p = 0; p < 15; ++p) {
                if (!present[p]) {
                    continue;
                }
                s_out side = sides.Append_Object();
                side.Int("player", p);
                side.Str("name", p < MAX_PLAYERS ? Utf8(MOX::player[p].race_name, sizeof(MOX::player[p].race_name)) :
                    (p == 8 ? std::string("Antarans") : std::string("Monsters")));
                side.Int("color", p < MAX_PLAYERS && p < MOX::NUM_PLAYERS ? (int64_t)MOX::player[p].color : -1);
                s_out by_hull = side.Add_List("hulls");
                int32_t count = 0;
                for (int16_t h = 0; h < 6; ++h) {
                    if (hulls[p][h] > 0) {
                        s_out hull = by_hull.Append_Object();
                        hull.Int("hull", h);
                        hull.Str("name", Utf8(TECHDATA::hull_data[h].name, 40));
                        hull.Int("count", hulls[p][h]);
                        count += hulls[p][h];
                    }
                }
                side.Int("ships", count);
                if (scanner) {
                    // Ours against theirs, in broad bands only (owner: vague and approximate).
                    const double ratio = theirs[p] == 0 ? 10.0 : (double)ours[p] / (double)theirs[p];
                    side.Str("strength", ratio < 0.67 ? "outmatched" : (ratio > 1.5 ? "superior" : "even"));
                }
            }
        }

        // A star clicked with RELOCATE on (FLT1::Star_Relocation): the first click picks where
        // ships are relocated from, the second where they go (the same star again cancels it);
        // the screen's own refusals and questions.
        void Relocate_Star(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t star = -1;
            if (!args.Int("star", &star) || star < 0 || star >= MOX::NUM_STARS) {
                out.Fail("bad_args", "star: a star id");
                return;
            }
            const int16_t from_before = s_relocate_from;
            FLT1::Star_Relocation(&s_relocate_from, &s_relocate_to, (int16_t)star);
            out.Int("from", s_relocate_from); // -1 again once the second star was taken
            out.Bool("set", from_before >= 0 && s_relocate_from < 0);
            out.Int("relocation", from_before >= 0 && HACCESS::Star_Has_Relocation(from_before, MOX::PLAYER_NUM) ?
                    HACCESS::Relocation(from_before, MOX::PLAYER_NUM) : -1);
        }

        // ---- Relocation from the system pane (owner, 2026-11-20): one system's new ships sent
        // to a star picked on the whole map, set and cleared directly. The Fleets panel's two
        // clicks (relocate_star) stay as the original's way. ----

        // The game's rules for a target (FLT1::Okay_To_Set_Relocate_To_Star), without its
        // messages: visited, not a black hole, not cut off from `from` by one. A monster there
        // is allowed after the game's question.
        bool Relocation_Target_Allowed(int16_t from, int16_t to, const char** reason) {
            const s_star_data& star = MOX::star[to];
            if (star.spectral_class == STAR_CLASS_BLACK_HOLE) {
                *reason = "A black hole";
                return false;
            }
            if (!HAROLD::Player_Has_Visited(MOX::PLAYER_NUM, to)) {
                *reason = "Not explored";
                return false;
            }
            if (struct_::Test_Bit_Field(MOX::star[from].black_hole_blocks, to) != 0) {
                *reason = "A black hole is in the way";
                return false;
            }
            *reason = nullptr;
            return true;
        }

        // relocation_targets {from}: where `from`'s new ships go now, the player's ship speed, and
        // for every star whether it can be the target, its distance and the ships' ETA there.
        void Query_Relocation_Targets(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t from = -1;
            if (!args.Int("from", &from) || from < 0 || from >= MOX::NUM_STARS) {
                out.Fail("bad_args", "from: a star id");
                return;
            }
            const int16_t source = (int16_t)from;
            const int32_t speed = std::max<int32_t>((uint8_t)MOX::player[MOX::PLAYER_NUM].ship_speed, 1);
            out.Int("from", source);
            out.Int("to", HACCESS::Star_Has_Relocation(source, MOX::PLAYER_NUM) ? HACCESS::Relocation(source, MOX::PLAYER_NUM) : -1);
            out.Int("speed", speed);
            s_out list = out.Add_List("stars");
            for (int16_t i = 0; i < MOX::NUM_STARS; ++i) {
                const char* reason = nullptr;
                const bool allowed = i != source && Relocation_Target_Allowed(source, i, &reason);
                int16_t guard = 0;
                const int32_t parsecs = GEO::Parsecs_Between_Stars(source, i);
                s_out item = list.Append_Object();
                item.Int("star", i);
                item.Bool("allowed", allowed);
                if (reason != nullptr) {
                    item.Str("reason", reason);
                }
                item.Bool("guarded", allowed && HAROLD::Star_Guarded_By_Monster(i, &guard));
                item.Int("parsecs", parsecs);
                item.Int("eta", (parsecs + speed - 1) / speed);
            }
        }

        // relocation_set {from, to}: the game's checks with their messages and question
        // (FLT1::Okay_To_Set_Relocate_Star / _To_Star), then the relocation (HACCESS::Set_Relocation).
        void Relocation_Set(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t from = -1;
            int64_t to = -1;
            if (!args.Int("from", &from) || !args.Int("to", &to) || from < 0 || from >= MOX::NUM_STARS || to < 0 ||
                to >= MOX::NUM_STARS || from == to) {
                out.Fail("bad_args", "from, to: two different star ids");
                return;
            }
            const bool ok = FLT1::Okay_To_Set_Relocate_Star((int16_t)from, 0) &&
                FLT1::Okay_To_Set_Relocate_To_Star((int16_t)from, (int16_t)to) != 0;
            mouse::Set_Mouse_List(&MOX::mouse_list_arrow, 1);
            if (ok) {
                HACCESS::Set_Relocation((int16_t)from, MOX::PLAYER_NUM, (int16_t)to);
            }
            out.Bool("set", ok);
        }

        // relocation_clear {from}: `from`'s new ships stay where they are built.
        void Relocation_Clear(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t from = -1;
            if (!args.Int("from", &from) || from < 0 || from >= MOX::NUM_STARS) {
                out.Fail("bad_args", "from: a star id");
                return;
            }
            HACCESS::Clear_Star_Relocation((int16_t)from, MOX::PLAYER_NUM);
            out.Bool("cleared", true);
        }

        // RELOCATE off, or a right click: a half-made relocation is dropped.
        void Relocate_Reset(int16_t, const s_args&, s_out& out) {
            s_relocate_from = -1;
            s_relocate_to = -1;
            out.Bool("reset", true);
        }

    } // namespace

    void Add_Fleet_Panel_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"ship_detail", e_kind::query, ANY_SCREEN, 1, "ship: one of the player's ships", Query_Ship_Detail});
        table->push_back({"combat_fleet", e_kind::query, ANY_SCREEN, 1, "star", Query_Combat_Fleet});
        table->push_back({"relocate_star", e_kind::command, galaxy, 0, "star: the from star, then the to star", Relocate_Star});
        table->push_back({"relocate_reset", e_kind::command, galaxy, 0, "", Relocate_Reset});
        table->push_back({"relocation_targets", e_kind::query, ANY_SCREEN, 1, "from: a star", Query_Relocation_Targets});
        table->push_back({"relocation_set", e_kind::command, galaxy, 0, "from, to: stars", Relocation_Set});
        table->push_back({"relocation_clear", e_kind::command, galaxy, 0, "from: a star", Relocation_Clear});
    }

} // namespace antares::commands::internal
