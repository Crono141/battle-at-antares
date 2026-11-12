#include "pch.h"

#include "antares/commands_internal.h"
#include "game/string_overrides.h"

#include <algorithm>
#include <vector>

// WP3.3a: the native galaxy's queries and commands (contract: notes/antares/native-bridge.md,
// "The interface"; rules inventory: notes/antares/screens/00-main.md and 00-system-popup.md).
//
// The queries return what the legacy map and system box show, as plain values in map units
// (MOX::star[].x/y), with the game's own text where the legacy screen prints text, so a view can
// show the original wording or build its own from the numbers. They read game state and the same
// pure helpers the legacy drawing calls; nothing here draws or loads art. Sprite entries name the
// art a view may ask for (native::Request_Sprite): always the closest zoom level's picture, the
// largest the game has.

namespace antares::commands::internal {
    namespace {

        // BUFFER0.LBX entries (mainscr.cpp, sys.cpp, ships.cpp).
        constexpr int32_t k_star_pictures = 148; // + class * 6 + zoom level + size
        constexpr int32_t k_star_class_stride = 6;
        constexpr int32_t k_box_background = 0x49;     // the system box
        constexpr int32_t k_sun_pictures = 0x53;       // + class
        constexpr int32_t k_planet_pictures = 0x5C;    // + climate * sizes + size; gas giant after
        constexpr int32_t k_player_fleet_icons = 205;  // + colour * 4 + (3 - zoom level)
        constexpr int32_t k_monster_fleet_icons = 237; // + (3 - zoom level)
        constexpr int32_t k_antaran_fleet_icons = 241; // + kind * 4 + zoom level
        constexpr int32_t k_orbit_rings = 0x4D;        // + slot (288x154)
        constexpr int32_t k_asteroid_belts = 0x5B;     // frame = slot
        constexpr int32_t k_box_player_fleets = 273;   // + colour
        constexpr int32_t k_box_monster_fleet = 281;
        constexpr int32_t k_box_antaran_fleets = 282; // + kind
        // The system box's layout in its picture's 1x pixels (sys.cpp: x_offset 0x0D, y_offset 0x2F).
        constexpr int32_t k_box_rings_x = 0x0D + 0x10;
        constexpr int32_t k_box_rings_y = 0x2F + 0x0C;
        constexpr int32_t k_box_planet_x = 0x0D + 0x9F;
        constexpr int32_t k_box_planet_y = 0x2F + 0x58;
        constexpr int32_t k_portal_icon = 21; // the system box's gate icons
        constexpr int32_t k_artemis_icon = 33;
        // OFFICER.LBX: colony leaders' portraits (OFFICER::Load_Officer_Picture, picture type 1).
        constexpr int32_t k_officer_portraits = 0xD2;
        // OFFICER.LBX: the skill icons, one per skill pair (OFFICER::Load_Officer_Skill_Icons).
        constexpr int32_t k_officer_skill_icons = 0x58;
        // COLONY2.LBX: government icons (+ government) and morale masks (COLONY::Govt_Anim, C_Anims).
        constexpr int32_t k_government_icons = 0x13;
        constexpr int32_t k_morale_happy = 0x10;
        constexpr int32_t k_morale_unhappy = 0x11;
        // STARBG.LBX
        constexpr int32_t k_nebula_pictures = 6; // + type * 4 + zoom level

        constexpr int16_t k_antaran_first_owner = 9;
        constexpr int16_t k_antaran_last_owner = 14;

        bool Valid_Star(int64_t star) {
            return star >= 0 && star < MOX::NUM_STARS;
        }

        bool Valid_Player(int16_t player) {
            return player >= 0 && player < MOX::NUM_PLAYERS && player < MAX_PLAYERS;
        }

        std::string Message(int16_t id) {
            return Utf8(HAROLD::H_Message(id), 400);
        }

        // An H_Message with one %s filled in (the legacy code passes the star itself, whose name
        // is its first field).
        std::string Message_With(int16_t id, const char* text) {
            char buffer[200];
            snprintf(buffer, sizeof(buffer), HAROLD::H_Message(id), text);
            return Utf8(buffer, sizeof(buffer));
        }

        std::string Star_Name(int16_t star) {
            return Utf8(MOX::star[star].name, sizeof(MOX::star[star].name));
        }

        int32_t Player_Color(int16_t player) {
            return Valid_Player(player) ? (int32_t)MOX::player[player].color : -1;
        }

        bool Omniscient() {
            return HAROLD::Player_Is_Omniscient(MOX::PLAYER_NUM);
        }

        // What the legacy box calls viewable: SYS::Ok_To_View_System, with the galaxy's
        // look_at_colonies (0).
        bool Viewable(int16_t star) {
            return SYS::Ok_To_View_System(star, MOX::PLAYER_NUM);
        }

        // The native system panel stands for the legacy system box (moveable box 0), whose record
        // is saved with the game: which star it shows, where it sits and that it is on top
        // (savegame.cpp writes moveable_box[] and moveable_box_order[]). Opening and closing the
        // panel keeps that record as MAINSCR::Do_System_Popup and MOVEBOX::Vanish_System_Display_Box
        // do, so a save made with the panel open is the save the legacy galaxy would make, and the
        // legacy galaxy (Ctrl+G) finds the box where the panel was opened.
        void Record_System_Box(int16_t star) {
            FLEETPOP::Set_Fleet_Movement_Icon_Selections(0, 0); // a later star click can't move ships
            MOX::moveable_box[0].id = star;
            if (MOVEBOX::Moveable_Box_Selected(2)) {
                MOVEBOX::Vanish_Fleet_Movement_Box(); // the system box replaces the fleet box (WP3.3c)
            }
            MAINSCR::Set_Popup_Draw_XY(0, star, &MOX::moveable_box[0].x, &MOX::moveable_box[0].y);
            MOVEBOX::Move_Box_To_Top(0);
        }

        // ---- galaxy ----

