#include "pch.h"

#include "antares/commands_internal.h"

#include <algorithm>
#include <cstdlib>

// WP3.4: the colony pane's query and commands (contract: notes/antares/native-bridge.md, "The
// colony pane"). The query reads the colony fields COLCALC::Colony_Calculation leaves behind and
// the colony screen's own pure helpers; it never calls the screen's Show_* breakdowns or the
// production calculators (they rewrite the colony). The commands are the colony screen's own
// actions: a colonist's new job (COLMOVE::Give_Colonist_New_Job) and BUY
// (COLONY::Buy_Or_Unbuy_Product, the screen's Tested_Colony_Buys_Outright without its redraw).

namespace antares::commands::internal {

    namespace {

        // COLONY2.LBX: the production icons (COLONY::Prod_Anims: type + variant * 4, variant 1 for
        // tens) and the import icons (COLONY::Import_Anims: type + tens * 4 + 8).
        constexpr int32_t k_prod_icons = 0;
        constexpr int32_t k_import_icons = 8;
        constexpr int32_t k_government_icons = 0x13;
        constexpr int32_t k_morale_happy = 0x10;
        constexpr int32_t k_morale_unhappy = 0x11;
        // RACEICON.LBX: the colonists (COLONY::People_Anim), the conquered (Colony_Pop_Icon), the
        // garrison (COLONY::Military_Anims: race * 13 + 6 + variant), natives and androids.
        constexpr int32_t k_raceicon_stride = 13;
        constexpr int32_t k_native_icon = 0xAA;
        constexpr int32_t k_android_icon = 0xA9;
        // COLONY.LBX: the satellites (COLONY::Buffer_Reload_Colony: 9 + Satellite_Anim_Pic).
        constexpr int32_t k_satellite_pictures = 9;
        // The building's picture on the colony screen's build box: the BLDG%d.LBX frame of grid cell
        // (5, 5) (COLONY::Draw_Current_Building2 -> COLBCACH::Cache_Load_Bldg, which uses
        // Bldg_Coords_To_Effective_Frame(5, 5) = 30, plus 36 frames per building in the file).
        constexpr int32_t k_building_frame = 30;
        constexpr int32_t k_building_frames = 36;

        bool Player_Colony(int64_t colony, s_out& out) {
            if (colony < 0 || colony >= MOX::NUM_COLONIES || MOX::colony[colony].owner != MOX::PLAYER_NUM) {
                out.Fail("bad_args", "colony: one of the player's colonies");
                return false;
            }
            return true;
        }

        int32_t Race_Of(int16_t player) {
            return player >= 0 && player < MAX_PLAYERS ? (uint8_t)MOX::player[player].race : 0;
        }

        // One production row as the colony screen draws it (COLDRAW::Draw_Colony_Prod_Both): runs
        // of icons, left to right, with "gap" marking the screen's empty step. `kind`: net (what
        // the colony makes), pollution / industry (the second group: pollution on industry, the
        // food row's industry imports), import, short (outlined: a shortage).
        void Production_Row(const s_colony& colony, int16_t type, s_out row) {
            const auto run = [&](const char* kind, int32_t sprite, int32_t count) {
                if (count <= 0) {
                    return;
                }
                s_out r = row.Append_Object();
                r.Str("kind", kind);
                r.Int("sprite", sprite);
                r.Int("count", count);
            };
            const auto gap = [&]() {
                s_out r = row.Append_Object();
                r.Str("kind", "gap");
            };
            int32_t import_value = type != ECON_INDUSTRY ? std::abs((int32_t)colony.imports[type]) : 0;
            int32_t secondary = 0;
            if (type == ECON_FOOD) {
                secondary = std::abs((int32_t)colony.imports[ECON_INDUSTRY]);
            } else if (type == ECON_INDUSTRY) {
                secondary = colony.pollution;
            }
            const int16_t produced = colony.production[type];
            int16_t shortage = (int16_t)(colony.maintenance[type] - colony.imports[type] - produced);
            shortage = shortage < 1 ? 0 : shortage;
            int32_t net = 0;
            if ((int8_t)colony.imports[type] < 0) {
                net = type == ECON_INDUSTRY ? std::max(0, (int32_t)produced - colony.maintenance[type])
                                            : (int32_t)produced - std::abs((int32_t)colony.imports[type]);
            } else if (colony.maintenance[ECON_INDUSTRY] == 0 || type != ECON_INDUSTRY) {
                net = produced;
            } else {
                net = std::max(0, (int32_t)produced - colony.maintenance[type]);
            }
            net = std::max(0, net);
            run("net", k_prod_icons + type + 4, net / 10);
            run("net", k_prod_icons + type, net % 10);
            if (secondary != 0) {
                if (type == ECON_INDUSTRY) {
                    run("pollution", k_import_icons + 1 + 4, secondary / 10);
                    run("pollution", k_import_icons + 1, secondary % 10);
                } else {
                    run("industry", k_prod_icons + 1 + 4, secondary / 10);
                    run("industry", k_prod_icons + 1, secondary % 10);
                }
                gap();
            }
            gap();
            if (colony.imports[type] < 0 || type == ECON_INDUSTRY) {
                run("export", k_prod_icons + type + 4, import_value / 10);
                run("export", k_prod_icons + type, import_value % 10);
            } else {
                run("import", k_import_icons + type + 4, import_value / 10);
                run("import", k_import_icons + type, import_value % 10);
                run("short", k_import_icons + type + 4, shortage / 10);
                run("short", k_import_icons + type, shortage % 10);
            }
        }

