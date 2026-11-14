#include "pch.h"

#include "antares/todo.h"

#include "antares/commands_internal.h"
#include "antares/native.h"

#include <algorithm>
#include <string>
#include <vector>

// WP3.4d: the start-of-turn to-do list (todo.h). Items noted by the reports chain live until the
// stardate changes; empty build queues are read live. The `todo` query lists them; `todo_read`
// marks news read; `todo_hire` and `todo_colonize` run the window the item stands for (native
// popups), with the rules the chain would have run.

namespace antares::todo {
    namespace {
        enum class e_kind { new_system, leader_level, summary, colonize, hire };

        struct s_item {
            e_kind kind;
            int16_t star = -1;
            int16_t leader = -1;
            int16_t special = 0;
            bool joined = false;
            bool read = false;
            bool done = false;
        };

        std::vector<s_item> s_items;
        int32_t s_stardate = -1;

        void Same_Turn() {
            if (s_stardate != (int32_t)MOX::stardate) {
                s_items.clear();
                s_stardate = (int32_t)MOX::stardate;
            }
        }

        s_item* Find(e_kind kind, int16_t star, int16_t leader) {
            for (s_item& item : s_items) {
                if (item.kind == kind && item.star == star && item.leader == leader) {
                    return &item;
                }
            }
            return nullptr;
        }

        void Add(const s_item& item) {
            Same_Turn();
            if (Find(item.kind, item.star, item.leader) == nullptr) {
                s_items.push_back(item);
            }
        }

        bool Ship_Of_Type_At(int16_t star, int16_t type) {
            return REPORT::Player_Ship_Type_At_Star(MOX::PLAYER_NUM, type, star, 0) != nullptr;
        }

        bool Colony_Base_At(int16_t star) {
            return REPORT::Player_Colony_Base_At_Star(MOX::PLAYER_NUM, star) != nullptr;
        }

        // Still to do: news unread, a colony ship (or a colony base) still there, the offer still open.
        bool Resolved(const s_item& item) {
            switch (item.kind) {
                case e_kind::colonize:
                    return item.done ||
                        (!Ship_Of_Type_At(item.star, SHIP_TYPE_COLONY) && !Ship_Of_Type_At(item.star, SHIP_TYPE_OUTPOST) && !Colony_Base_At(item.star));
                case e_kind::hire: return item.done || MOX::player[MOX::PLAYER_NUM].officer_for_hire != item.leader;
                default: return item.read;
            }
        }

        bool Empty_Queue(const s_colony& colony) {
            return colony.owner == MOX::PLAYER_NUM && colony.outpost_flag == 0 && colony.producing[0] == COLONY_PRODUCTION_NONE;
        }

        std::string Star_Name(int16_t star) {
            return star >= 0 && star < MOX::NUM_STARS ? std::string(MOX::star[star].name) : std::string();
        }
    } // namespace

    bool Active() {
        return native::Todo_Native();
    }

    void Note_New_System(int16_t star, int16_t special, int16_t marooned_leader, bool joined) {
        s_item item{e_kind::new_system};
        item.star = star;
        item.special = special;
        item.leader = marooned_leader;
        item.joined = joined;
        Add(item);
    }

    void Note_Leader_Level(int16_t leader, int16_t star) {
        s_item item{e_kind::leader_level};
        item.leader = leader;
        item.star = star;
        Add(item);
    }

    void Note_Summary() {
        Add(s_item{e_kind::summary});
    }

    void Note_Colonize(int16_t star) {
        s_item item{e_kind::colonize};
        item.star = star;
        Add(item);
    }

    void Note_Hire(int16_t leader) {
        s_item item{e_kind::hire};
        item.leader = leader;
        Add(item);
    }

    int32_t Open_Count() {
        if (!Active() || MOX::colony == nullptr) {
            return 0;
        }
        Same_Turn();
        int32_t open = 0;
        for (int16_t c = 0; c < MOX::NUM_COLONIES; ++c) {
            open += Empty_Queue(MOX::colony[c]) ? 1 : 0;
        }
        for (const s_item& item : s_items) {
            open += Resolved(item) ? 0 : 1;
        }
        return open;
    }

    void Before_Next_Turn() {
        if (!Active() || MOX::player == nullptr) {
            return;
        }
        // The offer the player didn't answer: declined, as REPORT::Has_Officer_For_Hire does
        // after the window's REJECT (the leader goes where a declined one goes).
        s_player& player = MOX::player[MOX::PLAYER_NUM];
        if (player.officer_for_hire != -1) {
            OFFICER::Set_Officer_To_Player(player.officer_for_hire, MOX::PLAYER_NUM, 0);
            player.officer_for_hire = -1;
        }
    }

