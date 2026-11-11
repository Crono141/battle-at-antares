#include "pch.h"

#include "antares/fleet_orders.h"

#include "antares/commands_internal.h"

#include <algorithm>

// WP3.3c: the galaxy's fleet box and fleet orders (screens/00-fleet-orders.md) as commands and
// queries. The native fleet panel owns its selection and sends ship ids with each order (porting
// note 1); every order calls the rules functions the legacy box calls, in the same order, and
// rebuilds the map's fleet icons afterwards as the legacy galaxy does.

namespace antares::fleet {

    void Consume_Ship(int16_t ship_idx) {
        for (int16_t leader = 0; leader < MAX_LEADERS; ++leader) {
            const s_leader_data& officer = MOX::leaders[leader];
            if (officer.type != LEADER_TYPE_SHIP || officer.location != ship_idx || officer.status == 5) {
                continue;
            }
            // Aboard (the ship's officer) or on the way to it (as SHIPS::Kill_Ship finds them).
            if (MOX::ship[ship_idx].officer_index == leader || officer.eta > 0) {
                OFFICER::Move_Officer_To_Pool(leader);
            }
        }
        MOX::ship[ship_idx].status = 5;
    }

} // namespace antares::fleet

namespace antares::commands::internal {

    namespace {

        std::string Message(int16_t id) {
            return Utf8(HAROLD::H_Message(id), 400);
        }

        std::string Star_Name(int16_t star) {
            return Utf8(MOX::star[star].name, sizeof(MOX::star[star].name));
        }

        bool Ship_Alive(int16_t ship) {
            return MOX::ship[ship].status < 5;
        }

        // The map's stacks (ship_stack_start, ship_node) are rebuilt by SHIPS::Build_Ship_Icons,
        // which the native galaxy runs on entry and after every order, as the legacy galaxy does.
        int16_t Stack_Of(int16_t ship) {
            for (int16_t stack = 0; stack < (int16_t)MOX::ship_stack_count; ++stack) {
                int16_t guard = 0;
                for (int16_t node = MOX::ship_stack_start[stack]; node != -1 && guard < MAX_SHIPS;
                     node = MOX::ship_node[node].next_node, ++guard) {
                    if (MOX::ship_node[node].ship_idx == ship) {
                        return stack;
                    }
                }
            }
            return -1;
        }

        // The stack's ships in node order: the order of the legacy box's icons.
        std::vector<int16_t> Stack_Ships(int16_t stack) {
            std::vector<int16_t> ships;
            for (int16_t node = MOX::ship_stack_start[stack]; node != -1 && (int16_t)ships.size() < MAX_SHIPS;
                 node = MOX::ship_node[node].next_node) {
                ships.push_back(MOX::ship_node[node].ship_idx);
            }
            return ships;
        }

        int16_t Head_Ship(int16_t stack) {
            return MOX::ship_node[MOX::ship_stack_start[stack]].ship_idx;
        }

        // The map icon that shows the stack, or -1.
        int16_t Icon_Of(int16_t stack) {
            for (int16_t icon = 0; icon < MOX::ship_icon_count; ++icon) {
                if (MOX::ship_icon[icon].node_idx == MOX::ship_stack_start[stack]) {
                    return icon;
                }
            }
            return -1;
        }

        // A stack is at a star when its head ship's location is a star (the context buttons'
        // test); 20000+ (arriving through a wormhole or gate) counts as that star for colonising.
        int16_t Stack_Star(int16_t stack, bool arriving_counts) {
            const s_ship_data& head = MOX::ship[Head_Ship(stack)];
            const int16_t location = head.location;
            if (location > -1 && location < SHIP_LOCATION_MOVING_OFFSET && location < MOX::NUM_STARS) {
                return location;
            }
            if (arriving_counts && location > SHIP_LOCATION_WORMHOLE_OFFSET && location < SHIP_LOCATION_LIMIT) {
                return HAROLD::XY_To_Star_Id(head.x, head.y);
            }
            return -1;
        }

        void Rebuild_Icons() {
            SHIPS::Build_Ship_Icons();
            SHIPS::Set_Ship_Icon_XYs();
        }

        // ---- Arguments ----

        bool Read_Ship(const s_args& args, const char* key, s_out& out, int16_t* ship) {
            int64_t id = -1;
            if (!args.Int(key, &id) || id < 0 || id >= MOX::NUM_SHIPS) {
                out.Fail("bad_args", std::string(key) + ": a ship id");
                return false;
            }
            if (!Ship_Alive((int16_t)id)) {
                out.Fail("refused", "ship " + std::to_string(id) + " is gone");
                return false;
            }
            *ship = (int16_t)id;
            return true;
        }