        // The star's label as Print_Star_Names shows it (MAINSCR::Get_Star_Name): hidden until
        // visited unless a colony there is known; the owner's colour when known; outposts in lower
        // case and dimmed; "(name)" for galactic lore about unvisited stars.
        void Star_Label(int16_t i, s_out& item) {
            const s_star_data& star = MOX::star[i];
            const bool lore = Omniscient();
            const int16_t owner = star.owner;
            const bool owner_known = Valid_Player(owner) && (lore || HAROLD::Contact_With_One_Colony(i) != 0);
            std::string label;
            if (star.spectral_class != STAR_CLASS_BLACK_HOLE) {
                if (owner_known || lore || HAROLD::Player_Has_Visited(MOX::PLAYER_NUM, i) != 0) {
                    label = Star_Name(i);
                    if (lore && owner != MOX::PLAYER_NUM && HAROLD::Player_Has_Visited(MOX::PLAYER_NUM, i) == 0) {
                        label = "(" + label + ")";
                    }
                }
            }
            const bool outpost = !label.empty() && HAROLD::Star_Is_Outpost_Star(i);
            if (outpost) {
                for (char& c : label) {
                    c = (char)tolower((unsigned char)c);
                }
            }
            item.Str("label", label);
            item.Int("label_owner", owner_known ? owner : -1);
            item.Bool("outpost", outpost);
            // Several empires' colonies: the legacy map colours the name in parts.
            if (!label.empty() && HAROLD::N_Colony_Owners(i) > 1) {
                int16_t counts[MAX_PLAYERS] = {};
                int16_t total = 0;
                HAROLD::Colony_Owners(i, counts, &total, !lore);
                s_out shares = item.Add_List("label_shares");
                for (int16_t p = 0; p < MAX_PLAYERS; ++p) {
                    if (counts[p] > 0) {
                        s_out share = shares.Append_Object();
                        share.Int("player", p);
                        share.Int("colonies", counts[p]);
                    }
                }
            }
        }

        void Query_Galaxy(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            out.Int("width", MOX::MAP_MAX_X);
            out.Int("height", MOX::MAP_MAX_Y);
            out.Int("player", MOX::PLAYER_NUM);
            out.Bool("omniscient", Omniscient());
            out.Bool("relocation_lines", MOX::settings.show_relocation_lines != 0);
            s_out players = out.Add_List("players");
            for (int16_t p = 0; p < MOX::NUM_PLAYERS && p < MAX_PLAYERS; ++p) {
                s_out item = players.Append_Object();
                item.Int("id", p);
                item.Int("color", MOX::player[p].color);
                item.Str("race", Utf8(MOX::player[p].race_name, sizeof(MOX::player[p].race_name)));
                item.Bool("contact", p == MOX::PLAYER_NUM || HAROLD::Contact_With_Player(MOX::PLAYER_NUM, p));
            }

            s_player* me = &MOX::player[MOX::PLAYER_NUM];
            s_out stars = out.Add_List("stars");
            for (int16_t i = 0; i < MOX::NUM_STARS; ++i) {
                s_star_data& star = MOX::star[i];
                s_out item = stars.Append_Object();
                const bool black_hole = star.spectral_class == STAR_CLASS_BLACK_HOLE;
                const bool visited = HAROLD::Player_Has_Visited(MOX::PLAYER_NUM, i) != 0;
                item.Int("id", i);
                item.Int("x", star.x);
                item.Int("y", star.y);
                item.Int("size", star.size);
                item.Int("class", star.spectral_class);
                item.Int("owner", star.owner);
                item.Int("color", Player_Color(star.owner));
                item.Bool("visited", visited);
                item.Bool("black_hole", black_hole);
                // The closest zoom level's picture: stars by size, black holes by zoom alone.
                item.Int("sprite", k_star_pictures + star.spectral_class * k_star_class_stride + (black_hole ? 0 : star.size));
                item.Int("dimension", MOX::star_fields_dim[black_hole ? 0 : star.size]);
                Star_Label(i, item);
                // Wormhole links show from visited ends (Draw_Wormhole_Links).
                item.Int("wormhole", star.wormhole_star_id >= 0 && (visited || Omniscient()) ? star.wormhole_star_id : -1);
                item.Int("relocation", HACCESS::Star_Has_Relocation(i, MOX::PLAYER_NUM) ? HACCESS::Relocation(i, MOX::PLAYER_NUM) : -1);
                // The player's own gates here (Draw_A_Gate_Icon: visited stars only).
                item.Bool("portal", visited && HAROLD::Player_Has_Dimensional_Portal(me, &star));
                item.Bool("artemis", visited && HAROLD::Player_Has_Artemis_Net(me, &star));
            }

            s_out nebulae = out.Add_List("nebulae");
            for (uint16_t i = 0; i < (uint16_t)MOX::NUM_NEBULAS; ++i) {
                const s_nebula& nebula = MOX::nebula[i];
                s_out item = nebulae.Append_Object();
                item.Int("type", nebula.type);
                item.Int("x", nebula.x); // the picture's top-left
                item.Int("y", nebula.y);
                item.Int("sprite", k_nebula_pictures + ((uint8_t)nebula.type % 12) * 4);
            }

            // Influence (MAINSCR::Draw_Influence_Overlay): one disc per known colony star, its
            // radius from the owner's ship range, in map units.
            int16_t max_non_thorium_range = 0;
            for (int16_t f = FUEL_NO_FUEL_CELLS; f < FUEL_THORIUM_FUEL_CELLS; ++f) {
                max_non_thorium_range = std::max<int16_t>(max_non_thorium_range, (int16_t)TECHDATA::fuel[f].ship_range);
            }
            (void)max_non_thorium_range; // only the extended (huge galaxy) view caps the range
            s_out influence = out.Add_List("influence");
            for (int16_t i = 0; i < MOX::NUM_STARS; ++i) {
                const int16_t owner = MOX::star[i].owner;
                if (!Valid_Player(owner)) {
                    continue;
                }
                const bool known = Omniscient() || HAROLD::Player_Has_Visited(MOX::PLAYER_NUM, i) != 0 ||
                    HAROLD::Contact_With_One_Colony(i) != 0;
                if (!known || (owner != MOX::PLAYER_NUM && !HAROLD::Contact_With_Player(MOX::PLAYER_NUM, owner))) {
                    continue;
                }
                const int16_t range = MOX::player[owner].ship_range;
                int32_t radius = MAP_SCALE::Influence_Overlay_Map_Radius_(range);
                if (range <= 0) {
                    radius += MOX::star_fields_dim[MOX::star[i].size] / 2; // pre-warp: the star's own size
                }
                s_out item = influence.Append_Object();
                item.Int("star", i);
                item.Int("owner", owner);
                item.Int("color", Player_Color(owner));
                item.Int("radius", radius);
            }
        }

        // ---- fleets ----

