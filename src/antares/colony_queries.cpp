#include "pch.h"

#include "antares/commands_internal.h"
#include "game/build_queue.h"
#include "yyjson.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

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

        // One record of an LBX record table (a u16 count and size, then the records), read as
        // farload::Far_Reload_Data reads it but without the popup's caches (pure).

        // The help text the popup's detail box prints for an item (Draw_Current_Selection,
        // Draw_Building_Info): a building's application, a unit's fixed application, or a
        // setting's description. Ship designs show their parts instead.
        std::string Item_Description(int16_t id) {
            int16_t app = -1;
            switch (id) {
                case COLONY_PRODUCTION_OUTPOST_SHIP: app = 109; break;
                case COLONY_PRODUCTION_FREIGHTERS: app = 69; break;
                case COLONY_PRODUCTION_COLONY_SHIP: app = 41; break;
                case COLONY_PRODUCTION_TRANSPORT_SHIP: app = 189; break;
                case COLONY_PRODUCTION_SPY: app = 166; break;
                case COLONY_PRODUCTION_SCIENTIST: app = 7; break;
                case COLONY_PRODUCTION_WORKER: app = 8; break;
                case COLONY_PRODUCTION_FARMER: app = 6; break;
                case COLONY_PRODUCTION_HOUSING:
                case COLONY_PRODUCTION_TRADE_GOODS:
                    return Lbx_Record(ESTRINGS::E_Strings(HESTR_170_MAX_POPULATION),
                        id == COLONY_PRODUCTION_HOUSING ? 13 : 12, 0, 0, 600);
                default:
                    if (COLBLDG::Colony_Production_Is_Building(id)) {
                        app = (int16_t)TECHDATA::buildings[id].tech_app;
                    }
                    break;
            }
            return Application_Help(app);
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
            if (id == COLONY_PRODUCTION_TRADE_GOODS) {
                item.Str("icon", "coin"); // the screen draws nothing; the interface's coin
                return;
            }
            const bool housing = id == COLONY_PRODUCTION_HOUSING;
            if (housing) {
                // The screen draws housing with building 14's BLDG art (Draw_Current_Building2), not
                // the dimensional portal's satellite that building 14 shows elsewhere.
                id = BUILDING_DIMENSIONAL_PORTAL;
            }
            if (COLBLDG::Colony_Production_Is_Building(id)) {
                const int16_t satellite = COLONY::Satellite_Anim_Pic(id);
                if (!housing && TECHDATA::buildings[id].group == 7 && satellite >= 0) {
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
            } else if (id == COLONY_PRODUCTION_COLONY_SHIP) {
                picture = 45; // COLBLDG::Draw_Current_Selection's pictures (owner, 2026-11-14: none shown)
            } else if (id == COLONY_PRODUCTION_OUTPOST_SHIP) {
                picture = 46;
            } else if (id == COLONY_PRODUCTION_TRANSPORT_SHIP) {
                picture = 47;
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

        // The jobs: each assigned colonist in the screen's order (state, conquered, race, index),
        // with the cluster a click picks up (COLMOVE::Get_Cluster: it and the identical ones after it).
        void Add_Jobs(int16_t c, s_out jobs) {
            const s_colony& colony = MOX::colony[c];
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
                    s_out cluster = p.Add_List("cluster");
                    for (int16_t k = i; k < (uint8_t)colony.n_pops; ++k) {
                        if ((colony.pop[k] & 0x200) != 0 && COLMOVE::Pops_Identical(c, i, c, k)) {
                            cluster.Append_Int(k);
                        }
                    }
                }
            }
        }

        // Any help record (HELP.LBX's table: applications 0-211, then the screens' help, e.g. the
        // colony screen's 483-500 in ERICHELP::colony_screen_help_list): the right click's help.
        void Query_Help(int16_t, const s_args& args, s_out& out) {
            int64_t topic = -1;
            if (!args.Int("topic", &topic) || topic < 0 || topic > 0xFFFF) {
                out.Fail("bad_args", "topic: a help record");
                return;
            }
            char lbx[20];
            TEXTBOX::Get_Help_Lbx_Name(lbx, sizeof(lbx));
            out.Int("topic", topic);
            out.Str("title", Lbx_Record(lbx, 0, (uint16_t)topic, 0, sizeof(s_help_record::title)));
            out.Str("text", Lbx_Record(lbx, 0, (uint16_t)topic, offsetof(s_help_record, body), sizeof(s_help_record::body)));
            // The record's picture, as the help box draws it (TEXTBOX: entry in the low word,
            // frame in the next byte).
            s_help_record record;
            if (Help_Record((uint16_t)topic, &record) && record.anim_lbx[0] != '\0') {
                s_out picture = out.Add_Object("picture");
                picture.Str("lbx", Utf8(record.anim_lbx, sizeof(record.anim_lbx)));
                picture.Int("entry", (int64_t)(record.anim_info & 0xFFFF));
                picture.Int("frame", (int64_t)((record.anim_info >> 16) & 0xFF));
            }
        }

        // OCPOLPUP::Occupation_Policy_Popup asks only governments other than Democracy and Federation.

        bool Occupation_Can_Annihilate(int16_t owner) {

            const int8_t government = MOX::player[owner].traits[TRAIT_CURRENT_GOVERNMENT];

            return government != GOVERNMENT_DEMOCRACY && government != GOVERNMENT_FEDERATION;

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
            // The occupation policy (owner, 2026-11-19: the legacy colony screen's button, natively):
            // for colonies with conquered people (or natives, with annihilation allowed).
            {
                bool targets = false;
                for (int16_t i = 0; i < (uint8_t)colony.n_pops && !targets; ++i) {
                    targets = COLONY::Colony_Pop_Is_Annihilation_Target(&colony, POP::Load(&colony.pop[i]));
                }
                if (targets) {
                    s_out occupation = out.Add_Object("occupation");
                    occupation.Bool("annihilate", colony.occupation_policy == 0);
                    occupation.Bool("can_annihilate", Occupation_Can_Annihilate(colony.owner));
                }
            }
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
            Add_Jobs(c, out.Add_List("jobs"));
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
                build.Int("treasury", MOX::player[colony.owner].bc); // BUY is greyed out beyond it (owner, 2026-11-14)
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
                b.Bool("sellable", COLONY::Building_Can_Be_Scrapped(id));
                b.Int("refund", COLONY::Building_Scrap_Refund(id));
                b.Str("description", Item_Description(id));
            }
            // Picked-up colonists (the original's TRANSPORT COLONISTS needs some).
            out.Int("unassigned", COLMOVE::Get_Num_Unemployed(c));
        }

        // WP3.5a: the Colonies panel (notes/antares/screens/20-colony-summary.md). Every colony of
        // the player's (outposts aside, as the Colonies screen lists them) with what its row and
        // its scan box show, plus the empire box (COLSUM::Draw_Empire_Info). Read only: the screen's
        // own sort and draw helpers, never its list globals.
        void Query_Colonies(int16_t, const s_args&, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            COLSUM::Load_Colsum_Strings(); // the row's event labels (loaded once, as the screen does)
            const int16_t me = MOX::PLAYER_NUM;
            s_player& player = MOX::player[me];
            std::vector<int16_t> ids;
            for (int16_t c = 0; c < MOX::NUM_COLONIES; ++c) {
                if (MOX::colony[c].owner == me && MOX::colony[c].outpost_flag == 0) {
                    ids.push_back(c);
                }
            }
            // The Building column's order, as the screen's product sort gives it (cmp_Prod: the
            // kind of item, then its name, then the turns).
            std::vector<int16_t> by_build = ids;
            std::stable_sort(by_build.begin(), by_build.end(), [](int16_t a, int16_t b) { return COLSUM::cmp_Prod(a, b) < 0; });
            s_out list = out.Add_List("colonies");
            for (int16_t c : ids) {
                s_colony& colony = MOX::colony[c];
                const s_planet_data& planet = MOX::planet[colony.planet];
                s_out row = list.Append_Object();
                row.Int("colony", c);
                row.Int("planet", colony.planet);
                row.Int("star", planet.star_index);
                row.Str("name", Utf8(HACCESS::Get_Planet_Name_No_Prime(colony.planet), 80));
                row.Int("pops", (uint8_t)colony.n_pops);
                row.Int("max_population", COLCALC::Planet_Max_Population_For_Player(&MOX::planet[colony.planet], me));
                row.Int("max_farms", colony.max_farms);
                row.Int("food", colony.production[ECON_FOOD]);
                row.Int("industry", colony.production[ECON_INDUSTRY]);
                row.Int("research", colony.production[ECON_RESEARCH]);
                row.Int("bc", colony.production[ECON_BC]);
                Add_Jobs(c, row.Add_List("jobs"));
                // The row's label: an event at the colony (good or bad), or a blockade.
                const int16_t event_id = EVENTS::Colony_Has_Event(c);
                if (event_id >= 0 && event_id < 36 && COLSUM::wee_event_strings[event_id] != nullptr) {
                    row.Str("event", Utf8(COLSUM::wee_event_strings[event_id], 80));
                    row.Bool("event_good", COLSUM::Event_Good(event_id));
                }
                row.Bool("blockaded", struct_::Test_Bit_Field(&MOX::star[planet.star_index].blockaded, me));
                // The Building column (Draw_Colony_Summary_For_Colony): the item, its turns, the
                // autobuild marker and the row's buy button.
                const int16_t item = colony.producing[0];
                s_out build = row.Add_Object("build");
                build.Int("id", item);
                build.Int("rank", (int32_t)(std::find(by_build.begin(), by_build.end(), c) - by_build.begin()));
                if (item != COLONY_PRODUCTION_NONE) {
                    build.Str("name", Item_Name(colony, item));
                    const int16_t turns = COLONY::Calculate_Current_Production_Turn_Count(c);
                    build.Int("turns", turns < 1 || turns > 9999 ? 0 : turns);
                }
                const int preset = BUILD_QUEUE::autobuild_settings.enabled != 0 ? BUILD_QUEUE::Decode_Autobuild_Preset(colony.auto_building) : -1;
                build.Bool("auto", BUILD_QUEUE::Is_Legacy_Autobuild(colony.auto_building) || preset >= 0);
                if (preset >= 0) {
                    build.Int("preset", preset);
                }
                build.Int("bought", colony.bought_outright);
                const bool settable = item != COLONY_PRODUCTION_NONE && item != COLONY_PRODUCTION_HOUSING && item != COLONY_PRODUCTION_TRADE_GOODS;
                build.Bool("can_buy", COLONY::Colony_Can_Buy_Product_0(c));
                build.Int("buy_cost", settable && colony.bought_outright == 0 ? COLCALC::Colony_Cost_To_Buy_Producing_Product(&colony) : 0);
                // The scan box (Draw_Colony_Scan_Info): the planet, the population with its growth
                // and settlers on the way, and the build's progress.
                s_out scan = row.Add_Object("scan");
                scan.Str("size", Utf8(MOX::planet_size_string[planet.size], 40));
                scan.Str("climate", Utf8(MOX::planet_climate_string[(uint8_t)colony.climate], 40));
                scan.Str("gravity", Utf8(MOX::planet_gravity_string[planet.gravity_class], 40));
                scan.Str("minerals", Utf8(MOX::mineral_class_string[planet.mineral_class], 40));
                int32_t roundoff = 0;
                int32_t growth = 0;
                for (int r = 0; r < 10; ++r) {
                    roundoff += colony.pop_roundoff[r];
                    growth += colony.pop_growth[r];
                }
                int32_t inbound = 0;
                for (int16_t i = 0; i < player.settlers_freighted; ++i) {
                    if (SETTLER::Get_Dest_Planet(player.settlers[i].val) == colony.planet) {
                        ++inbound;
                    }
                }
                int32_t change = (roundoff + growth) / 1000;
                if (roundoff + growth < 0) {
                    --change;
                }
                scan.Int("inbound", inbound);
                scan.Int("thousands", roundoff);
                scan.Int("growth", growth);
                scan.Int("change", change);
                scan.Int("spent", colony.production_spent + colony.bought_outright);
                scan.Int("cost", item != COLONY_PRODUCTION_NONE ? COLCALC::Colony_Producing_Product_Cost(&colony) : 0);
                scan.Int("net_industry", std::max(0, (int32_t)colony.production[ECON_INDUSTRY] - colony.maintenance[ECON_INDUSTRY]));
            }
            // The empire box (Draw_Empire_Info, the classic research line).
            s_out empire = out.Add_Object("empire");
            empire.Int("reserve", player.bc);
            empire.Int("income", player.surplus_bc);
            empire.Int("population", player.total_pop);
            empire.Int("settlers_freighted", player.settlers_freighted);
            empire.Int("freighters", player.surplus_freighters);
            empire.Int("food", player.surplus_food);
            empire.Int("research", player.research_produced);
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
            int64_t job = -1;
            std::vector<int16_t> pops;
            yyjson_val* list = args.Get("pops");
            int64_t pop = -1;
            if (yyjson_is_arr(list)) {
                size_t index = 0;
                size_t count = 0;
                yyjson_val* item = nullptr;
                yyjson_arr_foreach(list, index, count, item) {
                    pops.push_back(yyjson_is_int(item) ? (int16_t)yyjson_get_int(item) : (int16_t)-1);
                }
            } else if (args.Int("pop", &pop)) {
                pops.push_back((int16_t)pop);
            }
            if (!args.Int("colony", &colony_id) || pops.empty() || !args.Int("job", &job) || job < 0 || job > 2) {
                out.Fail("bad_args", "colony, pop (a colonist's index) or pops (a cluster), job (0 farmer, 1 worker, 2 scientist)");
                return;
            }
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            const int16_t c = (int16_t)colony_id;
            s_colony& colony = MOX::colony[c];
            for (int16_t p : pops) {
                if (colony.outpost_flag != 0 || p < 0 || p >= (uint8_t)colony.n_pops || (colony.pop[p] & 0x200) == 0) {
                    out.Fail("bad_args", "pops: assigned colonists of this colony");
                    return;
                }
            }
            // Picking natives up is refused with the game's message (COLMOVE::Get_Cluster).
            if (POP::Is_Native(colony.pop[pops[0]]) && !COLMOVE::native_population_transfer) {
                GENDRAW::Help(ESTRINGS::E_Strings(0x17e));
                out.Int("moved", 0);
                return;
            }
            // Dropped on a row (COLMOVE::Send_Cluster within the colony): each in turn takes the
            // job; at the first refusal (the game's message) the rest go back where they were.
            // Only a cluster moves together: the ones identical to the first, decided before any moves.
            std::vector<int16_t> cluster;
            for (int16_t p : pops) {
                if (p == pops[0] || COLMOVE::Pops_Identical(c, pops[0], c, p)) {
                    cluster.push_back(p);
                }
            }
            int moved = 0;
            for (int16_t p : cluster) {
                if ((int)((colony.pop[p] >> 7) & 0x3) == job) {
                    continue;
                }
                POP::Set_Assigned(&colony.pop[p], false); // picked up: not counted in its new row
                if (COLMOVE::Give_Colonist_New_Job(c, p, (int16_t)job, false) == 0) {
                    POP::Set_Assigned(&colony.pop[p], true); // put back
                    break;
                }
                ++moved;
            }
            COLCALC::Colony_Calculation(c);
            out.Bool("moved", moved > 0);
            out.Int("count", moved);
        }

        // WP3.5a: colonists to another colony, as the Colonies screen moves them (a click on a
        // colonist picks up its cluster, COLMOVE::Get_Cluster; a click on another colony's job,
        // or its name for any job, sends it, COLMOVE::Send_Cluster, with the game's questions and
        // messages). Whatever the game didn't take goes back (COLMOVE::Clear_Cluster, as RETURN).
        void Send_Colonists(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t from = -1;
            int64_t pop = -1;
            int64_t to = -1;
            int64_t job = -1;
            if (!args.Int("from", &from) || !args.Int("pop", &pop) || !args.Int("to", &to)) {
                out.Fail("bad_args", "from, pop (a colonist of that colony), to (a colony), job (0-2, or -1: any)");
                return;
            }
            args.Int("job", &job);
            if (job < -1 || job > 2 || !Player_Colony(from, out) || !Player_Colony(to, out)) {
                if (job < -1 || job > 2) {
                    out.Fail("bad_args", "job: 0-2, or -1");
                }
                return;
            }
            const s_colony& source = MOX::colony[from];
            const s_colony& target = MOX::colony[to];
            if (source.outpost_flag != 0 || target.outpost_flag != 0 || pop < 0 || pop >= (uint8_t)source.n_pops ||
                (source.pop[pop] & 0x200) == 0) {
                out.Fail("bad_args", "pop: an assigned colonist of a colony (not an outpost)");
                return;
            }
            if (COLMOVE::cluster_colony_n >= 0) {
                COLMOVE::Clear_Cluster(); // nothing may be left on the pointer from elsewhere
            }
            const int before = (uint8_t)source.n_pops; // settlers leave now and arrive later (freighted)
            COLMOVE::Get_Cluster((int16_t)from, (int16_t)pop);
            if (COLMOVE::cluster_colony_n != from) {
                out.Int("moved", 0); // refused at pickup (natives), with the game's message
                return;
            }
            COLMOVE::Send_Cluster((int16_t)to, (int16_t)job);
            if (COLMOVE::cluster_colony_n >= 0) {
                COLMOVE::Clear_Cluster();
            }
            COLCALC::Colony_Calculation((int16_t)from);
            if (to != from) {
                COLCALC::Colony_Calculation((int16_t)to);
            }
            out.Int("moved", before - (uint8_t)source.n_pops);
        }

        // transport_targets {colony, pop}: where that colonist (and its cluster) can be sent, as
        // the original's transport list offers (COLXPORT::Xport_Popup), with what the game's own
        // check says of each (SETTLER::Pop_Tries_To_Settle, which changes nothing): every other
        // colony of the player's (not outposts) with its system, population, planet and ETA, and
        // why it can't take them. A reason that holds for any destination (freighters, a
        // blockade at home, the last colonist) comes once, as `blocked`.
        void Query_Transport_Targets(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t colony_id = -1;
            int64_t pop = -1;
            if (!args.Int("colony", &colony_id) || !args.Int("pop", &pop)) {
                out.Fail("bad_args", "colony, pop (a colonist of that colony)");
                return;
            }
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            s_colony& source = MOX::colony[colony_id];
            if (source.outpost_flag != 0 || pop < 0 || pop >= (uint8_t)source.n_pops) {
                out.Fail("bad_args", "pop: a colonist of a colony (not an outpost)");
                return;
            }
            const int16_t me = source.owner;
            const s_player& player = MOX::player[me];
            out.Int("freighters", player.n_freighters);
            out.Int("settlers_freighted", player.settlers_freighted);
            std::string blocked;
            s_out list = out.Add_List("targets");
            for (int16_t c = 0; c < MOX::NUM_COLONIES; ++c) {
                s_colony& colony = MOX::colony[c];
                if (c == colony_id || colony.owner != me || colony.outpost_flag != 0 || colony.planet < 0) {
                    continue;
                }
                s_settler_settle_result result{};
                SETTLER::Pop_Tries_To_Settle(&source, (int16_t)pop, &colony, &result);
                const char* global = result.source_blockaded ? "This system is blockaded"
                    : result.source_space_anomaly ? "A space anomaly holds the colonists here"
                    : result.source_plague ? "The plague keeps the colonists here"
                    : result.only_one_pop_left ? "The last colonist can't leave"
                    : result.selected_pop_flag_0x400 ? "Conquered colonists can't be moved"
                    : nullptr;
                if (global != nullptr && blocked.empty()) {
                    blocked = global;
                }
                const s_planet_data& planet = MOX::planet[colony.planet];
                const int16_t star = planet.star_index;
                s_out row = list.Append_Object();
                row.Int("colony", c);
                row.Int("planet", colony.planet);
                row.Int("star", star);
                row.Str("name", Utf8(HACCESS::Get_Planet_Name_No_Prime(colony.planet), 80));
                row.Str("system", Utf8(MOX::star[star].name, sizeof(MOX::star[0].name)));
                row.Int("population", (uint8_t)colony.n_pops);
                row.Int("max_population", COLCALC::Planet_Max_Population_For_Player(&MOX::planet[colony.planet], me));
                row.Str("size_name", Utf8(MOX::planet_size_string[planet.size], 40));
                row.Str("climate_name", Utf8(MOX::planet_climate_string[(uint8_t)colony.climate], 40));
                row.Str("minerals_name", Utf8(MOX::mineral_class_string[planet.mineral_class], 40));
                if (planet.planet_special > SYSTEM_SPECIAL_NO_SPECIAL && planet.planet_special < SYSTEM_SPECIAL_COUNT &&
                    planet.planet_special != SYSTEM_SPECIAL_MAROONED_HERO && planet.planet_special != SYSTEM_SPECIAL_ORION_STAR) {
                    row.Str("special_name", Utf8(MOX::planet_special_string[planet.planet_special], 80));
                }
                row.Int("eta", result.can_settle ? (int)result.eta_turns
                                                 : (int)SETTLER::Settler_ETA(&MOX::player[me], MOX::planet[source.planet].star_index, star));
                const char* reason = global != nullptr ? nullptr
                    : result.insufficient_freighters ? "Not enough freighters"
                    : result.max_settlers_in_transit ? "Too many colonists already in transit"
                    : result.no_room_at_destination ? "No room for them there"
                    : result.dest_blockaded ? "That system is blockaded"
                    : result.dest_space_anomaly ? "A space anomaly there"
                    : nullptr;
                // The game still takes them where there is no room or a blockade (it asks first).
                row.Bool("can_send", global == nullptr && result.can_settle != 0);
                if (reason != nullptr) {
                    row.Str("reason", reason);
                }
            }
            if (!blocked.empty()) {
                out.Str("blocked", blocked);
            }
        }

        // A building scrapped from the colony pane (COLONY::Scrap_Building, as clicking it on the
        // landscape does): the game's question with the refund, its once-a-turn rule and message.
        void Sell_Building(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t colony_id = -1;
            int64_t building = -1;
            if (!args.Int("colony", &colony_id) || !args.Int("building", &building) || building <= 0 || building >= BUILDING_COUNT) {
                out.Fail("bad_args", "colony, building (a building id)");
                return;
            }
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            const int16_t c = (int16_t)colony_id;
            if (MOX::colony[c].buildings[building] == 0 || !COLONY::Building_Can_Be_Scrapped((int16_t)building)) {
                out.Fail("refused", "not a building this colony can scrap");
                return;
            }
            const bool sold = COLONY::Scrap_Building(c, (int16_t)building, false);
            out.Bool("sold", sold);
            out.Int("bc", MOX::player[MOX::colony[c].owner].bc);
        }

        // The occupation policy (OCPOLPUP::Occupation_Policy_Popup's two answers): annihilate the
        // conquered people (0) or keep them (3). Democracies and Federations always keep them.
        void Set_Occupation(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t colony_id = -1;
            int64_t annihilate_arg = -1;
            if (!args.Int("colony", &colony_id) || !args.Int("annihilate", &annihilate_arg) || annihilate_arg < 0 || annihilate_arg > 1) {
                out.Fail("bad_args", "colony, annihilate (1 or 0)");
                return;
            }
            const bool annihilate = annihilate_arg == 1;
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            const int16_t c = (int16_t)colony_id;
            if (annihilate && !Occupation_Can_Annihilate(MOX::colony[c].owner)) {
                out.Fail("refused", "your government keeps conquered people");
                return;
            }
            MOX::colony[c].occupation_policy = annihilate ? 0 : 3;
            COLONY::Col_Calc_Wrapper(c);
            out.Bool("annihilate", annihilate);
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

        // ---- The build pane (WP3.4b) ----

        // The build popup's globals (COLONY handles, the working queue, the lists, its modes), set
        // for one colony as COLBLDG::Build_Queue_Popup sets them, and put back afterwards: queries
        // and the commit borrow the popup's own functions, which read and fill these.
        struct s_build_scope {
            int16_t colony_handle = COLONY::colony_handle;
            int16_t planet_handle = COLONY::planet_handle;
            int16_t star_handle = COLONY::star_handle;
            int16_t current_item[7];
            int16_t active_prod[4];
            uint8_t field_mode = COLBLDG::field_mode;
            int16_t colony_auto_building = COLBLDG::colony_auto_building;
            int16_t building_indexes[54];
            int16_t n_building_indexes = COLBLDG::n_building_indexes;
            int16_t military_indexes[54];
            int16_t n_military_indexes = COLBLDG::n_military_indexes;

            explicit s_build_scope(int16_t colony) {
                memcpy(current_item, COLBLDG::current_item, sizeof(current_item));
                memcpy(active_prod, COLBLDG::active_prod, sizeof(active_prod));
                memcpy(building_indexes, COLBLDG::building_indexes, sizeof(building_indexes));
                memcpy(military_indexes, COLBLDG::military_indexes, sizeof(military_indexes));
                COLONY::colony_handle = colony;
                COLONY::planet_handle = MOX::colony[colony].planet;
                COLONY::star_handle = MOX::planet[MOX::colony[colony].planet].star_index;
                for (int i = 0; i < 7; ++i) {
                    COLBLDG::current_item[i] = MOX::colony[colony].producing[i];
                }
                COLBLDG::active_prod[0] = -1;
                COLBLDG::field_mode = 0;
                COLBLDG::colony_auto_building = MOX::colony[colony].auto_building != 0;
                COLBLDG::Forget_Refit_Originals();
            }

            ~s_build_scope() {
                COLBLDG::Forget_Refit_Originals();
                COLONY::colony_handle = colony_handle;
                COLONY::planet_handle = planet_handle;
                COLONY::star_handle = star_handle;
                memcpy(COLBLDG::current_item, current_item, sizeof(current_item));
                memcpy(COLBLDG::active_prod, active_prod, sizeof(active_prod));
                COLBLDG::field_mode = field_mode;
                COLBLDG::colony_auto_building = colony_auto_building;
                memcpy(COLBLDG::building_indexes, building_indexes, sizeof(building_indexes));
                COLBLDG::n_building_indexes = n_building_indexes;
                memcpy(COLBLDG::military_indexes, military_indexes, sizeof(military_indexes));
                COLBLDG::n_military_indexes = n_military_indexes;
            }
        };

        // One buildable item with what the popup's detail box shows (Draw_Cost_And_Time_Info,
        // Build_Ship_Design_Info_Formatted_Paragraph): cost here, build time, upkeep, the picture,
        // and a design's parts.
        void Build_Item(int16_t c, int16_t id, s_out item) {
            s_colony& colony = MOX::colony[c];
            item.Int("id", id);
            item.Str("name", Item_Name(colony, id));
            Item_Picture(colony, id, item.Add_Object("picture"));
            const std::string description = Item_Description(id);
            if (!description.empty()) {
                item.Str("description", description);
            }
            if (id == COLONY_PRODUCTION_HOUSING || id == COLONY_PRODUCTION_TRADE_GOODS) {
                return; // settings, not items
            }
            item.Int("cost", COLCALC::Colony_Product_Cost(&colony, id));
            item.Int("turns", COLONY::Calculate_Colony_Turn_Count_From_Scrap_For_Prod(c, id));
            if (COLBLDG::Colony_Production_Is_Building(id)) {
                item.Int("upkeep", TECHDATA::buildings[id].maintenance);
            }
            if (!COLBLDG::Colony_Production_Is_Ship_Design(id)) {
                return;
            }
            Design_Parts(MOX::player[colony.owner].ship_designs[COLBLDG::Colony_Production_Ship_Design_Index(id)], item);
        }

        // REFIT's requirement, as the popup checks it: Chemistry and Nuclear Fission researched.
        bool Can_Refit(int16_t owner) {
            const s_player& player = MOX::player[owner];
            return player.tech_fields[TECH_FIELD_CHEMISTRY] == TECH_RESEARCH_STATUS_RESEARCHED &&
                player.tech_fields[TECH_FIELD_NUCLEAR_FISSION] == TECH_RESEARCH_STATUS_RESEARCHED;
        }

        // A ship REFIT offers (COLREFIT::Build_Ship_List): the colony owner's warship in orbit,
        // not already refitting or building.
        bool Refit_Ship(const s_colony& colony, int64_t ship) {
            if (ship < 0 || ship >= MAX_SHIPS) {
                return false;
            }
            const s_ship_data& data = MOX::ship[ship];
            return data.owner == colony.owner && data.status < 3 &&
                data.location == MOX::planet[colony.planet].star_index && data.d.ship_type == SHIP_TYPE_COMBAT;
        }

        // A design a ship can be refitted to (COLREFIT::Evaluate2_Input): a saved design of its size.
        bool Refit_Target(const s_colony& colony, const s_ship_data& ship, int64_t slot) {
            if (slot < 0 || slot >= SHIP_SIZE_COUNT) {
                return false;
            }
            const s_ship_design& design = MOX::player[colony.owner].ship_designs[slot];
            return design.size == ship.d.size && design.name[0] != '\0' &&
                strcmp(COLBLDG::Selection_Name(COLBLDG::Colony_Production_From_Ship_Design_Index((int16_t)slot)), ESTRINGS::E_Strings(12)) != 0;
        }

        // REFIT (the popup's button and its two lists): the ships in orbit, each with the
        // designs it can become and what that costs (AIBUILD::Refit_Cost, as
        // Add_Refited_Ship_To_Queue charges it).
        void Refit_Options(int16_t c, s_out refit) {
            s_colony& colony = MOX::colony[c];
            const bool can = Can_Refit(colony.owner);
            refit.Bool("available", can);
            if (!can) {
                refit.Str("message", Utf8(ESTRINGS::E_Strings(0x244), 400));
            }
            s_out ships = refit.Add_List("ships");
            for (int16_t i = 0; i < MAX_SHIPS; ++i) {
                if (!Refit_Ship(colony, i)) {
                    continue;
                }
                s_ship_data& ship = MOX::ship[i];
                s_out row = ships.Append_Object();
                row.Int("ship", i);
                row.Str("name", Utf8(ship.d.name, sizeof(ship.d.name)));
                s_out picture = row.Add_Object("picture");
                picture.Str("lbx", "SHIPS.LBX");
                picture.Int("entry", (int32_t)ship.d.picture_num + (int32_t)MOX::player[colony.owner].color * 50);
                Design_Parts(ship.d, row);
                s_out targets = row.Add_List("targets");
                for (int16_t slot = 0; slot < SHIP_SIZE_COUNT; ++slot) {
                    if (!Refit_Target(colony, ship, slot)) {
                        continue;
                    }
                    s_ship_design design = MOX::player[colony.owner].ship_designs[slot];
                    design.cost = DESIGN::Cost_Of_Design(colony.owner, &design);
                    s_out target = targets.Append_Object();
                    target.Int("design", slot);
                    target.Str("name", Utf8(design.name, sizeof(design.name)));
                    target.Int("cost", AIBUILD::Refit_Cost(&MOX::player[colony.owner], &ship.d, &design));
                }
            }
        }

        // What CHANGE offers (COLBLDG::Build_Arrays for the colony, as the popup opens): Buildings
        // (trade goods, housing, the buildable buildings by name) and Units (in the popup's order,
        // separators kept), with the queue and the auto build switch.
        void Query_Build_Options(int16_t, const s_args& args, s_out& out) {
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
            s_build_scope scope(c);
            COLBLDG::Build_Arrays();
            out.Int("colony", c);
            out.Bool("auto", MOX::colony[c].auto_building != 0);
            s_out buildings = out.Add_List("buildings");
            for (int i = 0; i < COLBLDG::n_building_indexes; ++i) {
                Build_Item(c, COLBLDG::building_indexes[i], buildings.Append_Object());
            }
            s_out units = out.Add_List("units");
            for (int i = 0; i < COLBLDG::n_military_indexes; ++i) {
                const int16_t id = COLBLDG::military_indexes[i];
                if (id == COLONY_PRODUCTION_SEPARATOR) {
                    units.Append_Object().Bool("separator", true);
                    continue;
                }
                Build_Item(c, id, units.Append_Object());
            }
            Refit_Options(c, out.Add_Object("refit"));
            out.Int("slots", 7);
            s_out queue = out.Add_List("queue");
            for (int slot = 0; slot < 7; ++slot) {
                const int16_t id = MOX::colony[c].producing[slot];
                if (id == COLONY_PRODUCTION_NONE || id == 0) {
                    break;
                }
                s_out q = queue.Append_Object();
                q.Int("id", id);
                q.Str("name", Item_Name(MOX::colony[c], id));
                if (id != COLONY_PRODUCTION_REPEAT) {
                    Item_Picture(MOX::colony[c], id, q.Add_Object("picture"));
                }
            }
        }

        // CHANGE's OK: the build pane's edits replayed through the popup's own functions in the
        // order they were made, with the popup's random seed (the colony's number, as
        // Build_Queue_Popup sets it), then the popup's OK (COLBLDG::Commit_Build_Queue). Ships for
        // designs are made here, as the popup makes them when they're added; Cancel sends nothing.
        // Edits: {"add": id, "repeat": bool}, {"delete": slot}, {"move": [from, to]},
        // {"refit": ship, "design": slot}. Questions
        // the popup asks along the way (androids filling the planet, cancelling a refit, housing
        // or trade goods ahead of other items) are the game's own.
        void Build_Commit(int16_t, const s_args& args, s_out& out) {
            if (!Have_Game(out)) {
                return;
            }
            int64_t colony_id = -1;
            if (!args.Int("colony", &colony_id)) {
                out.Fail("bad_args", "colony, edits [...], auto");
                return;
            }
            if (!Player_Colony(colony_id, out)) {
                return;
            }
            yyjson_val* edits = args.Get("edits");
            if (edits != nullptr && !yyjson_is_arr(edits)) {
                out.Fail("bad_args", "edits: a list of {add, repeat} / {delete} / {move}");
                return;
            }
            const int16_t c = (int16_t)colony_id;
            s_build_scope scope(c);
            const uint32_t seed = game_random::Get_Random_Seed();
            game_random::Set_Random_Seed(c);
            COLBLDG::quiet_ui = true;
            int applied = 0;
            size_t index = 0;
            size_t count = 0;
            yyjson_val* edit = nullptr;
            if (edits != nullptr) {
                yyjson_arr_foreach(edits, index, count, edit) {
                    yyjson_val* add = yyjson_obj_get(edit, "add");
                    yyjson_val* del = yyjson_obj_get(edit, "delete");
                    yyjson_val* move = yyjson_obj_get(edit, "move");
                    yyjson_val* refit = yyjson_obj_get(edit, "refit");
                    if (yyjson_is_int(refit)) {
                        // REFIT's second list chose a design (COLREFIT::Evaluate2_Input), then the
                        // popup queues the ship (Add_Refited_Ship_To_Queue).
                        const int64_t ship = yyjson_get_int(refit);
                        const int64_t slot = yyjson_get_int(yyjson_obj_get(edit, "design"));
                        if (!Can_Refit(MOX::colony[c].owner) || !Refit_Ship(MOX::colony[c], ship) ||
                            !Refit_Target(MOX::colony[c], MOX::ship[ship], slot)) {
                            continue;
                        }
                        COLBLDG::Set_Pending_Refit_Design((int16_t)ship, &MOX::player[MOX::colony[c].owner].ship_designs[slot]);
                        COLBLDG::Add_Refited_Ship_To_Queue();
                    } else if (yyjson_is_int(add)) {
                        const int16_t id = (int16_t)yyjson_get_int(add);
                        if (id == COLONY_PRODUCTION_CUSTOM_DESIGN) {
                            continue; // the popup's Design button, not an item
                        }
                        COLBLDG::field_mode = yyjson_get_bool(yyjson_obj_get(edit, "repeat")) ? 2 : 0;
                        COLBLDG::Add_To_Queue(id);
                        COLBLDG::field_mode = 0;
                    } else if (yyjson_is_int(del)) {
                        const int64_t slot = yyjson_get_int(del);
                        if (slot < 0 || slot >= 7 || (slot == 0 && MOX::colony[c].bought_outright != 0)) {
                            continue;
                        }
                        // (a refit from this commit goes back to the ship it was: Delete_From_Queue)
                        COLBLDG::Delete_From_Queue((int16_t)slot);
                    } else if (yyjson_is_arr(move) && yyjson_arr_size(move) == 2) {
                        const int64_t from = yyjson_get_int(yyjson_arr_get(move, 0));
                        const int64_t to = yyjson_get_int(yyjson_arr_get(move, 1));
                        if (from >= 0 && from < 7 && to >= 0 && to < 7 && from != to) {
                            COLBLDG::Insert_Before_Queue_Slot((int16_t)from, (int16_t)to);
                            COLBLDG::Defragment_Queue();
                        }
                    } else {
                        continue;
                    }
                    ++applied;
                }
            }
            yyjson_val* automatic = args.Get("auto");
            if (yyjson_is_bool(automatic)) {
                COLBLDG::colony_auto_building = yyjson_get_bool(automatic) ? 1 : 0;
            }
            COLBLDG::Commit_Build_Queue();
            COLBLDG::quiet_ui = false;
            game_random::Set_Random_Seed(seed);
            COLCALC::Colony_Calculation(c);
            out.Int("applied", applied);
            s_out queue = out.Add_List("queue");
            for (int slot = 0; slot < 7; ++slot) {
                const int16_t id = MOX::colony[c].producing[slot];
                if (id == COLONY_PRODUCTION_NONE || id == 0) {
                    break;
                }
                queue.Append_Object().Int("id", id);
            }
        }

    } // namespace

    // One record of a record-table LBX entry (the help LBX), `length` bytes from `offset`, as UTF-8.
    std::string Lbx_Record(const char* lbx, int32_t entry, uint16_t record, size_t offset, size_t length) {
        s_vfs_file* file = vfs::fopen_lbx(lbx, entry);
        if (file == nullptr) {
            return {};
        }
        uint16_t header[2] = {};
        std::string text(length + 1, '\0');
        bool ok = vfs::fread(header, sizeof(header), 1, file) == 1 && record < header[0] && offset + length <= header[1];
        if (ok) {
            ok = vfs::fseek(file, (long)(sizeof(header) + (size_t)record * header[1] + offset), SEEK_SET) == 0 &&
                vfs::fread(text.data(), length, 1, file) == 1;
        }
        vfs::fclose(file);
        if (!ok) {
            return {};
        }
        return Utf8(text.data(), length);
    }

    // The number of records in a record-table LBX entry (HELP.LBX's topic lists), 0 if none.
    uint16_t Lbx_Record_Count(const char* lbx, int32_t entry) {
        s_vfs_file* file = vfs::fopen_lbx(lbx, entry);
        if (file == nullptr) {
            return 0;
        }
        uint16_t header[2] = {};
        const bool ok = vfs::fread(header, sizeof(header), 1, file) == 1;
        vfs::fclose(file);
        return ok ? header[0] : 0;
    }

    // One record's first `length` bytes raw, false if there is none.
    bool Lbx_Record_Bytes(const char* lbx, int32_t entry, uint16_t record, void* bytes, size_t length) {
        s_vfs_file* file = vfs::fopen_lbx(lbx, entry);
        if (file == nullptr) {
            return false;
        }
        uint16_t header[2] = {};
        bool ok = vfs::fread(header, sizeof(header), 1, file) == 1 && record < header[0] && length <= header[1];
        if (ok) {
            ok = vfs::fseek(file, (long)(sizeof(header) + (size_t)record * header[1]), SEEK_SET) == 0 &&
                vfs::fread(bytes, length, 1, file) == 1;
        }
        vfs::fclose(file);
        return ok;
    }

    // One help record (the help LBX's entry 0) whole, false if there is none.
    bool Help_Record(uint16_t topic, s_help_record* record) {
        char lbx[20];
        TEXTBOX::Get_Help_Lbx_Name(lbx, sizeof(lbx));
        return Lbx_Record_Bytes(lbx, 0, topic, record, sizeof(s_help_record));
    }

    // A design's parts, as the popup lists them (Build_Ship_Design_Info_Formatted_Paragraph).
    void Design_Parts(s_ship_design& design, s_out item) {
        item.Str("hull", Utf8(TECHDATA::hull_data[design.size].name, 40));
        item.Str("shield", Utf8(TECHDATA::shields[design.shield_type].name, 40));
        item.Str("computer", Utf8(TECHDATA::computers[design.computer_type].name, 40));
        item.Str("armor", Utf8(TECHDATA::armor[design.armor_type].name, 40));
        item.Str("drive", Utf8(TECHDATA::drives[design.ftl_type].name, 40));
        s_out weapons = item.Add_List("weapons");
        for (s_ship_weapons& weapon : design.ship_weapon) {
            if (weapon.type == 0) {
                continue;
            }
            char text[200];
            COLBLDG::Build_Weapon_String(&weapon, text, sizeof(text), 0, (uint8_t)weapon.count);
            weapons.Append_Object().Str("text", std::to_string((int)weapon.count) + " " + Utf8(text, sizeof(text)));
        }
        s_out specials = item.Add_List("specials");
        for (int i = 0; i < 39; ++i) {
            if (struct_::Test_Bit_Field(design.special_device_flags, i)) {
                specials.Append_Object().Str("text", Utf8(TECHDATA::specials[i].name, 60));
            }
        }
    }

    // An application's help text, as the game's help popup shows it (HELP.LBX's record body).
    std::string Application_Help(int16_t app) {
        if (app < 0) {
            return {};
        }
        char lbx[20];
        TEXTBOX::Get_Help_Lbx_Name(lbx, sizeof(lbx));
        return Lbx_Record(lbx, 0, (uint16_t)app, offsetof(s_help_record, body), sizeof(s_help_record::body));
    }

    void Add_Colony_Entries(std::vector<s_entry>* table) {
        const uint64_t galaxy = Screen_Bit(SCREEN_MAIN);
        table->push_back({"colony", e_kind::query, ANY_SCREEN, 1, "colony: colony id (the player's)", Query_Colony});
        table->push_back({"colonies", e_kind::query, ANY_SCREEN, 1, "", Query_Colonies});
        table->push_back({"help", e_kind::query, ANY_SCREEN, 1, "topic: a help record (HELP.LBX)", Query_Help});
        table->push_back({"move_colonist", e_kind::command, galaxy, 0, "colony, pop or pops (a cluster), job (0-2)", Move_Colonist});
        table->push_back({"send_colonists", e_kind::command, galaxy, 0, "from, pop, to, job (-1: any)", Send_Colonists});
        table->push_back({"transport_targets", e_kind::query, ANY_SCREEN, 1, "colony, pop", Query_Transport_Targets});
        table->push_back({"sell_building", e_kind::command, galaxy, 0, "colony, building", Sell_Building});
        table->push_back({"buy", e_kind::command, galaxy, 0, "colony", Buy});
        table->push_back({"set_occupation", e_kind::command, galaxy, 0, "colony, annihilate", Set_Occupation});
        table->push_back({"build_options", e_kind::query, ANY_SCREEN, 1, "colony", Query_Build_Options});
        table->push_back({"build_commit", e_kind::command, galaxy, 0, "colony, edits [{add, repeat} / {delete} / {move} / {refit, design}], auto", Build_Commit});
    }

} // namespace antares::commands::internal
