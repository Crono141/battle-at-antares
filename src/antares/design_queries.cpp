#include "pch.h"

#include "antares/commands_internal.h"
#include "antares/native.h"
#include "game/legacy_ids.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// WP3.6: the native Ship Design panel (notes/antares/screens/03-design.md, 03-design-pickers.md,
// the owner's mockup B2). The panel edits a working copy of one design slot, as the legacy
// screen does (MOX::design), but the copy is this file's own: MOX::design points at it only while
// a command runs, so the legacy screen and its allocations are never touched. Every edit goes
// through the game's own rules (DESIGN::, DESBOX::), with the picker state those helpers read
// (a list scroll bar, the weapon filter, the replacement slot) set up for the one call. Only
// BUILD writes the game: DESIGN::Update_Player_Design into the player's slot.

namespace antares::commands::internal {

    namespace {

        constexpr int32_t k_ship_pictures_per_colour = 50; // SHIPS.LBX: picture + colour * 50
        constexpr int16_t k_weapon_ids = 40;
        constexpr int16_t k_special_ids = 39;
        constexpr int16_t k_slots = 8;
        constexpr int16_t k_design_music = 8; // STREAM.LBX 8: the design theme (Draw_Design_Screen)

        s_current_design s_work;
        bool s_open = false;
        int16_t s_slot = -1;
        bool s_name_changed = false;
        s_weapon_filter_item s_filter[k_weapon_ids];
        char s_special_desc[k_special_ids][100];
        char s_weapon_notes[k_weapon_ids][100];
        bool s_text_loaded = false;

        // The game's design state for one call: the working copy, the picker helpers' list and
        // scroll bar, every weapon filter on, no refit; all put back after.
        struct s_scope {
            s_current_design* design = MOX::design;
            s_weapon_filter_item* filter = MOX::weapon_filter;
            s_scroll_bar* bar = MOX::scroll_bar;
            int16_t screen = MOX::current_screen;
            int16_t refit = MOX::refit_ship;
            int16_t replacement_slot = MOX::design_screen_replacement_slot;
            int16_t chosen = DESBOX::field_item_chosen;
            uint8_t filters[4] = {DESBOX::beam_filter_button_status, DESBOX::missile_filter_button_status,
                DESBOX::bomb_filter_button_status, DESBOX::special_filter_button_status};
            uint8_t changed_name = DESIGN::changed_design_name;
            s_scroll_bar own_bar{};

            s_scope() {
                MOX::design = &s_work;
                MOX::weapon_filter = s_filter;
                MOX::scroll_bar = &own_bar;
                MOX::refit_ship = 0;
                DESIGN::use_augmented_engines = 1;
                DESIGN::changed_design_name = s_name_changed ? 1 : 0;
                DESBOX::beam_filter_button_status = 1;
                DESBOX::missile_filter_button_status = 1;
                DESBOX::bomb_filter_button_status = 1;
                DESBOX::special_filter_button_status = 1;
            }
            ~s_scope() {
                MOX::design = design;
                MOX::weapon_filter = filter;
                MOX::scroll_bar = bar;
                MOX::current_screen = screen;
                MOX::refit_ship = refit;
                MOX::design_screen_replacement_slot = replacement_slot;
                DESBOX::field_item_chosen = chosen;
                DESBOX::beam_filter_button_status = filters[0];
                DESBOX::missile_filter_button_status = filters[1];
                DESBOX::bomb_filter_button_status = filters[2];
                DESBOX::special_filter_button_status = filters[3];
                DESIGN::changed_design_name = changed_name;
            }
        };

        // The special descriptions and weapon notes the screen prints (TECHDESC.LBX entries 1 and
        // 3, Load_Design_Screen_Pictures), read once into this file's own buffers.
        void Load_Text() {
            if (s_text_loaded) {
                return;
            }
            char lbx[20];
            DESIGN::Get_Tech_Description_Lbx_Name(lbx, sizeof(lbx));
            for (int16_t i = 0; i < k_special_ids; ++i) {
                Lbx_Record_Bytes(lbx, 1, (uint16_t)i, s_special_desc[i], sizeof(s_special_desc[i]) - 1);
                s_special_desc[i][sizeof(s_special_desc[i]) - 1] = '\0';
            }
            for (int16_t i = 0; i < k_weapon_ids; ++i) {
                Lbx_Record_Bytes(lbx, 3, (uint16_t)i, s_weapon_notes[i], sizeof(s_weapon_notes[i]) - 1);
                s_weapon_notes[i][sizeof(s_weapon_notes[i]) - 1] = '\0';
            }
            s_text_loaded = true;
        }

        bool Researched(uint16_t app) {
            return MOX::player[MOX::PLAYER_NUM].tech_applications[app] == TECH_RESEARCH_STATUS_RESEARCHED;
        }

        const char* Category(int16_t weapon) {
            switch (DESIGN::Weapon_Type(weapon)) {
                case WEAPON_TYPE_BEAM: return "beam";
                case WEAPON_TYPE_MISSILE:
                case WEAPON_TYPE_TORPEDO: return "missile";
                case WEAPON_TYPE_BOMB: return "bomb";
                default: return "special";
            }
        }

