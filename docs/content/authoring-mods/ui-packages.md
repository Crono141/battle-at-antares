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

### When a UI package and an art pack have the same image

The package higher in the launcher's list wins, image by image; images it doesn't have come from
the next package down. A UI package with five new star sprites in `data/art/`, enabled above
`antares-hd`, shows its five stars (on its own views and the original screens alike) and the HD
pack's art for everything else; enabled below `antares-hd`, the HD stars win. Godot files at the
same path in two packages follow the same rule. The player sets the order, so say in your README
where your package should go. Reordering packages while the game runs takes effect for game
sprites at once, and for Godot files at the next start.

## Themes

The look of `antares-ui` comes from **theme packages**: `antares-ui-chrome` (the original
screens' metal, small) and `antares-ui-glass` (dark glass panels) ship with the game. Enable any
number of them with `antares-ui`; Options > Theme in the native galaxy switches between the
enabled ones while playing, and remembers the choice. The first one enabled is the default. With
none enabled, `antares-ui` uses Godot's own look.

A theme package is a UI package without a `frontend.tscn`: its zip holds
`packages/<id>/theme.gd`, a script with one function,

```gdscript
extends RefCounted
func build(dir: String) -> Dictionary  # dir is res://packages/<id>
```

that returns `{}` when it can't build (missing art), or:

| Key | What it is |
|---|---|
| `name` | what the selector shows |
| `theme` | a Godot `Theme` put on every native view |
| `frame` | a `StyleBox` drawn round the whole view, over it, or `null` |
| `inset` | the frame's thickness: edge controls sit inside it |
| `edge_gap` | the gap between the buttons and slots along an edge (0: side by side) |
| `bar_height` | the top bar's height; its icons are 28 pixels |
| `pane_gap` | the gap between a pane and the screen's edge (or the frame) |

The views ask the theme for these type variations: `Pane`, `Row`, `RowOpen`, `Slot`, `Pocket`
(behind the top bar), `BottomPocket` (behind the fleet and colony buttons) and `Portrait`
(`PanelContainer`); `SlotButton` (the bar's figures), `SideTab` (the right edge's tabs) and
`Toggle` (`Button`); `Heading`, `Name`, `Dim` and `Section` (`Label`). An armed button (a pressed
toggle) letters in the button's `font_pressed_color`, glowing with the `Button` constant `glow`
and colour `glow_color`. `packages/antares-ui/themes.gd` has the details;
`packages/antares-ui-glass/theme.gd` is a short example made of `StyleBoxFlat`s.

A theme reads its own files (images, fonts) as files, not as imported Godot resources:
`Image.load_from_file()` and `FontFile.load_dynamic_font()` on paths under `dir`. So a plain zip
of the folder works, with no Godot export. The chrome theme's metal is cut from the player's own
game by the workspace tool `tools/themeart.py` into its `art/` folder, which the source repository
doesn't carry; fonts under an open licence can ship as they are, with their licence beside them.
