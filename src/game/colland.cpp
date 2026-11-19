#include "pch.h"

#include "antares/native.h"

namespace COLLAND {
    s_animation_header* l_anims;
    int32_t pod1l_sfx;
    int32_t pod2d_sfx;
    int32_t pod2l_sfx;
    int32_t pod1d_sfx;
    int16_t fields;

    void __cdecl Print_Planet_Data(int16_t planet_idx) {
        char buffer[500];
        s_planet_data* planet = &MOX::planet[planet_idx];

        COLONY::Set_Colony_Font_To(4);

        if (planet->colony_index == -1) {
            char* planet_name = HACCESS::Get_Planet_Name(planet_idx);
            char* fmt = ESTRINGS::E_Strings(0x46 /* ESTR_S1 */);
            snprintf(buffer, sizeof(buffer), fmt, planet_name);
        } else {
            s_colony* colony = &MOX::colony[planet->colony_index];
            char* race_name = MOX::player[colony->owner].race_name;
            char* planet_name = HACCESS::Get_Planet_Name(planet_idx);
            char* fmt = ESTRINGS::E_Strings(0x28 /* ESTR_S__S_COLONY */);
            snprintf(buffer, sizeof(buffer), fmt, planet_name, race_name);
        }
        fonts::Print(0x14, 0x28, buffer);

        char* climate_str;
        char* size_str = MOX::planet_size_string[planet->size];

        if (planet->colony_index == -1) {
            if (planet->planet_type != PLANET_TYPE_PLANET) {
                char* gas_giant_msg = ESTRINGS::E_Strings(0x132 /* ESTR_GAS_GIANT__UNINHABITABLE */);
                strlcpy(buffer, gas_giant_msg, sizeof(buffer));
                fonts::Print(0x14, 0x3c, buffer);
                return;
            }
            climate_str = MOX::planet_climate_string[planet->climate];
        } else {
            climate_str = MOX::planet_climate_string[(uint8_t)MOX::colony[planet->colony_index].climate];
        }

        const char* size_climate_fmt;
        if (MOX::settings.language == 4) {
            size_climate_fmt = "%s, %s";
        } else {
            size_climate_fmt = ESTRINGS::E_Strings(0x2d /* ESTR_S_S */);
        }

        snprintf(buffer, sizeof(buffer), size_climate_fmt, size_str, climate_str);
        BILL::_Print_Paragraph(0x14, 0x3c, 0x26b, buffer, 0);

        int16_t max_pop = COLCALC::Planet_Max_Population_For_Player(planet, MOX::PLAYER_NUM);
        char* fmt_pop = ESTRINGS::E_Strings(0x1f);
        snprintf(buffer, sizeof(buffer), fmt_pop, (int)max_pop);
        fonts::Print(0x14, 0x50, buffer);

        char* mineral_str = MOX::mineral_class_string[planet->mineral_class];
        char* fmt_minerals = ESTRINGS::E_Strings(0x176);
        snprintf(buffer, sizeof(buffer), fmt_minerals, mineral_str);
        fonts::Print(0x14, 100, buffer);

        fonts::Print(0x14, 0x78, MOX::planet_gravity_string[planet->gravity_class]);

        if (planet->planet_special != SYSTEM_SPECIAL_NO_SPECIAL) {
            char* special_str = MOX::planet_special_string[planet->planet_special];
            char* fmt_special = ESTRINGS::E_Strings(0x46 /* ESTR_S1 */);
            snprintf(buffer, sizeof(buffer), fmt_special, special_str);
            BILL::_Print_Paragraph(0x14, 0x8c, 0x26b, buffer, 0);
        }
    }

    void __cdecl Load_Anims() {
        COLLAND::l_anims = (s_animation_header*)farload::Far_Reload_Next(COLONY::COLONY_LBX, 6, MOX::screen_seg);
    }

    void Add_Fields() {
        ::fields::Clear_Fields();

        char* hotkey = ESTRINGS::E_Strings(12);

        fields = ::fields::Add_Hidden_Field(
            0,
            0,
            639,
            479,
            hotkey,
            0);
    }

