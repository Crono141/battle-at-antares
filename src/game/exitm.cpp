#include "pch.h"

#include "antares/host.h"

namespace exitm {
    [[noreturn]] void Exit_With_Message(const char* message) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "fatal exit: %s", message != nullptr ? message : "<null>");

        platform::Request_Fatal_Exit(message);
        platform::Check_Exit();
        std::abort();
    }

    [[noreturn]] void Exit_Normally(const char* message) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "normal exit: %s", message != nullptr ? message : "<null>");

        platform::Request_Exit(message);
        platform::Check_Exit();
        std::abort();
    }

    void Present_Exit_Message(void* window_handle) {
        const char* message = platform::Exit_Message();
        if (message == nullptr || message[0] == '\0') {
            return;
        }

        const bool is_error = platform::Exit_Status() != EXIT_SUCCESS;
        if (antares::host::Active()) return antares::host::Set_Exit_Message(message, is_error); // Antares: the frontend shows it (WP5.10)
        Show_Message_Box(window_handle, message, "Microprose", is_error);
    }

    [[noreturn]] void __cdecl Exit_With_message2(const char* message) {
        Exit_With_Message(message);
    }
} // namespace exitm