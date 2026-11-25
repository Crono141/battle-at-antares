#include "pch.h"

#include "antares/gog.h"
#include "doctest/doctest.h"

// GOG sign-in and download (WP5.13b): the choices made from GOG's answers. The answers here are
// shaped as the community documentation describes them; replace them with the owner's -gog-dump
// files once captured.
namespace gog = antares::gog;

DOCTEST_TEST_SUITE("GOG (WP5.13b)") {
    DOCTEST_TEST_CASE("the token request holds the code encoded; the token is read from the answer") {
        const std::string url = gog::Token_Url("a+b/c=d");
        DOCTEST_CHECK(url.rfind("https://auth.gog.com/token?client_id=46899977096215655&", 0) == 0);
        DOCTEST_CHECK(url.find("&code=a%2Bb%2Fc%3Dd&") != std::string::npos);
        DOCTEST_CHECK(url.find("redirect_uri=https%3A%2F%2Fembed.gog.com%2Fon_login_success%3Forigin%3Dclient") != std::string::npos);
        std::string token, error;
        DOCTEST_CHECK(gog::Parse_Token(R"({"expires_in":3600,"scope":"","token_type":"bearer","access_token":"TOKEN","user_id":"1","refresh_token":"R"})", &token, &error));
        DOCTEST_CHECK(token == "TOKEN");
        DOCTEST_CHECK_FALSE(gog::Parse_Token(R"({"error":"invalid_grant"})", &token, &error));
        DOCTEST_CHECK_FALSE(error.empty());
    }

    DOCTEST_TEST_CASE("the product: the 1+2 bundle first, a lone Master of Orion 2 next, nothing else") {
        gog::s_product product;
        std::string error;
        DOCTEST_REQUIRE(gog::Choose_Product(R"({"products":[{"id":1,"title":"Master of Orion"},{"id":1207661633,"title":"Master of Orion 1+2"},{"id":3,"title":"Master of Orion II: Battle at Antares"}]})", &product, &error));
        DOCTEST_CHECK(product.id == 1207661633u);
        DOCTEST_REQUIRE(gog::Choose_Product(R"({"products":[{"id":5,"title":"Master of Orion"},{"id":7,"title":"Master of Orion II: Battle at Antares"}]})", &product, &error));
        DOCTEST_CHECK(product.id == 7u);
        DOCTEST_CHECK_FALSE(gog::Choose_Product(R"({"products":[{"id":5,"title":"Master of Orion"},{"id":9,"title":"Master of Magic"}]})", &product, &error));
        DOCTEST_CHECK(error.find("isn't in your GOG library") != std::string::npos);
        DOCTEST_CHECK_FALSE(gog::Choose_Product("<html>", &product, &error));
    }

    DOCTEST_TEST_CASE("the download: English, this system, Master of Orion 2's installer; Windows when this system has none") {
        const std::string details = R"({"title":"Master of Orion 1+2","downloads":[
            ["Deutsch",{"windows":[{"manualUrl":"/downloads/master_of_orion_1_2/de1installer0","name":"Master of Orion 2","size":"190 MB"}]}],
            ["English",{"windows":[{"manualUrl":"/downloads/master_of_orion_1_2/en1installer0","name":"Master of Orion","size":"40 MB"},
                                   {"manualUrl":"/downloads/master_of_orion_1_2/en1installer1","name":"Master of Orion 2","size":"190 MB"}],
                        "mac":[{"manualUrl":"/downloads/master_of_orion_1_2/en2installer1","name":"Master of Orion 2","size":"184 MB"}],
                        "linux":[{"manualUrl":"/downloads/master_of_orion_1_2/en3installer1","name":"Master of Orion II","size":"0.17 GB"}]}]]})";
        gog::s_download download;
        std::string error;
        DOCTEST_REQUIRE(gog::Choose_Download(details, gog::SYSTEM_WINDOWS, &download, &error));
        DOCTEST_CHECK(download.path == "/downloads/master_of_orion_1_2/en1installer1");
        DOCTEST_CHECK(download.size == 190ull * 1048576);
        DOCTEST_REQUIRE(gog::Choose_Download(details, gog::SYSTEM_LINUX, &download, &error));
        DOCTEST_CHECK(download.path == "/downloads/master_of_orion_1_2/en3installer1");
        DOCTEST_CHECK(download.system == gog::SYSTEM_LINUX);
        const std::string windows_only = R"({"downloads":[["English",{"windows":[{"manualUrl":"/downloads/x/en1installer0","name":"Master of Orion 2","size":"190 MB"}],"linux":[]}]]})";
        DOCTEST_REQUIRE(gog::Choose_Download(windows_only, gog::SYSTEM_LINUX, &download, &error));
        DOCTEST_CHECK(download.system == gog::SYSTEM_WINDOWS); // extracts everywhere (WP5.13c)
        DOCTEST_CHECK_FALSE(gog::Choose_Download(R"({"downloads":[]})", gog::SYSTEM_LINUX, &download, &error));
    }

    DOCTEST_TEST_CASE("the saved name: GOG's own when the wizard recognises it, never a path") {
        DOCTEST_CHECK(gog::Installer_File_Name("https://gog-cdn.example/secure/setup_master_of_orion_2_1.40b_%2828044%29.exe?token=abc", gog::SYSTEM_WINDOWS) ==
            "setup_master_of_orion_2_1.40b_(28044).exe");
        DOCTEST_CHECK(gog::Installer_File_Name("https://cdn.example/a/gog_master_of_orion_2_2.0.0.6.sh", gog::SYSTEM_LINUX) == "gog_master_of_orion_2_2.0.0.6.sh");
        DOCTEST_CHECK(gog::Installer_File_Name("https://cdn.example/a/installer.bin", gog::SYSTEM_MAC) == "gog_master_of_orion_2.pkg");
        // A name that decodes to ../../...: the slashes go, and a leading dot means the plain name.
        DOCTEST_CHECK(gog::Installer_File_Name("https://cdn.example/a/..%2F..%2Fmaster_of_orion_2.exe", gog::SYSTEM_WINDOWS) == "setup_master_of_orion_2_gog.exe");
        DOCTEST_CHECK(gog::Installer_File_Name("", gog::SYSTEM_WINDOWS) == "setup_master_of_orion_2_gog.exe");
    }
}
