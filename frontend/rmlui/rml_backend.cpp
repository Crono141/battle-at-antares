#include "rml_backend.h"

#include "pch.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Log.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#include "stb_image.h"

#include <vector>

namespace antares::ui {

    Render_Interface::Render_Interface(SDL_Renderer* renderer) : renderer_(renderer) {
        // RmlUi gives premultiplied alpha.
        blend_mode_ = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    }

    void Render_Interface::Begin_Frame() {
        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_SetRenderDrawBlendMode(renderer_, blend_mode_);
    }

    namespace {
        struct s_geometry {
            Rml::Span<const Rml::Vertex> vertices;
            Rml::Span<const int> indices;
        };
    } // namespace

    Rml::CompiledGeometryHandle Render_Interface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
        return reinterpret_cast<Rml::CompiledGeometryHandle>(new s_geometry{vertices, indices});
    }

    void Render_Interface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
        delete reinterpret_cast<s_geometry*>(geometry);
    }

    void Render_Interface::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) {
        const s_geometry* geometry = reinterpret_cast<s_geometry*>(handle);
        const size_t count = geometry->vertices.size();
        if (vertices_size_ < count) {
            vertices_size_ = count + count / 2;
            vertices_.reset(new SDL_Vertex[vertices_size_]);
        }
        for (size_t i = 0; i < count; ++i) {
            const Rml::Vertex& v = geometry->vertices[i];
            SDL_Vertex& out = vertices_[i];
            out.position = {v.position.x + translation.x, v.position.y + translation.y};
            out.tex_coord = {v.tex_coord.x, v.tex_coord.y};
            out.color = {v.colour.red / 255.f, v.colour.green / 255.f, v.colour.blue / 255.f, v.colour.alpha / 255.f};
        }
        SDL_RenderGeometry(renderer_, (SDL_Texture*)texture, vertices_.get(), (int)count, geometry->indices.data(),
            (int)geometry->indices.size());
    }

    Rml::TextureHandle Render_Interface::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
        s_vfs_file* file = vfs::fopen(source.c_str(), "rb");
        if (file == nullptr) {
            return {};
        }
        std::vector<unsigned char> bytes((size_t)std::max(0, vfs::fsize(file)));
        vfs::fread(bytes.data(), 1, bytes.size(), file);
        vfs::fclose(file);
        int w = 0;
        int h = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &channels, 4);
        if (pixels == nullptr) {
            return {};
        }
        for (size_t i = 0; i < (size_t)w * (size_t)h * 4; i += 4) {
            const int alpha = pixels[i + 3];
            for (size_t j = 0; j < 3; ++j) {
                pixels[i + j] = (unsigned char)(pixels[i + j] * alpha / 255);
            }
        }
        texture_dimensions = {w, h};
        const Rml::TextureHandle texture = GenerateTexture({pixels, (size_t)w * (size_t)h * 4}, texture_dimensions);
        stbi_image_free(pixels);
        return texture;
    }

    Rml::TextureHandle Render_Interface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i size) {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(size.x, size.y, SDL_PIXELFORMAT_RGBA32, (void*)source.data(), size.x * 4);
        if (surface == nullptr) {
            return {};
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_DestroySurface(surface);
        if (texture != nullptr) {
            SDL_SetTextureBlendMode(texture, blend_mode_);
        }
        return (Rml::TextureHandle)texture;
    }

    void Render_Interface::ReleaseTexture(Rml::TextureHandle texture) {
        SDL_DestroyTexture((SDL_Texture*)texture);
    }

    void Render_Interface::EnableScissorRegion(bool enable) {
        SDL_SetRenderClipRect(renderer_, enable ? &scissor_ : nullptr);
        scissor_enabled_ = enable;
    }

    void Render_Interface::SetScissorRegion(Rml::Rectanglei region) {
        scissor_ = {region.Left(), region.Top(), region.Width(), region.Height()};
        if (scissor_enabled_) {
            SDL_SetRenderClipRect(renderer_, &scissor_);
        }
    }

    namespace {
        // A file from the VFS, or (for a trial build run from another folder) from beside the
        // executable.
        struct s_file {
            s_vfs_file* vfs = nullptr;
            SDL_IOStream* io = nullptr;
        };
    } // namespace

    Rml::FileHandle File_Interface::Open(const Rml::String& path) {
        s_file file;
        file.vfs = vfs::fopen(path.c_str(), "rb");
        if (file.vfs == nullptr) {
            const char* base = SDL_GetBasePath();
            if (base != nullptr) {
                file.io = SDL_IOFromFile((std::string(base) + path).c_str(), "rb");
            }
        }
        if (file.vfs == nullptr && file.io == nullptr) {
            return 0;
        }
        return (Rml::FileHandle) new s_file(file);
    }

    void File_Interface::Close(Rml::FileHandle handle) {
        s_file* file = (s_file*)handle;
        if (file->vfs != nullptr) {
            vfs::fclose(file->vfs);
        } else {
            SDL_CloseIO(file->io);
        }
        delete file;
    }

    size_t File_Interface::Read(void* buffer, size_t size, Rml::FileHandle handle) {
        s_file* file = (s_file*)handle;
        return file->vfs != nullptr ? vfs::fread(buffer, 1, size, file->vfs) : SDL_ReadIO(file->io, buffer, size);
    }

    bool File_Interface::Seek(Rml::FileHandle handle, long offset, int origin) {
        s_file* file = (s_file*)handle;
        if (file->vfs != nullptr) {
            return vfs::fseek(file->vfs, (int32_t)offset, origin) == 0;
        }
        const SDL_IOWhence whence = origin == SEEK_SET ? SDL_IO_SEEK_SET : origin == SEEK_CUR ? SDL_IO_SEEK_CUR : SDL_IO_SEEK_END;
        return SDL_SeekIO(file->io, offset, whence) >= 0;
    }

    size_t File_Interface::Tell(Rml::FileHandle handle) {
        s_file* file = (s_file*)handle;
        return file->vfs != nullptr ? (size_t)vfs::ftell(file->vfs) : (size_t)SDL_TellIO(file->io);
    }

    size_t File_Interface::Length(Rml::FileHandle handle) {
        s_file* file = (s_file*)handle;
        return file->vfs != nullptr ? (size_t)vfs::fsize(file->vfs) : (size_t)SDL_GetIOSize(file->io);
    }

    double System_Interface::GetElapsedTime() {
        return (double)SDL_GetTicksNS() / 1e9;
    }

    bool System_Interface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "rmlui: %s", message.c_str());
        } else if (type == Rml::Log::LT_WARNING) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "rmlui: %s", message.c_str());
        } else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "rmlui: %s", message.c_str());
        }
        return true;
    }

} // namespace antares::ui