        // The firing arc as the panel's glyphs number them: 0 forward, 1 forward extended,
        // 2 back extended, 3 back, 4 360 (the picker's arc box, FORWARD << i).
        int16_t Arc_Index(int8_t arc) {
            for (int16_t i = 0; i < 5; ++i) {
                if ((uint8_t)arc == (uint8_t)(WEAPON_FIRING_ARC_FORWARD << i)) {
                    return i;
                }
            }
            return 4;
        }

        bool Have_Session(s_out& out) {
            if (!Have_Game(out)) {
                return false;
            }
            if (!s_open) {
                out.Fail("refused", "no design open (design_open first)");
                return false;
            }
            return true;
        }

        // The weapon picker's list (DESBOX::Weapon_Choices, every filter on) and the row of
        // `weapon` in it, -1 when the player can't fit that weapon here (not researched, or a
        // second Tractor Beam).
        int16_t Weapon_Row(int16_t weapon, int16_t slot) {
            MOX::design_screen_replacement_slot = slot;
            DESBOX::field_item_chosen = -1;
            DESBOX::Weapon_Choices(MOX::PLAYER_NUM);
            for (int16_t row = 1; row < MOX::scroll_bar->total_rows && row < k_weapon_ids; ++row) {
                if (MOX::weapon_filter[row].weapon_idx == weapon) {
                    return row;
                }
            }
            return -1;
        }

        int16_t First_Empty_Weapon_Slot() {
            for (int16_t i = 0; i < k_slots; ++i) {
                if (s_work.weapon_type[i] <= 0) {
                    return i;
                }
            }
            return -1;
        }

        int16_t First_Empty_Special_Slot() {
            for (int16_t i = 0; i < k_slots; ++i) {
                if (s_work.special_devices[i] <= 0) {
                    return i;
                }
            }
            return -1;
        }

        // The mods a weapon can carry here (Valid_Modifications and Mod_Allowed_On_Weapon_Type,
        // the picker's own tests); `row` is its row in the weapon list.
        std::vector<int16_t> Valid_Mods(int16_t row) {
            std::vector<int16_t> mods;
            DESBOX::field_item_chosen = row;
            DESBOX::Valid_Modifications(DESBOX::valid_mods);
            for (int16_t i = 1; i < WEAPON_MOD_COUNT; ++i) {
                if (DESBOX::valid_mods[i] == 1 && DESBOX::Mod_Allowed_On_Weapon_Type((uint8_t)i)) {
                    mods.push_back(i);
                }
            }
            return mods;
        }

        void Mods_Out(uint16_t mask, s_out list) {
            for (int16_t i = 1; i < WEAPON_MOD_COUNT; ++i) {
                if ((mask >> i) & 1) {
                    s_out mod = list.Append_Object();
                    mod.Int("mod", i);
                    mod.Str("name", Utf8(TECHDATA::weapon_modifications[i].name, 40));
                }
            }
        }

