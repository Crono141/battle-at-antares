#include "pch.h"

#include "antares/hd_packs.h"
#include "doctest/doctest.h"
#include "game/sha256.h"

#include <chrono>
#include <filesystem>
#include <fstream>

// The HD packs' file work (WP5.14): the release list, the installed states, the checksum.
namespace {
    namespace fs = std::filesystem;
    namespace hd = antares::hd_packs;

    struct s_folder {
        fs::path root = fs::temp_directory_path() / ("antares-hd-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        s_folder() {
            fs::create_directories(root);
        }
        ~s_folder() {
            std::error_code error;
            fs::remove_all(root, error);
        }
        std::string Path() const {
            return root.string() + "/";
        }
        void Write(const std::string& name, const std::string& text) const {
            std::ofstream(root / name, std::ios::binary) << text;
        }
    };

    // GitHub's answer for the v0.1.0 release (2026-11-24), trimmed, in GitHub's order.
    const char* RELEASE = R"({ "tag_name": "v0.1.0", "assets": [
        { "name": "antares-hd-cinematics.zip", "size": 1079197749, "digest": "sha256:c50052efff5749ec54ab307614734227e3c2e6213f22dffa81aac6793e8f47f1",
          "browser_download_url": "https://github.com/Crono141/battle-at-antares-hd/releases/download/v0.1.0/antares-hd-cinematics.zip" },
        { "name": "antares-hd-diplomacy.zip", "size": 1016814002, "digest": "sha256:fec595d0798314c487d92c18aa63b2389224ace283bfa631a2866692eefb468b",
          "browser_download_url": "https://github.com/Crono141/battle-at-antares-hd/releases/download/v0.1.0/antares-hd-diplomacy.zip" },
        { "name": "SHA256SUMS", "size": 300, "browser_download_url": "https://example.invalid/SHA256SUMS" },
        { "name": "antares-hd.zip", "size": 1475194657, "digest": "sha256:bed9503e3aac717fea3f3a2a1f5094ebce62fcd32df8dcf024ce2ebbb0bac99e",
          "browser_download_url": "https://github.com/Crono141/battle-at-antares-hd/releases/download/v0.1.0/antares-hd.zip" } ] })";
} // namespace