        void Query_Fleets(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            s_out fleets = out.Add_List("fleets");
            for (int16_t i = 0; i < MOX::ship_icon_count; ++i) {
                const s_ship_icon& icon = MOX::ship_icon[i];
                const int16_t node = icon.node_idx;
                if (node < 0) {
                    continue;
                }
                const int16_t ship_id = MOX::ship_node[node].ship_idx;
                const s_ship_data& ship = MOX::ship[ship_id];
                const int16_t owner = ship.owner;
                s_out item = fleets.Append_Object();
                item.Int("icon", i);
                item.Int("node", node);
                item.Int("ship", ship_id); // the stack's first ship: what the fleet verbs take
                item.Int("owner", owner);
                item.Int("color", Player_Color(owner));
                int32_t sprite = -1;
                if (owner < MAX_PLAYERS) {
                    item.Str("kind", "empire");
                    sprite = k_player_fleet_icons + Player_Color(owner) * 4 + 3;
                } else if (owner == MAX_PLAYERS) {
                    item.Str("kind", "monster");
                    sprite = k_monster_fleet_icons + 3;
                } else {
                    item.Str("kind", "antaran");
                    if (owner >= k_antaran_first_owner && owner <= k_antaran_last_owner) {
                        sprite = k_antaran_fleet_icons + (owner - k_antaran_first_owner) * 4;
                    }
                }
                item.Int("sprite", sprite);
                item.Int("x", ship.x);
                item.Int("y", ship.y);
                const bool moving = ship.status == 1;
                item.Bool("moving", moving);
                // At a star: its id; the orbit slot the legacy map gives the icon (0 the first
                // empire at the star, 1 the second, 2-4 more; 5 in space).
                item.Int("star", moving ? -1 : HAROLD::XY_To_Star_Id(ship.x, ship.y));
                item.Int("slot", icon.stack_slot);
                int16_t count = 0;
                for (int16_t n = node; n != -1 && count < MAX_SHIPS; n = MOX::ship_node[n].next_node) {
                    ++count;
                }
                item.Int("ships", count);

                // Destination lines and ETA (SHIPS::Do_Ship_Destination_Lines; the fleet box's
                // own selection line comes with WP3.3c).
                const int16_t location = ship.location;
                const bool on_route = location >= SHIP_LOCATION_MOVING_OFFSET && location < SHIP_LOCATION_LIMIT;
                const int16_t destination = on_route ? HAROLD::Absolute_Location(location) : -1;
                const bool mine = owner == MOX::PLAYER_NUM;
                bool line = false;
                if (mine) {
                    line = HAROLD::Absolute_Location(location) != MOX::NUM_STARS && on_route;
                } else if (ship.status != 0 && on_route) {
                    line = MOX::settings.enemy_moves != 0 || SHIPS::Enemy_Ship_Heading_Toward_Our_Colony(node);
                }
                if (destination == MOX::NUM_STARS) {
                    line = false; // Antares
                }
                item.Int("destination", line ? destination : -1);
                item.Str("line", line ? (mine ? "own" : "enemy") : "");
                const bool eta = line && location < SHIP_LOCATION_WORMHOLE_OFFSET;
                item.Int("eta", eta ? (int64_t)(uint8_t)ship.turns_left : -1);
            }
        }

        // ---- system ----

        // The lines the legacy box prints for a hovered planet (SYS::Do_Print_Planet_Info).
        void Planet_Lines(int16_t planet_id, bool show_buildings, s_out& item) {
            const s_planet_data& planet = MOX::planet[planet_id];
            s_out lines = item.Add_List("lines");
            const auto line = [&](const std::string& text) {
                s_out entry = lines.Append_Object();
                entry.Str("text", text);
            };
            const int16_t colony_id = planet.colony_index;
            const s_colony* colony = colony_id > -1 ? &MOX::colony[colony_id] : nullptr;
            if (planet.planet_type == PLANET_TYPE_GAS_GIANT) {
                line(Message(0x15f));
                return;
            }
            if (planet.planet_type == PLANET_TYPE_ASTEROID) {
                line(Message(0x160));
                return;
            }
            std::string title = Utf8(HACCESS::Get_Planet_Name_No_Prime(planet_id), 80);
            if (colony != nullptr && Valid_Player(colony->owner)) {
                title += " (" + Utf8(MOX::player[colony->owner].race_name, sizeof(MOX::player[0].race_name));
                title += colony->outpost_flag != 0 ? Message(0x161) : std::string(")");
            }
            line(title);
            const uint8_t climate = (uint8_t)(colony != nullptr ? colony->climate : planet.climate);
            line(Utf8(MOX::planet_size_string[planet.size], 40) + ", " + Utf8(MOX::planet_climate_string[climate], 40));
            const int16_t max_population =
                COLCALC::Planet_Max_Population_For_Player(const_cast<s_planet_data*>(&planet), MOX::PLAYER_NUM);
            char buffer[120];
            if (colony != nullptr) {
                snprintf(buffer, sizeof(buffer), HAROLD::H_Message(0x181), (uint32_t)(uint8_t)colony->n_pops, (int32_t)max_population);
            } else {
                snprintf(buffer, sizeof(buffer), HAROLD::H_Message(0x162), (int32_t)max_population);
            }
            line(Utf8(buffer, sizeof(buffer)));
            std::string minerals = Utf8(MOX::mineral_class_string[planet.mineral_class], 40);
            if (planet.gravity_class != PLANET_GRAVITY_NORMAL_G) {
                minerals += ", " + Utf8(MOX::planet_gravity_string[planet.gravity_class], 40);
            }
            line(minerals);
            if (colony != nullptr && show_buildings) {
                static const int16_t k_special_buildings[7] = {BUILDING_STAR_FORTRESS, BUILDING_STAR_BASE, BUILDING_BATTLESTATION, BUILDING_MISSILE_BASE, BUILDING_GROUND_BATTERIES, BUILDING_STELLAR_CONVERTER, BUILDING_FIGHTER_GARRISON};
                for (const int16_t building : k_special_buildings) {
                    if (colony->buildings[building] != 0) {
                        line(Utf8(TECHDATA::buildings[building].name, 40));
                    }
                }
            }
            if (planet.planet_special != 0 && planet.planet_special != 0xb) {
                line(Utf8(MOX::planet_special_string[planet.planet_special], 80));
            }
        }

        // A skill's help text, as OFFICER::Print_Officer_Skill_Help shows it on a right click: the
        // skill descriptions file's record (OFFICER::Load_Officer_Skill_Name_Descriptions: data
        // entry 1, records of 0xFF bytes, string overrides 28 on) with the leader's name and the
        // bonus (10 and 15 for skills 28 and 29). Read from the file here: the screen's copy is only
        // loaded while the leaders screen is up.
        std::string Skill_Description(int16_t leader, int16_t skill, int16_t bonus) {
            char lbx[20];
            OFFICER::Get_Skill_Description_LBX_Name(lbx, sizeof(lbx));
            s_vfs_file* file = vfs::fopen_lbx(lbx, 1);
            if (file == nullptr) {
                return {};
            }
            uint16_t header[2] = {};
            char text[0x100] = {};
            const int16_t record = skill / 2;
            bool ok = vfs::fread(header, sizeof(header), 1, file) == 1 && record < header[0] && header[1] <= 0xFF;
            if (ok) {
                ok = vfs::fseek(file, (long)(sizeof(header) + (size_t)record * header[1]), SEEK_SET) == 0 &&
                    vfs::fread(text, header[1], 1, file) == 1;
            }
            vfs::fclose(file);
            if (!ok) {
                return {};
            }
            text[0xFF] = '\0';
            string_overrides::Apply_String(lbx, (size_t)27 + record + 1, text, 0xFF);
            char name[80];
            const s_leader_data& data = MOX::leaders[leader];
            snprintf(name, sizeof(name), "%s", data.name);
            if (MOX::settings.language != 4 && data.title[0] != '\0') {
                strlcat(name, ",", sizeof(name));
            }
            const int value = skill == 28 ? 10 : (skill == 29 ? 15 : bonus);
            char out[256];
            snprintf(out, sizeof(out), text, name, value);
            return Utf8(out, sizeof(out));
        }