        // The working copy as the screen shows it (Print_Current_Design): hull, picture, systems,
        // the ship's combat values, space and cost, the weapons and specials.
        void Design_Out(s_out& out) {
            s_current_design& d = s_work;
            const s_player& player = MOX::player[MOX::PLAYER_NUM];
            out.Int("slot", s_slot);
            out.Str("name", Utf8(d.name, sizeof(d.name)));
            out.Int("size", d.ship_size);
            out.Int("picture", d.picture_type + player.color * k_ship_pictures_per_colour);
            out.Bool("picture_cycles", d.ship_size != SHIP_SIZE_DOOM_STAR);
            // Each hull's space with this design's tech (Total_Design_Space for that size).
            s_out sizes = out.Add_List("sizes");
            const int16_t size_now = d.ship_size;
            for (int16_t i = 0; i < SHIP_SIZE_COUNT; ++i) {
                d.ship_size = i;
                s_out size = sizes.Append_Object();
                size.Int("size", i);
                size.Str("name", Utf8(TECHDATA::hull_data[i].name, 40));
                size.Int("space", DESIGN::Total_Design_Space());
            }
            d.ship_size = size_now;
            s_out drive = out.Add_Object("drive");
            drive.Str("name", Utf8(TECHDATA::drives[d.ftl_type].name, 40));
            drive.Int("parsecs", TECHDATA::drives[d.ftl_type].warp_speed);
            drive.Int("combat_speed", DESIGN::Current_Design_Combat_Speed(&s_work));
            s_out armor = out.Add_Object("armor");
            armor.Str("name", Utf8(TECHDATA::armor[d.armor_type].name, 40));
            armor.Int("structure", DESIGN::Current_Design_Structural_Points());
            armor.Int("armor", DESIGN::Current_Design_Armor_Absorption());
            s_out shield = out.Add_Object("shield");
            shield.Int("shield", d.shield);
            shield.Str("name", d.shield == SHIELD_NO_SHIELD ? Utf8(HAROLD::H_Message(HESTR_036_NO_SHIELD), 40) : Utf8(TECHDATA::shields[d.shield].name, 40));
            shield.Int("strength", d.shield == SHIELD_NO_SHIELD ? 0 : DESIGN::Shield_Strength());
            shield.Int("blocked", d.shield == SHIELD_NO_SHIELD ? 0 : DESIGN::Current_Shield_Damage_Points_Blocked());
            s_out computer = out.Add_Object("computer");
            computer.Int("computer", d.computer_type);
            computer.Str("name", Utf8(TECHDATA::computers[d.computer_type].name, 40));
            s_ship_design template_design;
            DESIGN::Build_Design_Template(&template_design);
            int16_t attack = 0;
            int16_t defense = 0;
            int16_t evasion = 0;
            INITSHIP::Get_Design_Combat_Bonuses(template_design, &attack, &defense, &evasion);
            out.Int("beam_attack", attack);
            out.Int("beam_defense", defense);
            out.Int("missile_evasion", evasion);
            out.Int("hull_space", d.hull_space);
            out.Int("space_used", d.space_used);
            out.Int("cost", DESIGN::Printed_Design_Cost());
            out.Bool("buildable", d.hull_space >= d.space_used);
            int32_t weapon_space = 0;
            s_out weapons = out.Add_List("weapons");
            for (int16_t i = 0; i < k_slots; ++i) {
                const int16_t w = d.weapon_type[i];
                if (w <= 0) {
                    continue;
                }
                weapon_space += d.weapon_space[i];
                s_out item = weapons.Append_Object();
                const int16_t count = std::max<int16_t>(int16_t(d.weapon_count[i]), 1);
                item.Int("slot", i);
                item.Int("weapon", w);
                item.Str("name", Utf8(count < 2 ? TECHDATA::weapons[w].name : TECHDATA::weapons[w].name_plural, 40));
                item.Str("category", Category(w));
                item.Int("count", count);
                item.Int("arc", Arc_Index(d.weapon_firing_arc[i]));
                item.Int("ammo", DESIGN::Weapon_Needs_Racks(w) ? d.weapon_ammo[i] : 0);
                Mods_Out(d.weapon_specials[i], item.Add_List("mods"));
                char damage[40];
                DESIGN::Design_Weapon_Damage_String(i, damage, sizeof(damage), -1, -1);
                item.Str("damage", Utf8(damage, sizeof(damage)));
                item.Int("space", d.weapon_space[i]);
                item.Int("cost", d.weapon_cost[i]);
                // + fits when one more of the same keeps the ship within its hull (Design_Screen).
                const int32_t more = DESIGN::Weapon_Space(MOX::PLAYER_NUM, w, (int16_t)(count + 1), (uint8_t)d.weapon_firing_arc[i],
                    (uint8_t)d.weapon_ammo[i], d.weapon_specials[i]);
                item.Bool("can_add", count < 99 && d.space_used + (more - d.weapon_space[i]) <= d.hull_space);
                item.Str("notes", Utf8(s_weapon_notes[w < k_weapon_ids ? w : 0], 100));
            }
            int32_t special_space = 0;
            s_out specials = out.Add_List("specials");
            for (int16_t i = 0; i < k_slots; ++i) {
                const int16_t sp = d.special_devices[i];
                if (sp <= 0) {
                    continue;
                }
                special_space += d.special_device_space[i];
                s_out item = specials.Append_Object();
                item.Int("slot", i);
                item.Int("special", sp);
                item.Str("name", Utf8(TECHDATA::specials[sp].name, 40));
                item.Str("description", Utf8(s_special_desc[sp < k_special_ids ? sp : 0], 100));
                item.Int("space", d.special_device_space[i]);
                item.Int("cost", d.special_device_cost[i]);
            }
            out.Int("weapon_space", weapon_space);
            out.Int("special_space", special_space);
            out.Bool("weapon_slot_free", First_Empty_Weapon_Slot() >= 0);
            out.Bool("special_slot_free", First_Empty_Special_Slot() >= 0);
        }

        void Recalculate() {
            DESIGN::Update_Calculated_Design_Data(MOX::PLAYER_NUM);
        }

        void Rename_From_Picture() {
            if (!s_name_changed) {
                const int16_t race = (uint8_t)MOX::player[MOX::PLAYER_NUM].race;
                DESIGN::Get_Ship_Design_Name(race, s_work.ship_size, (int16_t)(s_work.picture_type % 8), s_work.name, sizeof(s_work.name));
            }
        }

        // ---- The session ----

        // design_open {slot}: the slot's design as the screen opens it
        // (Build_Initial_Design_Template: best drive, armour and fuel, the CFG's shield and computer).
        void Design_Open(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t slot = -1;
            if (!args.Int("slot", &slot) || slot < 0 || slot > 5) {
                out.Fail("bad_args", "slot: 0-5");
                return;
            }
            Load_Text();
            s_work = s_current_design{};
            s_slot = (int16_t)slot;
            s_name_changed = false;
            s_open = true;
            native::Hold_Music(k_design_music);
            s_scope scope;
            DESIGN::Build_Initial_Design_Template(MOX::PLAYER_NUM, s_slot);
            Recalculate();
            Design_Out(out);
        }

        void Query_Design(int16_t, const s_args&, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            Design_Out(out);
        }