DOCTEST_TEST_SUITE("HD packs (WP5.14)") {
    DOCTEST_TEST_CASE("the release list: zips only, the main pack first, sizes and checksums") {
        std::vector<hd::s_pack> packs;
        std::string tag, error;
        DOCTEST_REQUIRE(hd::Parse_Release(RELEASE, &packs, &tag, &error));
        DOCTEST_CHECK(tag == "v0.1.0");
        DOCTEST_REQUIRE(packs.size() == 3);
        DOCTEST_CHECK(packs[0].id == "antares-hd");
        DOCTEST_CHECK(packs[0].recommended);
        DOCTEST_CHECK(packs[0].size == 1475194657ull);
        DOCTEST_CHECK(packs[0].sha256 == "bed9503e3aac717fea3f3a2a1f5094ebce62fcd32df8dcf024ce2ebbb0bac99e");
        DOCTEST_CHECK(packs[1].id == "antares-hd-diplomacy");
        DOCTEST_CHECK(packs[2].id == "antares-hd-cinematics");
        DOCTEST_CHECK(packs[2].title == "HD cutscenes");

        DOCTEST_CHECK_FALSE(hd::Parse_Release(R"({"message": "API rate limit exceeded"})", &packs, &tag, &error));
        DOCTEST_CHECK(error.find("rate limit") != std::string::npos);
        DOCTEST_CHECK_FALSE(hd::Parse_Release("not json", &packs, &tag, &error));
    }

    DOCTEST_TEST_CASE("installed states: not there, ours, an update, someone else's copy") {
        s_folder mods;
        hd::s_pack pack;
        pack.id = "antares-hd";
        pack.file = "antares-hd.zip";
        pack.sha256 = "aaaa";
        std::string error;
        DOCTEST_CHECK(hd::State(pack, mods.Path()) == hd::NOT_INSTALLED);
        mods.Write("antares-hd.zip", "zip");
        DOCTEST_CHECK(hd::State(pack, mods.Path()) == hd::OTHER_COPY); // put there by hand: left alone
        DOCTEST_REQUIRE(hd::Record_Installed(mods.Path(), pack, &error));
        DOCTEST_CHECK(hd::State(pack, mods.Path()) == hd::INSTALLED);
        pack.sha256 = "bbbb"; // a newer release
        DOCTEST_CHECK(hd::State(pack, mods.Path()) == hd::UPDATE);
        DOCTEST_REQUIRE(hd::Record_Installed(mods.Path(), pack, &error));
        DOCTEST_CHECK(hd::State(pack, mods.Path()) == hd::INSTALLED);
        hd::s_pack other = pack;
        other.id = "antares-hd-diplomacy";
        other.file = "antares-hd-diplomacy.zip";
        DOCTEST_REQUIRE(hd::Record_Installed(mods.Path(), other, &error));
        DOCTEST_CHECK(hd::State(pack, mods.Path()) == hd::INSTALLED); // both kept in the state file
    }

    DOCTEST_TEST_CASE("the streaming checksum equals the engine's, across block boundaries") {
        s_folder folder;
        for (const size_t size : {size_t(0), size_t(3), size_t(55), size_t(56), size_t(64), size_t(1000), size_t(3 << 20) + 17}) {
            std::string data(size, '\0');
            for (size_t index = 0; index < size; ++index) data[index] = static_cast<char>((index * 131 + 7) & 0xFF);
            folder.Write("blob", data);
            DOCTEST_CHECK_MESSAGE(hd::File_Sha256(folder.Path() + "blob") == orion2re::crypto::sha256_hex(data), size);
        }
        folder.Write("abc", "abc");
        DOCTEST_CHECK(hd::File_Sha256(folder.Path() + "abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        DOCTEST_CHECK(hd::File_Sha256(folder.Path() + "missing").empty());
    }

#if !defined(_WIN32)
    // Install through the system's curl on a file:// address: the real resume, size and checksum
    // paths, with nothing downloaded (curl resumes file:// as it does https://).
    DOCTEST_TEST_CASE("install: resumes a partial file, refuses a damaged one, keeps nothing half-done") {
        s_folder folder;
        std::string data(3 * 1024 * 1024 + 123, '\0');
        for (size_t index = 0; index < data.size(); ++index) data[index] = static_cast<char>((index * 7919 + 13) & 0xFF);
        folder.Write("source.zip", data);
        fs::create_directories(folder.root / "mods");
        const std::string mods = folder.Path() + "mods/";
        hd::s_pack pack;
        pack.id = "antares-hd-test";
        pack.file = "antares-hd-test.zip";
        pack.size = data.size();
        pack.sha256 = orion2re::crypto::sha256_hex(data);
        pack.url = "file://" + folder.Path() + "source.zip";
        std::mutex phase_mutex;
        std::string phase, error;
        const auto read = [&](const std::string& name) {
            std::ifstream file(folder.root / "mods" / name, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        };

        // A download that broke off after 1 MB: the rest is fetched, not the whole file again.
        folder.Write("mods/antares-hd-test.zip.part", data.substr(0, 1024 * 1024));
        {
            hd::s_progress progress;
            DOCTEST_REQUIRE_MESSAGE(hd::Install(pack, mods, &progress, &phase, &phase_mutex, &error), error);
        }
        DOCTEST_CHECK(read("antares-hd-test.zip") == data);
        DOCTEST_CHECK_FALSE(fs::exists(folder.root / "mods/antares-hd-test.zip.part"));
        DOCTEST_CHECK(hd::State(pack, mods) == hd::INSTALLED);

        // A complete .part left by an earlier run: checked and installed without fetching (the
        // address is gone, so a fetch would fail).
        fs::remove(folder.root / "mods/antares-hd-test.zip");
        folder.Write("mods/antares-hd-test.zip.part", data);
        hd::s_pack offline = pack;
        offline.url = "file://" + folder.Path() + "gone.zip";
        {
            hd::s_progress progress;
            DOCTEST_REQUIRE_MESSAGE(hd::Install(offline, mods, &progress, &phase, &phase_mutex, &error), error);
        }
        DOCTEST_CHECK(read("antares-hd-test.zip") == data);

        // A damaged download (right size, wrong bytes): refused, the .part removed, the
        // installed copy untouched.
        std::string damaged = data;
        damaged[12345] ^= 0x55;
        folder.Write("mods/antares-hd-test.zip.part", damaged);
        {
            hd::s_progress progress;
            DOCTEST_CHECK_FALSE(hd::Install(offline, mods, &progress, &phase, &phase_mutex, &error));
        }
        DOCTEST_CHECK(error.find("damaged") != std::string::npos);
        DOCTEST_CHECK_FALSE(fs::exists(folder.root / "mods/antares-hd-test.zip.part"));
        DOCTEST_CHECK(read("antares-hd-test.zip") == data);

        // A file that comes out the wrong size (the release changed under us): refused.
        hd::s_pack wrong = pack;
        wrong.size = data.size() + 10;
        fs::remove(folder.root / "mods/antares-hd-test.zip");
        {
            hd::s_progress progress;
            DOCTEST_CHECK_FALSE(hd::Install(wrong, mods, &progress, &phase, &phase_mutex, &error));
        }
        DOCTEST_CHECK(error.find("wrong size") != std::string::npos);
        DOCTEST_CHECK_FALSE(fs::exists(folder.root / "mods/antares-hd-test.zip"));

        // No network at all: a clear message, nothing left behind.
        hd::s_pack nowhere = pack;
        nowhere.url = "https://antares-hd.invalid/antares-hd-test.zip";
        {
            hd::s_progress progress;
            DOCTEST_CHECK_FALSE(hd::Install(nowhere, mods, &progress, &phase, &phase_mutex, &error));
        }
        DOCTEST_CHECK(error.find("couldn't be found") != std::string::npos);
        DOCTEST_CHECK_FALSE(fs::exists(folder.root / "mods/antares-hd-test.zip"));
    }
#endif
}