        // A ship of the player's (any ship of the stack), and its stack.
        bool Read_Own_Stack(const s_args& args, s_out& out, int16_t* stack) {
            int16_t ship = -1;
            if (!Read_Ship(args, "fleet", out, &ship)) {
                return false;
            }
            if (MOX::ship[ship].owner != MOX::PLAYER_NUM) {
                out.Fail("refused", "not one of the player's fleets");
                return false;
            }
            *stack = Stack_Of(ship);
            if (*stack < 0) {
                out.Fail("refused", "the ship is not on the map");
                return false;
            }
            return true;
        }

        // Ships the player can order now, all in one stack, returned in the stack's order (the
        // order HACCESS::Get_Fleet_Box_Selected_Ship_Ids lists a legacy selection in), -1 ended.
        bool Read_Orderable_Ships(const s_args& args, s_out& out, std::vector<int16_t>* list, int16_t* stack_out,
            int16_t only_type = -1) {
            std::vector<int64_t> ids;
            if (!args.Int_List("ships", &ids) || ids.empty() || ids.size() >= MAX_SHIPS) {
                out.Fail("bad_args", "ships: a list of ship ids");
                return false;
            }
            int16_t stack = -1;
            std::vector<int16_t> given;
            for (const int64_t id : ids) {
                if (id < 0 || id >= MOX::NUM_SHIPS) {
                    out.Fail("bad_args", "no ship " + std::to_string(id));
                    return false;
                }
                const int16_t ship = (int16_t)id;
                if (std::find(given.begin(), given.end(), ship) != given.end()) {
                    out.Fail("bad_args", "ship " + std::to_string(id) + " is listed twice");
                    return false;
                }
                if (!Ship_Alive(ship) || MOX::ship[ship].owner != MOX::PLAYER_NUM ||
                    !HACCESS::Player_Can_Order_Ship(MOX::PLAYER_NUM, &MOX::ship[ship])) {
                    out.Fail("refused", "ship " + std::to_string(id) + " can't be given orders");
                    return false;
                }
                if (only_type >= 0 && MOX::ship[ship].d.ship_type != only_type) {
                    out.Fail("refused", "ship " + std::to_string(id) + " is not of the right kind");
                    return false;
                }
                const int16_t at = Stack_Of(ship);
                if (at < 0 || (stack >= 0 && at != stack)) {
                    out.Fail("refused", "the ships are not one fleet");
                    return false;
                }
                stack = at;
                given.push_back(ship);
            }
            list->clear();
            for (const int16_t ship : Stack_Ships(stack)) {
                if (std::find(given.begin(), given.end(), ship) != given.end()) {
                    list->push_back(ship);
                }
            }
            list->push_back(-1);
            *stack_out = stack;
            return true;
        }

        bool Read_Star(const s_args& args, s_out& out, int16_t* star) {
            int64_t id = -1;
            if (!args.Int("star", &id) || id < 0 || id >= MOX::NUM_STARS) {
                out.Fail("bad_args", "star: a star id");
                return false;
            }
            *star = (int16_t)id;
            return true;
        }

        // ---- The box's status line (FLEETPOP::Print_Fleet_Box_Info) ----

        struct s_line {
            std::string text;
            bool warning = false;
        };

        // The info types of Print_Fleet_Box_Info: 1 at a star, 2 turns to a star (or Antares),
        // 3 turns to an unexplored star, 4 black hole, 5 immobile, 6/7 out of range, 8 flux.
        s_line Info_Line(int16_t type, int16_t value, int16_t destination) {
            char text[120];
            text[0] = '\0';
            s_line line;
            switch (type) {
                case 1:
                    snprintf(text, sizeof(text), HAROLD::H_Message(0x67), MOX::star[value].name);
                    break;
                case 2:
                    if (destination == MOX::NUM_STARS) {
                        strlcpy(text, HAROLD::H_Message(0x68), sizeof(text));
                    } else {
                        snprintf(text, sizeof(text), HAROLD::H_Message(value == 1 ? 0x69 : 0x6a), value,
                            MOX::star[destination].name);
                    }
                    break;
                case 3:
                    snprintf(text, sizeof(text), HAROLD::H_Message(value == 1 ? 0x6b : 0x6c), value);
                    break;
                case 4:
                    strlcpy(text, HAROLD::H_Message(0x6d), sizeof(text));
                    line.warning = true;
                    break;
                case 5:
                    strlcpy(text, HAROLD::H_Message(0x6e), sizeof(text));
                    line.warning = true;
                    break;
                case 6:
                    snprintf(text, sizeof(text), HAROLD::H_Message(value == 1 ? 0x6f : 0x70), value,
                        MOX::star[destination].name);
                    line.warning = true;
                    break;
                case 7:
                    if (value == 0) {
                        strlcpy(text, HAROLD::H_Message(0x71), sizeof(text));
                    } else {
                        snprintf(text, sizeof(text), HAROLD::H_Message(0xeb), value);
                    }
                    line.warning = true;
                    break;
                case 8:
                    strlcpy(text, HAROLD::H_Message(0x73), sizeof(text));
                    line.warning = true;
                    break;
                default:
                    break;
            }
            line.text = Utf8(text, sizeof(text));
            return line;
        }