    // ---- Commands ----
    namespace {
        using commands::s_args;
        using commands::s_out;

        void Write_Item(s_out list, const char* kind, const std::string& text, int32_t index, int16_t star, bool news, bool resolved,
                        int32_t* open, s_out* item_out) {
            s_out item = list.Append_Object();
            item.Int("item", index);
            item.Str("kind", kind);
            item.Str("text", commands::internal::Utf8(text.c_str(), text.size() + 1));
            item.Int("star", star);
            item.Bool("news", news);
            item.Bool("done", resolved);
            *open += resolved ? 0 : 1;
            *item_out = item;
        }

        // todo: the items, oldest first after the live build queues; "open" counts those not done
        // (news not read), "builds" the colonies with nothing to build.
        void Query_Todo(int16_t, const s_args&, s_out& out) {
            if (!commands::internal::Have_Game(out)) {
                return;
            }
            Same_Turn();
            int32_t open = 0;
            int32_t builds = 0;
            s_out list = out.Add_List("items");
            s_out item = list;
            // The colonies with nothing to build (REPORT::Empty_Colony_Queue, read live): the game
            // took the player to each before the turn went on; here each must get something.
            for (int16_t c = 0; c < MOX::NUM_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (Empty_Queue(colony)) {
                    const int16_t planet = colony.planet;
                    Write_Item(list, "build", std::string(HACCESS::Get_Planet_Name(planet)) + ": nothing to build", -1,
                        MOX::planet[planet].star_index, false, false, &open, &item);
                    item.Int("colony", c);
                    item.Int("planet", planet);
                    ++builds;
                }
            }
            for (size_t i = 0; i < s_items.size(); ++i) {
                const s_item& it = s_items[i];
                const bool resolved = Resolved(it);
                char line[300];
                switch (it.kind) {
                    case e_kind::new_system: {
                        snprintf(line, sizeof(line), ESTRINGS::E_Strings(456), Star_Name(it.star).c_str());
                        std::string text = line;
                        const bool special = it.special != SYSTEM_SPECIAL_NO_SPECIAL && it.special != SYSTEM_SPECIAL_MAROONED_HERO &&
                            it.special > 0 && it.special < SYSTEM_SPECIAL_COUNT;
                        if (special) {
                            text += std::string(": ") + MOX::planet_special_string[it.special];
                        }
                        const bool leader = it.leader >= 0 && it.leader < MAX_LEADERS;
                        if (leader) {
                            const std::string name = OFFICER::Leader_Name(it.leader, 0);
                            text += it.joined ? ", " + name + " joins you" : ", " + name + " stays behind";
                        }
                        Write_Item(list, "new_system", text, (int32_t)i, it.star, true, resolved, &open, &item);
                        if (special) {
                            s_out sp = item.Add_Object("special");
                            sp.Str("name", commands::internal::Utf8(MOX::planet_special_string[it.special], 80));
                            sp.Str("text", commands::internal::Special_Description(it.special));
                        }
                        if (leader) {
                            commands::internal::Leader_Card(it.leader, MOX::PLAYER_NUM, item.Add_Object("leader"));
                        }
                        break;
                    }
                    case e_kind::leader_level:
                        snprintf(line, sizeof(line), ESTRINGS::E_Strings(1), OFFICER::Leader_Name(it.leader, 0));
                        Write_Item(list, "leader_level", line, (int32_t)i, it.star, true, resolved, &open, &item);
                        commands::internal::Leader_Card(it.leader, MOX::PLAYER_NUM, item.Add_Object("leader"));
                        break;
                    case e_kind::summary: Write_Item(list, "summary", "Turn summary", (int32_t)i, -1, true, resolved, &open, &item); break;
                    case e_kind::colonize:
                        Write_Item(list, "colonize", std::string(Colony_Base_At(it.star) ? "Colony base" : Ship_Of_Type_At(it.star, SHIP_TYPE_COLONY) ? "Colony ship"
                                                                                                                                                      : "Outpost ship") +
                                " at " + Star_Name(it.star),
                            (int32_t)i,
                            it.star,
                            false,
                            resolved,
                            &open,
                            &item);
                        break;
                    case e_kind::hire:
                        Write_Item(list, "hire", std::string(OFFICER::Leader_Name(it.leader, 1)) + " offers to join", (int32_t)i, -1, false,
                            resolved, &open, &item);
                        commands::internal::Leader_Card(it.leader, MOX::PLAYER_NUM, item.Add_Object("leader"));
                        break;
                }
            }
            out.Int("open", open);
            out.Int("builds", builds);
            out.Int("stardate", (int64_t)MOX::stardate); // a new turn's list (the frontend opens it)
        }

