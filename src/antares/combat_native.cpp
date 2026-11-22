#include "pch.h"

#include "antares/combat_native.h"

#include "antares/commands.h"
#include "antares/commands_internal.h"
#include "antares/identity.h"
#include "antares/native.h"
#include "antares/render/capture.h"
#include "antares/sprites.h"
#define STB_IMAGE_WRITE_STATIC // spike 3.9: a private copy for the capture dumps
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "yyjson.h"

#include <map>

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

    bool Will_Be_Native() {
        return native::Combat_Native() && MOX::net_flag == 0;
    }

    void Begin(int16_t star_idx) {
        if (!Will_Be_Native()) {
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

    namespace {
        // Spike 3.9: the capture to name in the next Beam event ("capture": {...}), or empty.
        std::string s_beam_capture_json;
    } // namespace

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
        std::string json = Write(doc);
        if (!s_beam_capture_json.empty() && json.size() > 1) {
            json.insert(json.size() - 1, ",\"capture\":" + s_beam_capture_json);
            s_beam_capture_json.clear();
        }
        Post_Event(json);
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

    // ---- Spike 3.9: beam volleys captured from the legacy drawing (combat_native.h) ----

    namespace {
        struct s_world_frame {
            int32_t x = 0; // world position (combat pixels, 1x) of the crop's top-left
            int32_t y = 0;
            int32_t width = 0; // plane pixels
            int32_t height = 0;
            std::vector<uint8_t> rgba;
            std::vector<uint8_t> glass;
        };

        struct s_capture_set {
            std::vector<s_world_frame> frames;      // empty entries: nothing painted that frame
            int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; // world bounds, 1x
        };

        std::map<int32_t, s_capture_set> s_captures;
        int32_t s_capture_next = 1;
        int32_t s_volleys = 0;

        // Sounds asked for during a capture: (frame, sound) instead of played.
        bool s_sound_recording = false; // a capture pass runs: sounds aren't played
        bool s_sound_collect = false;   // ...and this pass's are kept (the first capturing pass)
        int32_t s_sound_frame = 0;
        std::vector<std::pair<int32_t, int32_t>> s_sounds;

        // Draw_Beam2 calls of the first capturing pass (spike 3.9, native beams test), as JSON
        // objects, and the window that pass used.
        std::vector<std::string> s_beam_records;
        int16_t s_record_cx = 0, s_record_cy = 0;

        enum e_pass { PASS_REFERENCE,
            PASS_COMPARE,
            PASS_TILE,
            PASS_SEAM };

        int32_t Env_Int(const char* name, int32_t fallback) {
            const char* value = antares::Get_Env(name);
            return value != nullptr ? atoi(value) : fallback;
        }

        // A page frame placed in the world (the window was at cx, cy squares).
        s_world_frame To_World(const render::capture::s_frame& f, int32_t cx, int32_t cy) {
            s_world_frame w;
            w.x = f.x + cx * 20;
            w.y = f.y + cy * 20;
            w.width = f.width;
            w.height = f.height;
            w.rgba = f.rgba;
            w.glass = f.glass;
            return w;
        }

        // Frames laid over each other by world position (later over earlier where both painted).
        s_world_frame Stitch(const std::vector<const s_world_frame*>& parts) {
            const int32_t S = render::capture::Scale();
            s_world_frame out;
            int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
            for (const s_world_frame* p : parts) {
                if (p == nullptr || p->width == 0) {
                    continue;
                }
                x0 = SDL_min(x0, p->x);
                y0 = SDL_min(y0, p->y);
                x1 = SDL_max(x1, p->x + p->width / S);
                y1 = SDL_max(y1, p->y + p->height / S);
            }
            if (x1 <= x0) {
                return out;
            }
            out.x = x0;
            out.y = y0;
            out.width = (x1 - x0) * S;
            out.height = (y1 - y0) * S;
            out.rgba.assign((size_t)out.width * out.height * 4, 0);
            out.glass.assign((size_t)out.width * out.height, 0);
            for (const s_world_frame* p : parts) {
                if (p == nullptr || p->width == 0) {
                    continue;
                }
                const int32_t ox = (p->x - x0) * S;
                const int32_t oy = (p->y - y0) * S;
                for (int32_t y = 0; y < p->height; ++y) {
                    for (int32_t x = 0; x < p->width; ++x) {
                        const size_t si = (size_t)y * p->width + x;
                        const size_t di = (size_t)(oy + y) * out.width + (ox + x);
                        if (p->rgba[si * 4 + 3] != 0) {
                            memcpy(&out.rgba[di * 4], &p->rgba[si * 4], 4);
                        }
                        if (p->glass[si] != 0) {
                            out.glass[di] = p->glass[si];
                        }
                    }
                }
            }
            return out;
        }

        // Pixels (plane scale) that differ by more than `tolerance` in any channel between two world
        // frames, over the pixels either one paints; also the painted count.
        void World_Diff(const s_world_frame& a, const s_world_frame& b, uint64_t* differ, uint64_t* painted) {
            std::vector<const s_world_frame*> both = {&a, &b};
            const int32_t S = render::capture::Scale();
            int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
            for (const s_world_frame* p : both) {
                if (p->width == 0) {
                    continue;
                }
                x0 = SDL_min(x0, p->x);
                y0 = SDL_min(y0, p->y);
                x1 = SDL_max(x1, p->x + p->width / S);
                y1 = SDL_max(y1, p->y + p->height / S);
            }
            if (x1 <= x0) {
                return;
            }
            auto at = [&](const s_world_frame& f, int32_t wx, int32_t wy, uint8_t* out) {
                const int32_t lx = wx - f.x * S;
                const int32_t ly = wy - f.y * S;
                if (f.width == 0 || lx < 0 || ly < 0 || lx >= f.width || ly >= f.height) {
                    memset(out, 0, 5);
                    return;
                }
                const size_t i = (size_t)ly * f.width + lx;
                memcpy(out, &f.rgba[i * 4], 4);
                out[4] = f.glass[i];
            };
            uint8_t pa[5], pb[5];
            for (int32_t y = y0 * S; y < y1 * S; ++y) {
                for (int32_t x = x0 * S; x < x1 * S; ++x) {
                    at(a, x, y, pa);
                    at(b, x, y, pb);
                    if (pa[3] == 0 && pb[3] == 0 && pa[4] == 0 && pb[4] == 0) {
                        continue;
                    }
                    ++*painted;
                    bool same = pa[4] == pb[4] && abs((int)pa[3] - (int)pb[3]) <= 32;
                    for (int32_t c = 0; c < 3 && same; ++c) {
                        same = abs((int)pa[c] * pa[3] / 255 - (int)pb[c] * pb[3] / 255) <= 32;
                    }
                    if (!same) {
                        ++*differ;
                    }
                }
            }
        }

        void Dump_Png(const std::string& name, const uint8_t* rgba, int32_t width, int32_t height) {
            const char* dir = antares::Get_Env("CAPTURE_DUMP");
            if (dir == nullptr || width <= 0 || height <= 0) {
                return;
            }
            const std::string path = std::string(dir) + "/" + name + ".png";
            stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4);
        }
    } // namespace

    bool Capturing() {
        static const bool off = [] {
            const char* value = antares::Get_Env("COMBAT_CAPTURE");
            return value != nullptr && strcmp(value, "0") == 0;
        }();
        return !off && Animating() && render::capture::Available();
    }

    void Record_Beam2(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t frame_index, int32_t palette_base, int32_t style, int32_t sfx_sprite_id, int32_t wide_bolt_size_bonus) {
        if (!s_sound_collect) {
            return;
        }
        const int32_t ox = s_record_cx * 20, oy = s_record_cy * 20;
        std::string ramp = "[";
        for (int32_t i = 0; i < 16; ++i) {
            const auto& c = video::current_palette[(palette_base + i) & 0xFF];
            char rgb[24];
            snprintf(rgb, sizeof(rgb), "%s[%d,%d,%d]", i ? "," : "", c.r * 255 / 63, c.g * 255 / 63, c.b * 255 / 63);
            ramp += rgb;
        }
        ramp += "]";
        char json[512];
        snprintf(json, sizeof(json), "{\"frame\":%d,\"frame_index\":%d,\"style\":%d,\"start\":[%d,%d],\"end\":[%d,%d],\"from\":[%d,%d],\"to\":[%d,%d],\"palette_base\":%d,"
                                     "\"sfx_sprite_id\":%d,\"wide_bolt_size_bonus\":%d,\"ramp\":",
            s_sound_frame,
            frame_index,
            style,
            start_x + ox,
            start_y + oy,
            end_x + ox,
            end_y + oy,
            x1 + ox,
            y1 + oy,
            x2 + ox,
            y2 + oy,
            palette_base,
            sfx_sprite_id,
            wide_bolt_size_bonus);
        s_beam_records.push_back(std::string(json) + ramp + "}");
    }

    void Record_Strand(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t* colors, int32_t count) {
        if (!s_sound_collect || s_beam_records.empty() && false) {
            return;
        }
        const int32_t ox = s_record_cx * 20, oy = s_record_cy * 20;
        std::string list = "[";
        for (int32_t i = 0; i < count; ++i) {
            list += (i ? "," : "") + std::to_string(colors[i]);
        }
        list += "]";
        char head[160];
        snprintf(head, sizeof(head), "{\"strand\":1,\"frame\":%d,\"a\":[%d,%d],\"b\":[%d,%d],\"colors\":", s_sound_frame, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
        s_beam_records.push_back(std::string(head) + list + "}");
    }

    bool Capture_Sound(int16_t sound_id) {
        if (!s_sound_recording) {
            return false;
        }
        if (s_sound_collect) {
            s_sounds.emplace_back(s_sound_frame, sound_id);
        }
        return true;
    }

    struct Beam_Capture::s_impl {
        bool active = false;
        int16_t from = 0, to = 0, non_ship = 0;
        int32_t wax = 0, way = 0, wtx = 0, wty = 0; // world positions (1x) the loop was given
        s_beam_fx_entry entries[16] = {};
        int16_t saved_cur_x = 0, saved_cur_y = 0;
        int16_t saved_near_x = 0, saved_near_y = 0, saved_mid_x = 0, saved_mid_y = 0, saved_far_x = 0, saved_far_y = 0;
        struct s_pass {
            e_pass kind;
            int16_t cx, cy;
        };
        std::vector<s_pass> passes;
        size_t pass = 0;
        int32_t frame = 0;
        std::vector<std::vector<s_world_frame>> pass_frames;
        // Comparison: legacy's own frames (window RGBA) and the background without the effect.
        std::vector<std::vector<uint8_t>> reference;
        std::vector<uint8_t> background;
        int32_t window_w = 0, window_h = 0;
        uint64_t reference_start = 0, reference_ms = 0;
        uint64_t capture_ns = 0, pass_start = 0;
        std::string ops, unported;
        int32_t tiles = 0;
        int32_t span_w = 0, span_h = 0;
        int32_t volley = 0;
        bool compare = false;
    };

    Beam_Capture::Beam_Capture(int16_t from, int16_t ax, int16_t ay, int16_t to, int16_t tx, int16_t ty, const s_beam_fx_entry* entries, int16_t non_ship_target)
        : impl_(new s_impl) {
        s_impl& m = *impl_;
        if (!Capturing() || entries == nullptr) {
            return;
        }
        m.active = true;
        m.from = from;
        m.to = to;
        m.non_ship = non_ship_target;
        m.wax = ax + MOX::cur_combat_x * 20;
        m.way = ay + MOX::cur_combat_y * 20;
        m.wtx = tx + MOX::cur_combat_x * 20;
        m.wty = ty + MOX::cur_combat_y * 20;
        memcpy(m.entries, entries, sizeof(m.entries));
        m.saved_cur_x = MOX::cur_combat_x;
        m.saved_cur_y = MOX::cur_combat_y;
        m.saved_near_x = MOX::combat_near_x;
        m.saved_near_y = MOX::combat_near_y;
        m.saved_mid_x = MOX::combat_mid_x;
        m.saved_mid_y = MOX::combat_mid_y;
        m.saved_far_x = MOX::combat_far_x;
        m.saved_far_y = MOX::combat_far_y;
        m.volley = ++s_volleys;
        m.compare = m.volley <= Env_Int("CAPTURE_COMPARE", 0);

        // The shooter's and target's centres, with 3 squares around them for the ships' size,
        // shields and impacts.
        int16_t ox = 0, oy = 0;
        COMBINIT::Ship_Center_Offsets(from, &ox, &oy);
        const int32_t acx = m.wax + ox, acy = m.way + oy;
        int32_t tcx = m.wtx, tcy = m.wty;
        if (non_ship_target == 0) {
            COMBINIT::Ship_Center_Offsets(to, &ox, &oy);
            tcx += ox;
            tcy += oy;
        }
        const int32_t sx0 = SDL_max(0, (SDL_min(acx, tcx) - 60) / 20);
        const int32_t sy0 = SDL_max(0, (SDL_min(acy, tcy) - 60) / 20);
        const int32_t sx1 = (SDL_max(acx, tcx) + 60) / 20 + 1;
        const int32_t sy1 = (SDL_max(acy, tcy) + 60) / 20 + 1;
        m.span_w = abs(acx - tcx) / 20;
        m.span_h = abs(acy - tcy) / 20;
        const int32_t max_x = SDL_max(0, MOX::combat_grid_width - COMBAT_GRID_VIEW_WIDTH);
        const int32_t max_y = SDL_max(0, MOX::combat_grid_height - COMBAT_GRID_VIEW_HEIGHT);
        // Tiles of 32 x 17 squares (the window's 351 rows hold 17 whole squares).
        for (int32_t y = sy0; y < sy1; y += 17) {
            for (int32_t x = sx0; x < sx1; x += 32) {
                m.passes.push_back({PASS_TILE, (int16_t)SDL_min(x, max_x), (int16_t)SDL_min(y, max_y)});
                ++m.tiles;
            }
        }
        m.compare = m.compare && m.tiles == 1;
        if (m.compare) {
            // Legacy's own frames at the tile's window (what legacy shows once it has snapped to
            // the volley), and the capture at the same window, before the tile pass.
            const s_impl::s_pass tile = m.passes.back();
            m.passes.insert(m.passes.begin(), {PASS_COMPARE, tile.cx, tile.cy});
            m.passes.insert(m.passes.begin(), {PASS_REFERENCE, tile.cx, tile.cy});
        }
        if (m.compare) {
            // The seam test: the same volley split at the middle between shooter and target.
            const int32_t mid = (acx + tcx) / 2 / 20;
            const int16_t cy = m.passes.back().cy;
            m.passes.push_back({PASS_SEAM, (int16_t)SDL_max(0, mid - 32), cy});
            m.passes.push_back({PASS_SEAM, (int16_t)SDL_min(mid, max_x), cy});
        }
        m.pass_frames.resize(m.passes.size());
    }

    Beam_Capture::~Beam_Capture() {
        if (impl_->active) {
            render::capture::End();
            s_sound_recording = false;
            MOX::cur_combat_x = impl_->saved_cur_x;
            MOX::cur_combat_y = impl_->saved_cur_y;
        }
        delete impl_;
    }

    bool Beam_Capture::Active() const {
        return impl_->active;
    }

    bool Beam_Capture::Reference_Pass() const {
        return impl_->active && impl_->passes[impl_->pass].kind == PASS_REFERENCE;
    }

    void Beam_Capture::Start_Pass(int16_t* ax, int16_t* ay, int16_t* tx, int16_t* ty, s_beam_fx_entry* entries) {
        s_impl& m = *impl_;
        if (!m.active) {
            return;
        }
        const s_impl::s_pass& p = m.passes[m.pass];
        MOX::cur_combat_x = p.cx;
        MOX::cur_combat_y = p.cy;
        *ax = (int16_t)(m.wax - p.cx * 20);
        *ay = (int16_t)(m.way - p.cy * 20);
        *tx = (int16_t)(m.wtx - p.cx * 20);
        *ty = (int16_t)(m.wty - p.cy * 20);
        memcpy(entries, m.entries, sizeof(m.entries));
        m.frame = 0;
        s_sound_frame = 0;
        s_sound_recording = true; // silent: the frontend plays the recorded sounds
        if (m.pass == 0) {
            s_sounds.clear();
        }
        bool first_capture = p.kind != PASS_REFERENCE;
        for (size_t i = 0; i < m.pass; ++i) {
            first_capture = first_capture && m.passes[i].kind == PASS_REFERENCE;
        }
        s_sound_collect = first_capture;
        if (first_capture) {
            s_beam_records.clear();
            s_record_cx = p.cx;
            s_record_cy = p.cy;
        }
        if (p.kind == PASS_REFERENCE) {
            video::Set_Page_Off();
            CMBTDRW1::Draw_Main_Combat_Screen();
            render::capture::Resolve_Window(&m.background, &m.window_w, &m.window_h);
            m.reference_start = SDL_GetTicks();
        } else {
            m.pass_start = SDL_GetTicksNS();
            render::capture::Begin(640, 351);
        }
    }

    void Beam_Capture::End_Frame() {
        s_impl& m = *impl_;
        if (!m.active) {
            return;
        }
        const s_impl::s_pass& p = m.passes[m.pass];
        if (p.kind == PASS_REFERENCE) {
            std::vector<uint8_t> window;
            int32_t w = 0, h = 0;
            render::capture::Resolve_Window(&window, &w, &h);
            m.reference.push_back(std::move(window));
        } else {
            render::capture::s_frame f;
            if (render::capture::Frame(&f)) {
                m.pass_frames[m.pass].push_back(To_World(f, p.cx, p.cy));
            } else {
                m.pass_frames[m.pass].emplace_back();
            }
        }
        ++m.frame;
        s_sound_frame = m.frame;
    }

    bool Beam_Capture::Next_Pass() {
        s_impl& m = *impl_;
        if (!m.active) {
            return false;
        }
        if (m.passes[m.pass].kind == PASS_REFERENCE) {
            m.reference_ms = SDL_GetTicks() - m.reference_start;
        } else {
            m.ops = render::capture::Ops();
            m.unported = render::capture::Unported();
            render::capture::End();
            m.capture_ns += SDL_GetTicksNS() - m.pass_start;
        }
        s_sound_recording = false;
        s_sound_collect = false;
        ++m.pass;
        if (m.pass >= m.passes.size()) {
            m.pass = m.passes.size() - 1;
            return false;
        }
        return true;
    }

    void Beam_Capture::Finish() {
        s_impl& m = *impl_;
        if (!m.active) {
            return;
        }
        s_sound_recording = false;
        MOX::cur_combat_x = m.saved_cur_x;
        MOX::cur_combat_y = m.saved_cur_y;
        MOX::combat_near_x = m.saved_near_x;
        MOX::combat_near_y = m.saved_near_y;
        MOX::combat_mid_x = m.saved_mid_x;
        MOX::combat_mid_y = m.saved_mid_y;
        MOX::combat_far_x = m.saved_far_x;
        MOX::combat_far_y = m.saved_far_y;
        const int32_t S = render::capture::Scale();

        // The tiles stitched frame by frame.
        std::vector<size_t> tiles, seams;
        size_t compare_pass = SIZE_MAX;
        for (size_t i = 0; i < m.passes.size(); ++i) {
            if (m.passes[i].kind == PASS_TILE) {
                tiles.push_back(i);
            } else if (m.passes[i].kind == PASS_SEAM) {
                seams.push_back(i);
            } else if (m.passes[i].kind == PASS_COMPARE) {
                compare_pass = i;
            }
        }
        size_t frames = 0;
        bool counts_agree = true;
        for (size_t t : tiles) {
            frames = SDL_max(frames, m.pass_frames[t].size());
            counts_agree = counts_agree && m.pass_frames[t].size() == m.pass_frames[tiles[0]].size();
        }
        auto stitch = [&](const std::vector<size_t>& from, size_t k) {
            std::vector<const s_world_frame*> parts;
            for (size_t t : from) {
                parts.push_back(k < m.pass_frames[t].size() ? &m.pass_frames[t][k] : nullptr);
            }
            return Stitch(parts);
        };
        s_capture_set set;
        set.x0 = INT32_MAX;
        set.y0 = INT32_MAX;
        set.x1 = INT32_MIN;
        set.y1 = INT32_MIN;
        uint64_t bytes = 0, glass_px = 0;
        for (size_t k = 0; k < frames; ++k) {
            s_world_frame f = stitch(tiles, k);
            if (f.width != 0) {
                set.x0 = SDL_min(set.x0, f.x);
                set.y0 = SDL_min(set.y0, f.y);
                set.x1 = SDL_max(set.x1, f.x + f.width / S);
                set.y1 = SDL_max(set.y1, f.y + f.height / S);
                bytes += f.rgba.size() + f.glass.size();
                for (uint8_t g : f.glass) {
                    glass_px += g != 0;
                }
            }
            set.frames.push_back(std::move(f));
        }
        if (set.x1 < set.x0) {
            set.x0 = set.y0 = set.x1 = set.y1 = 0;
        }
        // ANTARES_CAPTURE_DUMP_TILED=1: every stitched frame of volleys that needed several tiles.
        if (m.tiles > 1 && Env_Int("CAPTURE_DUMP_TILED", 0) != 0) {
            for (size_t k = 0; k < set.frames.size(); ++k) {
                const s_world_frame& f = set.frames[k];
                if (f.width != 0) {
                    char name[96];
                    snprintf(name, sizeof(name), "tiled_volley%d_frame%02zu_at_%d_%d", m.volley, k, f.x, f.y);
                    Dump_Png(name, f.rgba.data(), f.width, f.height);
                }
            }
            for (size_t t : tiles) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "capture: volley %d tile window at %d,%d squares", m.volley, (int)m.passes[t].cx, (int)m.passes[t].cy);
            }
        }

        // The comparison (ANTARES_CAPTURE_COMPARE): capture over the background against legacy's
        // own frames at the same window; the forced seam against the single-window capture.
        std::string compare_text;
        if (m.compare && compare_pass != SIZE_MAX) {
            const std::vector<s_world_frame>& cap = m.pass_frames[compare_pass];
            uint64_t differ = 0, effect = 0;
            uint64_t differ_glass = 0, differ_captured = 0, differ_outside = 0;
            const size_t n = SDL_min(cap.size(), m.reference.size());
            for (size_t k = 0; k < n; ++k) {
                std::vector<uint8_t> composite = m.background;
                if (cap[k].width != 0) {
                    render::capture::s_frame page;
                    page.x = cap[k].x - m.passes[compare_pass].cx * 20;
                    page.y = cap[k].y - m.passes[compare_pass].cy * 20;
                    page.width = cap[k].width;
                    page.height = cap[k].height;
                    page.rgba = cap[k].rgba;
                    page.glass = cap[k].glass;
                    render::capture::Composite(page, &composite, m.window_w);
                }
                const std::vector<uint8_t>& ref = m.reference[k];
                for (size_t i = 0; i + 3 < ref.size() && i + 3 < composite.size(); i += 4) {
                    const bool in_effect = memcmp(&ref[i], &m.background[i], 3) != 0 || memcmp(&composite[i], &m.background[i], 3) != 0;
                    if (!in_effect) {
                        continue;
                    }
                    ++effect;
                    if (abs((int)ref[i] - (int)composite[i]) > 24 || abs((int)ref[i + 1] - (int)composite[i + 1]) > 24 ||
                        abs((int)ref[i + 2] - (int)composite[i + 2]) > 24) {
                        ++differ;
                        // Which part of the capture the pixel is in (window pixel -> frame pixel).
                        const int32_t S4 = render::capture::Scale();
                        const int32_t wx = (int32_t)((i / 4) % (size_t)m.window_w);
                        const int32_t wy = (int32_t)((i / 4) / (size_t)m.window_w);
                        const int32_t fx = wx - (cap[k].x - m.passes[compare_pass].cx * 20) * S4;
                        const int32_t fy = wy - (cap[k].y - m.passes[compare_pass].cy * 20) * S4;
                        if (cap[k].width != 0 && fx >= 0 && fy >= 0 && fx < cap[k].width && fy < cap[k].height) {
                            const size_t fi = (size_t)fy * cap[k].width + fx;
                            if (cap[k].glass[fi] != 0) {
                                ++differ_glass;
                            } else if (cap[k].rgba[fi * 4 + 3] != 0) {
                                ++differ_captured;
                            } else {
                                ++differ_outside;
                            }
                        } else {
                            ++differ_outside;
                        }
                    }
                }
                // ANTARES_CAPTURE_DUMP_ALL=v1,v2: every frame of those volleys, else the middle one.
                const char* all = antares::Get_Env("CAPTURE_DUMP_ALL");
                const std::string tag = "," + std::to_string(m.volley) + ",";
                const bool every = all != nullptr && ("," + std::string(all) + ",").find(tag) != std::string::npos;
                if (k == n / 2 || every) {
                    char num[16];
                    snprintf(num, sizeof(num), "%02zu", k);
                    const std::string base = "volley" + std::to_string(m.volley) + "_frame" + num;
                    Dump_Png(base + "_legacy", ref.data(), m.window_w, m.window_h);
                    Dump_Png(base + "_capture_over_bg", composite.data(), m.window_w, m.window_h);
                    Dump_Png(base + "_background", m.background.data(), m.window_w, m.window_h);
                    if (cap[k].width != 0) {
                        Dump_Png(base + "_capture", cap[k].rgba.data(), cap[k].width, cap[k].height);
                    }
                }
            }
            char text[256];
            snprintf(text, sizeof(text), " compare: frames legacy %zu capture %zu, effect px %llu, differ %.2f%% (glass %.2f%%, captured %.2f%%, outside the capture %.2f%%), legacy %llu ms (%.1f ms/frame)", m.reference.size(), cap.size(), (unsigned long long)effect, effect != 0 ? 100.0 * differ / effect : 0.0, effect != 0 ? 100.0 * differ_glass / effect : 0.0, effect != 0 ? 100.0 * differ_captured / effect : 0.0, effect != 0 ? 100.0 * differ_outside / effect : 0.0, (unsigned long long)m.reference_ms, m.reference.empty() ? 0.0 : (double)m.reference_ms / m.reference.size());
            compare_text = text;
            if (seams.size() == 2 && !tiles.empty()) {
                uint64_t seam_differ = 0, seam_painted = 0;
                for (size_t k = 0; k < frames; ++k) {
                    const s_world_frame one = stitch(tiles, k);
                    const s_world_frame split = stitch(seams, k);
                    uint64_t frame_differ = 0, frame_painted = 0;
                    World_Diff(one, split, &frame_differ, &frame_painted);
                    seam_differ += frame_differ;
                    seam_painted += frame_painted;
                    const char* all = antares::Get_Env("CAPTURE_DUMP_ALL");
                    const bool every = all != nullptr && ("," + std::string(all) + ",").find("," + std::to_string(m.volley) + ",") != std::string::npos;
                    if ((k == frames / 2 || every) && split.width != 0) {
                        char num[16];
                        snprintf(num, sizeof(num), "%02zu", k);
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "capture: volley %d seam frame %zu: differ %llu of %llu px (one at %d,%d %dx%d, split at %d,%d %dx%d)", m.volley, k, (unsigned long long)frame_differ, (unsigned long long)frame_painted, one.x, one.y, one.width, one.height, split.x, split.y, split.width, split.height);
                        Dump_Png("volley" + std::to_string(m.volley) + "_seam_split_" + num, split.rgba.data(), split.width, split.height);
                        if (one.width != 0) {
                            Dump_Png("volley" + std::to_string(m.volley) + "_seam_one_" + num, one.rgba.data(), one.width, one.height);
                        }
                    }
                }
                snprintf(text, sizeof(text), "; seam (split at %d): differ %.2f%% of %llu px", (int)m.passes[seams[1]].cx, seam_painted != 0 ? 100.0 * seam_differ / seam_painted : 0.0, (unsigned long long)seam_painted);
                compare_text += text;
            }
        }

        std::string weapons;
        for (int32_t e = 0; e < 10 && m.entries[e].weapon_id != 0; ++e) {
            weapons += (weapons.empty() ? "" : ",") + std::to_string(m.entries[e].weapon_id);
        }
        // ANTARES_CAPTURE_DUMP_WEAPONS=7,10: volleys firing those weapons, every stitched frame
        // (PNG named with its world position) and the Draw_Beam2 calls (JSON).
        if (const char* want = antares::Get_Env("CAPTURE_DUMP_WEAPONS")) {
            bool match = false;
            for (int32_t e = 0; e < 10 && m.entries[e].weapon_id != 0; ++e) {
                match = match || ("," + std::string(want) + ",").find("," + std::to_string(m.entries[e].weapon_id) + ",") != std::string::npos;
            }
            const char* dir = antares::Get_Env("CAPTURE_DUMP");
            if (match && dir != nullptr) {
                for (size_t k = 0; k < set.frames.size(); ++k) {
                    const s_world_frame& f = set.frames[k];
                    if (f.width != 0) {
                        char name[96];
                        snprintf(name, sizeof(name), "w_volley%d_frame%02zu_at_%d_%d", m.volley, k, f.x, f.y);
                        Dump_Png(name, f.rgba.data(), f.width, f.height);
                    }
                }
                const std::string path = std::string(dir) + "/w_volley" + std::to_string(m.volley) + "_beams.json";
                if (FILE* out = fopen(path.c_str(), "w")) {
                    fprintf(out, "{\"volley\":%d,\"weapons\":[%s],\"scale\":%d,\"calls\":[\n", m.volley, weapons.c_str(), S);
                    for (size_t i = 0; i < s_beam_records.size(); ++i) {
                        fprintf(out, "%s%s\n", s_beam_records[i].c_str(), i + 1 < s_beam_records.size() ? "," : "");
                    }
                    fprintf(out, "]}\n");
                    fclose(out);
                }
            }
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
            "capture: volley %d weapons %s ship %d -> %d%s: span %dx%d squares, %d tile(s), %zu frames%s, %.1f ms, %llu KB, glass px %llu, tables %zu, sounds %zu; ops %s; unported %s;%s",
            m.volley,
            weapons.c_str(),
            (int)m.from,
            (int)m.to,
            m.non_ship != 0 ? " (missile)" : "",
            m.span_w,
            m.span_h,
            m.tiles,
            frames,
            counts_agree ? "" : " (tile frame counts differ)",
            (double)m.capture_ns / 1e6,
            (unsigned long long)(bytes / 1024),
            (unsigned long long)glass_px,
            render::capture::Tables().size(),
            s_sounds.size(),
            m.ops.c_str(),
            m.unported.empty() ? "none" : m.unported.c_str(),
            compare_text.c_str());

        const int32_t id = s_capture_next++;
        char json[256];
        std::string sounds = "[";
        for (size_t i = 0; i < s_sounds.size(); ++i) {
            sounds += (i ? "," : "") + std::string("[") + std::to_string(s_sounds[i].first) + "," + std::to_string(s_sounds[i].second) + "]";
        }
        sounds += "]";
        snprintf(json, sizeof(json), "{\"lbx\":\"capture\",\"entry\":%d,\"x\":%d,\"y\":%d,\"frames\":%zu,\"glass_px\":%llu,", id, set.x0, set.y0, set.frames.size(), (unsigned long long)glass_px);
        s_beam_capture_json = std::string(json) + "\"sounds\":" + sounds + "}";
        s_captures[id] = std::move(set);
        while (s_captures.size() > 48) {
            s_captures.erase(s_captures.begin());
        }
        m.active = false;
        Beam(m.from, m.to, m.non_ship, m.entries);
    }

    bool Export_Capture(int32_t entry, sprites::s_sprite* out) {
        auto it = s_captures.find(entry);
        if (it == s_captures.end()) {
            out->error = "no such capture";
            return false;
        }
        const s_capture_set& set = it->second;
        const int32_t S = render::capture::Scale();
        out->lbx = "capture";
        out->entry = entry;
        out->width = SDL_max(1, set.x1 - set.x0);
        out->height = SDL_max(1, set.y1 - set.y0);
        out->scale = S;
        out->hd = true;
        for (const s_world_frame& f : set.frames) {
            sprites::s_frame frame;
            if (f.width == 0) {
                frame.width = S;
                frame.height = S;
                frame.rgba.assign((size_t)S * S * 4, 0);
            } else {
                frame.width = f.width;
                frame.height = f.height;
                frame.origin_x = f.x - set.x0;
                frame.origin_y = f.y - set.y0;
                frame.rgba = f.rgba;
            }
            out->frames.push_back(std::move(frame));
        }
        return true;
    }

} // namespace antares::combat
