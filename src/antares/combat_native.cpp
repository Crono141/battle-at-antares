#include "pch.h"

#include "antares/combat_native.h"

#include "antares/commands.h"
#include "antares/commands_internal.h"
#include "antares/native.h"
#include "yyjson.h"

// WP3.9a: the battle as a native scene (combat_native.h). Everything here reads the battle globals
// the legacy screen reads, or applies an action through COMBAT1::Apply_Tactical_Action.

namespace antares::combat {

    using commands::s_out;

    namespace {
        bool s_active = false;
        int16_t s_star = -1;
        uint64_t s_last_frame_ms = 0;
        void Flush_Motion(); // WP3.9d, below

        // The legacy shield's outline layers (cmbtdrw1.cpp, Draw_Ship_To_Bitmap's switch on
        // (shield_arc_max + 19) / 10): 2, 4, 6, 8 or 10 layers, by the arc's maximum.
        int32_t Shield_Layers(const s_combat_data& ship) {
            if (ship.shield_type <= 0 || ship.shield_arc_max <= 0) {
                return 0;
            }
            int32_t value = (ship.shield_arc_max + 19) / 10;
            if (value > 10) {
                value = 10;
            }
            if (value < 1) {
                return 0;
            }
            return ((value - 1) / 2 + 1) * 2;
        }

        // The sprite the legacy screen draws for a combat ship (cmbtdrw1.cpp, Draw_Ship_To_Bitmap).
        void Ship_Sprite(const s_combat_data& ship, std::string* lbx, int32_t* entry) {
            const uint8_t previous_owner = ship.previous_owner;
            if (previous_owner < MAX_PLAYERS && ship.picture_num != 0x2C) {
                *lbx = "cmbtshp.lbx";
                *entry = MOX::player[previous_owner].color * 45 + ship.picture_num;
            } else {
                *lbx = "monster.lbx";
                *entry = ship.picture_num == 0x2C ? 25 : ship.picture_num;
            }
        }

        // The armour a ship starts with (Draw_Grey_Damage_Bars' total).
        int32_t Armor_Total(int16_t ship_idx) {
            const s_combat_data& ship = MOX::combat_data[ship_idx];
            int16_t hull_size = COMBINIT::Is_Combat_Satellite(&ship)
                ? COMBINIT::Combat_Satellite_Hull_Class(&ship)
                : ship.size_class;
            const int32_t hull_armor = TECHDATA::hull_data[hull_size].armor_hp;
            const int32_t percent = TECHDATA::armor[ship.armor_type].ships_bonus;
            int32_t total = hull_armor + (percent * hull_armor) / 100;
            if (COMBINIT::Does_Combat_Ship_Have_Special(ship_idx, SPECIAL_HEAVY_ARMOR) != 0) {
                total *= 3;
            }
            return total;
        }

        bool In_Grid(int64_t x, int64_t y) {
            return x >= 0 && y >= 0 && x < MOX::combat_grid_width && y < MOX::combat_grid_height;
        }

        bool Ship_Alive(int64_t ship_idx) {
            return ship_idx >= 0 && ship_idx < MOX::combat_total_ship_count &&
                MOX::combat_data[ship_idx].unit_status == 0;
        }

        std::string Text_Message(int32_t id) {
            char buffer[80] = {};
            KEN::Ken_Get_Text_Message(id, buffer, sizeof(buffer));
            return commands::internal::Utf8(buffer, sizeof(buffer));
        }