        // Where the stack is or goes, with nothing hovered.
        s_line Fleet_Line(int16_t head) {
            if (MOX::ship[head].status == 0) {
                return Info_Line(1, MOX::ship[head].location, -1);
            }
            int16_t value = -1;
            int16_t destination = -1;
            int16_t type = 9;
            FLEETPOP::Get_Absolute_Location_Scan_Info(head, &value, &destination, &type);
            return Info_Line(type, value, destination);
        }

        // The line for a hovered star (the box's ETA preview).
        s_line Preview_Line(const s_ship_move_info& info, int16_t star) {
            const bool visited = HAROLD::Player_Has_Visited(MOX::PLAYER_NUM, star) != 0;
            if (info.moving != 0) {
                if (info.turns_left == 0) {
                    return Info_Line(1, star, -1);
                }
                return visited ? Info_Line(2, info.turns_left, star) : Info_Line(3, info.turns_left, -1);
            }
            if (info.blackhole_blocks != 0) {
                return Info_Line(4, 0, -1);
            }
            if (info.immobile != 0) {
                return Info_Line(5, 0, -1);
            }
            if (info.out_of_range != 0) {
                return visited ? Info_Line(6, info.parsecs, star) : Info_Line(7, info.parsecs, -1);
            }
            if (info.hyperspace_flux != 0) {
                return Info_Line(8, 0, -1);
            }
            return {};
        }

        // The name the box shows for a ship (FLEETPOP::Get_Fleetpop_Scanned_Ship_Info): the
        // design name for the player's ships and non-combat ships, the hull for others' warships.
        std::string Ship_Label(int16_t ship_idx) {
            const s_ship_data& ship = MOX::ship[ship_idx];
            const int16_t owner = ship.owner;
            if (owner != MOX::PLAYER_NUM && owner <= 8 && HACCESS::Is_Combat_Ship(ship_idx)) {
                const int16_t hull = (int8_t)ship.d.size;
                return Utf8(TECHDATA::hull_data[hull].name, sizeof(TECHDATA::hull_data[hull].name));
            }
            return Utf8(ship.d.name, sizeof(ship.d.name));
        }

        const char* Ship_Type_Name(uint8_t type) {
            switch (type) {
                case SHIP_TYPE_COMBAT:
                    return "combat";
                case SHIP_TYPE_COLONY:
                    return "colony";
                case SHIP_TYPE_TRANSPORT:
                    return "transport";
                case SHIP_TYPE_OUTPOST:
                    return "outpost";
                default:
                    return "other";
            }
        }

        // ---- Planets a fleet's ships can settle (MAINPUPS::Evaluate_Selection_Input's rules) ----

        enum class e_target { colonize,
            outpost,
            land_troops };

        bool Target_Allowed(e_target kind, int16_t planet_idx) {
            if (planet_idx < 0) {
                return false;
            }
            const s_planet_data& planet = MOX::planet[planet_idx];
            const int16_t colony = planet.colony_index;
            switch (kind) {
                case e_target::colonize:
                    return planet.planet_type == PLANET_TYPE_PLANET && (colony == -1 || MOX::colony[colony].outpost_flag != 0);
                case e_target::outpost:
                    return colony == -1;
                case e_target::land_troops:
                    return colony != -1 && MOX::colony[colony].outpost_flag == 0 && MOX::colony[colony].owner == MOX::PLAYER_NUM;
            }
            return false;
        }

        void Add_Targets(s_out& out, const char* key, e_target kind, int16_t star) {
            s_out list = out.Add_List(key);
            if (star < 0) {
                return;
            }
            for (int16_t slot = 0; slot < 5; ++slot) {
                const int16_t planet = MOX::star[star].planet_index[slot];
                if (!Target_Allowed(kind, planet)) {
                    continue;
                }
                s_out item = list.Append_Object();
                item.Int("planet", planet);
                item.Int("slot", slot);
                item.Str("name", Utf8(HACCESS::Get_Planet_Name(planet), 80));
            }
        }

        // ---- Queries ----

