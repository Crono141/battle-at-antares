#include "pch.h"

#include "antares/hd_packs.h"

#include <filesystem>
#include <fstream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

// Downloads for the HD packs (WP5.14). Launcher only: the game never downloads anything. HTTPS is
// the system's: the curl program on Linux and macOS (part of every desktop install and of the
// Flatpak runtime), WinHTTP on Windows (also in Wine). Nothing here signs in anywhere.
namespace antares::hd_packs {
    namespace {
        namespace fs = std::filesystem;

        fs::path Path_(const std::string& text) {
            return fs::path(std::u8string(text.begin(), text.end()));
        }

        uint64_t File_Size_(const fs::path& path) {
            std::error_code error;
            const uint64_t size = fs::file_size(path, error);
            return error ? 0 : size;
        }

#if defined(_WIN32)
        std::wstring Wide_(const std::string& text) {
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
            if (length > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
            return wide;
        }

        // One GET; `offset` > 0 asks for the rest of a file (Range). The body goes to `sink`.
        bool Win_Get_(const std::string& url, uint64_t offset, const std::function<bool(const char*, size_t)>& sink, uint64_t* length, std::string* error) {
            const std::wstring wide_url = Wide_(url);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            wchar_t host[256], path[4096];
            parts.lpszHostName = host;
            parts.dwHostNameLength = 256;
            parts.lpszUrlPath = path;
            parts.dwUrlPathLength = 4096;
            if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts)) {
                *error = "bad address: " + url;
                return false;
            }
            HINTERNET session = WinHttpOpen(L"Battle-at-Antares-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
            HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                           : nullptr;
            bool ok = request != nullptr;
            if (ok && offset > 0) {
                const std::wstring range = L"Range: bytes=" + std::to_wstring(offset) + L"-";
                ok = WinHttpAddRequestHeaders(request, range.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
            }
            ok = ok && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr);
            DWORD status = 0, size = sizeof(status);
            if (ok) {
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                ok = status == 200 || status == 206;
                if (!ok) *error = "the server answered " + std::to_string(status);
                if (ok && offset > 0 && status == 200) {
                    *error = "restart"; // the server ignored the range: start over
                    ok = false;
                }
            } else {
                *error = "no connection (error " + std::to_string(GetLastError()) + ")";
            }
            if (ok && length != nullptr) {
                wchar_t text[32];
                DWORD text_size = sizeof(text);
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &text_size, WINHTTP_NO_HEADER_INDEX)) {
                    *length = _wcstoui64(text, nullptr, 10);
                }
            }
            std::vector<char> buffer(1 << 16);
            while (ok) {
                DWORD got = 0;
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
                    *error = "the download broke off (error " + std::to_string(GetLastError()) + ")";
                    ok = false;
                    break;
                }
                if (got == 0) break;
                if (!sink(buffer.data(), got)) {
                    *error = "cancelled";
                    ok = false;
                }
            }
            if (request) WinHttpCloseHandle(request);
            if (connection) WinHttpCloseHandle(connection);
            if (session) WinHttpCloseHandle(session);
            return ok;
        }
#else
        SDL_Process* Start_Curl_(const std::vector<std::string>& arguments, bool pipe_output) {
            std::vector<const char*> argv;
            for (const std::string& argument : arguments) argv.push_back(argument.c_str());
            argv.push_back(nullptr);
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, pipe_output ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
            SDL_Process* process = SDL_CreateProcessWithProperties(properties);
            SDL_DestroyProperties(properties);
            return process;
        }

        std::string Curl_Error_(int code) {
            switch (code) {
                case 6: return "the address couldn't be found (no internet connection?)";
                case 7: return "couldn't connect (no internet connection?)";
                case 22: return "the server refused the request";
                case 23: return "couldn't write the file (is the disk full?)";
                case 28: return "the connection timed out";
                case 35:
                case 60: return "the secure connection failed";
                default: return "the download failed (curl error " + std::to_string(code) + ")";
            }
        }
