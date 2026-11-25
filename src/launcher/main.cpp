#include "pch.h"

#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <string>
#include <vector>

#include "antares/identity.h"
#include "antares/user_folder.h"
#include "command_line.h"
#include "config/config_loader.h"
#include "config/config_runtime.h"
#include "game/user_paths.h"
#include "game/vfs.h"
#include "launcher/launcher_app.h"
#include "launcher/launcher_chrome.h"
#include "k_launcher_icon.h"
#include "launcher_font.h"

namespace {
    struct s_launcher_options {
        std::string game_dir;   // default: the folder holding the launcher
        std::string profile;    // profile to select at start
        std::string screenshot; // render one frame to this PNG and quit (headless review)
        std::string select;     // package whose details to show at start
        bool play = false;      // start the game with the profile and options, then quit
        std::string settings;   // open the Settings tab on this page or search
        int saves = -1;         // open the Saves tab with this slot selected (0: none)
        std::string set;        // change a setting in the profile first ("interface.scaler=hq2x")
        std::string enable;     // tick this package first, as the Packages tab does
        std::string disable;    // untick this package first
        bool confirm = false;   // answer OK to the popup listing what that switches off
        bool wait_game = false; // press Play, then (with -screenshot) wait for the game to stop first
        bool setup = false;     // open the setup wizard (WP5.11)
        std::string setup_folder; // ... on this folder's checklist
        std::string setup_run;    // run the whole wizard on this folder, no questions
        std::string setup_copy;   // check this folder and press Copy, as a player would
        bool setup_finish = false; // with -setup-run: press "Open the launcher" on the Done page
        bool update = false;       // press Update as soon as the check finds one (tests)
        int shot_after = 0;       // with -screenshot: draw this many ms after the start (mid-copy)
        int cancel_after = 0;     // press Cancel on the copy page after this many ms
        bool hd = false;          // open the HD art window (WP5.14)
        std::string hd_download;  // download this HD pack (headless: wait for it)
        bool wait_store = false;  // with -screenshot: wait while the wizard waits for a store
        std::string gog_dump;     // -gog-dump <folder>: GOG sign-in now, its library answers saved there
    };

    bool Parse_Options_(int argc, char** argv, s_launcher_options* options, std::string* error) {
        const command_line::s_option_definition definitions[] = {
            {"game-dir", true},
            {"profile", true},
            {"screenshot", true},
            {"select", true},
            {"play", false},
            {"settings", true},
            {"saves", true},
            {"set", true},
            {"enable", true},
            {"disable", true},
            {"confirm", false},
            {"wait-game", false},
            {"setup", false},
            {"setup-folder", true},
            {"setup-run", true},
            {"setup-copy", true},
            {"shot-after", true},
            {"cancel-after", true},
            {"hd", false},
            {"hd-download", true},
            {"wait-store", false},
            {"gog-dump", true},
            {"setup-finish", false},
            {"update", false},
        };
        std::vector<command_line::s_option> parsed;
        if (!command_line::Parse_Options(argc, argv, command_line::OPTION_PREFIX, definitions, std::size(definitions), &parsed, error)) {
            return false;
        }
        for (const command_line::s_option& option : parsed) {
            switch (option.definition_index) {
                case 0: options->game_dir = option.value; break;
                case 1: options->profile = option.value; break;
                case 2: options->screenshot = option.value; break;
                case 3: options->select = option.value; break;
                case 4: options->play = true; break;
                case 5: options->settings = option.value; break;
                case 6: options->saves = SDL_atoi(option.value.c_str()); break;
                case 7: options->set = option.value; break;
                case 8: options->enable = option.value; break;
                case 9: options->disable = option.value; break;
                case 10: options->confirm = true; break;
                case 11: options->wait_game = true; break;
                case 12: options->setup = true; break;
                case 13: options->setup_folder = option.value; break;
                case 14: options->setup_run = option.value; break;
                case 15: options->setup_copy = option.value; break;
                case 16: options->shot_after = SDL_atoi(option.value.c_str()); break;
                case 17: options->cancel_after = SDL_atoi(option.value.c_str()); break;
                case 18: options->hd = true; break;
                case 19: options->hd_download = option.value; break;
                case 20: options->wait_store = true; break;
                case 21: options->gog_dump = option.value; break;
                case 22: options->setup_finish = true; break;
                case 23: options->update = true; break;
                default: break;
            }
        }
        return true;
    }

    // The setup wizard is laid out for a canvas about 860x580 and drawn scaled up to the window
    // (owner 2026-11-24 at 2560x1440, 100 %: "font size is super small"). It grows with the window.
    constexpr float SETUP_CANVAS_WIDTH = 860.0f;
    constexpr float SETUP_CANVAS_HEIGHT = 580.0f;

