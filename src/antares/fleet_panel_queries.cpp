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
    }

} // namespace antares::commands::internal