        // design_edit {size | picture: ±1 | name}: the hull (its first picture and, unless renamed,
        // its name, as a size button does), the picture (Check_Plus_Minus_Buttons) or the name.
        void Design_Edit(int16_t, const s_args& args, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            int64_t size = -1;
            int64_t step = 0;
            std::string name;
            if (args.Int("size", &size)) {
                if (size < 0 || size >= SHIP_SIZE_COUNT) {
                    out.Fail("bad_args", "size: 0-5");
                    return;
                }
                if (s_work.ship_size != size) {
                    s_work.ship_size = (int16_t)size;
                    s_work.picture_type = size == SHIP_SIZE_DOOM_STAR ? 43 : (int16_t)(size * 8);
                }
                // A size button always names the ship anew (design_main.cpp:449-461).
                s_name_changed = false;
                Rename_From_Picture();
            }
            if (args.Int("picture", &step) && step != 0 && s_work.ship_size != SHIP_SIZE_DOOM_STAR) {
                const int16_t first = (int16_t)(s_work.ship_size * 8);
                s_work.picture_type = (int16_t)(first + (s_work.picture_type - first + (step > 0 ? 1 : 7)) % 8);
                Rename_From_Picture();
            }
            if (args.Str("name", &name)) {
                // CP850 in the game: plain ASCII only, 15 characters, trimmed; empty is the slot's.
                std::string clean;
                for (char c : name) {
                    if ((unsigned char)c >= 32 && (unsigned char)c < 127 && clean.size() < 15) {
                        clean += c;
                    }
                }
                while (!clean.empty() && clean.back() == ' ') {
                    clean.pop_back();
                }
                while (!clean.empty() && clean.front() == ' ') {
                    clean.erase(clean.begin());
                }
                if (clean.empty()) {
                    s_name_changed = false;
                    Rename_From_Picture();
                } else {
                    s_name_changed = true;
                    strlcpy(s_work.name, clean.c_str(), sizeof(s_work.name));
                }
            }
            Recalculate();
            Design_Out(out);
        }

        // design_weapon {slot (-1: a new row), weapon (0 removes), arc 0-4, rack 0-4, mods [..]}:
        // the weapon picker's ACCEPT (DESBOX::Update_Weapon_Replacement) with its refusals: a weapon
        // not in its list, a mod it doesn't offer, Heavy Mount with Point Defense.
        void Design_Weapon(int16_t, const s_args& args, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            int64_t slot = -1;
            int64_t weapon = -1;
            int64_t arc = 0;
            int64_t rack = DESIGN::missile_default_x2_rack ? 0 : 1;
            std::vector<int64_t> mods;
            args.Int("slot", &slot);
            args.Int_List("mods", &mods);
            args.Int("arc", &arc);
            args.Int("rack", &rack);
            if (!args.Int("weapon", &weapon) || weapon < 0 || weapon >= k_weapon_ids) {
                out.Fail("bad_args", "weapon: 0 (none) or a weapon id");
                return;
            }
            if (slot < 0) {
                slot = First_Empty_Weapon_Slot();
                if (slot < 0 && weapon > 0) {
                    out.Fail("refused", "all eight weapon rows are in use");
                    return;
                }
            }
            if (slot < 0 || slot >= k_slots) {
                out.Fail("bad_args", "slot: 0-7 or -1");
                return;
            }
            Recalculate(); // the counts Clear_Weapon_Slot trusts
            if (weapon == 0) {
                if (s_work.weapon_type[slot] > 0) {
                    DESIGN::Clear_Weapon_Slot((int16_t)slot);
                }
                Recalculate();
                Design_Out(out);
                return;
            }
            const int16_t row = Weapon_Row((int16_t)weapon, (int16_t)slot);
            if (row < 0) {
                out.Fail("refused", "that weapon can't go on this design");
                return;
            }
            const std::vector<int16_t> valid = Valid_Mods(row);
            memset(MOX::weapon_mod_field_status, 0, sizeof(MOX::weapon_mod_field_status));
            bool heavy = false;
            bool point_defense = false;
            for (int64_t m : mods) {
                if (std::find(valid.begin(), valid.end(), (int16_t)m) == valid.end()) {
                    out.Fail("refused", "that modification isn't available for this weapon");
                    return;
                }
                heavy = heavy || m == WEAPON_MOD_HEAVY_MOUNT;
                point_defense = point_defense || m == WEAPON_MOD_POINT_DEFENSE;
                MOX::weapon_mod_field_status[m] = 1;
            }
            if (heavy && point_defense) {
                out.Fail("refused", "Heavy Mount and Point Defense exclude each other");
                return;
            }
            const bool beam = DESIGN::Weapon_Type((int16_t)weapon) == WEAPON_TYPE_BEAM;
            DESBOX::weapon_replacement_arcs = beam ? (int16_t)(WEAPON_FIRING_ARC_FORWARD << std::clamp<int64_t>(arc, 0, 4)) : (int16_t)WEAPON_FIRING_ARC_360;
            DESBOX::weapon_replacement_rack = (uint16_t)std::clamp<int64_t>(rack, 0, 4);
            DESBOX::field_item_chosen = row;
            int16_t done = 0;
            DESBOX::Update_Weapon_Replacement(&done, row, (int16_t)slot);
            Recalculate();
            Design_Out(out);
        }