        void Query_System(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t star_id = -1;
            if (!args.Int("star", &star_id) || !Valid_Star(star_id)) {
                out.Fail("bad_args", "star: a star id");
                return;
            }
            const int16_t star = (int16_t)star_id;
            const s_star_data& data = MOX::star[star];
            out.Int("star", star);
            out.Str("name", Star_Name(star));
            out.Int("class", data.spectral_class);
            out.Int("box_sprite", k_box_background);
            out.Int("sun_sprite", k_sun_pictures + data.spectral_class);

            // Ships here (SYS::Build_System_Popup_Ships: one per empire, from the map's icons).
            bool my_ships_here = false;
            s_out fleets = out.Add_List("fleets");
            for (int16_t i = 0; i < MOX::ship_icon_count; ++i) {
                const int16_t node = MOX::ship_icon[i].node_idx;
                if (node < 0) {
                    continue;
                }
                const s_ship_data& ship = MOX::ship[MOX::ship_node[node].ship_idx];
                if (ship.location != star || ship.status >= 3) {
                    continue;
                }
                my_ships_here = my_ships_here || ship.owner == MOX::PLAYER_NUM;
                int16_t total = 0;
                int16_t colony_ships = 0;
                int16_t outposts = 0;
                int16_t transports = 0;
                int16_t hulls[SHIP_SIZE_DOOM_STAR + 1] = {};
                SYS::N_Ships_By_Type_In_Stack(node, &total, &colony_ships, &outposts, &transports, hulls);
                s_out item = fleets.Append_Object();
                item.Int("icon", i);
                item.Int("node", node);
                item.Int("ship", MOX::ship_node[node].ship_idx); // the stack's first ship: what the fleet verbs take
                item.Int("owner", ship.owner);
                item.Int("color", Player_Color(ship.owner));
                if (Valid_Player(ship.owner)) {
                    item.Str("race", Utf8(MOX::player[ship.owner].race_name, sizeof(MOX::player[0].race_name)));
                } else {
                    item.Str("name", Utf8(ship.d.name, sizeof(ship.d.name))); // monsters, Antarans
                }
                // The box's own fleet picture (SYS::Buffer_Reload_System_Ships).
                int32_t box_sprite = -1;
                if (ship.owner < MAX_PLAYERS) {
                    box_sprite = k_box_player_fleets + Player_Color(ship.owner);
                } else if (ship.owner == MAX_PLAYERS) {
                    box_sprite = k_box_monster_fleet;
                } else if (ship.owner < 15) {
                    box_sprite = k_box_antaran_fleets + (ship.owner - k_antaran_first_owner);
                }
                item.Int("box_sprite", box_sprite);
                item.Int("ships", total);
                item.Int("colony_ships", colony_ships);
                item.Int("outpost_ships", outposts);
                item.Int("transports", transports);
                s_out sizes = item.Add_List("hulls"); // frigate .. doom star
                for (const int16_t count : hulls) {
                    sizes.Append_Int(count);
                }
                // The lines the box prints on hover (SYS::Do_Print_System_Ship_Data).
                s_out lines = item.Add_List("lines");
                const auto line = [&](const std::string& text) {
                    s_out entry = lines.Append_Object();
                    entry.Str("text", text);
                };
                if (!Valid_Player(ship.owner)) {
                    line(Utf8(ship.d.name, sizeof(ship.d.name)));
                } else {
                    line(Message_With(0x163, MOX::player[ship.owner].race_name));
                    for (int16_t hull = 0; hull <= SHIP_SIZE_DOOM_STAR; ++hull) {
                        if (hulls[hull] > 0) {
                            const char* name = hulls[hull] == 1 ? TECHDATA::hull_data[hull].name : TECHDATA::hull_data[hull].size_name;
                            line(std::to_string(hulls[hull]) + " " + Utf8(name, 40));
                        }
                    }
                    char text[120];
                    const int16_t groups[3][3] = {{colony_ships, 0x164, 0x165}, {outposts, 0x166, 0x167}, {transports, 0x168, 0x169}};
                    for (const auto& group : groups) {
                        if (group[0] > 0) {
                            snprintf(text, sizeof(text), HAROLD::H_Message(group[0] == 1 ? group[1] : group[2]), (int32_t)group[0]);
                            line(Utf8(text, sizeof(text)));
                        }
                    }
                }
            }

            if (!Viewable(star)) {
                const bool monster = HAROLD::Player_Sighted_Monster(MOX::PLAYER_NUM, star);
                out.Str("view", monster ? "monster" : "hidden");
                out.Str("title", HAROLD::Contact_With_One_Colony(star) != 0 || monster ? Message_With(0x16a, data.name) : Message(0x16b));
                if (!monster) {
                    out.Str("description", Message(data.spectral_class + 0x0f));
                }
                return;
            }
            out.Str("view", "full");
            out.Str("title", Message_With(0x16a, data.name));
            // The player's colony leader here (v3): the colony screen's officer
            // (COLONY::Load_Officer_Anim; portrait OFFICER.LBX 0xD2 + pict_num).
            const int8_t governor = MOX::star[star].officer_index[MOX::PLAYER_NUM];
            if (governor >= 0 && governor < MAX_LEADERS && MOX::leaders != nullptr) {
                const s_leader_data& leader = MOX::leaders[governor];
                s_out g = out.Add_Object("governor");
                g.Int("leader", governor);
                g.Str("name", Utf8(leader.name, sizeof(leader.name)));
                g.Str("title", Utf8(leader.title, sizeof(leader.title)));
                g.Int("level", leader.level);
                g.Int("portrait", k_officer_portraits + leader.pict_num);
                // v4: the skills the officer screen lists for a colony leader (OFFICER::
                // Print_Officer_Data): colony skills, then general ones, each with its icon, its
                // name and its bonus as the screen prints it.
                s_out skills = g.Add_List("skills");
                const int16_t owner = static_cast<int8_t>(leader.player_index);
                for (int pass = 0; pass < 2; ++pass) {
                    for (int16_t i = 0; i < 54; ++i) {
                        const s_skill_data& skill = MOX::skill_data[i];
                        const bool has = pass == 0
                            ? skill.skill_type == 2 && OFFICER::Officer_Has_Special_Skill(owner, governor, i)
                            : skill.skill_type == 0 && OFFICER::Officer_Has_General_Skill(owner, governor, i);
                        if (!has || skill.name == nullptr) {
                            continue;
                        }
                        char bonus[68] = {};
                        if (skill.format_str != nullptr) {
                            snprintf(bonus, sizeof(bonus), skill.format_str,
                                static_cast<int32_t>(static_cast<int16_t>(OFFICER::Officer_Skill_Bonus(owner, governor, i))));
                        }
                        s_out item = skills.Append_Object();
                        item.Int("skill", i);
                        item.Str("name", Utf8(skill.name, 64));
                        item.Str("bonus", Utf8(bonus, sizeof(bonus)));
                        item.Int("icon", k_officer_skill_icons + i / 2);
                        item.Str("description", Skill_Description(governor, i,
                            static_cast<int16_t>(OFFICER::Officer_Skill_Bonus(owner, governor, i))));
                    }
                }
            }
            out.Int("ring_x", k_box_rings_x);
            out.Int("ring_y", k_box_rings_y);
            if (data.wormhole_star_id > -1) {
                const int16_t other = data.wormhole_star_id;
                const bool known = HAROLD::Player_Has_Visited(MOX::PLAYER_NUM, other) != 0;
                s_out wormhole = out.Add_Object("wormhole");
                wormhole.Int("star", known ? other : -1);
                wormhole.Str("text", known ? Message_With(0x16c, MOX::star[other].name) : Message(0x16d));
            }

            const bool show_buildings = my_ships_here || HAROLD::Player_Has_Colony(MOX::PLAYER_NUM, star) ||
                MOX::player[MOX::PLAYER_NUM].traits[TRAIT_OMNISCIENCE] != 0;
            s_out planets = out.Add_List("planets");
            for (int16_t slot = 0; slot < 5; ++slot) {
                const int16_t planet_id = data.planet_index[slot];
                if (planet_id < 0 || planet_id >= MOX::planet_count) {
                    continue;
                }
                const s_planet_data& planet = MOX::planet[planet_id];
                s_out item = planets.Append_Object();
                item.Int("slot", slot);
                item.Int("planet", planet_id);
                item.Int("orbit", planet.orbit);
                item.Str("type", planet.planet_type == PLANET_TYPE_GAS_GIANT ? "gas_giant" : planet.planet_type == PLANET_TYPE_ASTEROID ? "asteroids"
                                                                                                                                        : "planet");
                item.Int("size", planet.size);
                const int16_t colony_id = planet.colony_index;
                const bool colonised = colony_id > -1 && colony_id < MOX::NUM_COLONIES;
                const s_colony* colony = colonised ? &MOX::colony[colony_id] : nullptr;
                const uint8_t climate = (uint8_t)(colony != nullptr && colony->outpost_flag == 0 ? colony->climate : planet.climate);
                item.Int("climate", climate);
                item.Int("gravity", planet.gravity_class);
                item.Int("minerals", planet.mineral_class);
                item.Int("special", planet.planet_special);
                item.Int("max_population", COLCALC::Planet_Max_Population_For_Player(const_cast<s_planet_data*>(&planet), MOX::PLAYER_NUM));
                // The box's words for them (v3; Planet_Lines has them as sentences).
                if (planet.planet_type == PLANET_TYPE_PLANET) {
                    item.Str("name", Utf8(HACCESS::Get_Planet_Name_No_Prime(planet_id), 80));
                    item.Str("size_name", Utf8(MOX::planet_size_string[planet.size], 40));
                    item.Str("climate_name", Utf8(MOX::planet_climate_string[climate], 40));
                    item.Str("minerals_name", Utf8(MOX::mineral_class_string[planet.mineral_class], 40));
                    if (planet.gravity_class != PLANET_GRAVITY_NORMAL_G) {
                        item.Str("gravity_name", Utf8(MOX::planet_gravity_string[planet.gravity_class], 40));
                    }
                } else {
                    item.Str("name", Message(planet.planet_type == PLANET_TYPE_GAS_GIANT ? 0x15f : 0x160));
                }
                // The rotating picture SYS::Get_Rotating_Planet_Segment picks.
                int8_t size = planet.size;
                if (size > PLANET_SIZE_LARGE) {
                    size = PLANET_SIZE_HUGE;
                }
                const int32_t picture = planet.planet_type == PLANET_TYPE_GAS_GIANT
                    ? (int32_t)PLANET_CLIMATE_COUNT * (int32_t)PLANET_SIZE_COUNT
                    : (int32_t)climate * (int32_t)PLANET_SIZE_COUNT + size;
                item.Int("sprite", planet.planet_type == PLANET_TYPE_ASTEROID ? -1 : k_planet_pictures + picture);
                // Where the box draws it, in the box picture's 1x pixels (SYS::Load_New_System_Display,
                // Draw_System_Display_Popup): the orbit ring, then the planet at its place on the
                // orbit for this stardate, rotating from frame slot * 5.
                const int8_t dim_size = planet.planet_type == PLANET_TYPE_GAS_GIANT ? (int8_t)PLANET_SIZE_HUGE : planet.size;
                const int32_t dimension = MOX::rot_plan_dim[dim_size];
                int32_t orbit_x = 0;
                int32_t orbit_y = 0;
                int32_t orbit_frame = 0;
                GEO::Get_Orbit_Display_X_Y(star, (int32_t)planet.orbit, &orbit_x, &orbit_y, &orbit_frame);
                item.Int("dimension", dimension);
                item.Int("box_x", k_box_planet_x + orbit_x - dimension / 2);
                item.Int("box_y", k_box_planet_y + orbit_y - dimension / 2);
                item.Int("first_frame", slot * 5);
                item.Int("ring_sprite", planet.planet_type == PLANET_TYPE_ASTEROID ? k_asteroid_belts : k_orbit_rings + slot);
                item.Int("ring_frame", planet.planet_type == PLANET_TYPE_ASTEROID ? slot : 0);
                if (colony != nullptr) {
                    s_out c = item.Add_Object("colony");
                    c.Int("id", colony_id);
                    c.Int("owner", colony->owner);
                    c.Int("color", Player_Color(colony->owner));
                    c.Bool("outpost", colony->outpost_flag != 0);
                    c.Int("population", (uint8_t)colony->n_pops);
                    c.Bool("mine", colony->owner == MOX::PLAYER_NUM);
                    if (colony->owner == MOX::PLAYER_NUM && colony->outpost_flag == 0) {
                        // The colony screen's morale row (COLDRAW::Draw_Info_Morale_Both): the
                        // government's icon, then morale / 2 masks, none from Unification up.
                        const int8_t government = MOX::player[MOX::PLAYER_NUM].traits[TRAIT_CURRENT_GOVERNMENT];
                        const int16_t masks = government >= GOVERNMENT_UNIFICATION ? 0 : (int16_t)((int8_t)colony->morale / 2);
                        c.Int("government", government);
                        c.Int("government_sprite", k_government_icons + government);
                        c.Int("morale", (int8_t)colony->morale);
                        c.Int("morale_icons", masks);
                        c.Int("morale_sprite", masks >= 0 ? k_morale_happy : k_morale_unhappy);
                    }
                }
                Planet_Lines(planet_id, show_buildings, item);
            }

            // Gates here and who owns one (SYS::Draw_Gate_Icons, Print_Gate_Info).
            s_star_data* star_data = &MOX::star[star];
            s_out gates = out.Add_Object("gates");
            gates.Int("portal_sprite", k_portal_icon);
            gates.Int("artemis_sprite", k_artemis_icon);
            gates.Bool("portal", HAROLD::Star_Has_Dimensional_Portal(star_data));
            gates.Bool("artemis", HAROLD::Star_Has_Artemis_Net(star_data));
            s_out portal_owners = gates.Add_List("portal_owners");
            s_out artemis_owners = gates.Add_List("artemis_owners");
            for (int16_t p = 0; p < MOX::NUM_PLAYERS && p < MAX_PLAYERS; ++p) {
                if (HAROLD::Player_Has_Dimensional_Portal(&MOX::player[p], star_data)) {
                    portal_owners.Append_Int(p);
                }
                if (HAROLD::Player_Has_Artemis_Net(&MOX::player[p], star_data)) {
                    artemis_owners.Append_Int(p);
                }
            }
        }