        s_item* Item_Arg(const s_args& args, s_out& out) {
            Same_Turn();
            int64_t index = -1;
            if (!args.Int("item", &index) || index < 0 || index >= (int64_t)s_items.size()) {
                out.Fail("bad_args", "item: a to-do item");
                return nullptr;
            }
            return &s_items[(size_t)index];
        }

        void Todo_Read(int16_t, const s_args& args, s_out& out) {
            if (!commands::internal::Have_Game(out)) {
                return;
            }
            if (s_item* item = Item_Arg(args, out)) {
                item->read = true;
                out.Bool("read", true);
            }
        }

        // The turn's summary (owner, 2026-11-14: not a to-do item but a notification area that
        // doesn't hold the turn): TURNSUM::Turn_Summary_Popup's messages in its order, each with
        // the colony a click goes to (MSG_::Goto_Msg_Colony's rule), and whether the option is on.
        void Query_Turn_Summary(int16_t, const s_args&, s_out& out) {
            if (!commands::internal::Have_Game(out)) {
                return;
            }
            out.Bool("on", MOX::settings.end_of_turn_summary != 0);
            out.Int("stardate", (int64_t)MOX::stardate);
            s_out list = out.Add_List("items");
            const int16_t count = MSG_::N_Msgs_For_Player(MOX::PLAYER_NUM);
            if (count <= 0) {
                return;
            }
            uint8_t order[MAX_MESSAGES];
            REPORT::Order_Turn_Summary_Messages(order, MSG_::First_Msg_For_Player(MOX::PLAYER_NUM), count, REPORT::sort_serious_turn_summary_first);
            for (int16_t pos = 0; pos < count; ++pos) {
                s_MSG& msg = MSG_::msgs[order[pos]];
                char text[256];
                MSG_::sprintf_msg(text, sizeof(text), &msg);
                s_out item = list.Append_Object();
                item.Int("msg", order[pos]);
                item.Str("text", commands::internal::Utf8(text, sizeof(text)));
                item.Bool("serious", REPORT::Is_Serious_Msg(msg));
                int16_t colony = -1;
                if (msg.msg_type == MSG_TYPE_COLONY) {
                    const int16_t planet = msg.u.colony.planet_idx;
                    if (planet >= 0 && planet < MOX::planet_count) {
                        const int16_t c = MOX::planet[planet].colony_index;
                        if (c >= 0 && MOX::colony[c].owner == MOX::PLAYER_NUM) {
                            colony = c;
                            item.Int("star", MOX::planet[planet].star_index);
                        }
                    }
                }
                item.Int("colony", colony);
            }
        }

        // A leader's level: the window the reports chain shows (the native leader_level popup,
        // MAINPUPS::Officer_Gains_Level_Popup), then the item is read (owner, 2026-11-13: the
        // item cleared without showing it).
        void Todo_Level(int16_t, const s_args& args, s_out& out) {
            if (!commands::internal::Have_Game(out)) {
                return;
            }
            s_item* item = Item_Arg(args, out);
            if (item == nullptr) {
                return;
            }
            if (item->kind != e_kind::leader_level || item->leader < 0 || item->leader >= MAX_LEADERS) {
                out.Fail("bad_args", "item: a leader_level item");
                return;
            }
            const int16_t leader = item->leader;
            const int16_t star = item->star;
            item->read = true;
            MAINPUPS::Officer_Gains_Level_Popup(star, leader);
            MOX::FULL_DRAW_SCREEN_FLAG = 1;
            out.Bool("shown", true);
        }

