#pragma once

#include <string>

// Live game control for tests and development (WP0.8). With ANTARES_CONTROL=<socket path> the game
// listens on a local Unix socket (one client at a time) and takes input-script commands one line
// at a time; after each command has finished, including its waits, it answers with one JSON line.
// The command set and the reply are documented in platform.cpp next to the input script, and the
// Python client is tools/antares-mcp. Not available on Windows. Platform thread only.
namespace antares::control {
    /** True when ANTARES_CONTROL is set and the socket is listening. */
    bool Enabled();

    /** The next complete command line from the client, if one has arrived. Accepts a waiting
     *  client when none is connected. */
    bool Read_Line(std::string* line);

    /** Sends one reply line ('\n' is added). */
    void Reply(const std::string& line);

    /** Escapes text for a JSON string value. */
    std::string Json_Escape(const std::string& text);
} // namespace antares::control
