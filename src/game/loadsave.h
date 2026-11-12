#pragma once
#include <cstdint>

namespace LOADSAVE {
    extern char months[12][6];

    void __cdecl Load_Month_Names();

    void __cdecl Allocate_Data_Space_For_Game_Popup();

    void __cdecl Restore_Main_Screen();

    void __cdecl Restore_Game_Popup();

    int16_t __cdecl Game_Popup_User_Box(const char* message, int16_t type);

    void __cdecl Reload_Language_Strings();

    extern uint8_t draw_game_popup_to_back;
    extern uint8_t dont_draw_darkened_buttons;
    extern uint8_t draw_down;
    extern char empty_save_slot_message[20];

    extern int16_t music_level_field;
    extern int16_t sfx_level_field;
    extern uint8_t print_data_to_bitmap;
    extern struct s_animation_header* bitmap;

    extern s_game_anims* game_anims;

    extern s_game_popup_fields* game_popup_fields;
    // Antares (WP3.3f): the popup was opened on one page (Settings, Load or Save) from the native
    // galaxy's game menu, so leaving that page leaves the popup instead of showing its main page.
    extern uint8_t antares_direct_page;
    extern uint8_t loaded_new_game;
    extern uint8_t loaded_debug_save;
    extern uint8_t keep_end_of_turn_wait_in_multiplayer;

    int32_t __cdecl Check_Magic_Number(const char* filename);

    bool __cdecl Get_Game_Type_From_Saved_Game(const char* filename);

    bool __cdecl Is_Valid_Game_Type(uint32_t game_type);

    /** Populates each save-slot status entry by probing the on-disk save files. */
    void __cdecl Check_For_Saved_Games(int8_t* slot_status);

    void __cdecl Get_Game_Popup_Colors(int16_t font_idx, s_colors* out_colors_primary, s_colors* out_colors_secondary);

    void __cdecl Add_Game_Popup_Fields(int16_t* out_x, int16_t* out_y);

    bool __cdecl Active_Save_Slot_Invalid();

    void __cdecl Set_Up_Load_Save_Popup();

    int8_t __cdecl Do_Load_Game_Popup(int16_t* exit_flag);

    void __cdecl Do_Main_Game_Popup(int16_t* out_val);

    void __cdecl Do_Options_Game_Popup(int16_t* out_val);

    void __cdecl Do_Save_Game_Popup(int16_t* out_val);

    void __cdecl _Draw_Load_Save_Game_Popup();

    void __cdecl _Draw_Main_Game_Popup();

    void __cdecl _Draw_Main_Game_Popup_To_Bitmap(struct s_animation_header* bitmap);

    void __cdecl _Draw_Load_Save_Game_Popup_To_Bitmap(struct s_animation_header* bitmap);

    void __cdecl _Draw_Options_Game_Popup_To_Bitmap(s_animation_header* bitmap);

    void __cdecl Animate_Game_Popup(s_animation_header* header, int16_t y_offset, int16_t direction, uint16_t type);

    void __cdecl Animate_Game_Popup_Pic_Height(s_animation_header* animation, uint16_t type);

    void __cdecl Save_Session_Related_Settings();

    void __cdecl Bad_Game_File_Message(uint8_t type, int16_t file_index);

    void __cdecl _Draw_Options_Game_Popup();

    void __cdecl _Game_Popup();

    /** Formats the per-slot stardate strings into 25-byte popup records. */
    void __cdecl Get_Star_Date_Strings(int32_t* star_dates, char* output_buf);

    void __cdecl Auto_Save_Last_Saved_Game(int16_t* flag);

    void __cdecl Auto_Load_Last_Saved_Game(int16_t* flag);

    /** Populates each 25-byte save-slot date record from the save-file timestamps. */
    void __cdecl Get_Save_Game_Date_Strings(char* output_buffer);

    /** Formats a single save file's timestamp into the popup date buffer. */
    void __cdecl Get_File_Date_String(const char* file_path, char* out_string);

    /** Wraps a save-slot label in the popup's inline highlight control bytes. */
    void __cdecl Embed_Special_Color_Codes(char* text, int16_t max_len);

    /** Strips the popup's inline highlight control bytes from a save-slot label. */
    void __cdecl Remove_Embedded_Special_Codes(char* text);

    void __cdecl Draw_Popup_Pic1();
    void __cdecl Draw_Popup_Pic2();

    void __cdecl Set_Music_For_Game_Popup();

    void __cdecl Set_Sound_For_Game_Popup(char play_sound_effect);

    void __cdecl Load_Game_Popup_Pictures();

    void __cdecl Set_Current_Game_Option_Flags();

    void __cdecl Animate_Game_Popup_Pic(s_animation_header* animation_header);

    void __cdecl Update_Game_Settings();
} // namespace LOADSAVE