        // design_weapon_count {slot, delta ±1}: the row's − / + (design_main.cpp:418-438): one more
        // only while it fits, never below one (removing is design_weapon weapon 0).
        void Design_Weapon_Count(int16_t, const s_args& args, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            int64_t slot = -1;
            int64_t delta = 0;
            if (!args.Int("slot", &slot) || slot < 0 || slot >= k_slots || s_work.weapon_type[slot] <= 0 || !args.Int("delta", &delta)) {
                out.Fail("bad_args", "slot: a fitted weapon row, delta: 1 or -1");
                return;
            }
            if (delta > 0) {
                const int32_t more = DESIGN::Weapon_Space(MOX::PLAYER_NUM, s_work.weapon_type[slot], (int16_t)(s_work.weapon_count[slot] + 1),
                    (uint8_t)s_work.weapon_firing_arc[slot], (uint8_t)s_work.weapon_ammo[slot], s_work.weapon_specials[slot]);
                if (s_work.space_used + (more - s_work.weapon_space[slot]) <= s_work.hull_space && s_work.weapon_count[slot] < 99) {
                    s_work.weapon_count[slot]++;
                }
            } else if (delta < 0 && s_work.weapon_count[slot] > 1) {
                s_work.weapon_count[slot]--;
            }
            Recalculate();
            Design_Out(out);
        }

        // design_special {slot (-1: a new row), special (0 removes)}: the special picker's choice
        // (Special_Systems_Box) with its refusals: fitted already, excluded by a fitted one, eight
        // fitted. Like the original it doesn't check the space; BUILD waits until the ship fits.
        void Design_Special(int16_t, const s_args& args, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            int64_t slot = -1;
            int64_t special = -1;
            args.Int("slot", &slot);
            if (!args.Int("special", &special) || special < 0 || special >= k_special_ids) {
                out.Fail("bad_args", "special: 0 (none) or a special id");
                return;
            }
            if (slot < 0) {
                slot = First_Empty_Special_Slot();
                if (slot < 0 && special > 0) {
                    out.Fail("refused", "all eight special rows are in use");
                    return;
                }
            }
            if (slot < 0 || slot >= k_slots) {
                out.Fail("bad_args", "slot: 0-7 or -1");
                return;
            }
            Recalculate();
            MOX::design_screen_replacement_slot = (int16_t)slot;
            if (special == 0) {
                if (s_work.special_devices[slot] > 0) {
                    DESIGN::Clear_Special_System_Slot((int16_t)slot);
                }
                Recalculate();
                Design_Out(out);
                return;
            }
            DESBOX::Special_System_Choices();
            int16_t row = -1;
            for (int16_t i = 1; i < MOX::scroll_bar->total_rows; ++i) {
                if (MOX::design_choice_items[i] == special) {
                    row = i;
                }
            }
            if (row < 0) {
                out.Fail("refused", "that special isn't researched");
                return;
            }
            if (MOX::design_choice_item_selected[row] != 0 && s_work.special_devices[slot] != special) {
                out.Fail("refused", "that special is fitted already");
                return;
            }
            int16_t conflict = 0;
            if (DESBOX::Special_System_Exclusion((int16_t)special, &conflict) != 0) {
                out.Fail("refused", Utf8(TECHDATA::specials[special].name, 40) + " can't go with " + Utf8(TECHDATA::specials[conflict].name, 40));
                return;
            }
            if (DESBOX::Items_Selected() >= 8 && s_work.special_devices[slot] <= 0) {
                out.Fail("refused", "eight specials are fitted already");
                return;
            }
            s_work.special_devices[slot] = (int16_t)special;
            Recalculate();
            Design_Out(out);
        }

        // design_system {shield | computer}: the shield or computer picker (Generic_Replacement_Box)
        // with the refusals of Check_Generic_Replacement_Fields.
        void Design_System(int16_t, const s_args& args, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            int64_t value = -1;
            if (args.Int("shield", &value)) {
                if (value < 0 || value >= SHIELD_COUNT || (value > 0 && !Researched(TECHDATA::shields[value].tech_app_id))) {
                    out.Fail("bad_args", "shield: a researched shield");
                    return;
                }
                for (int16_t i = 0; i < k_slots; ++i) {
                    if (s_work.special_devices[i] == SPECIAL_DAMPER_FIELD) {
                        out.Fail("refused", Utf8(HAROLD::H_Message(0x51), 120));
                        return;
                    }
                }
                s_work.shield = (int16_t)value;
            } else if (args.Int("computer", &value)) {
                if (value < 0 || value >= COMPUTER_COUNT || (value > 0 && !Researched(TECHDATA::computers[value].tech_app_id))) {
                    out.Fail("bad_args", "computer: a researched computer");
                    return;
                }
                s_work.computer_type = (int16_t)value;
            } else {
                out.Fail("bad_args", "shield or computer");
                return;
            }
            Recalculate();
            Design_Out(out);
        }

        // design_clear: CLEAR (Clear_Design: no weapons or specials, the best drive, armour and fuel).
        void Design_Clear(int16_t, const s_args&, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            DESIGN::Clear_Design();
            Recalculate();
            Design_Out(out);
        }

