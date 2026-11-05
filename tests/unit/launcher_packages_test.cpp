#include "pch.h"

#include "antares/package_catalog.h"
#include "antares/profile_file.h"
#include "config/config_loader.h"
#include "config/mod_manager.h"
#include "doctest/doctest.h"
#include "game/vfs.h"
#include "lua/lua_mod_runtime.h"
#include "lua/lua_registry.h"
#include "miniz.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
    class s_launcher_test_directory {
      public:
        s_launcher_test_directory() {
            const auto unique_id = std::chrono::steady_clock::now().time_since_epoch().count();
            root_ = fs::temp_directory_path() / ("antares-launcher-" + std::to_string(unique_id));
            fs::create_directories(root_);
        }
        ~s_launcher_test_directory() {
            orion2re::lua::Reset_Mod_Runtimes();
            orion2re::lua::Reset_Registry();
            config::internal::Reset_Loaded_Mods();
            vfs::Shutdown();
            std::error_code error;
            fs::remove_all(root_, error);
        }
        void Init_Vfs() const {
            DOCTEST_REQUIRE(vfs::Init(root_.string().c_str()) == 1);
        }
        void Write(const fs::path& relative, const std::string& text) const {
            const fs::path path = root_ / relative;
            fs::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            DOCTEST_REQUIRE(output.good());
        }
        void Write_Zip(const fs::path& relative, const std::vector<std::pair<std::string, std::string>>& entries) const {
            const fs::path path = root_ / relative;
            fs::create_directories(path.parent_path());
            mz_zip_archive archive{};
            mz_zip_zero_struct(&archive);
            DOCTEST_REQUIRE(mz_zip_writer_init_file(&archive, path.string().c_str(), 0) == MZ_TRUE);
            for (const auto& [name, text] : entries) {
                DOCTEST_REQUIRE(mz_zip_writer_add_mem(&archive, name.c_str(), text.data(), text.size(), MZ_BEST_SPEED) == MZ_TRUE);
            }
            DOCTEST_REQUIRE(mz_zip_writer_finalize_archive(&archive) == MZ_TRUE);
            DOCTEST_REQUIRE(mz_zip_writer_end(&archive) == MZ_TRUE);
        }
        // A package folder with a manifest; `extra` goes inside the manifest object.
        void Package(const std::string& id, const std::string& extra = "", const std::string& config = "") const {
            Write("data/mods/" + id + "/mod.json5", Manifest(id, extra));
            Write("data/mods/" + id + "/config/main.cfg", config);
        }
        static std::string Manifest(const std::string& id, const std::string& extra = "") {
            return "{ id: \"" + id + "\", name: \"Package " + id + "\", version: \"1.2.0\", "
                                                                   "description: \"About " +
                id + "\", requires: { engine: \">=2.0.0 <3.0.0\" }, "
                     "config: \"config/main.cfg\", " +
                extra + " }\n";
        }

      private:
        fs::path root_;
    };

    const antares::s_package_info* Find_(const std::vector<antares::s_package_info>& packages, const std::string& id) {
        for (const antares::s_package_info& package : packages) {
            if (package.id == id) return &package;
        }
        return nullptr;
    }
} // namespace

DOCTEST_TEST_CASE("launcher profiles: only the enable lines change") {
    using namespace antares::profile;
    const std::string text =
        "# notes\n"
        "include ../antares.cfg;\n"
        "enable alpha;   # the first one wins\n"
        "interface fullscreen = true;\n"
        "  enable beta;\n"
        "enable gamma; enable delta;\n" // not managed: two statements on one line
        "# enable commented;\n";
    DOCTEST_CHECK(Enabled_Packages(text) == std::vector<std::string>{"alpha", "beta"});

    const std::string reordered = With_Enabled_Packages(text, {"beta", "alpha", "new-one"});
    DOCTEST_CHECK(reordered ==
        "# notes\n"
        "include ../antares.cfg;\n"
        "enable beta;\n"
        "enable alpha;\n"
        "enable new-one;\n"
        "interface fullscreen = true;\n"
        "enable gamma; enable delta;\n"
        "# enable commented;\n");
    DOCTEST_CHECK(Enabled_Packages(reordered) == std::vector<std::string>{"beta", "alpha", "new-one"});

    // With no enable lines, they go after the leading comments and includes.
    DOCTEST_CHECK(With_Enabled_Packages("# a\ninclude ../antares.cfg;\n\ncombat x = 1;\n", {"hd"}) ==
        "# a\ninclude ../antares.cfg;\nenable hd;\n\ncombat x = 1;\n");
    DOCTEST_CHECK(With_Enabled_Packages("", {"hd"}) == "enable hd;\n");
    DOCTEST_CHECK(With_Enabled_Packages("include ../antares.cfg;", {"hd"}) == "include ../antares.cfg;\nenable hd;\n");
    DOCTEST_CHECK(With_Enabled_Packages(New_Profile_Text(), {}) == New_Profile_Text());
    // Windows line endings are kept.
    DOCTEST_CHECK(With_Enabled_Packages("include ../antares.cfg;\r\nenable a;\r\n", {"b"}) == "include ../antares.cfg;\r\nenable b;\r\n");
    // Disabling everything removes the lines and nothing else.
    DOCTEST_CHECK(With_Enabled_Packages(text, {}).find("enable alpha") == std::string::npos);
    DOCTEST_CHECK(With_Enabled_Packages(text, {}).find("interface fullscreen = true;") != std::string::npos);

    DOCTEST_CHECK(Is_Valid_Profile_Name("hd"));
    DOCTEST_CHECK(Is_Valid_Profile_Name("ICE x 1.5"));
    DOCTEST_CHECK_FALSE(Is_Valid_Profile_Name(""));
    DOCTEST_CHECK_FALSE(Is_Valid_Profile_Name(".hidden"));
    DOCTEST_CHECK_FALSE(Is_Valid_Profile_Name("../escape"));
    DOCTEST_CHECK_FALSE(Is_Valid_Profile_Name("a/b"));
}