        // An item's name as COLBLDG::Selection_Name gives it, without its refit cache.
        std::string Item_Name(const s_colony& colony, int16_t id) {
            if (COLBLDG::Colony_Production_Is_Building(id)) {
                return Utf8(TECHDATA::buildings[id].name, 64);
            }
            if (COLBLDG::Colony_Production_Is_Queued_Ship(id)) {
                const s_ship_data& ship = MOX::ship[COLBLDG::Colony_Production_Ship_Index(id)];
                if (ship.status != 4) {
                    return Utf8(ship.d.name, sizeof(ship.d.name));
                }
                char text[160];
                snprintf(text, sizeof(text), ESTRINGS::E_Strings(45), COLBLDG::Option_String(COLONY_PRODUCTION_REFIT), ship.d.name);
                return Utf8(text, sizeof(text));
            }
            if (COLBLDG::Colony_Production_Is_Ship_Design(id)) {
                const int16_t slot = COLBLDG::Colony_Production_Ship_Design_Index(id);
                const s_player& owner = MOX::player[colony.owner];
                return Utf8(owner.ship_designs[slot].name, sizeof(owner.ship_designs[slot].name));
            }
            return Utf8(COLBLDG::Option_String(id), 64);
        }

        // The build box's picture: {lbx, entry} as the colony screen draws the item (Draw_Info_Build).
        void Item_Picture(const s_colony& colony, int16_t id, s_out item) {
            const int32_t race = Race_Of(colony.owner);
            if (id == COLONY_PRODUCTION_HOUSING) {
                id = BUILDING_DIMENSIONAL_PORTAL; // the screen draws housing with building 14's art
            }
            if (COLBLDG::Colony_Production_Is_Building(id)) {
                const int16_t satellite = COLONY::Satellite_Anim_Pic(id);
                if (TECHDATA::buildings[id].group == 7 && satellite >= 0) {
                    item.Str("lbx", "COLONY.LBX");
                    item.Int("entry", k_satellite_pictures + satellite);
                    return;
                }
                char lbx[16];
                snprintf(lbx, sizeof(lbx), "BLDG%d.LBX", (id - BUILDING_ALIEN_CONTROL_CENTER) / 10);
                item.Str("lbx", lbx);
                item.Int("entry", k_building_frame + ((id - BUILDING_ALIEN_CONTROL_CENTER) % 10) * k_building_frames);
                return;
            }
            int32_t picture = -1;
            const int32_t color = (int32_t)MOX::player[colony.owner].color;
            if (COLBLDG::Colony_Production_Is_Queued_Ship(id)) {
                picture = MOX::ship[COLBLDG::Colony_Production_Ship_Index(id)].d.picture_num;
            } else if (COLBLDG::Colony_Production_Is_Ship_Design(id)) {
                picture = MOX::player[colony.owner].ship_designs[COLBLDG::Colony_Production_Ship_Design_Index(id)].picture_num;
            } else if (id == COLONY_PRODUCTION_FREIGHTERS) {
                picture = 48;
            } else if (id == COLONY_PRODUCTION_SPY) {
                item.Str("lbx", "RACEICON.LBX");
                item.Int("entry", race * k_raceicon_stride + 11);
                return;
            } else if (id == COLONY_PRODUCTION_FARMER || id == COLONY_PRODUCTION_WORKER || id == COLONY_PRODUCTION_SCIENTIST) {
                item.Str("lbx", "RACEICON.LBX");
                item.Int("entry", k_android_icon);
                return;
            }
            if (picture >= 0) {
                item.Str("lbx", "SHIPS.LBX");
                item.Int("entry", picture + color * 50);
            }
        }

