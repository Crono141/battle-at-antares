// WP3.0 trial path B: C++ with RmlUi inside the SDL platform loop. The legacy game is presented
// as before; Ctrl+G covers it with a native galaxy scene view (our own camera, layers and picking,
// drawn with SDL_Renderer) and RmlUi documents for the label layer, the panel and the top bar
// (ui/trial/galaxy.rml through the game's VFS; F5 reloads them). Platform thread only.

#include "pch.h"

#include "antares/host.h"
#include "antares/script_capture.h"
#include "antares/ui_hooks.h"
#include "rml_backend.h"

#include "launcher_font.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace antares::ui {
    namespace {
        constexpr float k_zoom_min = 0.5f;
        constexpr float k_zoom_max = 16.0f;
        constexpr float k_label_show = 2.2f; // label layer fades in between these zooms
        constexpr float k_label_full = 3.0f;
        constexpr float k_pick_radius = 14.0f;
        constexpr const char* k_document = "ui/trial/galaxy.rml";

        const SDL_FColor k_spectral[7] = {{0.62f, 0.74f, 1.0f, 1}, {0.95f, 0.96f, 1.0f, 1}, {1.0f, 0.93f, 0.55f, 1},
            {1.0f, 0.68f, 0.32f, 1}, {1.0f, 0.38f, 0.28f, 1}, {0.62f, 0.42f, 0.30f, 1}, {0.45f, 0.20f, 0.70f, 1}};
        const SDL_FColor k_banner[8] = {{0.95f, 0.20f, 0.20f, 1}, {1.0f, 0.90f, 0.20f, 1}, {0.25f, 0.85f, 0.30f, 1},
            {0.78f, 0.78f, 0.82f, 1}, {0.30f, 0.50f, 1.0f, 1}, {0.62f, 0.42f, 0.25f, 1}, {0.70f, 0.30f, 0.90f, 1},
            {1.0f, 0.55f, 0.15f, 1}};
        const char* k_spectral_names[7] = {"Blue (B)", "White (F)", "Yellow (G)", "Orange (K)", "Red (M)", "Brown dwarf", "Black hole"};

        bool g_initialized = false;
        bool g_failed = false;
        bool g_galaxy = false;
        SDL_Renderer* g_renderer = nullptr;
        SDL_Window* g_window = nullptr;
        std::unique_ptr<Render_Interface> g_render;
        File_Interface g_files;
        System_Interface g_system;
        Rml::Context* g_context = nullptr;
        Rml::ElementDocument* g_document = nullptr;
        std::vector<Rml::Element*> g_labels;
        SDL_Texture* g_glow = nullptr;
        SDL_Texture* g_disc = nullptr;

        // Camera: world (galaxy map units) to screen pixels.
        float g_cam_x = 0;
        float g_cam_y = 0;
        float g_zoom = 1;
        float g_target_zoom = 1;
        float g_anchor_wx = 0;
        float g_anchor_wy = 0;
        float g_anchor_sx = 0;
        float g_anchor_sy = 0;
        bool g_fitted = false;
        bool g_dragging = false;
        bool g_drag_moved = false;
        float g_press_x = 0;
        float g_press_y = 0;
        float g_mouse_x = 0;
        float g_mouse_y = 0;
        int g_out_w = 1;
        int g_out_h = 1;
        float g_pixel_scale = 1; // output pixels per window coordinate

        std::vector<host::s_star> g_stars;
        uint64_t g_star_generation = UINT64_MAX;
        int g_selected = -1;

        uint64_t g_last_ns = 0;
        double g_frame_ms_total = 0;
        uint64_t g_frames = 0;
        uint64_t g_fps_frames = 0;
        uint64_t g_fps_start = 0;
        int g_fps = 0;

        float Screen_X(float wx) { return (wx - g_cam_x) * g_zoom + (float)g_out_w * 0.5f; }
        float Screen_Y(float wy) { return (wy - g_cam_y) * g_zoom + (float)g_out_h * 0.5f; }
        float World_X(float sx) { return g_cam_x + (sx - (float)g_out_w * 0.5f) / g_zoom; }
        float World_Y(float sy) { return g_cam_y + (sy - (float)g_out_h * 0.5f) / g_zoom; }

        SDL_Texture* Make_Radial(int size, float hardness) {
            std::vector<uint32_t> pixels((size_t)size * (size_t)size);
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    const float dx = ((float)x + 0.5f) / (float)size * 2.0f - 1.0f;
                    const float dy = ((float)y + 0.5f) / (float)size * 2.0f - 1.0f;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    float a;
                    if (hardness > 0) {
                        a = std::clamp((1.0f - d) * hardness, 0.0f, 1.0f); // anti-aliased disc
                    } else {
                        a = std::clamp(1.0f - d, 0.0f, 1.0f);
                        a = a * a; // soft glow
                    }
                    const uint32_t v = (uint32_t)(a * 255.0f + 0.5f);
                    pixels[(size_t)y * (size_t)size + (size_t)x] = (v << 24) | 0xFFFFFFu;
                }
            }
            SDL_Texture* texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, size, size);
            if (texture != nullptr) {
                SDL_UpdateTexture(texture, nullptr, pixels.data(), size * 4);
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            }
            return texture;
        }

        Rml::Element* Element(const char* id) {
            return g_document != nullptr ? g_document->GetElementById(id) : nullptr;
        }

        void Close_Panel() {
            g_selected = -1;
            if (Rml::Element* panel = Element("panel")) {
                panel->SetClass("open", false);
            }
        }

        void Open_Panel(int index) {
            g_selected = index;
            const host::s_star& star = g_stars[(size_t)index];
            Rml::Element* panel = Element("panel");
            if (panel == nullptr) {
                return;
            }
            Element("star-name")->SetInnerRML(Rml::StringUtilities::EncodeRml(star.name));
            Rml::Element* owner = Element("star-owner");
            if (star.owner >= 0) {
                owner->SetInnerRML("Owner: " + Rml::StringUtilities::EncodeRml(star.owner_name));
                const SDL_FColor c = k_banner[star.owner_color % 8];
                owner->SetProperty("color", Rml::CreateString("rgb(%d,%d,%d)", (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255)));
            } else {
                owner->SetInnerRML("Unclaimed");
                owner->RemoveProperty("color");
            }
            Element("star-class")->SetInnerRML(std::string("Star class: ") + k_spectral_names[std::clamp<int>(star.spectral_class, 0, 6)]);
            panel->SetClass("open", true);
        }

        struct Click_Listener final : Rml::EventListener {
            void ProcessEvent(Rml::Event& event) override {
                const Rml::String& id = event.GetCurrentElement()->GetId();
                if (id == "close") {
                    Close_Panel();
                } else if (id == "mode") {
                    g_galaxy = false;
                }
            }
        };
        Click_Listener g_click_listener;

        void Build_Labels() {
            g_labels.clear();
            Rml::Element* layer = Element("labels");
            if (layer == nullptr) {
                return;
            }
            while (layer->GetNumChildren() > 0) {
                layer->RemoveChild(layer->GetFirstChild());
            }
            for (const host::s_star& star : g_stars) {
                Rml::ElementPtr label = g_document->CreateElement("div");
                label->SetClass("label", true);
                label->SetInnerRML(Rml::StringUtilities::EncodeRml(star.name));
                if (star.owner >= 0) {
                    const SDL_FColor c = k_banner[star.owner_color % 8];
                    label->SetProperty("color", Rml::CreateString("rgb(%d,%d,%d)", (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255)));
                }
                g_labels.push_back(layer->AppendChild(std::move(label)));
            }
        }

        bool Load_Document() {
            if (g_document != nullptr) {
                g_document->Close();
                g_context->Update();
                g_document = nullptr;
            }
            Rml::Factory::ClearStyleSheetCache();
            g_document = g_context->LoadDocument(k_document);
            if (g_document == nullptr) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ui: unable to load %s", k_document);
                return false;
            }
            for (const char* id : {"close", "mode"}) {
                if (Rml::Element* element = Element(id)) {
                    element->AddEventListener(Rml::EventId::Click, &g_click_listener);
                }
            }
            g_document->Show();
            Build_Labels();
            if (g_selected >= 0 && g_selected < (int)g_stars.size()) {
                Open_Panel(g_selected);
            }
            return true;
        }

        bool Initialize(SDL_Renderer* renderer) {
            g_renderer = renderer;
            g_render = std::make_unique<Render_Interface>(renderer);
            Rml::SetRenderInterface(g_render.get());
            Rml::SetFileInterface(&g_files);
            Rml::SetSystemInterface(&g_system);
            if (!Rml::Initialise()) {
                return false;
            }
            if (!Rml::LoadFontFace({k_launcher_font_data, (size_t)k_launcher_font_size}, "Roboto", Rml::Style::FontStyle::Normal,
                    Rml::Style::FontWeight::Auto, true)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ui: font failed to load");
            }
            SDL_GetCurrentRenderOutputSize(renderer, &g_out_w, &g_out_h);
            g_context = Rml::CreateContext("antares", {g_out_w, g_out_h});
            if (g_context == nullptr) {
                return false;
            }
            g_glow = Make_Radial(128, 0);
            g_disc = Make_Radial(64, 24.0f);
            SDL_SetTextureBlendMode(g_glow, SDL_BLENDMODE_ADD);
            SDL_SetTextureBlendMode(g_disc, SDL_BLENDMODE_BLEND);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ui: RmlUi %s initialised (trial path B)", Rml::GetVersion().c_str());
            return Load_Document();
        }

        void Fit() {
            if (g_stars.empty()) {
                return;
            }
            float lo_x = 1e9f, lo_y = 1e9f, hi_x = -1e9f, hi_y = -1e9f;
            for (const host::s_star& s : g_stars) {
                lo_x = std::min(lo_x, (float)s.x);
                lo_y = std::min(lo_y, (float)s.y);
                hi_x = std::max(hi_x, (float)s.x);
                hi_y = std::max(hi_y, (float)s.y);
            }
            const float w = hi_x - lo_x + 40.0f;
            const float h = hi_y - lo_y + 40.0f;
            g_target_zoom = g_zoom = std::clamp(std::min((float)g_out_w / w, (float)g_out_h / h) * 0.92f, k_zoom_min, k_zoom_max);
            g_cam_x = (lo_x + hi_x) * 0.5f;
            g_cam_y = (lo_y + hi_y) * 0.5f;
            g_anchor_wx = g_cam_x;
            g_anchor_wy = g_cam_y;
            g_anchor_sx = (float)g_out_w * 0.5f;
            g_anchor_sy = (float)g_out_h * 0.5f;
            g_fitted = true;
        }

        void Pick(float sx, float sy) {
            int best = -1;
            float best_d = k_pick_radius * g_pixel_scale;
            for (size_t i = 0; i < g_stars.size(); ++i) {
                const float dx = Screen_X(g_stars[i].x) - sx;
                const float dy = Screen_Y(g_stars[i].y) - sy;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < best_d) {
                    best_d = d;
                    best = (int)i;
                }
            }
            if (best >= 0) {
                Open_Panel(best);
            } else {
                Close_Panel();
            }
        }

        // A ring of `width` pixels around (x, y).
        void Ring(float x, float y, float radius, float width, SDL_FColor color) {
            constexpr int k_segments = 48;
            SDL_Vertex vertices[(k_segments + 1) * 2];
            int indices[k_segments * 6];
            for (int i = 0; i <= k_segments; ++i) {
                const float a = (float)i / (float)k_segments * 6.2831853f;
                const float c = std::cos(a);
                const float s = std::sin(a);
                vertices[i * 2] = {{x + c * (radius - width * 0.5f), y + s * (radius - width * 0.5f)}, color, {0, 0}};
                vertices[i * 2 + 1] = {{x + c * (radius + width * 0.5f), y + s * (radius + width * 0.5f)}, color, {0, 0}};
            }
            for (int i = 0; i < k_segments; ++i) {
                const int b = i * 2;
                const int q[6] = {b, b + 1, b + 2, b + 1, b + 3, b + 2};
                std::copy(q, q + 6, indices + i * 6);
            }
            SDL_RenderGeometry(g_renderer, nullptr, vertices, (k_segments + 1) * 2, indices, k_segments * 6);
        }

        void Draw_Stars() {
            SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(g_renderer, 4, 4, 9, 255);
            SDL_RenderClear(g_renderer);
            const float min_px = g_pixel_scale;
            for (size_t i = 0; i < g_stars.size(); ++i) {
                const host::s_star& s = g_stars[i];
                const float x = Screen_X(s.x);
                const float y = Screen_Y(s.y);
                // Level of detail: a minimum size on screen when zoomed out.
                const float r = std::max((1.6f + 0.5f * (float)s.size) * g_zoom, (2.0f + 0.4f * (float)s.size) * min_px);
                if (x < -r * 6 || y < -r * 6 || x > (float)g_out_w + r * 6 || y > (float)g_out_h + r * 6) {
                    continue;
                }
                const SDL_FColor c = k_spectral[std::clamp<int>(s.spectral_class, 0, 6)];
                if (s.spectral_class == 6) {
                    Ring(x, y, r * 1.4f, 1.5f * min_px, c);
                    continue;
                }
                const float glow = r * 5.0f;
                SDL_SetTextureColorModFloat(g_glow, c.r, c.g, c.b);
                SDL_SetTextureAlphaModFloat(g_glow, 0.55f);
                const SDL_FRect glow_rect = {x - glow, y - glow, glow * 2, glow * 2};
                SDL_RenderTexture(g_renderer, g_glow, nullptr, &glow_rect);
                const float core = r * 0.8f;
                SDL_SetTextureColorModFloat(g_disc, (c.r + 1) * 0.5f, (c.g + 1) * 0.5f, (c.b + 1) * 0.5f);
                const SDL_FRect core_rect = {x - core, y - core, core * 2, core * 2};
                SDL_RenderTexture(g_renderer, g_disc, nullptr, &core_rect);
                if (s.owner >= 0) {
                    Ring(x, y, r * 2.4f, 1.5f * min_px, k_banner[s.owner_color % 8]);
                }
                if ((int)i == g_selected) {
                    Ring(x, y, r * 3.4f, 1.5f * min_px, {1, 1, 1, 0.9f});
                }
            }
        }

        void Place_Labels() {
            Rml::Element* layer = Element("labels");
            if (layer == nullptr) {
                return;
            }
            const float alpha = std::clamp((g_zoom / g_pixel_scale - k_label_show) / (k_label_full - k_label_show), 0.0f, 1.0f);
            layer->SetProperty("opacity", Rml::ToString(alpha));
            layer->SetProperty("display", alpha > 0 ? "block" : "none");
            if (alpha <= 0) {
                return;
            }
            const float dp = g_pixel_scale;
            for (size_t i = 0; i < g_labels.size() && i < g_stars.size(); ++i) {
                const host::s_star& s = g_stars[i];
                const float r = std::max((1.6f + 0.5f * (float)s.size) * g_zoom, (2.0f + 0.4f * (float)s.size) * dp);
                // In dp units: RmlUi lays out at the density ratio.
                const float x = Screen_X(s.x) / dp;
                const float y = (Screen_Y(s.y) + r * 2.4f) / dp + 4.0f;
                g_labels[i]->SetProperty("left", Rml::ToString(x - 80.0f) + "dp");
                g_labels[i]->SetProperty("top", Rml::ToString(y) + "dp");
                g_labels[i]->SetProperty("display", s.spectral_class == 6 || s.name.empty() ? "none" : "block");
            }
        }

        void Draw_Pointer() {
            const float x = g_mouse_x * g_pixel_scale;
            const float y = g_mouse_y * g_pixel_scale;
            const float s = 7.0f * g_pixel_scale;
            SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(g_renderer, 230, 235, 245, 230);
            SDL_RenderLine(g_renderer, x - s, y, x - 2, y);
            SDL_RenderLine(g_renderer, x + 2, y, x + s, y);
            SDL_RenderLine(g_renderer, x, y - s, x, y - 2);
            SDL_RenderLine(g_renderer, x, y + 2, x, y + s);
        }

        // ---- Headless check: ANTARES_UI_SMOKE=<folder> ----
        struct s_smoke {
            bool on = false;
            std::string dir;
            int step = 0;
            uint64_t next_ns = 0;
            std::string pending_shot;
        } g_smoke;

        void Push_Click(float wx, float wy) {
            for (int down = 1; down >= 0; --down) {
                SDL_Event e = {};
                e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.windowID = SDL_GetWindowID(g_window);
                e.button.button = SDL_BUTTON_LEFT;
                e.button.down = down != 0;
                e.button.x = wx;
                e.button.y = wy;
                SDL_PushEvent(&e);
            }
        }

        void Push_Key(SDL_Keycode key, SDL_Keymod mod) {
            SDL_Event e = {};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.windowID = SDL_GetWindowID(g_window);
            e.key.key = key;
            e.key.mod = mod;
            e.key.down = true;
            SDL_PushEvent(&e);
        }

        void Smoke_Step() {
            if (!g_smoke.on || g_window == nullptr) {
                return;
            }
            const uint64_t now = SDL_GetTicksNS();
            if (now < g_smoke.next_ns) {
                return;
            }
            const uint64_t second = 1000000000ull;
            switch (g_smoke.step) {
                case 0:
                    if (host::Galaxy().stars.empty()) {
                        return;
                    }
                    g_smoke.next_ns = now + 3 * second;
                    break;
                case 1: // enter the galaxy scene with the game's own key path
                    Push_Key(SDLK_G, SDL_KMOD_LCTRL);
                    g_smoke.next_ns = now + 1 * second;
                    break;
                case 2:
                    g_smoke.pending_shot = "galaxy-overview";
                    g_smoke.next_ns = now + second / 2;
                    break;
                case 3: { // zoom in with real wheel events at the screen centre
                    for (int i = 0; i < 6; ++i) {
                        SDL_Event e = {};
                        e.type = SDL_EVENT_MOUSE_WHEEL;
                        e.wheel.windowID = SDL_GetWindowID(g_window);
                        e.wheel.y = 1;
                        e.wheel.mouse_x = (float)g_out_w * 0.5f / g_pixel_scale;
                        e.wheel.mouse_y = (float)g_out_h * 0.5f / g_pixel_scale;
                        SDL_PushEvent(&e);
                    }
                    g_smoke.next_ns = now + 2 * second;
                    break;
                }
                case 4:
                    g_smoke.pending_shot = "galaxy-labels";
                    g_smoke.next_ns = now + second / 2;
                    break;
                case 5: { // click the first owned star on screen
                    for (const host::s_star& s : g_stars) {
                        const float x = Screen_X(s.x);
                        const float y = Screen_Y(s.y);
                        if (s.owner >= 0 && x > 50 && y > 50 && x < (float)g_out_w - 50 && y < (float)g_out_h - 50) {
                            Push_Click(x / g_pixel_scale, y / g_pixel_scale);
                            break;
                        }
                    }
                    g_smoke.next_ns = now + second;
                    break;
                }
                case 6:
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ui smoke: selected %d (%s), panel %s", g_selected,
                        g_selected >= 0 ? g_stars[(size_t)g_selected].name.c_str() : "-",
                        Element("panel") != nullptr && Element("panel")->IsClassSet("open") ? "open" : "closed");
                    g_smoke.pending_shot = "galaxy-panel";
                    g_smoke.next_ns = now + second / 2;
                    break;
                case 7: { // click Close in the RmlUi panel
                    if (Rml::Element* close = Element("close")) {
                        const Rml::Vector2f p = close->GetAbsoluteOffset(Rml::BoxArea::Border) + close->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
                        Push_Click(p.x / g_pixel_scale, p.y / g_pixel_scale);
                    }
                    g_smoke.next_ns = now + second;
                    break;
                }
                case 8:
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ui smoke: after Close, panel %s; galaxy frames %llu, %.2f ms average",
                        Element("panel") != nullptr && Element("panel")->IsClassSet("open") ? "open" : "closed",
                        (unsigned long long)g_frames, g_frames > 0 ? g_frame_ms_total / (double)g_frames : 0.0);
                    Push_Key(SDLK_G, SDL_KMOD_LCTRL);
                    g_smoke.next_ns = now + second;
                    break;
                case 9:
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ui smoke: done (galaxy %s)", g_galaxy ? "on" : "off");
                    g_smoke.on = false;
                    return;
                default: return;
            }
            ++g_smoke.step;
        }

        void Save_Shot(const std::string& name) {
            SDL_Surface* surface = SDL_RenderReadPixels(g_renderer, nullptr);
            if (surface == nullptr) {
                return;
            }
            SDL_Surface* argb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
            SDL_DestroySurface(surface);
            if (argb == nullptr) {
                return;
            }
            const std::string path = g_smoke.dir + "/" + name + ".png";
            const bool saved = antares::script_capture::Save_Argb_Png(path.c_str(), (const uint32_t*)argb->pixels, argb->w, argb->h, argb->pitch / 4);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ui smoke: saved %s%s", path.c_str(), saved ? "" : " FAILED");
            SDL_DestroySurface(argb);
        }
    } // namespace

    bool Handle_Event(SDL_Event& event, SDL_Window* window) {
        g_window = window;
        static bool started = false;
        if (!started) {
            started = true;
            host::Enable_Snapshots();
            if (const char* dir = SDL_getenv("ANTARES_UI_SMOKE")) {
                g_smoke.on = true;
                g_smoke.dir = dir;
            }
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_G && (event.key.mod & SDL_KMOD_CTRL) != 0 &&
            !event.key.repeat) {
            g_galaxy = !g_galaxy;
            g_dragging = false;
            return true;
        }
        if (!g_galaxy || !g_initialized) {
            return false;
        }
        // Window, quit and device events stay with the platform.
        if (event.type == SDL_EVENT_QUIT || (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) ||
            event.type == SDL_EVENT_RENDER_TARGETS_RESET || event.type == SDL_EVENT_RENDER_DEVICE_RESET) {
            return false;
        }
        const int mods = 0;
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_F5) {
                    Load_Document();
                } else if (event.key.key == SDLK_ESCAPE) {
                    Close_Panel();
                }
                return true;
            case SDL_EVENT_MOUSE_MOTION: {
                const float dx = event.motion.x - g_mouse_x;
                const float dy = event.motion.y - g_mouse_y;
                g_mouse_x = event.motion.x;
                g_mouse_y = event.motion.y;
                g_context->ProcessMouseMove((int)(g_mouse_x * g_pixel_scale), (int)(g_mouse_y * g_pixel_scale), mods);
                if (g_dragging) {
                    if (std::hypot(g_mouse_x - g_press_x, g_mouse_y - g_press_y) > 4.0f) {
                        g_drag_moved = true;
                    }
                    if (g_drag_moved) {
                        g_cam_x -= dx * g_pixel_scale / g_zoom;
                        g_cam_y -= dy * g_pixel_scale / g_zoom;
                        g_anchor_wx = g_cam_x;
                        g_anchor_wy = g_cam_y;
                        g_anchor_sx = (float)g_out_w * 0.5f;
                        g_anchor_sy = (float)g_out_h * 0.5f;
                    }
                }
                return true;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                g_mouse_x = event.button.x;
                g_mouse_y = event.button.y;
                g_context->ProcessMouseMove((int)(g_mouse_x * g_pixel_scale), (int)(g_mouse_y * g_pixel_scale), mods);
                const int button = event.button.button == SDL_BUTTON_LEFT ? 0 : event.button.button == SDL_BUTTON_RIGHT ? 1 : 2;
                // The UI documents first; the scene gets what they don't take.
                const bool ui_free = down ? g_context->ProcessMouseButtonDown(button, mods) : g_context->ProcessMouseButtonUp(button, mods);
                if (button != 0) {
                    return true;
                }
                if (down && ui_free) {
                    g_dragging = true;
                    g_drag_moved = false;
                    g_press_x = g_mouse_x;
                    g_press_y = g_mouse_y;
                } else if (!down && g_dragging) {
                    g_dragging = false;
                    if (!g_drag_moved) {
                        Pick(g_mouse_x * g_pixel_scale, g_mouse_y * g_pixel_scale);
                    }
                }
                return true;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                const float x = event.wheel.mouse_x * g_pixel_scale;
                const float y = event.wheel.mouse_y * g_pixel_scale;
                if (!g_context->ProcessMouseWheel(-event.wheel.y, mods)) {
                    return true;
                }
                g_anchor_sx = x;
                g_anchor_sy = y;
                g_anchor_wx = World_X(x);
                g_anchor_wy = World_Y(y);
                const float factor = event.wheel.y > 0 ? 1.2f : 1.0f / 1.2f;
                g_target_zoom = std::clamp(g_target_zoom * std::pow(factor, std::fabs(event.wheel.y)), k_zoom_min * g_pixel_scale,
                    k_zoom_max * g_pixel_scale);
                return true;
            }
            default:
                return true;
        }
    }

    bool Render(SDL_Renderer* renderer) {
        if (g_failed) {
            return false;
        }
        Smoke_Step();
        if (!g_galaxy) {
            g_last_ns = 0;
            return false;
        }
        if (!g_initialized) {
            g_initialized = true;
            if (!Initialize(renderer)) {
                g_failed = true;
                g_galaxy = false;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ui: RmlUi failed to start; the galaxy scene is off");
                return false;
            }
        }
        const uint64_t start = SDL_GetTicksNS();

        // Draw at the window's output resolution, not the game's 640x480 logical presentation.
        int logical_w = 0;
        int logical_h = 0;
        SDL_RendererLogicalPresentation logical_mode = SDL_LOGICAL_PRESENTATION_DISABLED;
        SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &logical_mode);
        SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_GetCurrentRenderOutputSize(renderer, &g_out_w, &g_out_h);
        int window_w = g_out_w;
        int window_h = g_out_h;
        if (g_window != nullptr) {
            SDL_GetWindowSize(g_window, &window_w, &window_h);
        }
        g_pixel_scale = window_w > 0 ? (float)g_out_w / (float)window_w : 1.0f;
        g_context->SetDimensions({g_out_w, g_out_h});
        g_context->SetDensityIndependentPixelRatio(g_pixel_scale);

        const host::s_galaxy galaxy = host::Galaxy();
        if (galaxy.generation != g_star_generation) {
            g_star_generation = galaxy.generation;
            g_stars = galaxy.stars;
            if (g_selected >= (int)g_stars.size()) {
                Close_Panel();
            }
            Build_Labels();
        }
        if (!g_fitted) {
            Fit();
        }

        // Eased zoom towards the target, keeping the anchor point still.
        const float dt = g_last_ns == 0 ? 0.0f : (float)(start - g_last_ns) / 1e9f;
        g_last_ns = start;
        if (std::fabs(g_zoom - g_target_zoom) > 0.0005f) {
            g_zoom += (g_target_zoom - g_zoom) * (1.0f - std::exp(-dt * 14.0f));
            if (std::fabs(g_zoom - g_target_zoom) < 0.0005f) {
                g_zoom = g_target_zoom;
            }
            g_cam_x = g_anchor_wx - (g_anchor_sx - (float)g_out_w * 0.5f) / g_zoom;
            g_cam_y = g_anchor_wy - (g_anchor_sy - (float)g_out_h * 0.5f) / g_zoom;
        }

        Draw_Stars();
        Place_Labels();
        if (Rml::Element* stats = Element("stats")) {
            stats->SetInnerRML(Rml::CreateString("presents/s %d &nbsp; scene %.2f ms &nbsp; stars %d &nbsp; F5 reloads the UI files", g_fps,
                g_frames > 0 ? g_frame_ms_total / (double)g_frames : 0.0, (int)g_stars.size()));
        }
        g_context->Update();
        g_render->Begin_Frame();
        g_context->Render();
        Draw_Pointer();

        if (!g_smoke.pending_shot.empty()) {
            Save_Shot(g_smoke.pending_shot);
            g_smoke.pending_shot.clear();
        }

        SDL_SetRenderClipRect(renderer, nullptr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, logical_mode);

        const uint64_t end = SDL_GetTicksNS();
        g_frame_ms_total += (double)(end - start) / 1e6;
        ++g_frames;
        ++g_fps_frames;
        if (g_fps_start == 0) {
            g_fps_start = end;
        }
        if (end - g_fps_start >= 1000000000ull) {
            g_fps = (int)g_fps_frames;
            g_fps_frames = 0;
            g_fps_start = end;
        }
        // Ask for another present only while the zoom eases; input and new game frames present
        // anyway. (Asking every time spins the loop at about 1,000 fps without vsync.)
        return std::fabs(g_zoom - g_target_zoom) > 0.0005f;
    }

} // namespace antares::ui
