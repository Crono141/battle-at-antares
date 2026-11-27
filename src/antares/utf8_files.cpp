#include "antares/utf8_files.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace antares::utf8 {
#ifdef _WIN32
    namespace {
        std::wstring Wide_(const char* text) {
            if (text == nullptr) return std::wstring();
            const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
            if (count <= 0) return std::wstring();
            std::wstring wide(static_cast<size_t>(count), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), count);
            wide.resize(static_cast<size_t>(count - 1)); // without the terminator
            return wide;
        }
    } // namespace

    FILE* Open(const char* path, const char* mode) {
        if (path == nullptr || mode == nullptr) return nullptr;
        return _wfopen(Wide_(path).c_str(), Wide_(mode).c_str());
    }

    FILE* Reopen(const char* path, const char* mode, FILE* stream) {
        if (path == nullptr || mode == nullptr) return nullptr;
        return _wfreopen(Wide_(path).c_str(), Wide_(mode).c_str(), stream);
    }

    int Remove(const char* path) {
        return _wremove(Wide_(path).c_str());
    }

    int Change_Directory(const char* path) {
        return _wchdir(Wide_(path).c_str());
    }

    std::filesystem::path Path(const std::string& text) {
        return std::filesystem::path(Wide_(text.c_str()));
    }

    std::string Text(const std::filesystem::path& path) {
        const std::wstring& wide = path.native();
        if (wide.empty()) return std::string();
        const int count = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string text(static_cast<size_t>(count > 0 ? count : 0), '\0');
        if (count > 0) WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), text.data(), count, nullptr, nullptr);
        return text;
    }
#else
    FILE* Open(const char* path, const char* mode) {
        return path != nullptr && mode != nullptr ? std::fopen(path, mode) : nullptr;
    }

    FILE* Reopen(const char* path, const char* mode, FILE* stream) {
        return path != nullptr && mode != nullptr ? std::freopen(path, mode, stream) : nullptr;
    }

    int Remove(const char* path) {
        return std::remove(path);
    }

    int Change_Directory(const char* path) {
        return chdir(path);
    }

    std::filesystem::path Path(const std::string& text) {
        return std::filesystem::path(text);
    }

    std::string Text(const std::filesystem::path& path) {
        return path.string();
    }
#endif
} // namespace antares::utf8
