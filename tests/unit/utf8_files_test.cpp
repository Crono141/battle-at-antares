#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "antares/utf8_files.h"

// WP5.15: a user folder under a non-ASCII name ("C:\Users\Zoë", "Олег") must open on every system.
TEST_SUITE("utf8 files") {
    TEST_CASE("UTF-8 names round-trip and open") {
        const std::string name = "antares-utf8-test-Zoë-Олег-測試";
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / antares::utf8::Path(name);
        std::error_code error;
        std::filesystem::remove_all(folder, error);
        DOCTEST_REQUIRE(std::filesystem::create_directories(folder, error));
        const std::string folder_text = antares::utf8::Text(folder);
        DOCTEST_CHECK(folder_text.find(name) != std::string::npos);
        DOCTEST_CHECK(antares::utf8::Path(folder_text) == folder);

        const std::string file = folder_text + "/sävé.txt";
        FILE* out = antares::utf8::Open(file.c_str(), "wb");
        DOCTEST_REQUIRE(out != nullptr);
        std::fputs("ok", out);
        std::fclose(out);
        DOCTEST_CHECK(std::filesystem::is_regular_file(antares::utf8::Path(file)));
        FILE* in = antares::utf8::Open(file.c_str(), "rb");
        DOCTEST_REQUIRE(in != nullptr);
        char text[3] = {};
        DOCTEST_CHECK(std::fread(text, 1, 2, in) == 2);
        std::fclose(in);
        DOCTEST_CHECK(std::string(text) == "ok");
        DOCTEST_CHECK(antares::utf8::Remove(file.c_str()) == 0);
        DOCTEST_CHECK(!std::filesystem::exists(antares::utf8::Path(file)));

        const std::filesystem::path back = std::filesystem::current_path();
        DOCTEST_CHECK(antares::utf8::Change_Directory(folder_text.c_str()) == 0);
        DOCTEST_CHECK(std::filesystem::equivalent(std::filesystem::current_path(), folder, error));
        std::filesystem::current_path(back);
        std::filesystem::remove_all(folder, error);
    }
}