        // fleet {ship}: the stack holding the ship, as the legacy fleet box shows it.
        void Query_Fleet(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t ship = -1;
            if (!Read_Ship(args, "ship", out, &ship)) {
                return;
            }
            const int16_t stack = Stack_Of(ship);
            if (stack < 0) {
                out.Fail("refused", "the ship is not on the map");
                return;
            }
            const std::vector<int16_t> ships = Stack_Ships(stack);
            const int16_t head = ships.front();
            const s_ship_data& lead = MOX::ship[head];
            const int16_t owner = lead.owner;
            const bool mine = owner == MOX::PLAYER_NUM;
            out.Int("stack", stack);
            out.Int("ship", head);
            out.Int("icon", Icon_Of(stack));
            out.Int("owner", owner);
            out.Int("color", owner < MAX_PLAYERS && owner < MOX::NUM_PLAYERS ? (int64_t)MOX::player[owner].color : -1);
            char title[80];
            if (owner < 9) {
                char race[40];
                HACCESS::Race_Name(owner, race, sizeof(race));
                snprintf(title, sizeof(title), HAROLD::H_Message(0x66), race);
                out.Str("kind", owner < MAX_PLAYERS ? "empire" : "monster");
            } else {
                strlcpy(title, lead.d.name, sizeof(title));
                strings::String_To_Upper(title);
                out.Str("kind", "antaran");
            }
            out.Str("title", Utf8(title, sizeof(title)));
            out.Int("x", lead.x);
            out.Int("y", lead.y);
            out.Bool("moving", lead.status == 1);
            out.Int("star", lead.status == 1 ? -1 : HAROLD::XY_To_Star_Id(lead.x, lead.y));
            const s_line line = Fleet_Line(head);
            out.Str("status", line.text);
            out.Bool("auto_select", mine && MOX::settings.auto_select_ships != 0);

            s_out list = out.Add_List("ships");
            for (const int16_t id : ships) {
                const s_ship_data& item_ship = MOX::ship[id];
                s_out item = list.Append_Object();
                item.Int("id", id);
                item.Str("name", Ship_Label(id));
                item.Str("type", Ship_Type_Name(item_ship.d.ship_type));
                item.Int("hull", (int8_t)item_ship.d.size);
                // KEN::Do_Get_Ship_Picture_Seg: SHIPS.LBX, picture + colour of the builder * 50.
                const int16_t builder = (int8_t)item_ship.d.previous_owner;
                const int32_t color = builder >= 0 && builder < MAX_PLAYERS ? MOX::player[builder].color : MAX_PLAYERS;
                item.Int("picture", item_ship.d.picture_num + color * 50);
                item.Bool("orderable", mine && HACCESS::Player_Can_Order_Ship(MOX::PLAYER_NUM, &MOX::ship[id]));
                item.Int("officer", mine ? item_ship.officer_index : -1);
            }

            // The context buttons (FLEETPOP::Set_Added_Button_Stuff), by the stack (D25 argument form).
            const int16_t count = (int16_t)ships.size();
            s_out buttons = out.Add_Object("buttons");
            const bool colonize = FLEETPOP::Set_Colonize_Button_Flag(stack, count) != 0;
            const bool outpost = FLEETPOP::Set_Build_Outpost_Button_Flag(stack, count) != 0;
            const bool troops = FLEETPOP::Set_Transport_Troops_Flag(stack, count) != 0;
            buttons.Bool("colonize", colonize);
            buttons.Bool("outpost", outpost);
            buttons.Bool("land_troops", troops);
            buttons.Bool("engage", FLEETPOP::Set_Engage_Enemy_Forces_Flag(stack, count) != 0);
            buttons.Bool("attack_antares", FLEETPOP::Set_Attack_Antares_Button_Flag(stack));
            s_out targets = out.Add_Object("targets");
            Add_Targets(targets, "colonize", e_target::colonize, colonize ? Stack_Star(stack, true) : -1);
            Add_Targets(targets, "outpost", e_target::outpost, outpost ? Stack_Star(stack, false) : -1);
            Add_Targets(targets, "land_troops", e_target::land_troops, troops ? Stack_Star(stack, false) : -1);
        }