        // A system explored: the discovery window the reports chain would have shown (the native
        // new_system popup: the special it found and what it means, a marooned leader joining or
        // staying), then the item is read (owner, 2026-11-13: a pirate cache's item only zoomed in,
        // never saying what it gave).
        void Todo_System(int16_t, const s_args& args, s_out& out) {
            if (!commands::internal::Have_Game(out)) {
                return;
            }
            s_item* item = Item_Arg(args, out);
            if (item == nullptr) {
                return;
            }
            if (item->kind != e_kind::new_system || item->star < 0 || item->star >= MOX::NUM_STARS) {
                out.Fail("bad_args", "item: a new_system item");
                return;
            }
            const s_item it = *item;
            item->read = true;
            if (it.leader >= 0 && it.leader < MAX_LEADERS) {
                // Decided when the news was noted (REPORT::Explored_New_Star): only shown here.
                if (it.joined) {
                    MAINPUPS::Marooned_Officer_Joins_Popup(it.star, it.leader);
                } else {
                    MAINPUPS::Marooned_Officer_Fails_Popup(it.star, it.leader);
                }
            }
            const int16_t special = it.special == SYSTEM_SPECIAL_MAROONED_HERO ? (int16_t)SYSTEM_SPECIAL_NO_SPECIAL : it.special;
            MAINPUPS::New_System_Discovery_Popup(it.star, (uint8_t)special);
            MOX::FULL_DRAW_SCREEN_FLAG = 1;
            out.Bool("shown", true);
        }

        // The leader's offer, as the reports chain would have shown it (the native leader_hire
        // popup and REPORT::Has_Officer_For_Hire's rules).
        void Todo_Hire(int16_t, const s_args&, s_out& out) {
            if (!commands::internal::Have_Game(out)) {
                return;
            }
            const int16_t leader = MOX::player[MOX::PLAYER_NUM].officer_for_hire;
            if (leader == -1) {
                out.Bool("offered", false);
                return;
            }
            REPORT::Has_Officer_For_Hire(MOX::PLAYER_NUM, 2); // 2: answered from the list
            if (s_item* item = Find(e_kind::hire, -1, leader)) {
                item->done = true;
            }
            MOX::FULL_DRAW_SCREEN_FLAG = 1;
            out.Bool("offered", true);
            out.Bool("hired", MOX::leaders[leader].player_index == MOX::PLAYER_NUM && MOX::leaders[leader].status >= 0 &&
                    MOX::leaders[leader].status != 4);
        }

        // A colony or outpost ship at a system: the chain's prompt for that star (its planet
        // picker or preselected planet, native popups), as REPORT::Colonization runs it.
        void Todo_Colonize(int16_t, const s_args& args, s_out& out) {
            if (!commands::internal::Have_Game(out)) {
                return;
            }
            int64_t star = -1;
            if (!args.Int("star", &star) || star < 0 || star >= MOX::NUM_STARS) {
                out.Fail("bad_args", "star: a star id");
                return;
            }
            // The player's colonies there before, so the new one can be named to the frontend (it
            // opens its pane: the landing screen is skipped with the native galaxy).
            std::vector<int16_t> before;
            for (const int16_t p : MOX::star[star].planet_index) {
                if (p >= 0 && p < MOX::planet_count) {
                    before.push_back(MOX::planet[p].colony_index);
                }
            }
            const int8_t status = REPORT::Colonization_At_Star(MOX::PLAYER_NUM, (int16_t)star, 1, false);
            if (s_item* item = Find(e_kind::colonize, (int16_t)star, -1)) {
                item->done = true;
            }
            int16_t founded = -1;
            for (const int16_t p : MOX::star[star].planet_index) {
                if (p < 0 || p >= MOX::planet_count) {
                    continue;
                }
                const int16_t c = MOX::planet[p].colony_index;
                if (c >= 0 && c < MOX::NUM_COLONIES && MOX::colony[c].owner == MOX::PLAYER_NUM && MOX::colony[c].outpost_flag == 0 &&
                    std::find(before.begin(), before.end(), c) == before.end()) {
                    founded = c;
                }
            }
            MOX::FULL_DRAW_SCREEN_FLAG = 1;
            out.Int("status", status);
            out.Int("colony", founded); // a colony founded here, or -1
            out.Int("star", star);
        }
    } // namespace

} // namespace antares::todo

namespace antares::commands::internal {
    void Add_Todo_Entries(std::vector<s_entry>* table) {
        table->push_back({"todo", e_kind::query, ANY_SCREEN, 1, "", todo::Query_Todo});
        table->push_back({"turn_summary", e_kind::query, ANY_SCREEN, 1, "", todo::Query_Turn_Summary});
        table->push_back({"todo_read", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "item", todo::Todo_Read});
        table->push_back({"todo_level", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "item", todo::Todo_Level});
        table->push_back({"todo_system", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "item", todo::Todo_System});
        table->push_back({"todo_hire", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "", todo::Todo_Hire});
        table->push_back({"todo_colonize", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "star", todo::Todo_Colonize});
    }
} // namespace antares::commands::internal