        void Ship_Out(int16_t i, s_out item) {
            const s_combat_data& ship = MOX::combat_data[i];
            item.Int("i", i);
            item.Str("name", commands::internal::Utf8(ship.name, sizeof(ship.name)));
            item.Int("owner", ship.owner);
            item.Int("color", ship.owner >= 0 && ship.owner < MAX_PLAYERS ? MOX::player[ship.owner].color : -1);
            item.Int("x", ship.x);
            item.Int("y", ship.y);
            item.Int("facing", ship.facing_dir);
            item.Int("size", ship.size_class);
            item.Int("status", ship.unit_status);
            item.Bool("planet", i == 0 && MOX::cmbt_colony_id != -1);
            item.Bool("retreating", ship.is_retreating != 0);
            item.Int("special_status", ship.special_status_flag);
            item.Bool("stasis", CMBTMOV1::Ship_Is_In_Stasis(i) != 0);
            item.Int("moves", ship.movement_left);
            item.Int("speed", ship.current_speed);
            item.Int("flags", ship.combat_status_flags);
            // The centre in combat pixels (square * 20 + Ship_Center_Offsets), as the legacy screen draws it.
            int16_t off_x = 0;
            int16_t off_y = 0;
            COMBINIT::Ship_Center_Offsets(i, &off_x, &off_y);
            item.Int("cx", ship.x * 20 + off_x);
            item.Int("cy", ship.y * 20 + off_y);
            std::string lbx;
            int32_t entry = 0;
            Ship_Sprite(ship, &lbx, &entry);
            if (i == 0 && MOX::cmbt_colony_id != -1) {
                // The colony's planet (Load_Combat_Colony_Picture).
                const s_planet_data& planet = MOX::planet[MOX::colony[MOX::cmbt_colony_id].planet];
                lbx = "cmbtplnt.lbx";
                entry = planet.size + planet.climate * 6;
            }
            s_out sprite = item.Add_Object("sprite");
            sprite.Str("lbx", lbx);
            sprite.Int("entry", entry);

            s_out shield = item.Add_Object("shield");
            shield.Int("type", ship.shield_type);
            shield.Int("max", ship.shield_arc_max);
            shield.Int("layers", ship.shield_current_hits > 0 ? Shield_Layers(ship) : 0);
            s_out arcs = shield.Add_List("arcs");
            for (int a = 0; a < 4; ++a) {
                arcs.Append_Int(ship.shield_arc_current[a]);
            }

            s_out armor = item.Add_Object("armor");
            armor.Int("now", ship.armor_remaining);
            armor.Int("max", Armor_Total(i));
            // A defended planet's structure is its ground defence (Draw_Grey_Damage_Bars, ship 0).
            int32_t structure_max = ship.structure_max;
            if (i == 0 && MOX::cmbt_colony_id != -1 && CMBTFIRE::Does_Combat_Planet_Have_Defenses() != 0) {
                const int16_t best = INITSHIP::Best_Armor(ship.owner);
                structure_max = CMBTFIRE::Ground_Defense_Structure(TECHDATA::armor[best].ships_bonus);
            }
            s_out structure = item.Add_Object("structure");
            structure.Int("now", structure_max - ship.structure_damage);
            structure.Int("max", structure_max);
            s_out systems = item.Add_Object("systems");
            systems.Int("now", CMBTFIR2::Total_Internal_Space(i));
            systems.Int("max", CMBTFIR2::Max_Total_Internal_Space(i));
            systems.Int("drive", ship.drive_current_hits);
            systems.Int("drive_max", ship.drive_max_hits);
            systems.Int("computer", ship.computer_current_hits);
            systems.Int("computer_max", ship.computer_max_hits);
            systems.Int("shield", ship.shield_current_hits);
            systems.Int("shield_max", ship.shield_max_hits);

            s_out weapons = item.Add_List("weapons");
            for (int w = 0; w < 8; ++w) {
                const s_combat_weapon_slot& slot = ship.weapons[w];
                if (slot.weapon_id <= 0 && slot.count == 0) {
                    continue;
                }
                s_out weapon = weapons.Append_Object();
                weapon.Int("slot", w);
                weapon.Int("weapon", slot.weapon_id);
                weapon.Str("name", commands::internal::Utf8(TECHDATA::weapons[slot.weapon_id].name, 40));
                weapon.Int("count", slot.count);
                weapon.Int("arc", slot.arc);
                weapon.Int("ammo", slot.ammo);
                weapon.Int("active", slot.active);
                weapon.Int("shots_left", slot.shots_left);
                // Can still fire this turn (the legacy weapon list's lit lines: on, shots left).
                weapon.Bool("ready", COMBAT1::Weapon_Is_On(i, (int16_t)w));
            }
            item.Int("marines", ship.marine_count);
            item.Int("crew", ship.crew_quality);
        }
    } // namespace

    bool Active() {
        return s_active;
    }

