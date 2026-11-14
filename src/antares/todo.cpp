#include "pch.h"

#include "antares/todo.h"

#include "antares/commands_internal.h"
#include "antares/native.h"

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

        // Still to do: news unread, a colony ship still there, the offer still open.
        bool Resolved(const s_item& item) {
            switch (item.kind) {
                case e_kind::colonize:
                    return item.done || (!Ship_Of_Type_At(item.star, SHIP_TYPE_COLONY) && !Ship_Of_Type_At(item.star, SHIP_TYPE_OUTPOST));
                case e_kind::hire: return item.done || MOX::player[MOX::PLAYER_NUM].officer_for_hire != item.leader;
                default: return item.read;
            }
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
            // took the player to each before the turn went on; here they ask once at its end.
            for (int16_t c = 0; c < MOX::NUM_COLONIES; ++c) {
                const s_colony& colony = MOX::colony[c];
                if (colony.owner == MOX::PLAYER_NUM && colony.outpost_flag == 0 && colony.producing[0] == COLONY_PRODUCTION_NONE) {
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
                            const char* desc = MAINPUPS::System_Special_Desc(it.special);
                            s_out sp = item.Add_Object("special");
                            sp.Str("name", commands::internal::Utf8(MOX::planet_special_string[it.special], 80));
                            sp.Str("text", commands::internal::Utf8(desc != nullptr ? desc : "", 600));
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
                        Write_Item(list, "colonize",
                            std::string(Ship_Of_Type_At(it.star, SHIP_TYPE_COLONY) ? "Colony ship" : "Outpost ship") + " at " + Star_Name(it.star),
                            (int32_t)i, it.star, false, resolved, &open, &item);
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
            const int8_t status = REPORT::Colonization_At_Star(MOX::PLAYER_NUM, (int16_t)star, 1, false);
            if (s_item* item = Find(e_kind::colonize, (int16_t)star, -1)) {
                item->done = true;
            }
            MOX::FULL_DRAW_SCREEN_FLAG = 1;
            out.Int("status", status);
        }
    } // namespace

} // namespace antares::todo

namespace antares::commands::internal {
    void Add_Todo_Entries(std::vector<s_entry>* table) {
        table->push_back({"todo", e_kind::query, ANY_SCREEN, 1, "", todo::Query_Todo});
        table->push_back({"todo_read", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "item", todo::Todo_Read});
        table->push_back({"todo_hire", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "", todo::Todo_Hire});
        table->push_back({"todo_colonize", e_kind::command, Screen_Bit(SCREEN_MAIN), 0, "star", todo::Todo_Colonize});
    }
} // namespace antares::commands::internal
