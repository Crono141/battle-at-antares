#include "pch.h"

#include "antares/control.h"

#include "antares/identity.h"

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace antares::control {
    namespace {
        int s_state = -1; // -1 not started, 0 off, 1 listening
#if !defined(_WIN32)
        int s_listen = -1;
        int s_client = -1;
        std::string s_buffer;

        bool Set_Nonblocking(int fd) {
            const int flags = fcntl(fd, F_GETFL, 0);
            return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
        }

        void Start() {
            s_state = 0;
            const char* path = antares::Get_Env("CONTROL");
            if (path == nullptr || path[0] == '\0') {
                return;
            }
            sockaddr_un address{};
            if (strlen(path) >= sizeof(address.sun_path)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "control: socket path too long: %s", path);
                return;
            }
            address.sun_family = AF_UNIX;
            strlcpy(address.sun_path, path, sizeof(address.sun_path));
            (void)unlink(path);
            s_listen = socket(AF_UNIX, SOCK_STREAM, 0);
            if (s_listen < 0 || bind(s_listen, (sockaddr*)&address, sizeof(address)) != 0 || listen(s_listen, 1) != 0 ||
                !Set_Nonblocking(s_listen)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "control: cannot listen on %s (%s)", path, strerror(errno));
                if (s_listen >= 0) {
                    close(s_listen);
                    s_listen = -1;
                }
                return;
            }
            s_state = 1;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "control: listening on %s", path);
        }

        void Drop_Client() {
            if (s_client >= 0) {
                close(s_client);
                s_client = -1;
            }
            s_buffer.clear();
        }
#endif
    } // namespace

    bool Enabled() {
#if defined(_WIN32)
        return false;
#else
        if (s_state < 0) {
            Start();
        }
        return s_state == 1;
#endif
    }

    bool Read_Line(std::string* line) {
#if defined(_WIN32)
        (void)line;
        return false;
#else
        if (!Enabled()) {
            return false;
        }
        if (s_client < 0) {
            s_client = accept(s_listen, nullptr, nullptr);
            if (s_client < 0) {
                return false;
            }
            (void)Set_Nonblocking(s_client);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "control: client connected");
        }
        char chunk[4096];
        while (true) {
            const ssize_t got = recv(s_client, chunk, sizeof(chunk), 0);
            if (got > 0) {
                s_buffer.append(chunk, (size_t)got);
                continue;
            }
            if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "control: client disconnected");
                Drop_Client();
                return false;
            }
            break;
        }
        const size_t end = s_buffer.find('\n');
        if (end == std::string::npos) {
            return false;
        }
        *line = s_buffer.substr(0, end);
        s_buffer.erase(0, end + 1);
        if (!line->empty() && line->back() == '\r') {
            line->pop_back();
        }
        return true;
#endif
    }

    void Reply(const std::string& line) {
#if !defined(_WIN32)
        if (s_client < 0) {
            return;
        }
        const std::string text = line + "\n";
        size_t sent = 0;
        while (sent < text.size()) {
            const ssize_t wrote = send(s_client, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
            if (wrote > 0) {
                sent += (size_t)wrote;
            } else if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                SDL_Delay(1);
            } else {
                Drop_Client();
                return;
            }
        }
#else
        (void)line;
#endif
    }

    std::string Json_Escape(const std::string& text) {
        std::string out;
        for (const char c : text) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if ((unsigned char)c < 0x20) {
                        char escaped[8];
                        snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)(unsigned char)c);
                        out += escaped;
                    } else {
                        out += c;
                    }
            }
        }
        return out;
    }
} // namespace antares::control