    std::string State_Json() {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        s_out out(doc, root);

        out.Str("scene", "combat");
        out.Bool("active", s_active);
        out.Int("round", MOX::combat_turn);
        out.Int("max_rounds", COMBAT1::max_combat_turns);
        out.Int("initiative", MOX::combat_init);
        out.Int("player", MOX::PLAYER_NUM);
        out.Int("attacker", MOX::combat_attacker_player);
        out.Int("defender", MOX::combat_defender_player);
        out.Int("colony", MOX::cmbt_colony_id);
        if (s_star >= 0 && s_star < MOX::NUM_STARS) {
            out.Str("star", commands::internal::Utf8(MOX::star[s_star].name, sizeof(MOX::star[s_star].name)));
        }
        auto race = [](int16_t player_idx) -> std::string {
            if (player_idx >= 0 && player_idx < MAX_PLAYERS) {
                return commands::internal::Utf8(MOX::player[player_idx].race_name, sizeof(MOX::player[player_idx].race_name));
            }
            char name[40] = {};
            HACCESS::Race_Name(player_idx, name, sizeof(name));
            return commands::internal::Utf8(name, sizeof(name));
        };
        out.Str("attacker_race", race(MOX::combat_attacker_player));
        out.Str("defender_race", race(MOX::combat_defender_player));
        out.Int("nebula", MOX::in_nebula_flag);
        s_out grid = out.Add_Object("grid");
        grid.Int("width", MOX::combat_grid_width);
        grid.Int("height", MOX::combat_grid_height);
        out.Int("ship", MOX::cur_ship);
        out.Int("auto", MOX::auto_combat);
        out.Bool("can_wait", COMBAT1::Combat_Wait_Is_Available());
        out.Bool("can_side_wait", COMBAT1::Combat_Side_Wait_Is_Available());

        s_out ships = out.Add_List("ships");
        for (int16_t i = 0; i < MOX::combat_total_ship_count; ++i) {
            Ship_Out(i, ships.Append_Object());
        }

        // The ships still to act this round, in order (the turn loop's ship_turn_order from cur_ptr).
        s_out order = out.Add_List("order");
        for (int16_t p = 0; p < MOX::combat_total_ship_count; ++p) {
            const int16_t ship_idx = MOX::ship_turn_order[p];
            if (ship_idx >= 0 && ship_idx < MOX::combat_total_ship_count) {
                order.Append_Int(ship_idx);
            }
        }

        // The legal squares for the acting ship (Assign_Combat_Grids marks them 2000).
        s_out legal = out.Add_List("legal");
        if (MOX::cur_ship >= 0 && MOX::cur_ship < MOX::combat_total_ship_count) {
            for (int16_t x = 0; x < MOX::combat_grid_width; ++x) {
                for (int16_t y = 0; y < MOX::combat_grid_height; ++y) {
                    if (MOX::combat_grid[x][y] == 2000) {
                        legal.Append_Int(x * 1000 + y);
                    }
                }
            }
        }

        // For the acting ship, the weapons that can fire at each enemy now (in arc, in range, a legal
        // target, ready): the legacy weapon list lit them for the ship under the pointer
        // (CMBTDRW1::Draw_Weapon_Status_Display).
        s_out targets = out.Add_List("targets");
        if (MOX::cur_ship >= 0 && MOX::cur_ship < MOX::combat_total_ship_count && MOX::combat_data[MOX::cur_ship].unit_status == 0) {
            const s_combat_data& shooter = MOX::combat_data[MOX::cur_ship];
            for (int16_t t = 0; t < MOX::combat_total_ship_count; ++t) {
                const s_combat_data& target = MOX::combat_data[t];
                if (target.unit_status != 0 || target.owner == shooter.owner) {
                    continue;
                }
                const uint8_t required_arc = CMBTDRW1::Get_Weapon_Arc(CMBTDRW1::Relative_Bearing(MOX::cur_ship, t));
                s_out entry = targets.Append_Object();
                entry.Int("ship", t);
                s_out slots = entry.Add_List("slots");
                for (int16_t w = 0; w < 8; ++w) {
                    const s_combat_weapon_slot& slot = shooter.weapons[w];
                    if (slot.weapon_id == 0 || !COMBAT1::Weapon_Is_On(MOX::cur_ship, w)) {
                        continue;
                    }
                    if ((slot.arc & required_arc) != 0 && CMBTFIRE::Weapon_In_Range(w, MOX::cur_ship, t, 0, 0) != 0 &&
                        CMBTFIRE::Target_Is_Legal_For_Weapon(slot.weapon_id, slot.specials, t, 0) != 0) {
                        slots.Append_Int(w);
                    }
                }
            }
        }

        s_out missiles = out.Add_List("missiles");
        for (int16_t m = 0; m < MAX_COMBAT_FLYERS; ++m) {
            const s_missile& missile = MOX::missile[m];
            if (missile.is_active == 0 || missile.quantity <= 0) {
                continue;
            }
            s_out item = missiles.Append_Object();
            item.Int("i", m);
            item.Int("type", missile.type);
            item.Int("owner", missile.owner);
            item.Int("target", missile.target_unit_idx);
            item.Int("x", missile.x);
            item.Int("y", missile.y);
            item.Int("count", missile.quantity);
            item.Int("facing", missile.facing_dir);
        }

        char* text = yyjson_mut_write(doc, 0, nullptr);
        std::string json = text != nullptr ? text : "{}";
        free(text);
        yyjson_mut_doc_free(doc);
        return json;
    }