        // A colonist's picture (COLONY::Colony_Pop_Anim).
        int32_t Pop_Sprite(int16_t colony_idx, int16_t pop) {
            const uint32_t data = MOX::colony[colony_idx].pop[pop];
            const int16_t state = COLONY::Pop_To_Pop_State(colony_idx, pop);
            const int32_t race = Race_Of(COLONY::Get_Effective_Pop_Player(colony_idx, pop));
            if ((data & 0x400) != 0) {
                return race * k_raceicon_stride + 12; // conquered
            }
            const int32_t job = (int32_t)((data >> 7) & 0x3);
            if (state == 3) {
                return k_native_icon;
            }
            if (state == 4) {
                return k_android_icon;
            }
            return race * k_raceicon_stride + job * 2 + 1;
        }

        void Query_Colony(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t colony_id = -1;
            if (!args.Int("colony", &colony_id)) {
                out.Fail("bad_args", "colony: one of the player's colonies");
                return;
            }
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            const int16_t c = (int16_t)colony_id;
            const s_colony& colony = MOX::colony[c];
            const s_player& player = MOX::player[colony.owner];
            out.Int("colony", c);
            out.Int("planet", colony.planet);
            out.Int("star", MOX::planet[colony.planet].star_index);
            out.Str("name", Utf8(HACCESS::Get_Planet_Name_No_Prime(colony.planet), 80));
            out.Str("race", Utf8(player.race_name, sizeof(player.race_name)));
            out.Int("race_id", Race_Of(colony.owner));
            out.Bool("outpost", colony.outpost_flag != 0);
            // Population as the header gives it: assigned colonists, thousands, growth.
            int32_t pops = 0;
            int32_t roundoff = 0;
            int32_t growth = 0;
            for (int16_t i = 0; i < (uint8_t)colony.n_pops; ++i) {
                pops += (colony.pop[i] & 0x200) != 0 ? 1 : 0;
            }
            for (int r = 0; r < 10; ++r) {
                roundoff += colony.pop_roundoff[r];
                growth += colony.pop_growth[r];
            }
            out.Int("population", pops + roundoff / 1000);
            out.Int("population_thousands", roundoff % 1000);
            out.Int("growth", growth);
            // As the system box gives it (Planet_Lines: COLCALC::Planet_Max_Population_For_Player).
            out.Int("max_population", COLCALC::Planet_Max_Population_For_Player(&MOX::planet[colony.planet], colony.owner));
            // Morale (COLDRAW::Draw_Info_Morale_Both).
            const int8_t government = player.traits[TRAIT_CURRENT_GOVERNMENT];
            const int16_t masks = government >= GOVERNMENT_UNIFICATION ? 0 : (int16_t)((int8_t)colony.morale / 2);
            out.Int("government", government);
            out.Int("government_sprite", k_government_icons + government);
            out.Int("morale", (int8_t)colony.morale);
            out.Int("morale_icons", std::min<int16_t>(std::abs(masks), 20));
            out.Int("morale_sprite", masks >= 0 ? k_morale_happy : k_morale_unhappy);
            // The four rows: BC, food, industry, research (Draw_Colony_Info_Production).
            s_out rows = out.Add_Object("rows");
            const struct {
                const char* name;
                int16_t type;
            } kinds[] = {{"bc", ECON_BC}, {"food", ECON_FOOD}, {"industry", ECON_INDUSTRY}, {"research", ECON_RESEARCH}};
            for (const auto& kind : kinds) {
                s_out row = rows.Add_Object(kind.name);
                row.Int("produced", colony.production[kind.type]);
                row.Int("upkeep", colony.maintenance[kind.type]);
                row.Int("imports", colony.imports[kind.type]);
                Production_Row(colony, kind.type, row.Add_List("icons"));
            }
            out.Int("pollution", colony.pollution);
            // The jobs: each assigned colonist in the screen's order (state, conquered, race, index).
            out.Int("max_farms", colony.max_farms); // 0 no farming, 255 unlimited
            out.Int("food_per_farmer_x2", colony.food2_per_farmer);
            out.Int("industry_per_worker", colony.industry_per_worker);
            out.Int("research_per_scientist", colony.research_per_scientist);
            s_out jobs = out.Add_List("jobs");
            for (int job = 0; job < 3; ++job) {
                s_out list = jobs.Append_Object();
                list.Int("job", job);
                s_out pops_out = list.Add_List("colonists");
                for (int16_t i = 0; i < (uint8_t)colony.n_pops; ++i) {
                    const uint32_t data = colony.pop[i];
                    if ((data & 0x200) == 0 || (int)((data >> 7) & 0x3) != job) {
                        continue;
                    }
                    const int16_t state = COLONY::Pop_To_Pop_State(c, i);
                    s_out p = pops_out.Append_Object();
                    p.Int("pop", i);
                    p.Int("sprite", Pop_Sprite(c, i));
                    p.Str("state", state == 3 ? "native" : (state == 4 ? "android" : ((data & 0x400) != 0 ? "conquered" : "citizen")));
                }
            }
            // The build (Draw_Info_Build): the front of the queue, its progress and its price.
            const int16_t building = colony.producing[0];
            s_out build = out.Add_Object("build");
            build.Int("id", building);
            build.Str("name", Item_Name(colony, building));
            build.Bool("auto", colony.auto_building != 0);
            const bool settable = building != COLONY_PRODUCTION_NONE && building != COLONY_PRODUCTION_HOUSING &&
                building != COLONY_PRODUCTION_TRADE_GOODS;
            if (building != COLONY_PRODUCTION_NONE) {
                Item_Picture(colony, building, build.Add_Object("picture"));
            }
            s_colony* mutable_colony = const_cast<s_colony*>(&colony); // the helpers take a pointer, and only read
            if (settable) {
                const int32_t cost = COLCALC::Colony_Producing_Product_Cost(mutable_colony);
                build.Int("cost", cost);
                build.Int("spent", colony.production_spent);
                build.Int("bought", colony.bought_outright);
                const int16_t turns = COLCALC::Colony_N_Turns_To_Produce(mutable_colony);
                build.Int("turns", turns < 0 ? 10000 : turns);
                build.Int("buy_cost", colony.bought_outright != 0 ? 0 : COLCALC::Colony_Cost_To_Buy_Producing_Product(mutable_colony));
            }
            s_out queue = build.Add_List("queue");
            for (int slot = 0; slot < 7; ++slot) {
                const int16_t id = colony.producing[slot];
                if (id == COLONY_PRODUCTION_NONE || id == 0) {
                    break;
                }
                s_out q = queue.Append_Object();
                q.Int("id", id);
                q.Str("name", Item_Name(colony, id));
            }
            // Defences: the satellites, lit when built (Make_Bldg_Array_For_Colony, Sat_Should_Be_Replaced).
            s_out defences = out.Add_List("defences");
            const auto has = [&](int16_t id) { return colony.buildings[id] != 0; };
            int16_t base = BUILDING_STAR_BASE;
            if (has(BUILDING_STAR_FORTRESS)) {
                base = BUILDING_STAR_FORTRESS;
            } else if (has(BUILDING_BATTLESTATION)) {
                base = BUILDING_BATTLESTATION;
            }
            for (const int16_t id : {base, (int16_t)BUILDING_ARTEMIS_SYSTEM_NET, (int16_t)BUILDING_DIMENSIONAL_PORTAL}) {
                s_out d = defences.Append_Object();
                d.Int("building", id);
                d.Str("name", Utf8(TECHDATA::buildings[id].name, 64));
                d.Bool("built", has(id));
                d.Int("sprite", k_satellite_pictures + COLONY::Satellite_Anim_Pic(id));
            }
            // The garrison (Do_Colony_Info_Military_Stuff_For, Military_Anim with the owner's techs).
            const int32_t race = Race_Of(colony.owner);
            s_out garrison = out.Add_Object("garrison");
            const bool powered = player.tech_applications[TECH_APP_POWERED_ARMOR] == TECH_RESEARCH_STATUS_RESEARCHED;
            const bool battleoids = player.tech_applications[TECH_APP_BATTLEOIDS] == TECH_RESEARCH_STATUS_RESEARCHED;
            garrison.Int("marines", colony.military[0]);
            garrison.Int("marine_sprite", race * k_raceicon_stride + 6 + (powered ? 2 : 1));
            garrison.Int("armor", colony.military[1]);
            garrison.Int("armor_sprite", race * k_raceicon_stride + 6 + (battleoids ? 4 : 3));
            // Buildings: every one standing (colony.buildings), satellites aside.
            s_out buildings = out.Add_List("buildings");
            for (int16_t id = 1; id < 49; ++id) {
                if (colony.buildings[id] == 0 || TECHDATA::buildings[id].group == 7) {
                    continue;
                }
                s_out b = buildings.Append_Object();
                b.Int("building", id);
                b.Str("name", Utf8(TECHDATA::buildings[id].name, 64));
            }
            // Picked-up colonists (the original's TRANSPORT COLONISTS needs some).
            out.Int("unassigned", COLMOVE::Get_Num_Unemployed(c));
        }

