#include "pch.h"

#include "antares/package_catalog.h"
#include "antares/profile_file.h"
#include "antares/settings_docs.h"
#include "config/cfg_parser.h"
#include "config/config_decode.h"
#include "config/config_runtime.h"
#include "config/config_loader.h"
#include "config/mod_manager.h"
#include "doctest/doctest.h"
#include "game/vfs.h"
#include "lua/lua_mod_runtime.h"
#include "lua/lua_registry.h"
#include "miniz.h"

#include <chrono>
#include <cstring>
#include <iterator>
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

DOCTEST_TEST_CASE("launcher settings: every editable default round-trips through the game's parser") {
    config::s_config_state defaults;
    DOCTEST_REQUIRE(config::Initialize_Defaults(&defaults));
    size_t editable = 0;
    size_t formatted = 0;
    std::vector<std::string> failures;
    for (config::t_variable_id id = 0; id < config::variable_count; ++id) {
        if (!antares::profile::Is_Editable_Setting(id)) continue;
        ++editable;
        config::s_typed_value value;
        DOCTEST_REQUIRE(config::Read_State_Value(&defaults, id, &value));
        std::string text;
        if (!antares::profile::Format_Setting_Value(id, value, &text)) continue;
        ++formatted;
        const std::string line = antares::profile::Setting_Path(id) + " = " + text + ";";
        config::s_cfg_document document;
        config::s_cfg_diagnostic diagnostic;
        config::s_config_write_set writes;
        const bool decoded = config::Parse_Cfg("test", line.data(), line.size(), &document, &diagnostic) &&
            document.statements.size() == 1 && config::Decode_Assignment(&document.statements[0], &writes, &diagnostic) &&
            writes.writes.size() == 1;
        if (!decoded || writes.writes[0].variable_id != id || !config::Config_Values_Equal(writes.writes[0].value, value)) {
            if (failures.size() < 10) failures.push_back(line + "  -> " + diagnostic.message);
            continue;
        }
        DOCTEST_CHECK(antares::profile::Setting_Line_Variable(line) == id);
    }
    for (const std::string& failure : failures) DOCTEST_MESSAGE(failure);
    DOCTEST_CHECK(failures.empty());
    DOCTEST_MESSAGE("editable settings: " << editable << ", formatted: " << formatted);
    DOCTEST_CHECK(editable > 17000);
    DOCTEST_CHECK(formatted == editable);
}

DOCTEST_TEST_CASE("launcher settings: setting lines are replaced, added and removed") {
    using namespace antares::profile;
    config::t_variable_id fullscreen = config::CONFIG_NO_VARIABLE;
    config::t_variable_id width = config::CONFIG_NO_VARIABLE;
    DOCTEST_REQUIRE(config::Find_Variable("interface.fullscreen", 20, &fullscreen));
    DOCTEST_REQUIRE(config::Find_Variable("interface.window_width", 22, &width));
    const std::string text =
        "include ../antares.cfg;\n"
        "enable hd;\n"
        "interface   fullscreen=false;  # mine\n"
        "combat bombardment_hit_frames = 5;\n"
        "interface fullscreen = true;\n";
    DOCTEST_CHECK(Setting_Line_Variable("interface   fullscreen=false;  # mine") == fullscreen);
    DOCTEST_CHECK(Setting_Line_Variable("enable hd;") == config::CONFIG_NO_VARIABLE);
    DOCTEST_CHECK(Setting_Line_Variable("# interface fullscreen = true;") == config::CONFIG_NO_VARIABLE);
    DOCTEST_CHECK(Profile_Settings(text).size() == 2);

    DOCTEST_CHECK(With_Setting(text, fullscreen, "true") ==
        "include ../antares.cfg;\n"
        "enable hd;\n"
        "interface fullscreen = true;  # mine\n"
        "combat bombardment_hit_frames = 5;\n");
    DOCTEST_CHECK(With_Setting(text, fullscreen, "") ==
        "include ../antares.cfg;\n"
        "enable hd;\n"
        "combat bombardment_hit_frames = 5;\n");
    DOCTEST_CHECK(With_Setting("enable hd;", width, "1280") == "enable hd;\ninterface window_width = 1280;\n");
    DOCTEST_CHECK(With_Setting("", width, "1280") == "interface window_width = 1280;\n");
    // The enable lines are untouched by settings, and settings by enable changes.
    DOCTEST_CHECK(Enabled_Packages(With_Setting(text, width, "800")) == std::vector<std::string>{"hd"});
    DOCTEST_CHECK(Profile_Settings(With_Enabled_Packages(text, {})).size() == 2);
}