DOCTEST_TEST_CASE("launcher discovery lists folders and zips, with the game's manifest errors") {
    s_launcher_test_directory files;
    files.Package("base-rules", "conflicts: [\"rival\"],", "combat bombardment_hit_frames = 3;\n");
    files.Write_Zip("data/mods/zipped.zip", {{"mod.json5", s_launcher_test_directory::Manifest("zipped")}, {"config/main.cfg", ""}});
    files.Package("art-pack");
    files.Write("data/mods/art-pack/data/art/README.txt", "art");
    files.Write("data/mods/broken/mod.json5", "{ id: \"other\", name: \"x\", version: \"1.0.0\" }");
    files.Write("data/mods/empty/readme.txt", "no manifest");
    files.Package("twice");
    files.Write_Zip("data/mods/twice.zip", {{"mod.json5", s_launcher_test_directory::Manifest("twice")}});
    files.Write("data/mods/.hidden/mod.json5", "{}");
    files.Write("data/mods/notes.txt", "not a package");
    files.Init_Vfs();

    const std::vector<antares::s_package_info> packages = antares::Discover_Packages();
    std::vector<std::string> ids;
    for (const auto& package : packages) ids.push_back(package.id);
    DOCTEST_CHECK(ids == std::vector<std::string>{"art-pack", "base-rules", "broken", "empty", "twice", "zipped"});

    const antares::s_package_info* rules = Find_(packages, "base-rules");
    DOCTEST_REQUIRE(rules != nullptr);
    DOCTEST_CHECK(rules->valid);
    DOCTEST_CHECK_FALSE(rules->archive);
    DOCTEST_CHECK_FALSE(rules->art_only);
    DOCTEST_CHECK(rules->manifest.name == "Package base-rules");
    DOCTEST_CHECK(rules->manifest.version_text == "1.2.0");
    DOCTEST_CHECK(rules->manifest.conflicts == std::vector<std::string>{"rival"});

    const antares::s_package_info* zipped = Find_(packages, "zipped");
    DOCTEST_REQUIRE(zipped != nullptr);
    DOCTEST_CHECK(zipped->valid);
    DOCTEST_CHECK(zipped->archive);
    DOCTEST_CHECK(zipped->manifest.description == "About zipped");

    DOCTEST_CHECK(Find_(packages, "art-pack")->art_only);

    const antares::s_package_info* broken = Find_(packages, "broken");
    DOCTEST_CHECK_FALSE(broken->valid);
    DOCTEST_CHECK(broken->error.find("mods/broken/mod.json5") != std::string::npos);
    DOCTEST_CHECK_FALSE(Find_(packages, "empty")->valid);
    DOCTEST_CHECK(Find_(packages, "empty")->error == "mods/empty/mod.json5: mod manifest not found");
    DOCTEST_CHECK(Find_(packages, "twice")->error == "package 'twice' has both directory and ZIP forms");
}

DOCTEST_TEST_CASE("launcher check agrees with the game's load and leaves nothing active") {
    s_launcher_test_directory files;
    files.Package("base");
    files.Package("addon", "dependencies: [{ id: \"base\", version: \">=1.0.0\" }],");
    files.Package("rival", "conflicts: [\"base\"],");
    files.Package("bad-setting", "", "combat no_such_setting = 1;\n");
    files.Write("data/antares.cfg", "interface show_exit_message = false;\n");
    files.Write("data/profiles/good.cfg", "include ../antares.cfg;\nenable addon;\n");
    files.Write("data/profiles/clash.cfg", "include ../antares.cfg;\nenable rival;\nenable addon;\n");
    files.Write("data/profiles/missing.cfg", "enable nowhere;\n");
    files.Write("data/profiles/setting.cfg", "enable bad-setting;\n");
    files.Init_Vfs();

    const antares::s_config_check good = antares::Check_Config("profiles/good.cfg");
    DOCTEST_CHECK(good.ok);
    DOCTEST_CHECK(good.package_order == std::vector<std::string>{"addon", "base"});
    DOCTEST_CHECK(config::Get_Loaded_Mods().empty()); // a dry run

    for (const char* profile : {"profiles/clash.cfg", "profiles/missing.cfg", "profiles/setting.cfg"}) {
        DOCTEST_CAPTURE(profile);
        const antares::s_config_check check = antares::Check_Config(profile);
        DOCTEST_CHECK_FALSE(check.ok);
        // The game refuses the same file with the same message.
        config::s_effective_config effective;
        config::s_cfg_diagnostic diagnostic;
        DOCTEST_CHECK_FALSE(config::Load_Config_File(profile, "base_game", &effective, &diagnostic));
        DOCTEST_CHECK(check.error == antares::Format_Diagnostic(diagnostic));
    }
    DOCTEST_CHECK(antares::Check_Config("profiles/clash.cfg").error ==
        "mods/rival/mod.json5: package 'rival' conflicts with active package 'base'");
    DOCTEST_CHECK(antares::Check_Config("profiles/missing.cfg").error.find("package not found: 'nowhere'") != std::string::npos);
}