        // The right-click popup (SYS::Potential_Colony_Info_Popup) as values and its lines.
        void Query_Potential_Colony(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t planet_id = -1;
            if (!args.Int("planet", &planet_id) || planet_id < 0 || planet_id >= MOX::planet_count) {
                out.Fail("bad_args", "planet: a planet id");
                return;
            }
            s_planet_data* planet = &MOX::planet[planet_id];
            if (planet->planet_type == PLANET_TYPE_GAS_GIANT) {
                out.Fail("refused", "gas giants have no colony information");
                return;
            }
            s_potential_colony_info info{};
            COLCALC::Compute_Potential_Colony_Info(&MOX::player[MOX::PLAYER_NUM], planet, &info);
            const int16_t max_population = COLCALC::Planet_Max_Population_For_Player(planet, MOX::PLAYER_NUM);
            char food[16];
            COLCALC::Format_Food2((uint16_t)info.food, food, sizeof(food));
            out.Int("planet", planet_id); // v2: which planet the answer is for
            out.Str("title", Message_With(0x178, HACCESS::Get_Planet_Name_No_Prime((int16_t)planet_id)));
            out.Int("max_population", max_population);
            out.Str("food_per_farmer", Utf8(food, sizeof(food)));
            out.Int("industry_per_worker", info.industry);
            out.Int("research_per_scientist", info.research);
            out.Int("maintenance", info.maintenance);   // percent; below 0 a bonus
            out.Int("worker_bonus", info.worker_bonus); // percent; below 0 a penalty
            s_out lines = out.Add_List("lines");
            const auto line = [&](const std::string& label, const std::string& value) {
                s_out entry = lines.Append_Object();
                entry.Str("label", label);
                entry.Str("value", value);
            };
            line(Message(0x170), std::to_string(max_population));
            line(Message(0x171), Utf8(food, sizeof(food)));
            line(Message(0x172), std::to_string(info.industry));
            line(Message(0x173), std::to_string(info.research));
            if (info.maintenance == 0) {
                line(Message(0x174), "--");
            } else {
                line(Message(info.maintenance <= 0 ? 0x189 : 0x18a), std::to_string(abs((int)info.maintenance)) + "%");
            }
            if (info.worker_bonus == 0) {
                line(Message(0x176), "--");
            } else {
                line(Message(info.worker_bonus > 0 ? 0x18b : 0x18c), std::to_string(abs((int)info.worker_bonus)) + "%");
            }
        }