        // design_build: BUILD (Update_Player_Design into the slot), only when the ship fits.
        void Design_Build(int16_t, const s_args&, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            Recalculate();
            if (s_work.hull_space < s_work.space_used) {
                out.Fail("refused", "the parts need more space than the hull has");
                return;
            }
            DESIGN::Update_Player_Design(MOX::PLAYER_NUM, s_slot);
            DESIGN::use_augmented_engines = 1;
            out.Int("slot", s_slot);
            out.Str("name", Utf8(MOX::player[MOX::PLAYER_NUM].ship_designs[s_slot].name, 16));
            out.Bool("built", true);
            s_open = false;
            native::Hold_Music(-1);
        }

        // auto_design: the build screen's Alt+A, D (colbldg.cpp, automatic_design_command) as a
        // command (owner, 2026-11-20: a button on the ship design screen): every design slot gets
        // the automatic design for the player's technology, custom designs included. With Doom
        // Star Construction researched the slots take the advanced level's hulls, as there.
        void Auto_Design(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            if (s_open) {
                out.Fail("busy", "a design is open in the design panel");
                return;
            }
            const int16_t player_idx = MOX::PLAYER_NUM;
            const uint8_t saved_civilization_level = MOX::settings.starting_civilization_level;
            if (MOX::player[player_idx].tech_applications[TECH_APP_DOOM_STAR_CONSTRUCTION] ==
                TECH_RESEARCH_STATUS_RESEARCHED) {
                MOX::settings.starting_civilization_level = 2;
            }
            INITSHIP::Init_Ship_Designs(player_idx);
            MOX::settings.starting_civilization_level = saved_civilization_level;
            out.Bool("designed", true);
        }

        // design_auto: the open design redone by the automatic designer for its hull (owner,
        // 2026-11-20: Auto Design per ship, to tweak, build or cancel). The game's designer writes
        // a saved design, so it designs into a copy, which the slot holds just long enough for
        // Build_Initial_Design_Template to load it as the working copy; nothing is saved until
        // BUILD. The picture stays, and the name if the player gave it one.
        void Design_Auto(int16_t, const s_args&, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            s_ship_design& slot_design = MOX::player[MOX::PLAYER_NUM].ship_designs[s_slot];
            const s_ship_design saved = slot_design;
            char name[sizeof(s_work.name)];
            std::memcpy(name, s_work.name, sizeof(name));
            const int16_t picture = s_work.picture_type;
            const int16_t size = s_work.ship_size;
            s_ship_design made = saved;
            made.size = (uint8_t)size;
            aidesign::Auto_Design_Ship(&game::current, legacy_world::Player_Id(MOX::PLAYER_NUM), &made, size);
            slot_design = made;
            DESIGN::Build_Initial_Design_Template(MOX::PLAYER_NUM, s_slot);
            slot_design = saved;
            s_work.ship_size = size;
            s_work.picture_type = picture;
            if (s_name_changed) {
                std::memcpy(s_work.name, name, sizeof(name));
            } else {
                Rename_From_Picture();
            }
            Recalculate();
            Design_Out(out);
        }

        // design_cancel: CANCEL (the working copy dropped).
        void Design_Cancel(int16_t, const s_args&, s_out& out) {
            s_open = false;
            native::Hold_Music(-1);
            out.Bool("cancelled", true);
        }

        // ---- The parts the panel offers ----

        // design_weapons: the weapon picker's list (researched, every filter on, one Tractor Beam):
        // each weapon's category, damage, notes, one unit's space and cost at its default arc and
        // rack, whether that fits, the arcs (beams) or racks (missiles) and mods it can take.
        void Query_Design_Weapons(int16_t, const s_args&, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            Recalculate();
            MOX::design_screen_replacement_slot = First_Empty_Weapon_Slot();
            if (MOX::design_screen_replacement_slot < 0) {
                MOX::design_screen_replacement_slot = 0;
            }
            DESBOX::field_item_chosen = -1;
            DESBOX::Weapon_Choices(MOX::PLAYER_NUM);
            std::vector<int16_t> rows;
            for (int16_t row = 1; row < MOX::scroll_bar->total_rows && row < k_weapon_ids; ++row) {
                rows.push_back(row);
            }
            const int16_t default_rack = DESIGN::missile_default_x2_rack ? 0 : 1;
            s_out list = out.Add_List("weapons");
            for (int16_t row : rows) {
                const int16_t w = MOX::weapon_filter[row].weapon_idx;
                if (w <= 0 || w >= k_weapon_ids) {
                    continue;
                }
                s_out item = list.Append_Object();
                const bool beam = DESIGN::Weapon_Type(w) == WEAPON_TYPE_BEAM;
                const bool racks = DESIGN::Weapon_Needs_Racks(w) != 0;
                const int16_t arc = beam ? WEAPON_FIRING_ARC_FORWARD : WEAPON_FIRING_ARC_360;
                const int16_t ammo = racks ? DESIGN::Missile_Rack_Quantity(default_rack) : 0;
                const int32_t space = DESIGN::Weapon_Space(MOX::PLAYER_NUM, w, 1, arc, ammo, 0);
                item.Int("weapon", w);
                item.Str("name", Utf8(TECHDATA::weapons[w].name, 40));
                item.Str("category", Category(w));
                item.Int("app", TECHDATA::weapons[w].tech_app_id); // its help record (the picker's right click)
                char damage[40];
                DESBOX::field_item_chosen = -1;
                DESIGN::Design_Weapon_Damage_String(w, damage, sizeof(damage), -1, 0);
                item.Str("damage", Utf8(damage, sizeof(damage)));
                item.Str("notes", Utf8(s_weapon_notes[w], 100));
                item.Int("space", space);
                item.Int("cost", DESIGN::Weapon_Cost(w, 1, arc, ammo, 0));
                item.Bool("fits", s_work.space_used + space <= s_work.hull_space);
                item.Bool("arcs", beam);
                if (racks) {
                    s_out list_racks = item.Add_List("racks");
                    for (int16_t r = 0; r < 5; ++r) {
                        list_racks.Append_Int(DESIGN::Missile_Rack_Quantity(r));
                    }
                    item.Int("rack", default_rack);
                }
                s_out mods = item.Add_List("mods");
                for (int16_t m : Valid_Mods(row)) {
                    s_out mod = mods.Append_Object();
                    mod.Int("mod", m);
                    mod.Str("name", Utf8(TECHDATA::weapon_modifications[m].name, 40));
                    mod.Int("help", 400 + m); // Print_Weapon_Mod_Help_Description: help record 400 + mod
                }
            }
        }