    // The launcher's own tabs are laid out for 1120x760 and grow the same way in a larger window.
    constexpr float LAUNCHER_CANVAS_WIDTH = 1120.0f;
    constexpr float LAUNCHER_CANVAS_HEIGHT = 760.0f;

    float Canvas_Scale_(SDL_Window* window, bool setup) {
        int width = 0, height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if (width <= 0 || height <= 0) return 1.0f;
        const float canvas_width = setup ? SETUP_CANVAS_WIDTH : LAUNCHER_CANVAS_WIDTH;
        const float canvas_height = setup ? SETUP_CANVAS_HEIGHT : LAUNCHER_CANVAS_HEIGHT;
        return std::clamp(std::min(width / canvas_width, height / canvas_height), 1.0f, 4.0f);
    }

    // Mouse positions from window coordinates to the wizard's canvas.
    void Scale_Mouse_Event_(SDL_Event* event, float scale) {
        switch (event->type) {
            case SDL_EVENT_MOUSE_MOTION:
                event->motion.x /= scale;
                event->motion.y /= scale;
                event->motion.xrel /= scale;
                event->motion.yrel /= scale;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                event->button.x /= scale;
                event->button.y /= scale;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                event->wheel.mouse_x /= scale;
                event->wheel.mouse_y /= scale;
                break;
            default: break;
        }
    }