        // The player's ship design slots (s_player::ship_designs; 0-4 are the build list's combat
        // designs, 5 the Doom Star slot): what a design list shows, and the slot `open` takes.
        void Query_Designs(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const s_player& player = MOX::player[MOX::PLAYER_NUM];
            s_out list = out.Add_List("designs");
            for (int16_t slot = 0; slot < 6; ++slot) {
                const s_ship_design& design = player.ship_designs[slot];
                if (design.name[0] == 0 || design.size < 0 || design.size > SHIP_SIZE_DOOM_STAR) {
                    continue;
                }
                s_out item = list.Append_Object();
                item.Int("slot", slot);
                item.Str("name", Utf8(design.name, sizeof(design.name)));
                item.Int("hull", design.size);
                item.Str("hull_name", TECHDATA::hull_data[design.size].name);
                // KEN::Do_Get_Ship_Picture_Seg: SHIPS.LBX, picture + the designer's colour * 50.
                item.Int("picture", design.picture_num + player.color * 50);
                item.Int("cost", design.cost);
            }
        }

        // The legacy map's view, saved with the game: where a native view starts.
        void Query_View(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            out.Int("x", MOX::cur_map_x); // map units at the window's top-left
            out.Int("y", MOX::cur_map_y);
            out.Int("scale", MOX::cur_map_scale); // map units per 10 pixels of the 506x400 map window
            out.Int("max_scale", MOX::max_map_scale);
            out.Int("width", MOX::MAP_MAX_X);
            out.Int("height", MOX::MAP_MAX_Y);
            out.Bool("auto_select_colony", MOX::settings.auto_select_colony != 0);
        }

        // The Alt+F1..F8 game options (MOX::settings), for the options menu.
        void Query_Options(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const struct {
                int option;
                int16_t message;
                uint8_t value;
            } options[] = {{0, 0xa6, MOX::settings.end_of_turn_summary}, {1, 0xa7, MOX::settings.end_of_turn_wait}, {2, 0xa8, MOX::settings.enemy_moves}, {4, 0xaa, MOX::settings.auto_select_ships}, {5, 0xab, MOX::settings.animations_on}, {6, 0xac, MOX::settings.auto_select_colony}, {7, 0xad, MOX::settings.show_relocation_lines}};
            s_out list = out.Add_List("options");
            for (const auto& option : options) {
                s_out item = list.Append_Object();
                item.Int("option", option.option);
                item.Str("name", Message(option.message));
                item.Bool("on", option.value != 0);
            }
            // v2 (WP3.3g): the game menu's volume sliders, 0-100 (0: off).
            out.Int("music", MOX::settings.music_on != 0 ? MOX::settings.music_level : 0);
            out.Int("sound", MOX::settings.sound_fx_on != 0 ? MOX::settings.sound_fx_level : 0);
        }

        // ---- Research (WP3.3g: the research flyout) ----

