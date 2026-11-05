#include "pch.h"

#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <string>
#include <vector>

#include "antares/identity.h"
#include "command_line.h"
#include "config/config_loader.h"
#include "config/config_runtime.h"
#include "game/vfs.h"
#include "launcher/launcher_app.h"
#include "launcher_font.h"

namespace {
    struct s_launcher_options {
        std::string game_dir;   // default: the folder holding the launcher
        std::string profile;    // profile to select at start
        std::string screenshot; // render one frame to this PNG and quit (headless review)
        std::string select;     // package whose details to show at start
        bool play = false;      // start the game with the profile and options, then quit
    };

    bool Parse_Options_(int argc, char** argv, s_launcher_options* options, std::string* error) {
        const command_line::s_option_definition definitions[] = {
            {"game-dir", true},
            {"profile", true},
            {"screenshot", true},
            {"select", true},
            {"play", false},
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
                default: break;
            }
        }
        return true;
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
        ImVec4* colours = style.Colors;
        colours[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.082f, 0.110f, 1.0f);
        colours[ImGuiCol_ChildBg] = ImVec4(0.095f, 0.104f, 0.138f, 1.0f);
        colours[ImGuiCol_PopupBg] = ImVec4(0.110f, 0.120f, 0.158f, 1.0f);
        colours[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
        colours[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.0f);
        colours[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.30f, 1.0f);
        colours[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.28f, 0.36f, 1.0f);
        colours[ImGuiCol_Button] = ImVec4(0.18f, 0.22f, 0.32f, 1.0f);
        colours[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.30f, 0.44f, 1.0f);
        colours[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.36f, 0.52f, 1.0f);
        colours[ImGuiCol_Header] = ImVec4(0.20f, 0.26f, 0.38f, 1.0f);
        colours[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.31f, 0.45f, 1.0f);
        colours[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.36f, 0.52f, 1.0f);
        colours[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
        colours[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
        colours[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
        colours[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
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
    if (vfs::Init(options.game_dir.c_str()) == 0) {
        const std::string message = "Unable to read the game folder " + options.game_dir;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Battle at Antares launcher", message.c_str(), nullptr);
        SDL_Quit();
        return 1;
    }

    launcher::c_launcher app;
    app.Init(options.game_dir, options.profile, options.select);
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

    const bool headless = !options.screenshot.empty();
    float scale = headless ? 1.0f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f) scale = 1.0f;
    const std::string title = std::string(antares::PRODUCT_NAME) + " launcher";
    SDL_Window* window = SDL_CreateWindow(title.c_str(), static_cast<int>(1040 * scale), static_cast<int>(680 * scale), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (headless ? SDL_WINDOW_HIDDEN : 0));
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

    int exit_code = 0;
    int frames = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
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
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
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
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    config::Shutdown_Config_Loader();
    config::Shutdown_Runtime();
    vfs::Shutdown();
    SDL_Quit();
    return exit_code;
}
