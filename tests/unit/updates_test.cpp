#include <doctest/doctest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "antares/updates.h"

namespace up = antares::updates;

namespace {
    std::string Read_(const char* path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }
} // namespace

TEST_SUITE("updates (WP5.19)") {
    TEST_CASE("versions: releases, pre-releases and test builds") {
        DOCTEST_CHECK(up::Compare_Versions("0.7.1", "0.7.0") == 1);
        DOCTEST_CHECK(up::Compare_Versions("v0.7.1", "0.7.1") == 0);
        DOCTEST_CHECK(up::Compare_Versions("0.8.0", "0.7.12") == 1);
        DOCTEST_CHECK(up::Compare_Versions("0.10.0", "0.9.9") == 1);
        // A 0.8 test build is newer than 0.7.1 (never "updated" back to it), older than 0.8.0.
        DOCTEST_CHECK(up::Compare_Versions("0.8.0-test12", "0.7.1") == 1);
        DOCTEST_CHECK(up::Compare_Versions("0.8.0-test12", "0.8.0") == -1);
        DOCTEST_CHECK(up::Compare_Versions("0.8.0-test9", "0.8.0-test10") == -1);
        DOCTEST_CHECK(up::Compare_Versions("0.8.0-test10", "0.8.0-test10") == 0);
    }

    TEST_CASE("the real 0.7.1 release: its files by package kind") {
        up::s_release release;
        std::string error;
        DOCTEST_REQUIRE(up::Parse_Release(Read_(UPDATES_TEST_DATA_DIR "/release-0.7.1.json"), &release, &error));
        DOCTEST_CHECK(release.version == "0.7.1");
        DOCTEST_CHECK(release.page_url.find("/releases/tag/v0.7.1") != std::string::npos);
        DOCTEST_CHECK(release.intro.find("crash in 0.7.0") != std::string::npos);
        DOCTEST_CHECK(release.intro.find("**") == std::string::npos);
        const up::s_asset* archive = up::Asset_For(release, up::KIND_LINUX_ARCHIVE);
        DOCTEST_REQUIRE(archive != nullptr);
        DOCTEST_CHECK(archive->name == "battle-at-antares-0.7.1-linux-x86_64.tar.gz");
        DOCTEST_CHECK(archive->sha256 == "327020a02aff841a5183b8e6313da0a6a7d93009d0d6b712d293e592d044740f");
        DOCTEST_CHECK(archive->size > 1000000);
        DOCTEST_CHECK(up::Asset_For(release, up::KIND_WINDOWS_ZIP) != nullptr);
        DOCTEST_CHECK(up::Asset_For(release, up::KIND_MAC_ZIP) != nullptr);
        // 0.7.1 had no installer, AppImage, Flatpak or disk image: nothing to offer those kinds.
        DOCTEST_CHECK(up::Asset_For(release, up::KIND_WINDOWS_INSTALLER) == nullptr);
        DOCTEST_CHECK(up::Asset_For(release, up::KIND_APPIMAGE) == nullptr);
        DOCTEST_CHECK(up::Asset_For(release, up::KIND_FLATPAK) == nullptr);
        DOCTEST_CHECK(up::Asset_For(release, up::KIND_MAC_APP) == nullptr);
        // A 0.8 test build doesn't take it for an update.
        DOCTEST_CHECK(up::Compare_Versions(release.version, "0.8.0-test12") < 0);
    }

    TEST_CASE("not a release") {
        up::s_release release;
        std::string error;
        DOCTEST_CHECK(!up::Parse_Release("not json", &release, &error));
        DOCTEST_CHECK(!up::Parse_Release("{\"message\": \"API rate limit exceeded\"}", &release, &error));
    }

    TEST_CASE("the release label and the player's files") {
        DOCTEST_CHECK(up::Installed_Version("/no/such/folder/") == "");
        DOCTEST_CHECK(up::Players_File("data/antares.cfg"));
        DOCTEST_CHECK(!up::Players_File("antares-launcher"));
        DOCTEST_CHECK(!up::Players_File("data/mods/antares-ui-chrome.zip"));
        DOCTEST_CHECK(up::Can_Install(up::KIND_APPIMAGE));
        DOCTEST_CHECK(!up::Can_Install(up::KIND_FLATPAK));
    }
}
