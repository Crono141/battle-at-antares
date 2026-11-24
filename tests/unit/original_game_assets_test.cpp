#include "pch.h"

#include "antares/game_files.h"
#include "doctest/doctest.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
    namespace fs = std::filesystem;

    struct Original_Assets_Fixture {
        fs::path launch_directory = fs::current_path();
        fs::path root = fs::temp_directory_path() /
            ("orion-original-assets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        Original_Assets_Fixture() {
            fs::create_directory(root);
        }

        ~Original_Assets_Fixture() {
            vfs::Shutdown();
            platform::Shutdown_Exit();
            std::error_code error;
            fs::current_path(launch_directory, error);
            fs::remove_all(root, error);
        }

        void Init() {
            fs::current_path(root);
            MOX::settings.language = 0;
            MOX2::use_explicit_game_directory = false;
            platform::Reset_Exit();
            DOCTEST_REQUIRE(vfs::Init(root.string().c_str()));
        }

        // Every original file the game needs (antares::game_files, WP5.10), empty, in `folder`.
        void Write_Originals(const fs::path& folder, const char* contents) {
            for (const std::string& name : antares::game_files::Required_Files()) {
                Write(folder / name, contents);
            }
        }

        void Write(const fs::path& path, const char* contents) {
            const fs::path full = root / path;
            fs::create_directories(full.parent_path());
            std::ofstream file(full, std::ios::binary);
            file << contents;
            DOCTEST_REQUIRE(file.good());
        }
    };
} // namespace

DOCTEST_TEST_CASE("original assets beside the executable do not require ORIONCD.INI") {
    Original_Assets_Fixture fixture;
    fixture.Init();
    fixture.Write_Originals("", "local");

    DOCTEST_SUBCASE("missing INI") {}
    DOCTEST_SUBCASE("empty INI") { fixture.Write("ORIONCD.INI", ""); }
    DOCTEST_SUBCASE("blank first line") { fixture.Write("ORIONCD.INI", "\r\n"); }
    DOCTEST_SUBCASE("INI is a directory") { fs::create_directory(fixture.root / "ORIONCD.INI"); }

    DOCTEST_CHECK_NOTHROW(MOX2::Set_Mox_Alt_Path());
    DOCTEST_CHECK_FALSE(platform::Exit_Requested());
    DOCTEST_CHECK(vfs::Get_Data_Search_Paths().size() == 1);
}

DOCTEST_TEST_CASE("ORIONCD.INI supplies a fallback without overriding local assets") {
    Original_Assets_Fixture fixture;
    fixture.Init();
    fixture.Write("ORIONCD.INI", "cd\r\n");
    fixture.Write_Originals("cd", "cd");

    DOCTEST_SUBCASE("diplomat from CD") {}
    DOCTEST_SUBCASE("diplomat from local install") { fixture.Write("DIPLOMAT.LBX", "local"); }

    DOCTEST_REQUIRE_NOTHROW(MOX2::Set_Mox_Alt_Path());
    DOCTEST_CHECK(vfs::Get_Data_Search_Paths().size() == 2);
    char resolved[512];
    DOCTEST_REQUIRE(vfs::Resolve_Read_Path_No_Zip("diplomat.lbx", resolved, sizeof(resolved)));
    const fs::path expected = fs::exists("DIPLOMAT.LBX") ? "DIPLOMAT.LBX" : "cd/DIPLOMAT.LBX";
    DOCTEST_CHECK(fs::equivalent(resolved, expected));
}

DOCTEST_TEST_CASE("startup reports the missing original asset, not an optional INI") {
    Original_Assets_Fixture fixture;
    fixture.Init();

    DOCTEST_CHECK_THROWS_AS(MOX2::Set_Mox_Alt_Path(), platform::process_exit_requested);
    DOCTEST_CHECK(platform::Exit_Status() == EXIT_FAILURE);
    DOCTEST_CHECK(strstr(platform::Exit_Message(), "The original Master of Orion II files aren't there") != nullptr);
}

DOCTEST_TEST_CASE("startup names the missing original files and where it looked (WP5.10)") {
    Original_Assets_Fixture fixture;
    fixture.Init();
    fixture.Write_Originals("", "local");
    fs::remove(fixture.root / "FONTS.LBX");
    fs::remove(fixture.root / "TANM_150.LBX");
    fixture.Write("fonts.lbx/000", "a folder of entries is a package override, not the original");

    const antares::game_files::s_files_check check = antares::game_files::Check_Original_Files();
    DOCTEST_CHECK_FALSE(check.ok);
    DOCTEST_CHECK(check.missing == std::vector<std::string>{"FONTS.LBX", "TANM_150.LBX"});
    DOCTEST_CHECK_THROWS_AS(MOX2::Set_Mox_Alt_Path(), platform::process_exit_requested);
    const std::string message = platform::Exit_Message();
    DOCTEST_CHECK(message.find("2 of the 353 original Master of Orion II files the game needs are missing: FONTS.LBX, TANM_150.LBX.") != std::string::npos);
    DOCTEST_CHECK(message.find("Looked in:") != std::string::npos);
}

DOCTEST_TEST_CASE("the required list: one entry per file, upper case, the families complete") {
    const std::vector<std::string>& files = antares::game_files::Required_Files();
    DOCTEST_CHECK(files.size() == 353);
    std::vector<std::string> sorted = files;
    std::sort(sorted.begin(), sorted.end());
    DOCTEST_CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    for (const char* name : {"DIPLOMAT.LBX", "PATCH13.LBX", "STREAMHD.LBX", "SR_RC_TR.LBX", "TANM_213.LBX", "BLDG5.LBX"}) {
        DOCTEST_CHECK_MESSAGE(std::find(files.begin(), files.end(), name) != files.end(), name);
    }
}

DOCTEST_TEST_CASE("an explicit game directory does not use the INI fallback") {
    Original_Assets_Fixture fixture;
    fixture.Init();
    fixture.Write("ORIONCD.INI", "cd\r\n");
    fixture.Write("cd/DIPLOMAT.LBX", "cd");
    fixture.Write_Originals("explicit", "explicit");
    DOCTEST_REQUIRE(vfs::Add_Raw_Search_Path("explicit"));
    MOX2::use_explicit_game_directory = true;

    DOCTEST_CHECK_NOTHROW(MOX2::Set_Mox_Alt_Path());
    DOCTEST_CHECK(vfs::Get_Data_Search_Paths().size() == 2);
}