    void Begin(int16_t star_idx) {
        if (!native::Combat_Native() || MOX::net_flag != 0) {
            return;
        }
        s_active = true;
        s_star = star_idx;
        native::Set_Scene(State_Json);
        native::Post_Scene_Snapshot("combat");
    }

    void End() {
        if (!s_active) {
            return;
        }
        Flush_Motion();
        s_active = false;
        native::Post_Snapshot_Json("combat", State_Json());
        native::Clear_Scene();
    }

    void Frame() {
        if (!s_active) {
            return;
        }
        // Every redraw of a moving ship is a step; a few per frame are enough for the frontend.
        const uint64_t now = SDL_GetTicks();
        if (now - s_last_frame_ms < 15) {
            return;
        }
        s_last_frame_ms = now;
        native::Post_Scene_Snapshot("combat");
        native::Serve_Sprites_Now(4); // the scene's art while no request waits (AI turns)
    }

    namespace {
        // A move or turn being gathered (Turn_Step, Move_Step), posted as one segment.
        struct s_motion {
            int kind = 0; // 0 none, 1 turn, 2 move
            int16_t ship = -1;
            int16_t from_facing = 0, to_facing = 0, dir = 0;
            int16_t from_x = 0, from_y = 0, to_x = 0, to_y = 0;
            int32_t steps = 0;
        };
        s_motion s_motion_pending;
        uint64_t s_event_seq = 0;

        std::string Write(yyjson_mut_doc* doc) {
            char* text = yyjson_mut_write(doc, 0, nullptr);
            std::string json = text != nullptr ? text : "{}";
            free(text);
            yyjson_mut_doc_free(doc);
            return json;
        }

        // Posts one effect: an "animate" request while Animating (the rules wait for it), else
        // the 3.9c "combat_event" snapshot for observers.
        void Post_Event(const std::string& event) {
            if (Animating()) {
                native::Ask_Animate("\"event\":" + event);
            } else {
                native::Post_Scene_Snapshot("combat");
                native::Post_Snapshot_Json("combat_event", event);
            }
        }

        void Flush_Motion() {
            s_motion& m = s_motion_pending;
            if (m.kind == 0) {
                return;
            }
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* root = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, root);
            s_out out(doc, root);
            out.Str("kind", m.kind == 1 ? "turn" : "move");
            out.Int("seq", (int64_t)++s_event_seq);
            out.Int("ship", m.ship);
            out.Int("steps", m.steps);
            if (m.kind == 1) {
                out.Int("from", m.from_facing);
                out.Int("to", m.to_facing);
                out.Int("dir", m.dir);
            } else {
                out.Int("x0", m.from_x);
                out.Int("y0", m.from_y);
                out.Int("x1", m.to_x);
                out.Int("y1", m.to_y);
                out.Int("facing", MOX::combat_data[m.ship].facing_dir);
            }
            m = s_motion{};
            Post_Event(Write(doc));
        }

