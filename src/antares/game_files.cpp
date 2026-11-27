#include "pch.h"

#include "antares/game_files.h"

#include "game/vfs.h"
#include "game/vfs_detail.h"

namespace antares::game_files {
    namespace {
        // Generated 2026-11-24 from the engine's LBX names and the GOG 1.31 install (see the header).
        const char* const NAMED_FILES[] = {
            "AMEBAFIN.LBX",
            "ANATKFIN.LBX",
            "ANTARMSG.LBX",
            "ANTAROOM.LBX",
            "ANWINFIN.LBX",
            "APP_PICS.LBX",
            "BEAMS.LBX",
            "BILLTEX2.LBX",
            "BILLTEXT.LBX",
            "BUFFER0.LBX",
            "CMBTFGTR.LBX",
            "CMBTMISL.LBX",
            "CMBTPLNT.LBX",
            "CMBTSFX.LBX",
            "CMBTSHP.LBX",
            "COLBLDG.LBX",
            "COLGCBT.LBX",
            "COLONY2.LBX",
            "COLONY.LBX",
            "COLPUPS.LBX",
            "COLROADS.LBX",
            "COLSUM.LBX",
            "COLSYSDI.LBX",
            "COLVEGGI.LBX",
            "COMBAT.LBX",
            "CONFIRM.LBX",
            "COUNCIL.LBX",
            "COUNCMSG.LBX",
            "CREDITS.LBX",
            "DESIGN.LBX",
            "DIMTVFIN.LBX",
            "DIPLOMAT.LBX",
            "DIPLOMSE.LBX",
            "DIPSTARS.LBX",
            "ESTRINGS.LBX",
            "EVENTMSE.LBX",
            "EVENTS.LBX",
            "FIREPTS.LBX",
            "FLEET.LBX",
            "FLTICONS.LBX",
            "FONTS.LBX",
            "GAME.LBX",
            "GENWINFN.LBX",
            "GSTAR.LBX",
            "HELP.LBX",
            "HERODATA.LBX",
            "HESTRNGS.LBX",
            "INBOX.LBX",
            "INFO.LBX",
            "INTRO.LBX",
            "JIMTEXT2.LBX",
            "JIMTEXT.LBX",
            "KENTEXT1.LBX",
            "KENTEXT.LBX",
            "LOGO.LBX",
            "LOSERFIN.LBX",
            "MAINMENU.LBX",
            "MAINPUPS.LBX",
            "MAINTEXT.LBX",
            "MONSTER.LBX",
            "MULTIGM.LBX",
            "NEWGAME.LBX",
            "OFFICER.LBX",
            "ORIONFIN.LBX",
            "PATCH13.LBX",
            "PLANETS.LBX",
            "PLNTDFIN.LBX",
            "PLNTSUM.LBX",
            "RACEICON.LBX",
            "RACENAME.LBX",
            "RACEOPT.LBX",
            "RACERPRT.LBX",
            "RACESEL.LBX",
            "RACES.LBX",
            "RACESTUF.LBX",
            "REFITPUP.LBX",
            "RSTRING0.LBX",
            "SCIENCE.LBX",
            "SCORE.LBX",
            "SHIPNAME.LBX",
            "SHIPS.LBX",
            "SKILDESC.LBX",
            "SOUND.LBX",
            "SPHERSFX.LBX",
            "STARBG.LBX",
            "STARNAME.LBX",
            "STREAMHD.LBX",
            "STREAM.LBX",
            "TECHDESC.LBX",
            "TECHNAME.LBX",
            "TECHSEL.LBX",
            "TEXTBOX.LBX",
            "TURNSUM.LBX",
            "WARNING.LBX",
            "WININFIN.LBX",
        };
    } // namespace

    const std::vector<std::string>& Required_Files() {
        static const std::vector<std::string> files = [] {
            std::vector<std::string> list(std::begin(NAMED_FILES), std::end(NAMED_FILES));
            char name[32];
            for (int index = 0; index <= 5; ++index) {
                snprintf(name, sizeof(name), "BLDG%d.LBX", index);
                list.push_back(name);
            }
            for (int race = 0; race <= 0xC; ++race) {
                for (const char* kind : {"SC", "SP", "TR"}) {
                    snprintf(name, sizeof(name), "SR_R%X_%s.LBX", race, kind);
                    list.push_back(name);
                }
            }
            for (int tech = 1; tech <= 213; ++tech) {
                snprintf(name, sizeof(name), "TANM_%03d.LBX", tech);
                list.push_back(name);
            }
            return list;
        }();
        return files;
    }

    s_files_check Check_Original_Files() {
        s_files_check check;
        const vfs_detail::s_vfs_state& state = vfs_detail::g_vfs_state;
        if (state.initialized) {
            check.searched.push_back(state.base_path);
            for (const std::string& path : state.raw_search_paths) check.searched.push_back(path);
            if (state.fallback_path[0] != '\0') check.searched.push_back(state.fallback_path);
        }
        for (const std::string& file : Required_Files()) {
            char asset[vfs_detail::VFS_MAX_PATH];
            char resolved[vfs_detail::VFS_MAX_PATH];
            if (!state.initialized || !vfs_detail::Normalize_Asset_Path_(file.c_str(), asset, sizeof(asset)) ||
                !vfs_detail::Resolve_Raw_Asset_Path_(asset, resolved, sizeof(resolved))) {
                check.missing.push_back(file);
            }
        }
        check.ok = state.initialized && check.missing.empty();
        return check;
    }

    std::string Describe(const s_files_check& check) {
        if (check.ok) return "All " + std::to_string(Required_Files().size()) + " original game files found.";
        std::string text;
        if (check.missing.size() == Required_Files().size()) {
            text = "The original Master of Orion II files aren't there (none of the " + std::to_string(check.missing.size()) + " the game needs).";
        } else {
            text = std::to_string(check.missing.size()) + " of the " + std::to_string(Required_Files().size()) +
                " original Master of Orion II files the game needs are missing: ";
            const size_t shown = std::min<size_t>(check.missing.size(), 8);
            for (size_t index = 0; index < shown; ++index) {
                text += (index == 0 ? "" : ", ") + check.missing[index];
            }
            if (check.missing.size() > shown) text += " and " + std::to_string(check.missing.size() - shown) + " more";
            text += ".";
        }
        text += "\nLooked in:";
        for (const std::string& folder : check.searched) text += "\n  " + folder;
        return text;
    }
} // namespace antares::game_files