        // move_preview {ships, star}: the route check the box runs for a hovered star.
        void Query_Move_Preview(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            std::vector<int16_t> list;
            int16_t stack = -1;
            int16_t star = -1;
            if (!Read_Orderable_Ships(args, out, &list, &stack) || !Read_Star(args, out, &star)) {
                return;
            }
            if (MOX::star[star].spectral_class == STAR_CLASS_BLACK_HOLE) {
                // The legacy box shows nothing over a black hole (Main_Screen: eta_star_id -1).
                out.Bool("moving", false);
                out.Str("line", "");
                out.Str("text", "");
                return;
            }
            s_ship_move_info info{};
            SHIPMOVE::Ships_Try_To_Move_To(list.data(), star, &info);
            out.Bool("moving", info.moving != 0);
            out.Int("turns", info.turns_left);
            out.Bool("out_of_range", info.out_of_range != 0);
            out.Int("parsecs", info.parsecs);
            out.Bool("black_hole", info.blackhole_blocks != 0);
            out.Bool("immobile", info.immobile != 0);
            out.Bool("flux", info.hyperspace_flux != 0);
            out.Bool("wormhole", info.uses_wormhole != 0);
            out.Bool("stargate", info.has_stargate_route != 0);
            out.Bool("jump_gate", info.has_jumpgate_route != 0);
            // MAINSCR::Draw_ETA_Destination_Line: green when moving, red when blocked.
            const char* color = "";
            if (info.moving != 0 && info.turns_left != 0) {
                color = "ok";
            } else if (info.blackhole_blocks != 0 || info.hyperspace_flux != 0 ||
                       (info.out_of_range != 0 && info.immobile == 0)) {
                color = "blocked";
            }
            out.Str("line", color);
            const s_line line = Preview_Line(info, star);
            out.Str("text", line.text);
            out.Bool("warning", line.warning);
        }

        // next_fleet {fleet, dir}: F1/F2 (MAINSCR::Cycle_Ship_Icons): the next stack of the same
        // owner that is alive, wrapping; 0 forward, 1 back.
        void Query_Next_Fleet(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t ship = -1;
            int64_t dir = 0;
            if (!Read_Ship(args, "fleet", out, &ship)) {
                return;
            }
            if (!args.Int("dir", &dir) || (dir != 0 && dir != 1)) {
                out.Fail("bad_args", "dir: 0 next, 1 previous");
                return;
            }
            const int16_t start = Stack_Of(ship);
            const int16_t count = (int16_t)MOX::ship_stack_count;
            const int16_t owner = MOX::ship[ship].owner;
            int16_t found = -1;
            for (int16_t step = 1; start >= 0 && step <= count && found < 0; ++step) {
                const int16_t stack = (int16_t)(dir == 0 ? (start + step) % count : (start - step + count) % count);
                const int16_t head = Head_Ship(stack);
                if (MOX::ship[head].owner == owner && MOX::ship[head].status < 3) {
                    found = stack;
                }
            }
            out.Int("ship", found >= 0 ? Head_Ship(found) : -1);
        }

        // ---- Commands ----

        // The box record's size, as the legacy draw sets it for the stack shown
        // (FLEETPOP::Fleet_Box_Data: width, height, and a position kept on screen). The legacy
        // galaxy does this the first time it draws the box after opening it or after an order;
        // without it the record kept the size of the box open when the game was saved.
        void Size_Box() {
            const int16_t stack = MOX::fleet_box_ship_stack;
            if (!MOVEBOX::Moveable_Box_Selected(2) || stack < 0 || stack >= (int16_t)MOX::ship_stack_count) {
                return;
            }
            FLEETPOP::Fleet_Box_Data(FLEETPOP::N_Fleet_Icons(MOX::ship_stack_start[stack]));
        }

        // open_fleet {fleet}: the native fleet panel stands for the legacy fleet box (moveable
        // box 2), whose record is saved with the game, as the system panel does for box 0. This
        // keeps the record as MAINSCR::Do_Fleet_Popup does (it also closes the system box).
        void Open_Fleet(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t ship = -1;
            if (!Read_Ship(args, "fleet", out, &ship)) {
                return;
            }
            const int16_t stack = Stack_Of(ship);
            const int16_t icon = stack >= 0 ? Icon_Of(stack) : -1;
            if (icon < 0) {
                out.Fail("refused", "the fleet is not on the map");
                return;
            }
            MOX::fleet_ship_scanned = -1;
            MOX::eta_star_id = -1;
            HACCESS::Clear_Fleet_Box_Selected_Ship_Status();
            FLEETPOP::Clear_Ship_Node_Selection_Flags();
            MOX::first_time = 1;
            MOX::just_moved_stack = 0;
            if (MOVEBOX::Moveable_Box_Selected(0)) {
                MOVEBOX::Vanish_System_Display_Box();
            }
            MOX::moveable_box[2].id = 1;
            MOX::fleet_box_ship_stack = stack;
            MOX::first_node_ptr_in_stack_ = -1;
            MAINSCR::Set_Popup_Draw_XY(2, icon, &MOX::moveable_box[2].x, &MOX::moveable_box[2].y);
            MOVEBOX::Move_Box_To_Top(2);
            if (MOX::moveable_box[2].x < 32 && MOX::moveable_box[2].y < 32) {
                int16_t icon_x = 0;
                int16_t icon_y = 0;
                SHIPS::Get_Ship_Icon_Coords(icon, &icon_x, &icon_y);
                const int16_t x = MOX::moveable_box[2].x;
                const int16_t y = MOX::moveable_box[2].y;
                if (MISC::Occluded(x, y, x + (int16_t)MOX::moveable_box[2].width, y + (int16_t)MOX::moveable_box[2].height,
                        icon_x, icon_y, icon_x + 13, icon_y + 10)) {
                    MOX::moveable_box[2].x = 619;
                    MOX::moveable_box[2].y = 421;
                }
            }
            Size_Box();
            out.Int("ship", Head_Ship(stack));
        }