    void Apply_Style_(float scale) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(14.0f, 12.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(8.0f, 7.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.FrameRounding = 4.0f;
        style.ChildRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.WindowBorderSize = 0.0f;
        // Look B, the Chrome theme (owner 2026-11-24): dark plates over the metal panel, gold and
        // amber accents, as the setup wizard.
        style.FrameRounding = 2.0f;
        style.ChildRounding = 0.0f;
        style.TabRounding = 2.0f;
        ImVec4* colours = style.Colors;
        const auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
        colours[ImGuiCol_Text] = rgb(226, 222, 210);
        colours[ImGuiCol_TextDisabled] = rgb(120, 122, 130);
        colours[ImGuiCol_WindowBg] = rgb(11, 12, 16, 0.0f); // the metal panel is drawn behind
        colours[ImGuiCol_ChildBg] = rgb(10, 11, 14, 0.0f);
        colours[ImGuiCol_PopupBg] = rgb(18, 19, 24, 0.97f);
        colours[ImGuiCol_Border] = rgb(84, 88, 98);
        colours[ImGuiCol_FrameBg] = rgb(30, 32, 38, 0.95f); // a step lighter than the dark rows
        colours[ImGuiCol_FrameBgHovered] = rgb(40, 36, 30, 0.95f);
        colours[ImGuiCol_FrameBgActive] = rgb(60, 48, 32, 0.95f);
        colours[ImGuiCol_Button] = rgb(52, 55, 62);
        colours[ImGuiCol_ButtonHovered] = rgb(78, 82, 92);
        colours[ImGuiCol_ButtonActive] = rgb(110, 76, 36);
        colours[ImGuiCol_Header] = rgb(255, 138, 28, 0.24f);
        colours[ImGuiCol_HeaderHovered] = rgb(255, 138, 28, 0.13f);
        colours[ImGuiCol_HeaderActive] = rgb(255, 138, 28, 0.32f);
        colours[ImGuiCol_CheckMark] = rgb(255, 138, 28);
        colours[ImGuiCol_CheckboxSelectedBg] = rgb(30, 32, 38, 0.95f);
        colours[ImGuiCol_SliderGrab] = rgb(255, 138, 28);
        colours[ImGuiCol_SliderGrabActive] = rgb(255, 170, 80);
        colours[ImGuiCol_Tab] = rgb(34, 37, 44, 0.95f);
        colours[ImGuiCol_TabHovered] = rgb(70, 56, 38);
        colours[ImGuiCol_TabSelected] = rgb(54, 44, 32);
        colours[ImGuiCol_TabSelectedOverline] = rgb(255, 138, 28);
        colours[ImGuiCol_TableRowBg] = rgb(10, 11, 14, 0.55f);
        colours[ImGuiCol_TableRowBgAlt] = rgb(22, 23, 28, 0.55f);
        colours[ImGuiCol_TableHeaderBg] = rgb(34, 37, 44);
        colours[ImGuiCol_TableBorderStrong] = rgb(59, 63, 72);
        colours[ImGuiCol_TableBorderLight] = rgb(40, 43, 50);
        colours[ImGuiCol_Separator] = rgb(59, 63, 72);
        colours[ImGuiCol_ScrollbarBg] = rgb(10, 11, 14, 0.5f);
        colours[ImGuiCol_ScrollbarGrab] = rgb(78, 82, 92);
        colours[ImGuiCol_ScrollbarGrabHovered] = rgb(110, 114, 124);
        colours[ImGuiCol_ScrollbarGrabActive] = rgb(255, 138, 28);
        colours[ImGuiCol_TextSelectedBg] = rgb(255, 138, 28, 0.35f);
        colours[ImGuiCol_NavCursor] = rgb(255, 138, 28, 0.6f);
        colours[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
        colours[ImGuiCol_TitleBg] = rgb(34, 37, 44);
        colours[ImGuiCol_TitleBgActive] = rgb(54, 44, 32);
        colours[ImGuiCol_TitleBgCollapsed] = rgb(34, 37, 44);
        colours[ImGuiCol_ResizeGrip] = rgb(255, 138, 28, 0.25f);
        colours[ImGuiCol_ResizeGripHovered] = rgb(255, 138, 28, 0.6f);
        colours[ImGuiCol_ResizeGripActive] = rgb(255, 138, 28);
        colours[ImGuiCol_PlotHistogram] = rgb(255, 138, 28);
        style.ScaleAllSizes(scale);
        style.FontScaleDpi = scale;
    }

    bool Save_Screenshot_(SDL_Renderer* renderer, const std::string& path) {
        SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
        if (surface == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: unable to read pixels: %s", SDL_GetError());
            return false;
        }
        const bool saved = SDL_SavePNG(surface, path.c_str());
        if (!saved) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: unable to save %s: %s", path.c_str(), SDL_GetError());
        }
        SDL_DestroySurface(surface);
        return saved;
    }
} // namespace

int main(int argc, char** argv) {
    s_launcher_options options;
    std::string error;
    if (!Parse_Options_(argc, argv, &options, &error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: %s", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Battle at Antares launcher", error.c_str(), nullptr);
        return 1;
    }
    if (options.game_dir.empty()) {
        const char* base = SDL_GetBasePath();
        options.game_dir = base != nullptr ? base : "./";
    }
    if (options.game_dir.back() != '/' && options.game_dir.back() != '\\') {
        options.game_dir += '/';
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    // The same portable-or-user-folder decision as the game's (WP5.9).
    user_paths::Init(options.game_dir.c_str(), user_paths::PLATFORM_USES_XDG);
    if (vfs::Init(options.game_dir.c_str()) == 0 || !antares::user_folder::Mount(options.game_dir)) {
        const std::string message = "Unable to read the game folder " + options.game_dir;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Battle at Antares launcher", message.c_str(), nullptr);
        SDL_Quit();
        return 1;
    }

    launcher::c_launcher app;
    app.Init(options.game_dir, options.profile, options.select);
    if (!options.settings.empty()) {
        app.Open_Settings(options.settings);
    }
    if (options.saves >= 0) {
        app.Open_Saves(options.saves);
    }
    if (!options.set.empty()) {
        const bool changed = app.Apply_Setting(options.set);
        SDL_Log("launcher: %s", app.Message().c_str());
        if (!changed && options.screenshot.empty()) {
            SDL_Quit();
            return 1;
        }
    }
    for (const auto& [id, on] : {std::pair{options.disable, false}, std::pair{options.enable, true}}) {
        if (id.empty()) continue;
        const bool changed = app.Set_Package_Enabled(id, on, options.confirm);
        SDL_Log("launcher: %s %s: %s", on ? "enable" : "disable", id.c_str(),
            app.Toggle_Pending() ? "asks to confirm (use -confirm)" : app.Message().c_str());
        if (!changed && !app.Toggle_Pending() && options.screenshot.empty()) {
            SDL_Quit();
            return 1;
        }
    }
    if (options.play) {
        // A shortcut: no window, the launcher only starts the game.
        const bool started = app.Play();
        app.Shutdown();
        config::Shutdown_Config_Loader();
        config::Shutdown_Runtime();
        vfs::Shutdown();
        SDL_Quit();
        return started ? 0 : 1;
    }

    if (!options.setup_run.empty()) {
        const bool done = app.Run_Setup(options.setup_run, options.setup); // -setup too: stop on the saves page
        SDL_Log("launcher: setup %s", done ? "done" : "failed");
        if (done && options.setup_finish) app.Finish_Setup(false);
        if (!done && options.screenshot.empty()) {
            SDL_Quit();
            return 1;
        }
    } else if (!options.setup_copy.empty()) {
        app.Open_Setup(options.setup_copy);
        if (!app.Start_Setup_Copy()) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: setup: the checklist doesn't allow a copy");
    } else if (!options.setup_folder.empty() || options.setup) {
        app.Open_Setup(options.setup_folder);
    }
    if (options.hd) app.Open_Hd_Window();
    if (options.update) app.Update_When_Available();
    if (!options.gog_dump.empty()) app.Start_Gog_Signin(options.gog_dump);
    if (!options.hd_download.empty() && !app.Download_Hd_Pack(options.hd_download) && options.screenshot.empty()) {
        SDL_Quit();
        return 1;
    }
    if (options.wait_game) {
        (void)app.Play(); // refused with a reason when the files or the profile won't do
    }
    const bool headless = !options.screenshot.empty();
    const Uint64 wait_deadline = SDL_GetTicks() + 180000;
    float scale = headless ? 1.0f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f) scale = 1.0f;
    const std::string title = std::string(antares::PRODUCT_NAME) + " launcher";
    // Look B (WP5.11b): a roomier window on a large screen (owner at 2560x1440), never past 85 % of it.
    float window_width = 1120.0f * scale, window_height = 760.0f * scale;
    SDL_Rect usable{};
    if (!headless && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0) {
        window_width = std::max(window_width, std::min(1280.0f * scale, usable.w * 0.85f));
        window_height = std::max(window_height, std::min(860.0f * scale, usable.h * 0.85f));
    }
    SDL_Window* window = SDL_CreateWindow(title.c_str(), static_cast<int>(window_width), static_cast<int>(window_height), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (headless ? SDL_WINDOW_HIDDEN : 0));
    if (window != nullptr) { // the Antaran's face in the title bar and task switcher (owner 2026-11-25)
        SDL_IOStream* icon_stream = SDL_IOFromConstMem(k_launcher_icon_data, k_launcher_icon_size);
        if (SDL_Surface* icon = icon_stream != nullptr ? SDL_LoadPNG_IO(icon_stream, true) : nullptr) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }
    SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, headless ? "software" : nullptr) : nullptr;
    if (renderer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: unable to create a window: %s", SDL_GetError());
        vfs::Shutdown();
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowMinimumSize(window, static_cast<int>(760 * scale), static_cast<int>(480 * scale));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Apply_Style_(scale);
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(k_launcher_font_data), k_launcher_font_size, 17.0f, &font_config);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    const char* base_path = SDL_GetBasePath();                                                            // the program's folder (an app bundle's Resources)
    if (launcher::chrome::Load(renderer, std::string(base_path != nullptr ? base_path : "") + "data/")) { // look B: the wizard's and the launcher's (WP5.11b)
        io.FontDefault = launcher::chrome::Body_Font();
        ImGui::GetStyle().FontSizeBase = 20.0f;   // Saira draws small for its size
        ImGui::GetStyle().FrameBorderSize = 1.0f; // fields and checkboxes stand out from the dark plates
    }

