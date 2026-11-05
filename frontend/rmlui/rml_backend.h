#pragma once

// WP3.0 path B: RmlUi's interfaces onto the game's SDL renderer and VFS.
// The render interface is adapted from RmlUi's Backends/RmlUi_Renderer_SDL.cpp (MIT licence,
// Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors; (c) 2019-2024
// The RmlUi Team, and contributors), with textures decoded by stb_image from the game's VFS
// instead of SDL_image.

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <SDL3/SDL.h>

#include <memory>

namespace antares::ui {

    class Render_Interface final : public Rml::RenderInterface {
    public:
        explicit Render_Interface(SDL_Renderer* renderer);

        void Begin_Frame();

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
        void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override;
        Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
        void ReleaseTexture(Rml::TextureHandle texture_handle) override;
        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;

    private:
        SDL_Renderer* renderer_;
        SDL_BlendMode blend_mode_ = {};
        SDL_Rect scissor_ = {};
        bool scissor_enabled_ = false;
        std::unique_ptr<SDL_Vertex[]> vertices_;
        size_t vertices_size_ = 0;
    };

    // Documents, styles, fonts and images come through the game's VFS, so packages override them
    // like any other data file.
    class File_Interface final : public Rml::FileInterface {
    public:
        Rml::FileHandle Open(const Rml::String& path) override;
        void Close(Rml::FileHandle file) override;
        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
        bool Seek(Rml::FileHandle file, long offset, int origin) override;
        size_t Tell(Rml::FileHandle file) override;
        size_t Length(Rml::FileHandle file) override;
    };

    class System_Interface final : public Rml::SystemInterface {
    public:
        double GetElapsedTime() override;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    };

} // namespace antares::ui