        // close_fleet: the box's Close (MOVEBOX::Vanish_Fleet_Movement_Box).
        void Close_Fleet(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            FLEETPOP::Clear_Ship_Node_Selection_Flags();
            MOVEBOX::Vanish_Fleet_Movement_Box();
        }

        // After an order the box shows the stack that holds `ship`, or closes.
        void Follow(int16_t ship, s_out& out) {
            const int16_t stack = ship >= 0 && Ship_Alive(ship) ? Stack_Of(ship) : -1;
            MOX::eta_star_id = -1;
            MOX::first_node_ptr_in_stack_ = -1;
            if (stack < 0) {
                if (MOVEBOX::Moveable_Box_Selected(2)) {
                    MOVEBOX::Vanish_Fleet_Movement_Box();
                }
                out.Int("fleet", -1);
                return;
            }
            if (MOVEBOX::Moveable_Box_Selected(2)) {
                MOX::fleet_box_ship_stack = stack;
                Size_Box();
            }
            out.Int("fleet", Head_Ship(stack));
        }

        // move {ships, star}: a star clicked with ships selected (Main_Screen): the route check,
        // its failure message, then SHIPMOVE::Apply_Player_Movement_Order, whose question about
        // splitting a fleet is a `confirm` request. `done` is false when the ships can't go or
        // the player said No.
        void Move(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            std::vector<int16_t> list;
            int16_t stack = -1;
            int16_t star = -1;
            if (!Read_Orderable_Ships(args, out, &list, &stack) || !Read_Star(args, out, &star)) {
                return;
            }
            if (MOX::star[star].spectral_class == STAR_CLASS_BLACK_HOLE) {
                out.Fail("refused", "ships can't be sent to a black hole");
                return;
            }
            const std::vector<int16_t> stack_ships = Stack_Ships(stack);
            s_ship_move_info info{};
            SHIPMOVE::Ships_Try_To_Move_To(list.data(), star, &info);
            if (info.moving == 0) {
                int16_t message = 0;
                if (info.blackhole_blocks != 0) {
                    message = HESTR_021_A_BLACK_HOLE_BLOCKS_MO;
                } else if (info.insufficient_fuel != 0) {
                    message = HESTR_022_ONE_OR_MORE_SHIPS_DOES;
                } else if (info.immobile != 0) {
                    message = HESTR_023_ONE_OR_MORE_SHIPS_CHOS;
                } else if (info.hyperspace_flux != 0) {
                    message = HESTR_024_SHIP_MOVEMENT_IS_NOT_A;
                }
                if (message != 0) {
                    char text[200];
                    strlcpy(text, HAROLD::H_Message(message), sizeof(text));
                    HAROLD::User_Box(&MOX::draw_main_screen_to_back, text, 3);
                }
                out.Bool("done", false);
                return;
            }
            if (!SHIPMOVE::Apply_Player_Movement_Order(list.data(), star, &info)) {
                out.Bool("done", false);
                return;
            }
            // The box follows the ships when the whole stack moved, else the first ship left.
            const bool all = list.size() - 1 == stack_ships.size();
            int16_t follow = list[0];
            if (!all) {
                for (const int16_t ship : stack_ships) {
                    if (std::find(list.begin(), list.end(), ship) == list.end()) {
                        follow = ship;
                        break;
                    }
                }
            }
            MOX::just_moved_stack = all ? 1 : 0;
            Rebuild_Icons();
            out.Bool("done", true);
            Follow(follow, out);
        }