#endif
    } // namespace

    bool Fetch_Text(const std::string& url, std::string* text, std::string* error) {
        text->clear();
#if defined(_WIN32)
        return Win_Get_(url, 0, [&](const char* data, size_t size) {
            text->append(data, size);
            return text->size() < (16u << 20); }, nullptr, error);
#else
        SDL_Process* process = Start_Curl_({"curl", "-fsSL", "--connect-timeout", "20", "--max-time", "60", url}, true);
        if (process == nullptr) {
            *error = "the curl program isn't installed, so nothing can be downloaded";
            return false;
        }
        size_t size = 0;
        int code = 0;
        void* data = SDL_ReadProcess(process, &size, &code);
        if (data != nullptr) {
            text->assign(static_cast<const char*>(data), size);
            SDL_free(data);
        }
        SDL_DestroyProcess(process);
        if (code != 0) {
            *error = Curl_Error_(code);
            return false;
        }
        return true;
#endif
    }

    bool Fetch(const std::string& url, const std::string& path_text, s_progress* progress, std::string* error) {
        const fs::path path = Path_(path_text);
        progress->done = File_Size_(path);
#if defined(_WIN32)
        for (int attempt = 0; attempt < 2; ++attempt) {
            const uint64_t offset = File_Size_(path);
            std::ofstream file(path, std::ios::binary | (offset > 0 ? std::ios::app : std::ios::trunc));
            if (!file) {
                *error = "can't write " + path.string();
                return false;
            }
            progress->done = offset;
            uint64_t length = 0;
            const bool ok = Win_Get_(url, offset, [&](const char* data, size_t size) {
                file.write(data, static_cast<std::streamsize>(size));
                progress->done += size;
                return file.good() && !progress->cancel; }, &length, error);
            if (ok) return true;
            if (*error != "restart") return false;
            file.close();
            std::error_code code;
            fs::remove(path, code);
        }
        return false;
#else
        SDL_Process* process = Start_Curl_({"curl", "-fsSL", "--retry", "3", "--connect-timeout", "20", "-C", "-", "-o", path_text, url}, false);
        if (process == nullptr) {
            *error = "the curl program isn't installed, so nothing can be downloaded";
            return false;
        }
        int code = 0;
        while (!SDL_WaitProcess(process, false, &code)) {
            if (progress->cancel) {
                SDL_KillProcess(process, true);
                SDL_WaitProcess(process, true, &code);
                SDL_DestroyProcess(process);
                *error = "cancelled";
                return false;
            }
            progress->done = File_Size_(path);
            SDL_Delay(200);
        }
        SDL_DestroyProcess(process);
        progress->done = File_Size_(path);
        if (code != 0) {
            *error = Curl_Error_(code);
            return false;
        }
        return true;
#endif
    }

#if !defined(_WIN32)
    namespace {
        // A string for curl's config file format: quoted, with \ and " escaped.
        std::string Curl_Quote_(const std::string& text) {
            std::string quoted = "\"";
            for (char character : text) {
                if (character == '\\' || character == '"') quoted += '\\';
                if (character == '\n' || character == '\r') continue; // never let a value start a new line
                quoted += character;
            }
            return quoted + "\"";
        }
    } // namespace
#endif

    bool Gog_Request(const std::string& url, const std::string& bearer, const std::string& output_path, std::string* body, std::string* final_url, s_progress* progress, std::string* error) {
#if defined(_WIN32)
        // WinHTTP: redirects followed by hand, so the Authorization header goes to GOG only.
        // Untested on Windows yet (WP5.15).
        std::string address = url;
        std::ofstream file;
        if (!output_path.empty()) file.open(Path_(output_path), std::ios::binary | std::ios::trunc);
        for (int hop = 0; hop < 6; ++hop) {
            const std::wstring wide = Wide_(address);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            wchar_t host[256], path[8192];
            parts.lpszHostName = host;
            parts.dwHostNameLength = 256;
            parts.lpszUrlPath = path;
            parts.dwUrlPathLength = 8192;
            if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
                *error = "a bad address";
                return false;
            }
            const std::wstring host_text(host);
            const bool gog = host_text == L"auth.gog.com" || host_text == L"embed.gog.com" || host_text == L"api.gog.com";
            HINTERNET session = WinHttpOpen(L"Battle-at-Antares-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            if (session) WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
            HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
            HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                           : nullptr;
            bool ok = request != nullptr;
            if (ok && gog && !bearer.empty()) {
                const std::wstring header = L"Authorization: Bearer " + Wide_(bearer);
                ok = WinHttpAddRequestHeaders(request, header.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
            }
            ok = ok && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
            DWORD status = 0, size = sizeof(status);
            if (ok) WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (ok && status >= 300 && status < 400) {
                wchar_t location[8192];
                DWORD location_size = sizeof(location);
                ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &location_size, WINHTTP_NO_HEADER_INDEX);
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                if (!ok) break;
                const int length = WideCharToMultiByte(CP_UTF8, 0, location, -1, nullptr, 0, nullptr, nullptr);
                std::string next(length > 0 ? length - 1 : 0, '\0');
                if (length > 1) WideCharToMultiByte(CP_UTF8, 0, location, -1, next.data(), length, nullptr, nullptr);
                address = next.rfind("http", 0) == 0 ? next : "https://" + std::string(host_text.begin(), host_text.end()) + next;
                continue;
            }
            if (ok && status != 200) {
                *error = "GOG answered " + std::to_string(status);
                ok = false;
            }
            std::vector<char> buffer(1 << 16);
            while (ok) {
                DWORD got = 0;
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
                    *error = "the connection broke off";
                    ok = false;
                    break;
                }
                if (got == 0) break;
                if (file.is_open()) {
                    file.write(buffer.data(), got);
                } else if (body != nullptr) {
                    body->append(buffer.data(), got);
                }
                if (progress != nullptr) {
                    progress->done += got;
                    if (progress->cancel) {
                        *error = "cancelled";
                        ok = false;
                    }
                }
            }
            if (request) WinHttpCloseHandle(request);
            if (connection) WinHttpCloseHandle(connection);
            if (session) WinHttpCloseHandle(session);
            if (ok && final_url != nullptr) *final_url = address;
            if (!ok && error->empty()) *error = "no connection to GOG";
            return ok;
        }
        *error = "too many redirects";
        return false;
