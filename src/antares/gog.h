#pragma once

#include <cstdint>
#include <string>

// GOG sign-in and download (WP5.13b, D29 case 3): the choices, without the network. The launcher
// (launcher_gog.cpp) does the requests. GOG's API is unofficial and documented by the community;
// these are GOG Galaxy's public client id and secret, the same ones Lutris, Heroic and minigalaxy
// ship (recorded in D29).
namespace antares::gog {
    inline constexpr char CLIENT_ID[] = "46899977096215655";
    inline constexpr char CLIENT_SECRET[] = "9d85c43b1482497dbbce61f6e4aa173a433796eeae2ca8c5f6129f2dc4de46d9";
    inline constexpr char REDIRECT_URI[] = "https://embed.gog.com/on_login_success?origin=client";
    inline constexpr char LIBRARY_URL[] = "https://embed.gog.com/account/getFilteredProducts?mediaType=1&search=orion";
    inline constexpr char STORE_URL[] = "https://www.gog.com/game/master_of_orion_1_2";

    /** The address that trades a sign-in code for a token. It holds the code: never log it. */
    std::string Token_Url(const std::string& code);
    /** The access token from /token's answer. */
    bool Parse_Token(const std::string& json, std::string* token, std::string* error);

    struct s_product {
        uint64_t id = 0;
        std::string title;
    };
    /** Master of Orion II in the library search's answer: "Master of Orion 1+2" or anything titled
     *  Master of Orion 2/II. False when the player doesn't own it. */
    bool Choose_Product(const std::string& json, s_product* product, std::string* error);
    std::string Details_Url(uint64_t product_id);

    enum e_system { SYSTEM_WINDOWS,
        SYSTEM_MAC,
        SYSTEM_LINUX };
    struct s_download {
        std::string path; // manualUrl, e.g. /downloads/master_of_orion_1_2/en1installer0
        std::string name;
        e_system system = SYSTEM_WINDOWS;
        uint64_t size = 0; // GOG's "190 MB", roughly
    };
    /** The installer to fetch from gameDetails' "downloads": English (else the first language),
     *  this system's (else Windows', which extracts everywhere), the one for Master of Orion 2 when
     *  the bundle lists both games. */
    bool Choose_Download(const std::string& json, e_system system, s_download* download, std::string* error);

    /** The name to save the installer under, from the address the download ended at; always one
     *  the wizard recognises (…master_of_orion_2… with .exe, .sh or .pkg). */
    std::string Installer_File_Name(const std::string& final_url, e_system system);
} // namespace antares::gog