    void __cdecl Draw_Colony_Landing_Screen() {
        char buffer[500];

        video::Set_Page_Off();

        if (COLONY::full_draw != 0) {
            graphics::Fill(0, 0, 0x27F, 0x1DF, 0);

            s_animation_header* anim1 = COLONY::C_Anims(1);
            animate::Draw(0, 0, anim1);

            s_animation_header* anim0 = COLONY::C_Anims(0);
            animate::Draw(0, 0, anim0);

            COLONY::Set_Colony_Font_To(4);

            char* planet_name = HACCESS::Get_Planet_Name(COLONY::planet_handle);
            char* title_fmt = ESTRINGS::E_Strings(0xE5 /* ESTR_COLONY_BUILT_ON_S */);
            snprintf(buffer, sizeof(buffer), title_fmt, planet_name);
            fonts::Print_Centered(0x13F, 10, buffer);

            if (COLONY::full_draw != 0) {
                video::Copy_Off_To_Back();
                video::Set_Refresh_Stencil();
                COLONY::full_draw = 0;
            }
        }

        animate::Draw(0, 0, COLLAND::l_anims);

        int16_t current_frame = animate::Get_Current_Frame(COLLAND::l_anims);
        int16_t frame_count = animate::Get_Animation_Frame_Count(COLLAND::l_anims);

        if (((int32_t)frame_count - 2) <= (int32_t)current_frame) {
            COLLAND::Print_Planet_Data(COLONY::planet_handle);
        }

        if (current_frame == 1) {
            COLLAND::pod1d_sfx = ERICNET::Mox_Play_Sound(0x3A);
            return;
        }

        if (current_frame == 2) {
            COLLAND::pod2d_sfx = ERICNET::Mox_Play_Sound(0x3A);
            return;
        }

        if (current_frame == 0x39) {
            sound::Fadeout_Sound(COLLAND::pod1d_sfx, 2);
            COLLAND::pod1l_sfx = ERICNET::Mox_Play_Sound(0x39);
            return;
        }

        if (current_frame == 0x3E) {
            sound::Fadeout_Sound(COLLAND::pod2d_sfx, 2);
            COLLAND::pod2l_sfx = ERICNET::Mox_Play_Sound(0x39);
        }
    }

    namespace {
        // Antares (WP3.11a, owner 2026-11-19): the landing in the native scene window, the full
        // pane the research room uses; the frontend opens the new colony and its build list behind
        // it, so a click lands on the build decision. The legacy screen stays for the rare landing
        // that asks the occupation policy first (its popup is legacy-drawn).
        struct s_native_landing {
            std::string state;
            uint64_t start = 0;
            int16_t step = 0;
        };
        s_native_landing native_landing;

        std::string Native_Landing_Text(int16_t planet_idx) {
            char buffer[500];
            std::string text;
            const s_planet_data* planet = &MOX::planet[planet_idx];
            const s_colony* colony = &MOX::colony[planet->colony_index];
            snprintf(buffer, sizeof(buffer), ESTRINGS::E_Strings(0x28 /* ESTR_S__S_COLONY */), HACCESS::Get_Planet_Name(planet_idx),
                MOX::player[colony->owner].race_name);
            text += buffer;
            const char* size_climate_fmt = MOX::settings.language == 4 ? "%s, %s" : ESTRINGS::E_Strings(0x2d /* ESTR_S_S */);
            snprintf(buffer, sizeof(buffer), size_climate_fmt, MOX::planet_size_string[planet->size],
                MOX::planet_climate_string[(uint8_t)colony->climate]);
            text += std::string("\n") + buffer;
            snprintf(buffer, sizeof(buffer), ESTRINGS::E_Strings(0x1f),
                (int)COLCALC::Planet_Max_Population_For_Player(const_cast<s_planet_data*>(planet), MOX::PLAYER_NUM));
            text += std::string("\n") + buffer;
            snprintf(buffer, sizeof(buffer), ESTRINGS::E_Strings(0x176), MOX::mineral_class_string[planet->mineral_class]);
            text += std::string("\n") + buffer;
            text += std::string("\n") + MOX::planet_gravity_string[planet->gravity_class];
            if (planet->planet_special != SYSTEM_SPECIAL_NO_SPECIAL) {
                snprintf(buffer, sizeof(buffer), ESTRINGS::E_Strings(0x46 /* ESTR_S1 */), MOX::planet_special_string[planet->planet_special]);
                text += std::string("\n") + buffer;
            }
            return text;
        }

        std::string Native_Landing_State() {
            return native_landing.state;
        }