#else
        // curl reads its options from standard input (-K -): the address and the token are never
        // on its command line, where other users of the machine could read them. curl doesn't send
        // Authorization on to another host after a redirect (only --location-trusted would).
        std::string config = "url = " + Curl_Quote_(url) + "\nlocation\nfail\nsilent\nconnect-timeout = 20\nretry = 2\n";
        if (!bearer.empty()) config += "header = " + Curl_Quote_("Authorization: Bearer " + bearer) + "\n";
        if (!output_path.empty()) config += "output = " + Curl_Quote_(output_path) + "\nwrite-out = \"%{url_effective}\"\n";
        const char* argv[] = {"curl", "-K", "-", nullptr};
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (process == nullptr) {
            *error = "the curl program isn't installed, so nothing can be downloaded";
            return false;
        }
        SDL_IOStream* input = SDL_GetProcessInput(process);
        const bool sent = input != nullptr && SDL_WriteIO(input, config.data(), config.size()) == config.size();
        if (input != nullptr) SDL_CloseIO(input); // end of the config: curl starts
        config.assign(config.size(), '\0');       // the token leaves memory as soon as it can
        if (!sent) {
            SDL_KillProcess(process, true);
            SDL_DestroyProcess(process);
            *error = "couldn't start the download";
            return false;
        }
        std::string output;
        SDL_IOStream* stdout_stream = SDL_GetProcessOutput(process);
        int code = 0;
        for (;;) {
            char buffer[4096];
            const size_t got = stdout_stream != nullptr ? SDL_ReadIO(stdout_stream, buffer, sizeof(buffer)) : 0;
            if (got > 0) {
                output.append(buffer, got);
                continue;
            }
            if (SDL_WaitProcess(process, false, &code)) break;
            if (progress != nullptr) {
                if (!output_path.empty()) progress->done = File_Size_(Path_(output_path));
                if (progress->cancel) {
                    SDL_KillProcess(process, true);
                    SDL_WaitProcess(process, true, &code);
                    SDL_DestroyProcess(process);
                    *error = "cancelled";
                    return false;
                }
            }
            SDL_Delay(100);
        }
        for (char buffer[4096];;) { // what curl wrote just before it ended
            const size_t got = stdout_stream != nullptr ? SDL_ReadIO(stdout_stream, buffer, sizeof(buffer)) : 0;
            if (got == 0) break;
            output.append(buffer, got);
        }
        SDL_DestroyProcess(process);
        if (code != 0) {
            *error = code == 22 ? "GOG refused the request" : Curl_Error_(code);
            return false;
        }
        if (!output_path.empty()) {
            if (final_url != nullptr) *final_url = output;
            if (progress != nullptr) progress->done = File_Size_(Path_(output_path));
        } else if (body != nullptr) {
            *body = output;
        }
        return true;
#endif
    }

    bool Install(const s_pack& pack, const std::string& mods_folder, s_progress* progress, std::string* phase, std::mutex* phase_mutex, std::string* error) {
        const auto set_phase = [&](const char* text) {
            std::lock_guard<std::mutex> lock(*phase_mutex);
            *phase = text;
        };
        std::error_code code;
        fs::create_directories(Path_(mods_folder), code);
        const std::string part = mods_folder + pack.file + ".part";
        progress->total = pack.size;
        // A complete .part from an earlier run needs no download (curl would refuse the range).
        if (File_Size_(Path_(part)) != pack.size || pack.size == 0) {
            set_phase("Downloading");
            if (!Fetch(pack.url, part, progress, error)) return false;
        }
        if (pack.size != 0 && File_Size_(Path_(part)) != pack.size) {
            fs::remove(Path_(part), code);
            *error = "the download came out the wrong size; try again";
            return false;
        }
        set_phase("Checking");
        progress->done = 0;
        const std::string sha = File_Sha256(part, [&](uint64_t done) {
            progress->done = done;
            return !progress->cancel.load();
        });
        if (progress->cancel) {
            *error = "cancelled";
            return false;
        }
        if (!pack.sha256.empty() && sha != pack.sha256) {
            fs::remove(Path_(part), code);
            *error = "the download is damaged (its checksum doesn't match); try again";
            return false;
        }
        fs::rename(Path_(part), Path_(mods_folder + pack.file), code);
        if (code) {
            *error = "can't finish " + pack.file + ": " + code.message();
            return false;
        }
        return Record_Installed(mods_folder, pack, error);
    }
} // namespace antares::hd_packs
