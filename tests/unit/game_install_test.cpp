#include "pch.h"

#include "antares/game_files.h"
#include "antares/game_install.h"
#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

// The setup wizard's file work (WP5.11): detection on fake folder trees, the checklist, the copy
// and the save import.
namespace {
    namespace fs = std::filesystem;
    namespace gi = antares::game_install;

    struct s_tree {
        fs::path root = fs::temp_directory_path() / ("antares-install-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        s_tree() {
            fs::create_directories(root);
        }
        ~s_tree() {
            std::error_code error;
            fs::remove_all(root, error);
        }
        std::string Path(const std::string& relative) const {
            return (root / relative).string();
        }
        void Write(const std::string& relative, const std::string& text) const {
            const fs::path path = root / relative;
            fs::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary) << text;
        }
        // A Master of Orion II folder: every required file (one byte each), plus a language file.
        void Game(const std::string& relative, size_t skip = 0) const {
            const auto& files = antares::game_files::Required_Files();
            for (size_t index = skip; index < files.size(); ++index) Write(relative + "/" + files[index], "x");
            Write(relative + "/DIPLOMSG.LBX", "german");
            Write(relative + "/ORION95.EXE", "exe");
        }
    };

    const gi::s_candidate* Find_(const std::vector<gi::s_candidate>& found, const std::string& path) {
        for (const auto& candidate : found) {
            if (candidate.path == path + "/") return &candidate;
        }
        return nullptr;
    }
} // namespace