        // A colonist to another job, as dropping it on a job row does (COLONY::Pickup_Colonists,
        // COLMOVE::Send_Cluster within the colony): picked up, then Give_Colonist_New_Job with
        // its refusals (natives, androids, a full row, no room to farm) as the game's own help
        // messages, then the colony is calculated again.
        void Move_Colonist(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t colony_id = -1;
            int64_t pop = -1;
            int64_t job = -1;
            if (!args.Int("colony", &colony_id) || !args.Int("pop", &pop) || !args.Int("job", &job) || job < 0 || job > 2) {
                out.Fail("bad_args", "colony, pop (a colonist's index), job (0 farmer, 1 worker, 2 scientist)");
                return;
            }
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            const int16_t c = (int16_t)colony_id;
            s_colony& colony = MOX::colony[c];
            if (colony.outpost_flag != 0 || pop < 0 || pop >= (uint8_t)colony.n_pops || (colony.pop[pop] & 0x200) == 0) {
                out.Fail("bad_args", "pop: an assigned colonist of this colony");
                return;
            }
            if ((int)((colony.pop[pop] >> 7) & 0x3) == job) {
                out.Bool("moved", false);
                return;
            }
            POP::Set_Assigned(&colony.pop[pop], false); // picked up: not counted in its new row
            const bool moved = COLMOVE::Give_Colonist_New_Job(c, (int16_t)pop, (int16_t)job, false) != 0;
            if (!moved) {
                POP::Set_Assigned(&colony.pop[pop], true); // put back
            }
            COLCALC::Colony_Calculation(c);
            out.Bool("moved", moved);
        }

        // The colony screen's BUY (COLONY::Buy_Or_Unbuy_Product): un-buys a bought item, else asks
        // the game's question and buys; its refusals are the game's own messages.
        void Buy(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t colony_id = -1;
            if (!args.Int("colony", &colony_id)) {
                out.Fail("bad_args", "colony: one of the player's colonies");
                return;
            }
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            COLONY::Buy_Or_Unbuy_Product((int16_t)colony_id, false);
            out.Bool("bought", MOX::colony[colony_id].bought_outright != 0);
            out.Int("bc", MOX::player[MOX::PLAYER_NUM].bc);
        }

    } // namespace

    void Add_Colony_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"colony", e_kind::query, ANY_SCREEN, 1, "colony: colony id (the player's)", Query_Colony});
        table->push_back({"move_colonist", e_kind::command, galaxy, 0, "colony, pop, job (0-2)", Move_Colonist});
        table->push_back({"buy", e_kind::command, galaxy, 0, "colony", Buy});
    }

} // namespace antares::commands::internal
