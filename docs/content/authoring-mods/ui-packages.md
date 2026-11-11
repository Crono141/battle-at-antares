# UI packages (Godot build)

The Godot build of Battle at Antares (`antares-godot`) shows every screen exactly as the original
does. A **UI package** replaces some of those screens with its own views, made as Godot scenes
and scripts. The game's rules don't change: the views read the game through queries and act
through commands, the same calls the original screens make.

`antares-ui` ships with the game as the first UI package, and as an example to copy or change. It
replaces the galaxy screen with a map you can zoom and drag, with panels over it. It is off by
default; enable it like any other package. A UI package doesn't change saves: saves don't record
it, and a save loads with or without it.

## How it fits together

- A UI package is an ordinary package (see [Packages and manifests](packages-and-manifests.md))
  with its Godot files in `godot/`, as a `.zip` (or a `.pck` exported by Godot).
- Inside that zip, every file sits under `packages/<package id>/`, which is its path in the Godot
  project (`res://packages/<package id>/...`).
- The entry point is `packages/<package id>/frontend.tscn`. When the package is enabled, the game
  creates that scene over the original screen. Its root script says which screens it draws
  (`claimed_screens`, the game's `SCREEN_*` numbers) and which pop-up kinds it answers
  (`claimed_requests`); everything else stays the original screen.
- If several UI packages are enabled, the one with the highest priority (listed first) is used.
- Packages also replace single files: any enabled package whose zip holds a file at the same path
  replaces it, the highest priority winning. A package that only restyles the fleet panel ships
  `packages/antares-ui/views/fleet_panel.tscn` and nothing else (enable it above `antares-ui`).

```text
data/mods/my-ui/
  mod.json5                          id, name, version, requires.engine, config
  config/main.cfg                    required, may be empty
  README.md                          optional: shown by the launcher
  godot/my-ui.zip
      packages/my-ui/frontend.tscn   the entry scene
      packages/my-ui/frontend.gd     its script: claimed screens and requests, the view logic
      packages/my-ui/views/...       panels, one scene and script each
```

Keep the package free of rule changes (an empty `config/main.cfg`, no Lua, no dependencies), so
the game treats it as art-only: saves don't record it, and players can switch it on and off
freely. A dependency, even an optional one, makes saves record the package.

## Making one

You need the Godot 4.7 editor (one download from godotengine.org, nothing to install) and the
game's source, which comes with the game (`source/`) and is on GitHub.

1. Open `frontend/godot/project/` in the Godot editor. Copy `libantares.so` (Linux) or
   `antares.dll` (Windows) from your game folder into the project's `bin/` folder first, so the
   editor knows the game's node types.
2. To start from `antares-ui`, copy `packages/antares-ui/` to `packages/my-ui/`, change `id` and
   `name` in its `mod.json5`, and fix the paths at the top of `frontend.tscn` (the
   `ext_resource` lines name `res://packages/antares-ui/...`; scripts use relative paths and need
   no change).
3. Change the scenes in the editor: move panels, change colours and fonts, add buttons. Each view
   is a plain scene with named nodes; its script lists the commands and queries it uses.
4. Zip the folder so the paths inside start with `packages/my-ui/`. From the project folder:
   `zip -r my-ui.zip packages/my-ui` (or any zip tool, keeping the folder). No Godot export is
   needed: scenes, scripts and shaders are text, and the game loads them from the zip as they are.
5. Put `mod.json5`, `config/main.cfg` and `godot/my-ui.zip` in `data/mods/my-ui/`, enable it in
   the launcher above (or instead of) `antares-ui`, and start the Godot build.

Images you add (a panel frame, an icon) are the exception to step 4: Godot converts them when it
imports them, so export a `.pck` from the editor instead (Project, Export, Export PCK/ZIP, with
only your package's folder selected) and put that in `godot/`.

The commands and queries a view can use are listed in the engine notes
(`notes/antares/native-bridge.md` in the workspace; the view scripts in `antares-ui` show each one
in use).

## Art: the game's sprites and a UI package's own images

There are two kinds of art, and they never replace each other:

- **The game's sprites** (stars, planets, ships, the original panel frames) come from the game's
  LBX files, or from an [HD art pack](art-packs.md) when one has the sprite. A view asks for them
  by LBX name and entry number (`art.sprite("BUFFER0.LBX", 3)`), and gets the HD version when the
  enabled art packs have one. Which art pack wins is the art packs' own rule: the first enabled
  pack with the sprite.
- **A UI package's own images** (its frames, buttons, icons, fonts, a theme) are Godot files in
  its zip or pack, used by its scenes like any Godot resource. They are not game sprites, so art
  packs never touch them.

So a UI package decides per element which kind it uses. A panel that draws the game's box art via
`art.sprite()` picks up the HD pack, or a player's own art pack, automatically. A panel with its
own frame image shows that image whatever art packs are enabled.

If a UI package wants to change a **game sprite** (a different star or ship picture), it ships it
the art pack way, in `data/art/<LBX>/<entry>/`, not in its Godot files. Then the original screens
show it too, and it takes part in the art packs' order: enable the package above `antares-hd` for
its sprites to win. `data/art/` keeps the package art-only.