DOCTEST_TEST_SUITE("setup wizard (WP5.11)") {
    DOCTEST_TEST_CASE("Steam's library list, with Windows paths unescaped") {
        const std::string vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"/home/a/.local/share/Steam\"\n\t}\n"
                                "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\Games\\\\Steam\"\n\t\t\"label\"\t\t\"\"\n\t}\n}\n";
        DOCTEST_CHECK(gi::Parse_Steam_Libraries(vdf) == std::vector<std::string>{"/home/a/.local/share/Steam", "D:\\Games\\Steam"});
    }

    DOCTEST_TEST_CASE("detection finds Steam, GOG and Lutris copies, best first") {
        s_tree tree;
        const std::string home = tree.Path("home");
        tree.Write("home/.local/share/Steam/steamapps/libraryfolders.vdf",
            "\"libraryfolders\" { \"0\" { \"path\" \"" + home + "/.local/share/Steam\" } \"1\" { \"path\" \"" + tree.Path("lib2") + "\" } }");
        tree.Game("lib2/steamapps/common/Master of Orion 2/MOO2");        // a bundle: one folder down
        tree.Game("home/GOG Games/Master of Orion 2", 10);                // a partial copy
        tree.Game("home/Games/moo2/drive_c/GOG Games/Master of Orion 2"); // a Lutris prefix
        tree.Write("home/GOG Games/Other Game/game.dat", "not this");

        const std::vector<gi::s_candidate> found = gi::Detect(gi::Detect_Roots(home));
        const gi::s_candidate* steam = Find_(found, tree.Path("lib2/steamapps/common/Master of Orion 2/MOO2"));
        const gi::s_candidate* gog = Find_(found, tree.Path("home/GOG Games/Master of Orion 2"));
        const gi::s_candidate* lutris = Find_(found, tree.Path("home/Games/moo2/drive_c/GOG Games/Master of Orion 2"));
        DOCTEST_REQUIRE(steam != nullptr);
        DOCTEST_REQUIRE(gog != nullptr);
        DOCTEST_REQUIRE(lutris != nullptr);
        DOCTEST_CHECK(steam->source == "Steam");
        DOCTEST_CHECK(gog->source == "GOG");
        DOCTEST_CHECK(steam->found == 353);
        DOCTEST_CHECK(gog->found == 343);
        DOCTEST_CHECK(found.size() == 3);
        DOCTEST_CHECK(found.back().path == gog->path); // the partial copy last
    }

    DOCTEST_TEST_CASE("detection: Heroic's installed list, a Wine prefix, the registry's folders first") {
        s_tree tree;
        const std::string home = tree.Path("home");
        tree.Game("elsewhere/heroic/Master of Orion 2");
        tree.Write("home/.config/heroic/gog_store/installed.json",
            "{ \"installed\": [ { \"appName\": \"1207661633\", \"install_path\": \"" + tree.Path("elsewhere/heroic/Master of Orion 2") + "\" } ] }");
        tree.Game("home/.wine/drive_c/GOG Games/Master of Orion 2");
        tree.Game("registry/MOO2");
        const std::vector<gi::s_root> roots = gi::Detect_Roots(home, {{tree.Path("registry/MOO2"), "GOG", 1}});
        DOCTEST_REQUIRE_FALSE(roots.empty());
        DOCTEST_CHECK(roots.front().path == tree.Path("registry/MOO2")); // exact places before broad ones
        const std::vector<gi::s_candidate> found = gi::Detect(roots);
        DOCTEST_REQUIRE(found.size() == 3);
        DOCTEST_CHECK(found[0].path == tree.Path("registry/MOO2") + "/");
        DOCTEST_CHECK(Find_(found, tree.Path("elsewhere/heroic/Master of Orion 2"))->source == "Heroic");
        DOCTEST_CHECK(Find_(found, tree.Path("home/.wine/drive_c/GOG Games/Master of Orion 2"))->source == "Wine");
    }

    DOCTEST_TEST_CASE("the checklist: missing, empty, 1.31, a bundle's inner folder") {
        s_tree tree;
        tree.Game("bundle/MOO2");
        gi::s_folder_check check = gi::Check_Folder(tree.Path("bundle"));
        DOCTEST_CHECK(check.ok());
        DOCTEST_CHECK(check.folder == tree.Path("bundle/MOO2") + "/");
        DOCTEST_CHECK(check.patched);
        DOCTEST_CHECK(check.to_copy.size() == 354); // the required files and the German text, not the EXE
        DOCTEST_CHECK(check.bytes == 353 + 6);

        fs::remove(tree.root / "bundle/MOO2/PATCH13.LBX");
        tree.Write("bundle/MOO2/FONTS.LBX", "");
        check = gi::Check_Folder(tree.Path("bundle/MOO2"));
        DOCTEST_CHECK_FALSE(check.ok());
        DOCTEST_CHECK_FALSE(check.patched);
        DOCTEST_CHECK(check.missing == std::vector<std::string>{"PATCH13.LBX"});
        DOCTEST_CHECK(check.empty == std::vector<std::string>{"FONTS.LBX"});

        DOCTEST_CHECK_FALSE(gi::Check_Folder(tree.Path("nowhere")).exists);
    }

    DOCTEST_TEST_CASE("the copy: every LBX, no half files, same-size files kept, cancel") {
        s_tree tree;
        tree.Game("moo2");
        tree.Write("target/fonts.lbx", "old"); // another case and size: replaced
        const gi::s_folder_check check = gi::Check_Folder(tree.Path("moo2"));
        gi::s_copy_progress progress;
        DOCTEST_REQUIRE(gi::Copy_Files(check, tree.Path("target"), &progress));
        DOCTEST_CHECK(progress.finished);
        DOCTEST_CHECK(progress.bytes_done == check.bytes);
        DOCTEST_CHECK(gi::Check_Folder(tree.Path("target")).ok());
        DOCTEST_CHECK_FALSE(fs::exists(tree.root / "target/fonts.lbx"));
        size_t parts = 0;
        for (const auto& entry : fs::directory_iterator(tree.root / "target")) parts += entry.path().extension() == ".part";
        DOCTEST_CHECK(parts == 0);

        gi::s_copy_progress again; // everything there: nothing written
        DOCTEST_REQUIRE(gi::Copy_Files(check, tree.Path("target"), &again));

        gi::s_copy_progress cancelled;
        cancelled.cancel = true;
        DOCTEST_CHECK_FALSE(gi::Copy_Files(check, tree.Path("other"), &cancelled));
        DOCTEST_CHECK(cancelled.failed);
        DOCTEST_CHECK(fs::is_empty(tree.root / "other"));

        gi::s_copy_progress same; // the folder itself: nothing to do
        DOCTEST_CHECK(gi::Copy_Files(check, tree.Path("moo2"), &same));
    }

    DOCTEST_TEST_CASE("saves: found beside the game and in cloud_saves; taken slots kept or replaced") {
        s_tree tree;
        tree.Game("moo2");
        tree.Write("moo2/SAVE1.GAM", "original one");
        tree.Write("moo2/SAVE2.GAM", "older two");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        tree.Write("moo2/cloud_saves/save2.gam", "newer two");
        tree.Write("moo2/SAVE11.GAM", "not a slot");
        tree.Write("antares/SAVE1.GAM", "antares one");

        const std::vector<gi::s_found_save> saves = gi::Find_Saves(tree.Path("moo2"), "", tree.Path("antares"));
        DOCTEST_REQUIRE(saves.size() == 2);
        DOCTEST_CHECK(saves[0].slot == 1);
        DOCTEST_CHECK(saves[0].taken);
        DOCTEST_CHECK(saves[1].slot == 2);
        DOCTEST_CHECK_FALSE(saves[1].taken);
        DOCTEST_CHECK(saves[1].path.find("cloud_saves") != std::string::npos);

        const auto read = [&](const char* name) {
            std::ifstream file(tree.root / "antares" / name, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        };
        std::string error;
        DOCTEST_CHECK(gi::Import_Saves(saves, tree.Path("antares"), false, &error) == 1);
        DOCTEST_CHECK(read("SAVE1.GAM") == "antares one");
        DOCTEST_CHECK(read("SAVE2.GAM") == "newer two");
        DOCTEST_CHECK(gi::Import_Saves(saves, tree.Path("antares"), true, &error) == 2);
        DOCTEST_CHECK(read("SAVE1.GAM") == "original one");
        DOCTEST_CHECK(error.empty());
    }

    DOCTEST_TEST_CASE("Steam: libraries, and the app's manifest while it installs and once it's there") {
        s_tree tree;
        const std::string home = tree.Path("home");
        tree.Write("home/.local/share/Steam/steamapps/libraryfolders.vdf",
            "\"libraryfolders\" { \"0\" { \"path\" \"" + home + "/.local/share/Steam\" } \"1\" { \"path\" \"" + tree.Path("Storage/SteamLibrary") + "\" } }");
        tree.Write("Storage/SteamLibrary/steamapps/.keep", "");
        tree.Write("nosteam/.keep", "");
        const std::vector<std::string> libraries = gi::Steam_Libraries(home);
        DOCTEST_REQUIRE(libraries.size() == 2);
        DOCTEST_CHECK(libraries[1] == tree.Path("Storage/SteamLibrary") + "/");
        DOCTEST_CHECK(gi::Steam_Libraries(tree.Path("nosteam")).empty()); // no Steam: no handoff

        DOCTEST_CHECK_FALSE(gi::Steam_App(libraries, gi::STEAM_APP_ID).known);
        // Downloading (StateFlags 1026: update required and running).
        const std::string manifest = "Storage/SteamLibrary/steamapps/appmanifest_410980.acf";
        tree.Write(manifest, "\"AppState\"\n{\n\t\"appid\"\t\t\"410980\"\n\t\"StateFlags\"\t\t\"1026\"\n\t\"installdir\"\t\t\"Master of Orion 2\"\n"
                             "\t\"BytesToDownload\"\t\t\"400000000\"\n\t\"BytesDownloaded\"\t\t\"100000000\"\n}\n");
        gi::s_steam_app app = gi::Steam_App(libraries, gi::STEAM_APP_ID);
        DOCTEST_CHECK(app.known);
        DOCTEST_CHECK_FALSE(app.installed);
        DOCTEST_CHECK(app.done == 100000000ull);
        DOCTEST_CHECK(app.total == 400000000ull);
        DOCTEST_CHECK(app.folder == tree.Path("Storage/SteamLibrary") + "/steamapps/common/Master of Orion 2/");
        // Installed (StateFlags 4), the files in data/ as on the owner's copy: detection finds them.
        tree.Write(manifest, "\"AppState\" { \"StateFlags\" \"4\" \"installdir\" \"Master of Orion 2\" }");
        DOCTEST_CHECK(gi::Steam_App(libraries, gi::STEAM_APP_ID).installed);
        tree.Game("Storage/SteamLibrary/steamapps/common/Master of Orion 2/data");
        const auto found = gi::Detect(gi::Detect_Roots(home));
        DOCTEST_REQUIRE(found.size() == 1);
        DOCTEST_CHECK(found[0].source == "Steam");
        DOCTEST_CHECK(found[0].path == tree.Path("Storage/SteamLibrary/steamapps/common/Master of Orion 2/data") + "/");
    }
}