DOCTEST_TEST_CASE("launcher settings: docs come from the schema") {
    std::ifstream file(ANTARES_CONFIG_SCHEMA_PATH, std::ios::binary);
    DOCTEST_REQUIRE(file.good());
    const std::string schema((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    antares::settings::c_schema_docs docs;
    std::string error;
    DOCTEST_REQUIRE(docs.Load(schema, &error));
    DOCTEST_CHECK(docs.Describe("interface.window_width").title == "Window width");
    DOCTEST_CHECK(docs.Describe("combat.bombardment_hit_frames").description.find("bomb hits") != std::string::npos);
    const antares::settings::s_setting_doc nested = docs.Describe("interface.animation.cloak_steps.fast");
    DOCTEST_CHECK(nested.found);
    DOCTEST_CHECK(nested.title == "Interface animation settings › Cloak animation steps › Fast");
    DOCTEST_CHECK(nested.description == "Frame increment for fast and network cloak animations.");
    DOCTEST_CHECK(docs.Category_Of("combat") == "tactical_combat");
    DOCTEST_CHECK(docs.Categories().size() >= 5);
    // Every setting gets a title, and nearly all are documented.
    size_t found = 0;
    for (config::t_variable_id id = 0; id < config::variable_count; ++id) {
        std::string name;
        DOCTEST_REQUIRE(config::Get_Variable_Name(id, &name));
        const antares::settings::s_setting_doc doc = docs.Describe(name);
        DOCTEST_CHECK_FALSE(doc.title.empty());
        if (doc.found) ++found;
    }
    DOCTEST_MESSAGE("documented settings: " << found << " of " << config::variable_count);
    DOCTEST_CHECK(found * 100 >= config::variable_count * 95);
}

DOCTEST_TEST_CASE("launcher check returns each setting's value and where it came from") {
    s_launcher_test_directory files;
    files.Package("pace", "", "combat bombardment_hit_frames = 4;\n");
    files.Write("data/antares.cfg", "interface window_width = 1024;\n");
    files.Write("data/profiles/p.cfg", "include ../antares.cfg;\nenable pace;\ninterface fullscreen = true;\n");
    files.Write("data/profiles/mine.cfg", "enable pace;\ncombat bombardment_hit_frames = 7;\n");
    files.Init_Vfs();
    const auto value_of = [](const antares::s_config_check& check, const char* name) {
        config::t_variable_id id = config::CONFIG_NO_VARIABLE;
        DOCTEST_REQUIRE(config::Find_Variable(name, std::strlen(name), &id));
        config::s_typed_value value;
        DOCTEST_REQUIRE(config::Read_State_Value(&check.effective->state, id, &value));
        return std::make_pair(value, check.effective->provenance[id]);
    };
    const antares::s_config_check check = antares::Check_Config("profiles/p.cfg");
    DOCTEST_REQUIRE(check.ok);
    DOCTEST_REQUIRE(check.effective != nullptr);
    const auto [fullscreen, fullscreen_from] = value_of(check, "interface.fullscreen");
    DOCTEST_CHECK(fullscreen.bool_value);
    DOCTEST_CHECK(fullscreen_from.location.source == "profiles/p.cfg");
    const auto [width, width_from] = value_of(check, "interface.window_width");
    DOCTEST_CHECK(width.integer_value == 1024);
    DOCTEST_CHECK(width_from.location.source == "antares.cfg");
    const auto [pace, pace_from] = value_of(check, "combat.bombardment_hit_frames");
    DOCTEST_CHECK(pace.integer_value == 4);
    DOCTEST_CHECK(pace_from.package_id == "pace");
    // The game applies packages after the config file, so a package's value wins over the
    // profile's own line; the launcher shows this rather than hiding it.
    const antares::s_config_check mine = antares::Check_Config("profiles/mine.cfg");
    DOCTEST_REQUIRE(mine.ok);
    DOCTEST_CHECK(value_of(mine, "combat.bombardment_hit_frames").first.integer_value == 4);
    DOCTEST_CHECK(value_of(mine, "combat.bombardment_hit_frames").second.package_id == "pace");
}