        // The research areas in the change-research screen's order (TECH::entry_to_group, two
        // columns of four), each named as the screen's art labels it.
        struct s_research_area {
            uint8_t group;
            const char* name;
        };
        constexpr s_research_area k_research_areas[8] = {{4, "Construction"}, {2, "Power"}, {6, "Chemistry"},
            {8, "Sociology"}, {7, "Computers"}, {1, "Biology"}, {3, "Physics"}, {5, "Force Fields"}};

        // A field whose every application comes at once: creative races in any field, everyone in
        // these (TECH::Draw_Little_Arrow and Display_Entry_Text point at all of its applications).
        bool Gives_All_Applications(const s_player& player, int16_t field) {
            return player.traits[TRAIT_CREATIVE] == 1 || field == 0x37 || field == 0x39 || field == 0x1D || field == 0x16 ||
                field == 0x1C || field == 0x17;
        }

        struct s_research_choice {
            int16_t field = 0;
            std::vector<int16_t> apps; // available applications; empty: none left in the field
            int32_t cost = 0;
        };

        // One area's field as the change-research screen offers it (TECH::Init_Entry_Data with the
        // current field cleared, as TECH::_Tech_Select does when changing: the field being
        // researched is offered too, and the cost already paid comes off every field).
        s_research_choice Research_Choice(const s_player& player, uint8_t group) {
            s_research_choice choice;
            int16_t field = MOX::first_field_in_group[group];
            while (field != 0 && player.tech_fields[field] != 2) {
                field = TECHDATA::technology_fields[field].next_field_id;
            }
            if (field == 0) {
                return choice;
            }
            choice.field = field;
            if (field < TECH_FIELD_BIOLOGY) {
                for (const int16_t app : TECHDATA::technology_fields[field].tech) {
                    if (app > 0 && app < TECH_APP_COUNT && player.tech_applications[app] == TECH_RESEARCH_STATUS_AVAILABLE) {
                        choice.apps.push_back(app);
                    }
                }
            } else {
                choice.apps.push_back(TECH::Get_Hyper_Tech_App_ID(field)); // a hyper-advanced field
            }
            const int32_t cost = COLCALC::Player_Research_Cost(const_cast<s_player*>(&player), field) - player.research_accumulated;
            choice.cost = cost < 0 ? 0 : cost;
            return choice;
        }

        void Query_Research(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            const s_player& player = MOX::player[MOX::PLAYER_NUM];
            out.Int("field", (uint8_t)player.current_research_field);
            out.Int("app", (uint8_t)player.current_research_application); // stored in a signed byte
            out.Int("accumulated", player.research_accumulated);
            out.Bool("creative", player.traits[TRAIT_CREATIVE] == 1);
            char none[80] = {};
            JIM::Get_Text_Message("billtext.lbx", 62, none, sizeof(none)); // "no applications left"
            s_out areas = out.Add_List("areas");
            for (const s_research_area& area : k_research_areas) {
                const s_research_choice choice = Research_Choice(player, area.group);
                s_out item = areas.Append_Object();
                item.Int("group", area.group);
                item.Str("area", area.name);
                item.Int("field", choice.field);
                if (choice.field == 0) {
                    continue; // all researched
                }
                item.Str("field_name", Utf8(TECH::Technology_Fields_Name(choice.field), 62));
                item.Int("cost", choice.cost);
                item.Bool("all", Gives_All_Applications(player, choice.field)); // one pick, or all of them
                s_out apps = item.Add_List("apps");
                for (const int16_t app : choice.apps) {
                    s_out a = apps.Append_Object();
                    a.Int("app", app);
                    a.Str("name", Utf8(TECH::Technology_Applications_Name(app, MOX::PLAYER_NUM), 64));
                }
                if (choice.apps.empty()) {
                    item.Str("none", Utf8(none, sizeof(none)));
                }
            }
        }

        // A choice from the research flyout, as the change-research screen commits one
        // (TECH::Commit_Selected_Entry, then TECH::Tech_Change: a new field loses the breakthrough).
        void Choose_Research(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t field = 0;
            int64_t app = 0;
            if (!args.Int("field", &field) || !args.Int("app", &app)) {
                out.Fail("bad_args", "field and app: one of the research query's choices");
                return;
            }
            s_player& player = MOX::player[MOX::PLAYER_NUM];
            for (const s_research_area& area : k_research_areas) {
                const s_research_choice choice = Research_Choice(player, area.group);
                if (choice.field != field || std::find(choice.apps.begin(), choice.apps.end(), (int16_t)app) == choice.apps.end()) {
                    continue;
                }
                const int8_t previous = player.current_research_field;
                player.current_research_field = (int8_t)field;
                player.current_research_application = (int8_t)app;
                if (player.current_research_field != previous) {
                    player.research_breakthrough = 0;
                }
                out.Int("field", (uint8_t)player.current_research_field);
                out.Int("app", (uint8_t)player.current_research_application);
                return;
            }
            out.Fail("refused", "not one of the research choices");
        }

        // The game menu's settings page's volume sliders (LOADSAVE::Set_Music_For_Game_Popup and
        // Set_Sound_For_Game_Popup, on 0-100 instead of the slider's 0-155): 5 or less is off. Saved
        // with the session settings, as leaving the game menu does.
        void Set_Volume(int16_t, const s_args& args, s_out& out) {
            int64_t music = -1;
            int64_t sound = -1;
            if ((args.Has("music") && (!args.Int("music", &music) || music < 0 || music > 100)) ||
                (args.Has("sound") && (!args.Int("sound", &sound) || sound < 0 || sound > 100)) ||
                (music < 0 && sound < 0)) {
                out.Fail("bad_args", "music and / or sound: 0-100");
                return;
            }
            if (music >= 0) {
                int16_t level = (int16_t)music;
                if (level <= 5) {
                    level = 0;
                    sound::Stop_Stream();
                    JIM::Stop_Background_Music();
                    MOX::settings.music_on = 0;
                } else if (MOX::settings.music_on == 0) {
                    sound::Stream_On();
                    MOX::settings.music_on = 1;
                    JIM::Play_Background_Music();
                }
                MOX::settings.music_level = (int8_t)level;
                sound::Set_Stream_Max_Volume(level);
                sound::Set_Stream_Volume(level);
            }
            if (sound >= 0) {
                MOX::settings.sound_fx_level = (int8_t)sound;
                if (sound > 5) {
                    if (MOX::settings.sound_fx_on == 0) {
                        sound::Sound_On();
                    }
                    MOX::settings.sound_fx_on = 1;
                    sound::Set_Sound_Max_Volume(MOX::settings.sound_fx_level);
                } else {
                    MOX::settings.sound_fx_level = 0;
                    sound::Set_Sound_Max_Volume(0);
                    MOX::settings.sound_fx_on = 0;
                }
            }
            LOADSAVE::Save_Session_Related_Settings();
            out.Int("music", MOX::settings.music_on != 0 ? MOX::settings.music_level : 0);
            out.Int("sound", MOX::settings.sound_fx_on != 0 ? MOX::settings.sound_fx_level : 0);
        }