        // design_weapon_preview {weapon, arc, rack, mods}: what one unit would take and do with
        // those choices (the picker's selected row: Weapon_Space, Weapon_Cost, the damage string
        // with the mod toggles).
        void Query_Design_Weapon_Preview(int16_t, const s_args& args, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            int64_t weapon = -1;
            int64_t arc = 0;
            int64_t rack = DESIGN::missile_default_x2_rack ? 0 : 1;
            std::vector<int64_t> mods;
            if (!args.Int("weapon", &weapon) || weapon <= 0 || weapon >= k_weapon_ids) {
                out.Fail("bad_args", "weapon: a weapon id");
                return;
            }
            args.Int("arc", &arc);
            args.Int("rack", &rack);
            args.Int_List("mods", &mods);
            const int16_t w = (int16_t)weapon;
            const bool beam = DESIGN::Weapon_Type(w) == WEAPON_TYPE_BEAM;
            const int16_t arc_flag = beam ? (int16_t)(WEAPON_FIRING_ARC_FORWARD << std::clamp<int64_t>(arc, 0, 4)) : (int16_t)WEAPON_FIRING_ARC_360;
            const int16_t ammo = DESIGN::Weapon_Needs_Racks(w) ? DESIGN::Missile_Rack_Quantity((int16_t)std::clamp<int64_t>(rack, 0, 4)) : 0;
            uint16_t mask = 0;
            memset(MOX::weapon_mod_field_status, 0, sizeof(MOX::weapon_mod_field_status));
            for (int64_t m : mods) {
                if (m > 0 && m < WEAPON_MOD_COUNT) {
                    mask = (uint16_t)(mask | (1 << m));
                    MOX::weapon_mod_field_status[m] = 1;
                }
            }
            Recalculate();
            const int32_t space = DESIGN::Weapon_Space(MOX::PLAYER_NUM, w, 1, arc_flag, ammo, (int16_t)mask);
            char damage[40];
            DESIGN::Design_Weapon_Damage_String(w, damage, sizeof(damage), 0, 0);
            out.Int("weapon", w);
            out.Int("space", space);
            out.Int("cost", DESIGN::Weapon_Cost(w, 1, arc_flag, ammo, (int16_t)mask));
            out.Str("damage", Utf8(damage, sizeof(damage)));
            out.Int("ammo", ammo);
            out.Bool("fits", s_work.space_used + space <= s_work.hull_space);
            out.Int("free_after", s_work.hull_space - s_work.space_used - space);
        }

        // The tech review's group of an application (INFO::review_list 2-3: weapons, equipment), the
        // panel's headings for specials; "" when none lists it.
        std::string Group_Of(uint16_t app) {
            for (int l = 0; l < 4; ++l) {
                const s_tech_review_achievement* entry = INFO::review_list[l];
                int16_t group = -1;
                for (int i = 0; entry != nullptr && i < INFO::TECH_REVIEW_ACHIEVEMENT_COUNT; ++i, ++entry) {
                    if (entry->application == TECH_APP_NO_TECH && entry->group == TECH_REVIEW_GROUP_NONE) {
                        break;
                    }
                    if (entry->group < TECH_REVIEW_GROUP_COUNT) {
                        group = entry->group;
                    } else if (entry->application == app && group >= 0) {
                        char text[64] = {};
                        JIM::Get_Text_Message("billtex2.lbx", group, text, sizeof(text));
                        return Utf8(text, sizeof(text));
                    }
                }
            }
            return {};
        }