    int exit_code = 0;
    int frames = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        const float setup_scale = Canvas_Scale_(window, app.Setup_Open());
        while (SDL_PollEvent(&event)) {
            if (setup_scale != 1.0f) Scale_Mouse_Event_(&event, setup_scale);
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))) {
                running = false;
            }
        }
        if (!headless && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
            app.Update();
            SDL_Delay(50);
            continue;
        }
        app.Update();
        const Uint64 elapsed = SDL_GetTicks() - (wait_deadline - 180000);
        if (options.cancel_after > 0 && app.Setup_Copying() && elapsed >= static_cast<Uint64>(options.cancel_after)) {
            app.Cancel_Setup_Copy();
        }
        // Headless: wait for the copy (the page moves on by itself) unless asked for a mid-copy frame.
        if (headless && app.Setup_Copying() && SDL_GetTicks() < wait_deadline &&
            (options.shot_after == 0 || elapsed < static_cast<Uint64>(options.shot_after))) {
            SDL_Delay(50);
            continue;
        }
        // Headless: wait for the HD art list or a download before the screenshot (a download may
        // take a while: no deadline).
        if (headless && options.wait_store && app.Setup_Waiting() && SDL_GetTicks() < wait_deadline) {
            SDL_Delay(100);
            continue;
        }
        if (headless && app.Hd_Waiting() && (options.shot_after == 0 || elapsed < static_cast<Uint64>(options.shot_after))) {
            SDL_Delay(100);
            continue;
        }
        if (headless && app.Game_Running() && SDL_GetTicks() < wait_deadline) {
            SDL_Delay(100); // -wait-game: the screenshot shows the launcher after the game
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (setup_scale != 1.0f) { // the canvas, scaled up by the renderer, fonts rasterised sharp
            io.DisplaySize.x /= setup_scale;
            io.DisplaySize.y /= setup_scale;
            io.DisplayFramebufferScale.x *= setup_scale;
            io.DisplayFramebufferScale.y *= setup_scale;
        }
        ImGui::NewFrame();
        if (!app.Draw()) {
            running = false;
        }
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 19, 21, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (headless && ++frames == 3) {
            // ImGui settles its layout over the first frames; read the third before presenting.
            exit_code = Save_Screenshot_(renderer, options.screenshot) ? 0 : 1;
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    app.Shutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    launcher::chrome::Unload();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    config::Shutdown_Config_Loader();
    config::Shutdown_Runtime();
    vfs::Shutdown();
    SDL_Quit();
    return exit_code;
}