        // ---- Commands ----

        // A colony screen for one of the player's colonies, or the outpost message
        // (MAINSCR::Do_Colony_Screen). Reports what happened.
        void Open_Colony_Screen(int16_t colony, s_out& out) {
            const bool outpost = MOX::colony[colony].outpost_flag != 0;
            int16_t exit_flag = 0;
            MAINSCR::Do_Colony_Screen(colony, &exit_flag); // the outpost message is a request
            out.Str("opened", outpost ? "none" : "colony");
            out.Int("colony", colony);
        }

        // What a click on a star does in the legacy galaxy with no ships selected
        // (mainscr_main.cpp, Main_Screen): with auto-select colony on, the player's colony there
        // (HACCESS::Last_Colony_Selected, which may write star.last_planet_selected), else the
        // system box (MAINSCR::Do_System_Popup's non-drawing part).
        void Select_Star_Id(int16_t star, s_out& out) {
            int16_t colony = -1;
            if (MOX::settings.auto_select_colony != 0 && MOX::star[star].spectral_class != STAR_CLASS_BLACK_HOLE) {
                colony = HACCESS::Last_Colony_Selected(star, MOX::PLAYER_NUM);
            }
            out.Int("star", star);
            if (colony != -1) {
                Open_Colony_Screen(colony, out);
                return;
            }
            Record_System_Box(star);
            // The box's drawing sets the start for F5/F6 when the system is viewable (sys.cpp).
            if (Viewable(star)) {
                MOX::system_star_id_for_auto_select = star;
            }
            out.Str("opened", "system");
        }

        void Select_Star(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t star = -1;
            if (!args.Int("star", &star) || !Valid_Star(star)) {
                out.Fail("bad_args", "star: a star id");
                return;
            }
            Select_Star_Id((int16_t)star, out);
        }

        // A click on a planet in the system box: the player's own colonies open their screen,
        // outposts show their message, other planets do nothing.
        void Open_Colony(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t planet = -1;
            if (!args.Int("planet", &planet) || planet < 0 || planet >= MOX::planet_count) {
                out.Fail("bad_args", "planet: a planet id");
                return;
            }
            const int16_t colony = MOX::planet[planet].colony_index;
            if (colony < 0 || colony >= MOX::NUM_COLONIES || MOX::colony[colony].owner != MOX::PLAYER_NUM) {
                out.Fail("refused", "not one of the player's colonies");
                return;
            }
            Open_Colony_Screen(colony, out);
        }

        // F5 / F6: the next or previous star with one of the player's colonies, from the last
        // system shown (mainscr.cpp, the -1006 and -1005 keys).
        void Cycle_Colony_Star(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t direction = 0;
            if (!args.Int("dir", &direction) || (direction != 0 && direction != 1)) {
                out.Fail("bad_args", "dir: 0 next, 1 previous");
                return;
            }
            MAINSCR::Cycle_Star_Icons(&MOX::system_star_id_for_auto_select, MOX::PLAYER_NUM, (int16_t)direction);
            const int16_t star = MOX::system_star_id_for_auto_select;
            if (!Valid_Star(star)) {
                out.Str("opened", "none");
                return;
            }
            int16_t colony = -1;
            if (MOX::settings.auto_select_colony != 0) {
                colony = HACCESS::Last_Colony_Selected(star, MOX::PLAYER_NUM);
            }
            out.Int("star", star);
            if (colony > -1) {
                Open_Colony_Screen(colony, out);
                return;
            }
            Record_System_Box(star);
            out.Str("opened", "system");
        }

        // The panel's Close (the box's close button and Esc).
        void Close_System(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            MOVEBOX::Vanish_System_Display_Box();
        }
        // Alt+F1..F8 (MAINSCR::Toggle_Game_Option): end-of-turn summary, end-of-turn wait, enemy
        // moves, (3 unused), auto-select ships, animations, auto-select colony, relocation lines.
        // Saved with the session settings; the ON/OFF message is a request. Turning auto-select
        // colony on also sets every star's colony choice (HACCESS::Set_All_Colony_Auto_Select_Flags).
        void Toggle_Option(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t option = -1;
            if (!args.Int("option", &option) || option < 0 || option > 7 || option == 3) {
                out.Fail("bad_args", "option: 0-7, not 3");
                return;
            }
            MAINSCR::Toggle_Game_Option((int16_t)option);
            const uint8_t values[8] = {MOX::settings.end_of_turn_summary, MOX::settings.end_of_turn_wait, MOX::settings.enemy_moves, 0, MOX::settings.auto_select_ships, MOX::settings.animations_on, MOX::settings.auto_select_colony, MOX::settings.show_relocation_lines};
            out.Int("option", option);
            out.Bool("on", values[option] != 0);
        }
    } // namespace

    void Add_Galaxy_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"select_star", e_kind::command, galaxy, 0, "star: star id", Select_Star});
        table->push_back({"open_colony", e_kind::command, galaxy, 0, "planet: planet id", Open_Colony});
        table->push_back({"close_system", e_kind::command, galaxy, 0, "", Close_System});
        table->push_back({"cycle_colony_star", e_kind::command, galaxy, 0, "dir: 0 next, 1 previous", Cycle_Colony_Star});
        table->push_back({"toggle_option", e_kind::command, galaxy, 0, "option: 0-7 (Alt+F1..F8), not 3", Toggle_Option});
        table->push_back({"galaxy", e_kind::query, ANY_SCREEN, 1, "", Query_Galaxy});
        table->push_back({"fleets", e_kind::query, ANY_SCREEN, 2, "", Query_Fleets});
        table->push_back({"system", e_kind::query, ANY_SCREEN, 4, "star: star id", Query_System});
        table->push_back({"designs", e_kind::query, ANY_SCREEN, 1, "", Query_Designs});
        table->push_back({"potential_colony", e_kind::query, ANY_SCREEN, 2, "planet: planet id", Query_Potential_Colony});
        table->push_back({"options", e_kind::query, ANY_SCREEN, 2, "", Query_Options});
        table->push_back({"set_volume", e_kind::command, ANY_SCREEN, 0, "music, sound: 0-100", Set_Volume});
        table->push_back({"research", e_kind::query, ANY_SCREEN, 1, "", Query_Research});
        table->push_back({"choose_research", e_kind::command, galaxy, 0, "field, app: a research choice", Choose_Research});
        table->push_back({"view", e_kind::query, ANY_SCREEN, 1, "", Query_View});
    }

} // namespace antares::commands::internal