        // colonize / outpost {fleet, planet}: the box's Colonize and Outpost buttons
        // (MAINSCR2::Planet_Colonization_In_Main_Screen, Building_Outposts_In_Main_Screen) after
        // the planet was picked: the first colony (outpost) ship of the stack founds it (porting
        // note 4), through the shared consume step. The pick and its confirmation are the view's.
        void Settle(const s_args& args, s_out& out, bool outpost) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t stack = -1;
            if (!Read_Own_Stack(args, out, &stack)) {
                return;
            }
            int64_t planet = -1;
            if (!args.Int("planet", &planet) || planet < 0 || planet >= MOX::planet_count) {
                out.Fail("bad_args", "planet: a planet id");
                return;
            }
            const std::vector<int16_t> ships = Stack_Ships(stack);
            const int16_t count = (int16_t)ships.size();
            const bool allowed = outpost ? FLEETPOP::Set_Build_Outpost_Button_Flag(stack, count) != 0
                                         : FLEETPOP::Set_Colonize_Button_Flag(stack, count) != 0;
            const int16_t star = Stack_Star(stack, !outpost);
            if (!allowed || star < 0) {
                out.Fail("refused", outpost ? "this fleet can't build an outpost here" : "this fleet can't colonise here");
                return;
            }
            const e_target kind = outpost ? e_target::outpost : e_target::colonize;
            if (MOX::planet[planet].star_index != star || !Target_Allowed(kind, (int16_t)planet)) {
                out.Fail("refused", "that planet can't be settled by this fleet");
                return;
            }
            const uint8_t type = outpost ? SHIP_TYPE_OUTPOST : SHIP_TYPE_COLONY;
            int16_t used = -1;
            for (const int16_t ship : ships) {
                if (MOX::ship[ship].d.ship_type == type) {
                    used = ship;
                    break;
                }
            }
            if (used < 0) {
                out.Fail("refused", "no ship of that kind in the fleet");
                return;
            }
            if (outpost) {
                COLONIZE::Make_New_Outpost((int16_t)planet, MOX::PLAYER_NUM);
            } else {
                COLONIZE::Make_New_Colony((int16_t)planet, MOX::PLAYER_NUM);
            }
            antares::fleet::Consume_Ship(used);
            COLCALC::Update_Player_Stats(&MOX::player[MOX::PLAYER_NUM]);
            Rebuild_Icons();
            out.Bool("done", true);
            out.Int("ship", used);
            out.Int("colony", MOX::planet[planet].colony_index);
            int16_t follow = -1;
            for (const int16_t ship : ships) {
                if (ship != used) {
                    follow = ship;
                    break;
                }
            }
            Follow(follow, out);
        }

        void Colonize(int16_t, const s_args& args, s_out& out) {
            Settle(args, out, false);
        }

        void Outpost(int16_t, const s_args& args, s_out& out) {
            Settle(args, out, true);
        }

        // land_troops {ships, planet}: the box's Transport button (MAINSCR2::Unloading_Transports_In_Main_Screen):
        // the listed transports land on the player's colony at their star.
        void Land_Troops(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            std::vector<int16_t> list;
            int16_t stack = -1;
            if (!Read_Orderable_Ships(args, out, &list, &stack, SHIP_TYPE_TRANSPORT)) {
                return;
            }
            int64_t planet = -1;
            if (!args.Int("planet", &planet) || planet < 0 || planet >= MOX::planet_count) {
                out.Fail("bad_args", "planet: a planet id");
                return;
            }
            const int16_t star = Stack_Star(stack, false);
            if (FLEETPOP::Set_Transport_Troops_Flag(stack, (int16_t)Stack_Ships(stack).size()) == 0 || star < 0 ||
                MOX::planet[planet].star_index != star || !Target_Allowed(e_target::land_troops, (int16_t)planet)) {
                out.Fail("refused", "these transports can't land there");
                return;
            }
            const int16_t colony = MOX::planet[planet].colony_index;
            const int landed = invasion::Unload_Transports(&MOX::colony[colony], list.data(), (int)list.size() - 1);
            Rebuild_Icons();
            out.Bool("done", landed > 0);
            out.Int("landed", landed);
            int16_t follow = -1;
            for (const int16_t ship : Stack_Ships(stack)) {
                if (Ship_Alive(ship)) {
                    follow = ship;
                    break;
                }
            }
            Follow(follow, out);
        }

        // engage {fleet}: the box's Engage button: combat at the stack's star next turn.
        void Engage(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int16_t stack = -1;
            if (!Read_Own_Stack(args, out, &stack)) {
                return;
            }
            if (FLEETPOP::Set_Engage_Enemy_Forces_Flag(stack, (int16_t)Stack_Ships(stack).size()) == 0) {
                out.Fail("refused", "nothing to engage here");
                return;
            }
            const int16_t star = HACCESS::Ship_Stack_Star_Id(MOX::ship_stack_start[stack]);
            if (star > -1 && star < MOX::NUM_STARS) {
                MOX::star[star].ignore_combat_ships &= ~(1 << MOX::PLAYER_NUM);
            }
            out.Bool("done", true);
        }

