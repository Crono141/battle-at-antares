# Scripted render suite (WP0.4)

Input scripts that visit every screen and save screenshots of the 4x HD frame
(`screenshot_hd`, always 2560x1920) and the 640x480 page (`screenshot_page`).
The runner is in the Moo2Mods workspace: `tools/antares-test/run.py` (it needs the private
saves in `work/fixtures/` and the HD pack in `work/hdpack/`).

    tools/antares-test/run.py                 # all scripts, about 4 minutes
    tools/antares-test/run.py --only combat   # one script

Results: `work/antares-test/runs/latest/` with `summary.json`, `contact-sheet.png` and one folder
per script (screenshots, `game.log`). The exit status is non-zero when a script times out, a
screenshot is missing, or an `expect_screen`/`wait_input` gives up.

## Writing a script

Header lines: `# args:` (options after `-skip-intro`), `# fixture:` (a save copied in first),
`# timeout:` (seconds). `$OUT` is the script's output folder. Commands are listed in
`src/game/platform.cpp` above `s_script_step`. Coordinates are 640x480 game space.

Waiting, most reliable first:
- `expect_screen <lbx>`: the target screen has started (an LBX it loads was opened after the last
  input). Use an LBX the previous screen doesn't load: the galaxy is `STARBG.LBX`, because
  `BUFFER0.LBX` already loads at startup.
- `wait_input [n]`: the screen polled input n times with no LBX load and no change of field
  count in between, so it has settled (dialogs included). Screens that stream art while they
  animate never settle (ship design, combat, the race portraits): use `wait <ms>` there.
- `wait <ms>` and `wait_frames <n>` as a last resort.

Gotchas: AUDIENCE (races) and DESIGN (build list) are modes; click the race or ship after them.