        // design_specials: the special picker's list (researched specials, Special_System_Choices)
        // with each one's group (the tech review's), description, space and cost on this hull
        // (System_Added_Cost_And_Space), whether it fits now, is fitted, or is ruled out by a fitted
        // special (Special_System_Exclusion).
        void Query_Design_Specials(int16_t, const s_args&, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            Recalculate();
            int16_t slot = First_Empty_Special_Slot();
            MOX::design_screen_replacement_slot = slot < 0 ? 0 : slot;
            DESBOX::Special_System_Choices();
            out.Int("fitted_count", DESBOX::Items_Selected());
            s_out list = out.Add_List("specials");
            for (int16_t row = 1; row < MOX::scroll_bar->total_rows; ++row) {
                const int16_t sp = MOX::design_choice_items[row];
                if (sp <= 0) {
                    continue;
                }
                int16_t cost = 0;
                int16_t space = 0;
                DESBOX::System_Added_Cost_And_Space(row, &cost, &space);
                bool fitted = false;
                for (int16_t i = 0; i < k_slots; ++i) {
                    fitted = fitted || s_work.special_devices[i] == sp;
                }
                int16_t conflict = 0;
                const bool excluded = !fitted && DESBOX::Special_System_Exclusion(sp, &conflict) != 0;
                s_out item = list.Append_Object();
                item.Int("special", sp);
                item.Str("name", Utf8(TECHDATA::specials[sp].name, 40));
                item.Int("app", TECHDATA::specials[sp].tech_app_id);
                item.Str("group", Group_Of(TECHDATA::specials[sp].tech_app_id));
                item.Str("description", Utf8(s_special_desc[sp], 100));
                item.Int("space", space);
                item.Int("cost", cost);
                item.Bool("fitted", fitted);
                item.Bool("fits", s_work.space_used + space <= s_work.hull_space);
                if (excluded) {
                    item.Str("excluded_by", Utf8(TECHDATA::specials[conflict].name, 40));
                }
            }
        }

        // design_systems: the shields and computers the player can choose (Generic_Replacement_Box
        // _Choices: the first always, the others researched), each with its cost and space.
        void Query_Design_Systems(int16_t, const s_args&, s_out& out) {
            if (!Have_Session(out)) {
                return;
            }
            s_scope scope;
            s_out shields = out.Add_List("shields");
            for (int16_t i = 0; i < SHIELD_COUNT; ++i) {
                if (i == 0 || Researched(TECHDATA::shields[i].tech_app_id)) {
                    s_out item = shields.Append_Object();
                    item.Int("shield", i);
                    item.Str("name", i == SHIELD_NO_SHIELD ? Utf8(HAROLD::H_Message(HESTR_036_NO_SHIELD), 40) : Utf8(TECHDATA::shields[i].name, 40));
                    item.Int("cost", i == 0 ? 0 : DESIGN::Shield_Cost(i));
                    item.Int("space", i == 0 ? 0 : DESIGN::Shield_Space(i, s_work.ship_size));
                }
            }
            s_out computers = out.Add_List("computers");
            for (int16_t i = 0; i < COMPUTER_COUNT; ++i) {
                if (i == 0 || Researched(TECHDATA::computers[i].tech_app_id)) {
                    s_out item = computers.Append_Object();
                    item.Int("computer", i);
                    item.Str("name", Utf8(TECHDATA::computers[i].name, 40));
                    item.Int("cost", i == 0 ? 0 : DESIGN::Computer_Cost(i));
                }
            }
        }

    } // namespace

    void Add_Design_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"design_open", e_kind::command, galaxy, 0, "slot: 0-5", Design_Open});
        table->push_back({"design", e_kind::query, ANY_SCREEN, 1, "", Query_Design});
        table->push_back({"design_edit", e_kind::command, galaxy, 0, "size 0-5, picture ±1, name", Design_Edit});
        table->push_back({"design_weapon", e_kind::command, galaxy, 0, "slot (-1 new), weapon (0 removes), arc 0-4, rack 0-4, mods", Design_Weapon});
        table->push_back({"design_weapon_count", e_kind::command, galaxy, 0, "slot, delta ±1", Design_Weapon_Count});
        table->push_back({"design_special", e_kind::command, galaxy, 0, "slot (-1 new), special (0 removes)", Design_Special});
        table->push_back({"design_system", e_kind::command, galaxy, 0, "shield or computer", Design_System});
        table->push_back({"design_clear", e_kind::command, galaxy, 0, "", Design_Clear});
        table->push_back({"design_build", e_kind::command, galaxy, 0, "", Design_Build});
        table->push_back({"design_cancel", e_kind::command, galaxy, 0, "", Design_Cancel});
        table->push_back({"design_auto", e_kind::command, galaxy, 0, "", Design_Auto});
        table->push_back({"auto_design", e_kind::command, galaxy, 0, "", Auto_Design});
        table->push_back({"design_weapons", e_kind::query, ANY_SCREEN, 1, "", Query_Design_Weapons});
        table->push_back({"design_weapon_preview", e_kind::query, ANY_SCREEN, 1, "weapon, arc, rack, mods", Query_Design_Weapon_Preview});
        table->push_back({"design_specials", e_kind::query, ANY_SCREEN, 1, "", Query_Design_Specials});
        table->push_back({"design_systems", e_kind::query, ANY_SCREEN, 1, "", Query_Design_Systems});
    }

} // namespace antares::commands::internal