        // The pods' sounds at the frames Draw_Colony_Landing_Screen plays them (55 ms a frame, after
        // the window has opened, about half a second).
        void Native_Landing_Tick() {
            constexpr uint64_t k_open_ms = 500;
            constexpr uint64_t k_frame_ms = 55;
            const uint64_t elapsed = SDL_GetTicks() - native_landing.start;
            const auto reached = [&](uint64_t frame) { return elapsed >= k_open_ms + frame * k_frame_ms; };
            switch (native_landing.step) {
                case 0:
                    if (reached(1)) {
                        pod1d_sfx = ERICNET::Mox_Play_Sound(0x3A);
                        native_landing.step = 1;
                    }
                    break;
                case 1:
                    if (reached(2)) {
                        pod2d_sfx = ERICNET::Mox_Play_Sound(0x3A);
                        native_landing.step = 2;
                    }
                    break;
                case 2:
                    if (reached(0x39)) {
                        sound::Fadeout_Sound(pod1d_sfx, 2);
                        pod1l_sfx = ERICNET::Mox_Play_Sound(0x39);
                        native_landing.step = 3;
                    }
                    break;
                case 3:
                    if (reached(0x3E)) {
                        sound::Fadeout_Sound(pod2d_sfx, 2);
                        pod2l_sfx = ERICNET::Mox_Play_Sound(0x39);
                        native_landing.step = 4;
                    }
                    break;
                default:
                    break;
            }
        }

        void Native_Landing() {
            char title[200];
            snprintf(title, sizeof(title), ESTRINGS::E_Strings(0xE5 /* ESTR_COLONY_BUILT_ON_S */), HACCESS::Get_Planet_Name(COLONY::planet_handle));
            const s_planet_data& planet = MOX::planet[COLONY::planet_handle];
            const int16_t ground = (int16_t)((int8_t)planet.climate_bg_type + (uint8_t)MOX::colony[COLONY::colony_handle].climate * 3);
            const int16_t last_frame = (int16_t)(animate::Get_Animation_Frame_Count(COLLAND::l_anims) - 1);
            native_landing = s_native_landing();
            native_landing.state = "{\"scene\":\"landing\",\"placement\":\"research\",\"open_style\":\"slide\",\"frame_ms\":55"
                ",\"colony\":" + std::to_string(COLONY::colony_handle) + ",\"star\":" + std::to_string(COLONY::star_handle) +
                ",\"backdrop\":{\"lbx\":\"COLONY2.LBX\",\"entry\":49,\"frame\":0}"
                ",\"ground\":{\"lbx\":\"PLANETS.LBX\",\"entry\":" + std::to_string(ground) + ",\"frame\":0}"
                ",\"intro\":{\"lbx\":\"COLONY.LBX\",\"entry\":6,\"from\":0}"
                ",\"actor\":{\"lbx\":\"COLONY.LBX\",\"entry\":6,\"frame\":" + std::to_string(last_frame) + "}"
                ",\"title\":" + antares::native::Quote(title) +
                ",\"text\":" + antares::native::Quote(Native_Landing_Text(COLONY::planet_handle).c_str()) + ",\"text_box\":[20,100,600,140],\"text_align\":\"left\"}";
            native_landing.start = SDL_GetTicks();
            antares::native::Set_Scene(Native_Landing_State, Native_Landing_Tick);
            if (COLONY::Colony_Needs_Occupation_Policy(COLONY::colony_handle)) {
                // As the legacy screen, before the click: the question over the window (owner,
                // 2026-11-19).
                OCPOLPUP::Occupation_Policy_Popup();
                COLONY::Col_Calc_Wrapper(COLONY::colony_handle);
            }
            antares::native::Popup("scene", "", "\"scene\":\"landing\",\"page\":0", nullptr); // until a click
            COLONY::Reset_Field_Mode();
            sound::Fadeout_Sound(pod1d_sfx, 10);
            sound::Fadeout_Sound(pod2d_sfx, 10);
            sound::Fadeout_Sound(pod1l_sfx, 10);
            sound::Fadeout_Sound(pod2l_sfx, 10);
        }
    } // namespace