        // attack_antares {ships}: the box's Attack Antares button: its Yes/No (a `confirm`
        // request), then SHIPS::Deploy_Ships_To_Attack_Antares in its argument form.
        void Attack_Antares(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            std::vector<int16_t> list;
            int16_t stack = -1;
            if (!Read_Orderable_Ships(args, out, &list, &stack)) {
                return;
            }
            if (!FLEETPOP::Set_Attack_Antares_Button_Flag(stack)) {
                out.Fail("refused", "no dimensional portal here");
                return;
            }
            const int16_t count = (int16_t)(list.size() - 1);
            char text[100];
            snprintf(text, sizeof(text), HAROLD::H_Message(count == 1 ? 0xE4 : 0xE5), count);
            if (HAROLD::User_Box(&MOX::draw_main_screen_to_back, text, 1) == 0) {
                out.Bool("done", false);
                return;
            }
            const std::vector<int16_t> stack_ships = Stack_Ships(stack);
            SHIPS::Deploy_Ships_To_Attack_Antares(list.data());
            MAINSCR::Update_Ship_Icons_In_Between_Turns();
            out.Bool("done", true);
            int16_t follow = stack_ships.front();
            if (list.size() - 1 != stack_ships.size()) {
                for (const int16_t ship : stack_ships) {
                    if (std::find(list.begin(), list.end(), ship) == list.end()) {
                        follow = ship;
                        break;
                    }
                }
            }
            Follow(follow, out);
        }

        // merge_relocations {star?}: F4. Without a star: the F4 key's check (with one relocation
        // or none, its message as a request), `ready` when a star may be picked. With a star:
        // every relocation goes there (HACCESS::Set_All_Star_Relocations) and the message says so.
        void Merge_Relocations(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            char text[200];
            if (!args.Has("star")) {
                const int16_t count = HACCESS::N_Star_Relocations(MOX::PLAYER_NUM);
                if (count <= 1) {
                    strlcpy(text, HAROLD::H_Message(count == 1 ? 0xD1 : 0xD2), sizeof(text));
                    HAROLD::User_Box(&MOX::draw_main_screen_to_back, text, 0);
                }
                out.Bool("ready", count > 1);
                return;
            }
            int16_t star = -1;
            if (!Read_Star(args, out, &star)) {
                return;
            }
            if (MOX::star[star].spectral_class == STAR_CLASS_BLACK_HOLE) {
                out.Fail("refused", "not a black hole");
                return;
            }
            if (HACCESS::N_Star_Relocations(MOX::PLAYER_NUM) <= 1) {
                out.Fail("refused", "fewer than two relocation points");
                return;
            }
            HACCESS::Set_All_Star_Relocations(star, MOX::PLAYER_NUM);
            snprintf(text, sizeof(text), HAROLD::H_Message(HESTR_0DD_ALL_NEW_SHIPS_BUILT_WI), &MOX::star[star]);
            HAROLD::User_Box(&MOX::draw_main_screen_to_back, text, 0);
            out.Bool("done", true);
        }

    } // namespace

    void Add_Fleet_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"open_fleet", e_kind::command, galaxy, 0, "fleet: ship id", Open_Fleet});
        table->push_back({"close_fleet", e_kind::command, galaxy, 0, "", Close_Fleet});
        table->push_back({"move", e_kind::command, galaxy, 0, "ships: [ship id], star: star id", Move});
        table->push_back({"colonize", e_kind::command, galaxy, 0, "fleet: ship id, planet: planet id", Colonize});
        table->push_back({"outpost", e_kind::command, galaxy, 0, "fleet: ship id, planet: planet id", Outpost});
        table->push_back({"land_troops", e_kind::command, galaxy, 0, "ships: [ship id], planet: planet id", Land_Troops});
        table->push_back({"engage", e_kind::command, galaxy, 0, "fleet: ship id", Engage});
        table->push_back({"attack_antares", e_kind::command, galaxy, 0, "ships: [ship id]", Attack_Antares});
        table->push_back({"merge_relocations", e_kind::command, galaxy, 0, "star: star id (optional)", Merge_Relocations});
        table->push_back({"fleet", e_kind::query, ANY_SCREEN, 1, "ship: ship id", Query_Fleet});
        table->push_back({"move_preview", e_kind::query, ANY_SCREEN, 1, "ships: [ship id], star: star id", Query_Move_Preview});
        table->push_back({"next_fleet", e_kind::query, ANY_SCREEN, 1, "fleet: ship id, dir: 0 or 1", Query_Next_Fleet});
    }

} // namespace antares::commands::internal