        // Every other effect: the motion so far first, so the frontend has the ship where it is.
        void Post_Effect(yyjson_mut_doc* doc) {
            Flush_Motion();
            Post_Event(Write(doc));
        }

        yyjson_mut_doc* Effect_Doc(const char* kind, s_out* out) {
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* root = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, root);
            *out = s_out(doc, root);
            out->Str("kind", kind);
            out->Int("seq", (int64_t)++s_event_seq);
            return doc;
        }
    } // namespace

    bool Animating() {
        return s_active && MOX::super_fast_flag == 0 && native::Animate_Native();
    }

    bool Legacy_Draws() {
        return MOX::super_fast_flag == 0 && !Animating();
    }

    void Effect(const char* kind, std::initializer_list<std::pair<const char*, int64_t>> fields) {
        if (!s_active) {
            return;
        }
        s_out out(nullptr, nullptr);
        yyjson_mut_doc* doc = Effect_Doc(kind, &out);
        for (const auto& field : fields) {
            out.Int(field.first, field.second);
        }
        Post_Effect(doc);
    }

    void Turn_Step(int16_t ship, int16_t from, int16_t to) {
        if (!s_active) {
            return;
        }
        s_motion& m = s_motion_pending;
        if (m.kind != 1 || m.ship != ship) {
            Flush_Motion();
            m.kind = 1;
            m.ship = ship;
            m.from_facing = from;
            m.dir = (int16_t)(((to - from + 16) % 16) == 1 ? 1 : -1);
        }
        m.to_facing = to;
        m.steps++;
    }

    void Move_Step(int16_t ship, int16_t from_x, int16_t from_y, int16_t to_x, int16_t to_y) {
        if (!s_active) {
            return;
        }
        s_motion& m = s_motion_pending;
        if (m.kind != 2 || m.ship != ship) {
            Flush_Motion();
            m.kind = 2;
            m.ship = ship;
            m.from_x = from_x;
            m.from_y = from_y;
        }
        m.to_x = to_x;
        m.to_y = to_y;
        m.steps++;
    }

    void Motion_End() {
        if (s_active) {
            Flush_Motion();
        }
    }

    void Teleport(int16_t ship, int16_t to_x, int16_t to_y) {
        if (!s_active) {
            return;
        }
        s_out out(nullptr, nullptr);
        yyjson_mut_doc* doc = Effect_Doc("teleport", &out);
        out.Int("ship", ship);
        out.Int("x0", MOX::combat_data[ship].x);
        out.Int("y0", MOX::combat_data[ship].y);
        out.Int("x1", to_x);
        out.Int("y1", to_y);
        Post_Effect(doc);
    }

    void Destroyed(int16_t ship, int16_t killer, int16_t death) {
        if (!s_active) {
            return;
        }
        s_out out(nullptr, nullptr);
        yyjson_mut_doc* doc = Effect_Doc("destroyed", &out);
        out.Int("ship", ship);
        out.Int("killer", killer);
        out.Int("death", death);
        out.Int("size", MOX::combat_data[ship].size_class);
        Post_Effect(doc);
    }

    void Missile_Launch(int16_t stack, int16_t from, int16_t to, bool at_missile, int16_t tx, int16_t ty) {
        if (!s_active) {
            return;
        }
        const s_missile& msl = MOX::missile[stack];
        s_out out(nullptr, nullptr);
        yyjson_mut_doc* doc = Effect_Doc("missile_launch", &out);
        out.Int("stack", stack);
        out.Int("from", from);
        out.Int("to", to);
        out.Bool("at_missile", at_missile);
        out.Int("weapon", msl.type);
        out.Str("name", commands::internal::Utf8(TECHDATA::weapons[msl.type].name, 40));
        out.Int("weapon_type", TECHDATA::weapons[msl.type].type);
        out.Int("count", msl.quantity);
        out.Int("x", msl.x);
        out.Int("y", msl.y);
        out.Int("tx", tx);
        out.Int("ty", ty);
        Post_Effect(doc);
    }

    void Missile_Arrive(int16_t stack, int16_t x, int16_t y) {
        if (!s_active) {
            return;
        }
        const s_missile& msl = MOX::missile[stack];
        s_out out(nullptr, nullptr);
        yyjson_mut_doc* doc = Effect_Doc("missile_arrive", &out);
        out.Int("stack", stack);
        out.Int("to", msl.target_unit_idx);
        out.Bool("at_missile", msl.target_is_missile != 0);
        out.Int("weapon", msl.type);
        out.Int("count", msl.quantity);
        out.Int("x", x);
        out.Int("y", y);
        Post_Effect(doc);
    }

    void Beam(int16_t from, int16_t to, int16_t non_ship_target, const s_beam_fx_entry* entries) {
        if (!s_active || entries == nullptr) {
            return;
        }
        Flush_Motion();
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        s_out out(doc, root);
        out.Str("kind", "beam");
        out.Int("seq", (int64_t)++s_event_seq);
        out.Int("from", from);
        out.Int("to", to);
        out.Bool("missile", non_ship_target != 0);
        if (non_ship_target != 0 && to >= 0 && to < MAX_COMBAT_FLYERS) {
            out.Int("tx", MOX::missile[to].x);
            out.Int("ty", MOX::missile[to].y);
        }
        s_out shots = out.Add_List("shots");
        for (int i = 0; i < 10 && entries[i].weapon_id != 0; ++i) {
            const s_beam_fx_entry& e = entries[i];
            s_out shot = shots.Append_Object();
            shot.Int("weapon", e.weapon_id);
            shot.Str("name", commands::internal::Utf8(TECHDATA::weapons[e.weapon_id].name, 40));
            shot.Int("type", TECHDATA::weapons[e.weapon_id].type);
            shot.Int("count", e.shot_count);
            shot.Int("flags", e.result_flags);
        }
        Post_Event(Write(doc));
    }

    std::string Apply_Action(const std::string& json, int16_t active_player_id, int16_t* next_ship_flag) {
        yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
        if (doc == nullptr || !yyjson_is_obj(yyjson_doc_get_root(doc))) {
            if (doc != nullptr) {
                yyjson_doc_free(doc);
            }
            return "bad action";
        }
        const commands::s_args args(yyjson_doc_get_root(doc));
        std::string action;
        args.Str("action", &action);
        int64_t x = -1;
        int64_t y = -1;
        int64_t target = -1;
        int64_t slot = -1;
        args.Int("x", &x);
        args.Int("y", &y);
        args.Int("target", &target);
        args.Int("slot", &slot);
        yyjson_doc_free(doc);

        const int16_t cur = MOX::cur_ship;
        if (cur < 0 || cur >= MOX::combat_total_ship_count) {
            return "no ship is acting";
        }
        s_combat_data& ship = MOX::combat_data[cur];
        auto apply = [&](int16_t code, int64_t a1, int64_t a2, int64_t a3) {
            COMBAT1::Apply_Tactical_Action(code, (int16_t)a1, (int16_t)a2, (int16_t)a3, active_player_id, next_ship_flag);
        };

        if (action == "move") {
            if (!In_Grid(x, y) || MOX::combat_grid[x][y] != 2000) {
                return "can't move there";
            }
            apply(TACTICAL_NET_MSG_MOVE_SHIP, x, y, 0);
            return "";
        }
        if (action == "turn") {
            if (!In_Grid(x, y)) {
                return "outside the field";
            }
            apply(TACTICAL_NET_MSG_TURN_TO_FACE, x, y, 0);
            return "";
        }
        if (action == "fire") {
            if (!Ship_Alive(target) || MOX::combat_data[target].owner == ship.owner) {
                return "not an enemy ship";
            }
            if (CMBTMOV1::Ship_Is_In_Stasis((int16_t)target) != 0) {
                return Text_Message(61); // the legacy question; answer it with "remove_stasis"
            }
            if (MOX::combat_data[target].special_status_flag == 4) {
                return Text_Message(80);
            }
            apply(TACTICAL_NET_MSG_FIRE_WEAPON, target, 0, 0);
            return "";
        }
        if (action == "fire_missile") {
            if (target < 0 || target >= MAX_COMBAT_FLYERS || MOX::missile[target].is_active == 0 ||
                CMBTMIS::Missile_Owner((int16_t)target) == ship.owner) {
                return "not an enemy missile";
            }
            apply(TACTICAL_NET_MSG_FIRE_WEAPON, target, 1, 0);
            return "";
        }
        if (action == "remove_stasis") {
            if (!Ship_Alive(target) || CMBTMOV1::Ship_Is_In_Stasis((int16_t)target) == 0) {
                return "not in stasis";
            }
            apply(TACTICAL_NET_MSG_REMOVE_STASIS, target, 0, 0);
            return "";
        }
        if (action == "wait") {
            if (!COMBAT1::Combat_Wait_Is_Available()) {
                return "can't wait now";
            }
            apply(TACTICAL_NET_MSG_WAIT, 0, 0, 0);
            return "";
        }
        if (action == "side_wait") {
            if (!COMBAT1::Combat_Side_Wait_Is_Available()) {
                return "can't wait now";
            }
            apply(TACTICAL_NET_MSG_SIDE_WAIT, 0, 0, 0);
            return "";
        }
        if (action == "done") {
            // As the Done button: the ship is marked done first (the network replay leaves it out).
            CMBTFIR2::Set_Combat_Ship_Done(cur);
            apply(TACTICAL_NET_MSG_SHIP_DONE, 0, 0, 0);
            return "";
        }
        if (action == "end_turn") {
            apply(TACTICAL_NET_MSG_END_TURN, 0, 0, 0);
            return "";
        }
        if (action == "retreat") {
            if (ship.is_retreating != 0) {
                return "already retreating";
            }
            if (COMBAT1::Enemy_Dissipator_Exists(ship.owner) != 0) {
                return Text_Message(58);
            }
            if (cur == 0 || COMBINIT::Is_Combat_Satellite(&ship)) {
                return Text_Message(59);
            }
            if (ship.current_speed < 1) {
                return Text_Message(79);
            }
            apply(TACTICAL_NET_MSG_RETREAT, 0, 0, 0);
            return "";
        }
        if (action == "toggle_weapon") {
            if (slot < 0 || slot >= 8 || ship.weapons[slot].count == 0) {
                return "no such weapon";
            }
            apply(TACTICAL_NET_MSG_TOGGLE_WEAPON, slot, 0, 0);
            return "";
        }
        if (action == "select") {
            if (MOX::combat_init != 0) {
                return "the turn order decides";
            }
            if (!Ship_Alive(target) || MOX::combat_data[target].owner != ship.owner) {
                return "not your ship";
            }
            apply(TACTICAL_NET_MSG_SELECT_SHIP, target, 0, 0);
            return "";
        }
        if (action == "self_destruct") {
            if (cur == 0) {
                return Text_Message(56);
            }
            apply(TACTICAL_NET_MSG_SELF_DESTRUCT, 0, 0, 0);
            return "";
        }
        if (action == "missile_warning") {
            apply(TACTICAL_NET_MSG_CYCLE_MISSILE_WARNING, 0, 0, 0);
            return "";
        }
        if (action == "auto") {
            // As the Auto button (combat1.cpp): the side's flag flips; off also ends super fast.
            const int side = ship.owner == MOX::combat_attacker_player ? 0 : 1;
            MOX::auto_combat_flag[side] = 1 - MOX::auto_combat_flag[side];
            MOX::auto_combat = MOX::auto_combat_flag[side];
            if (MOX::auto_combat == 0) {
                MOX::super_fast_flag = 0;
            }
            return "";
        }
        if (action == "board") {
            return "boarding is not native yet (WP3.9d)";
        }
        return "unknown action";
    }

    bool Native_Action(int16_t active_player_id, int16_t* next_ship_flag) {
        if (!s_active) {
            return false;
        }
        COMBAT1::Assign_Combat_Grids(MOX::cur_ship);
        std::string error;
        for (;;) {
            std::string fields = error.empty() ? std::string() : "\"error\":" + native::Quote_Utf8(error);
            std::string answer;
            native::Ask_Combat(fields, &answer);
            error = Apply_Action(answer, active_player_id, next_ship_flag);
            if (error.empty()) {
                return true;
            }
        }
    }

} // namespace antares::combat