    void __cdecl Colony_Landing_Screen() {
        MOX2::action_func_t saved_auto_fn;
        int16_t input_result;

        COLONY::Set_Colony_Font();
        COLONY::Load_Colony_Screen_Seg();
        video::Clip_Off();
        COLONY::using_colony_screen_palette = 1;

        COLONY::star_handle = MOX::screen_data;
        COLONY::orbit_n = COLONY::orbit_temp;

        COLONY::planet_handle = HAROLD::Planet_Index(MOX::screen_data, COLONY::orbit_temp);
        COLONY::colony_handle = MOX::planet[COLONY::planet_handle].colony_index;
        COLONY::valid_colony = 1;
        COLONY::drawing_display = 0;
        COLONY::sys_disp_star_handle = COLONY::star_handle;

        Load_Anims();
        COLONY::Load_Planet_Screen();

        if (antares::native::Galaxy_Will_Be_Native() && antares::native::Popups_Native()) {
            Native_Landing();
            const int16_t star_idx = MOX::planet[MOX::colony[COLONY::colony_handle].planet].star_index;
            if (COLCALC::N_Colonies_And_Outposts_At_Star(star_idx) == 1 && MOX::settings.animations_on) {
                namestar::Change_Star_Name_Popup(namestar::Star_Name_Popup_Screen_Center_X(), namestar::Star_Name_Popup_Screen_Center_Y(), star_idx, 1);
            }
            antares::native::Popup("scene_end", "", "\"scene\":\"landing\"", nullptr);
            antares::native::Clear_Scene();
            COLONY::using_colony_screen_palette = 0;
            if (MOX::colony[COLONY::colony_handle].just_produced == COLONY_PRODUCTION_JUST_COLONIZED) {
                MOX::colony[COLONY::colony_handle].just_produced = COLONY_PRODUCTION_NONE;
            }
            MOX::current_screen = SCREEN_MAIN;
            return;
        }

        if (ERIC::Fast_Fade_Out_Needed_For_Colony_Palette()) {
            ERIC::Fast_Fade_Out();
            graphics::Fill(0, 0, 639, 479, 0);
            COLONY::Update_Colony_Palette();
        }

        if (ERIC::Fast_Fade_Out_Needed_For_Colony_Palette()) {
            palstore::Darken_Palette(100);
            palette::Clear_Palette_Changes(0, 255);
        }

        palette::Refresh_Palette();
        COLONY::Reset_Field_Arrays();
        Add_Fields();
        COLONY::full_draw = 1;

        MOX2::Get_Auto_Function(&saved_auto_fn);
        ::fields::Assign_Auto_Function(Draw_Colony_Landing_Screen, 1);
        Draw_Colony_Landing_Screen();
        ERIC::Fast_Fade_In();
        video::Set_Refresh_Stencil();

        if (COLONY::Colony_Needs_Occupation_Policy(COLONY::colony_handle)) {
            OCPOLPUP::Occupation_Policy_Popup();
            COLONY::Col_Calc_Wrapper(COLONY::colony_handle);
        }

        do {
            timer::Mark_Time();
            Add_Fields();
            Draw_Colony_Landing_Screen();
            animate::Get_Animation_Frame_Count(COLLAND::l_anims);
            animate::Get_Current_Frame(COLLAND::l_anims);
            RUSS::Mox_Sync_Update();
            input_result = ::fields::Get_Input();
            ::fields::Scan_Input();
            timer::Release_Time(1);
        } while (input_result == 0);

        COLONY::Reset_Field_Mode();
        ::fields::Clear_Fields();
        ::fields::Deactivate_Auto_Function();

        sound::Fadeout_Sound(pod1d_sfx, 10);
        sound::Fadeout_Sound(pod2d_sfx, 10);
        sound::Fadeout_Sound(pod1l_sfx, 10);
        sound::Fadeout_Sound(pod2l_sfx, 10);

        int16_t planet_idx = MOX::colony[COLONY::colony_handle].planet;
        int16_t star_idx = MOX::planet[planet_idx].star_index;

        if (COLCALC::N_Colonies_And_Outposts_At_Star(star_idx) == 1 && MOX::settings.animations_on) {
            int16_t cy = namestar::Star_Name_Popup_Screen_Center_Y();
            int16_t cx = namestar::Star_Name_Popup_Screen_Center_X();
            namestar::Change_Star_Name_Popup(cx, cy, star_idx, 1);
        }

        MOX2::Put_Auto_Function(&saved_auto_fn);
        COLONY::using_colony_screen_palette = 0;
        if (antares::native::Galaxy_Will_Be_Native()) {
            // Antares (owner, 2026-11-14): back to the native galaxy, where the frontend opens the
            // new colony's pane, not the legacy colony screen with its "place your orders" box and
            // build list.
            if (COLONY::colony_handle >= 0 && MOX::colony[COLONY::colony_handle].just_produced == COLONY_PRODUCTION_JUST_COLONIZED) {
                MOX::colony[COLONY::colony_handle].just_produced = COLONY_PRODUCTION_NONE;
            }
            MOX::current_screen = SCREEN_MAIN;
            return;
        }
        MOX::current_screen = SCREEN_COLONY;
    }
} // namespace COLLAND