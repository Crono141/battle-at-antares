#include "pch.h"

#include "antares/commands_internal.h"

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
        table->push_back({"relocate_star", e_kind::command, galaxy, 0, "star: the from star, then the to star", Relocate_Star});
        table->push_back({"relocate_reset", e_kind::command, galaxy, 0, "", Relocate_Reset});
        table->push_back({"relocation_targets", e_kind::query, ANY_SCREEN, 1, "from: a star", Query_Relocation_Targets});
        table->push_back({"relocation_set", e_kind::command, galaxy, 0, "from, to: stars", Relocation_Set});
        table->push_back({"relocation_clear", e_kind::command, galaxy, 0, "from: a star", Relocation_Clear});
    }

} // namespace antares::commands::internal
