# The interface's source

`project/` is the Godot project of the Godot build (`antares-godot`): the host that runs the game
(`main.gd`) and the built-in native interface, **Antares UI**, in `project/packages/antares-ui/`.
It is the same set of scenes and scripts the game uses, so it's the place to start when you want
to move things around on screen.

The game has three layers:

1. **The engine** (`antares-godot`'s library): the rules, the saves, the original screens, and the
   bridge the interface talks through: queries it reads (the galaxy, a system, research…) and
   commands it sends (move a fleet, choose research, set the tax rate…).
2. **The layout**, Antares UI: which panels exist, where they sit and what they do. Turn the
   legacy interface off in the launcher (Settings, "Legacy interface") to see it.
3. **The theme**, a package such as Antares UI: Chrome or Antares UI: Glass: how the panels and
   buttons look. Any layout that keeps the theme names (`Pane`, `Slot`, `SideTab`… see
   `packages/antares-ui/themes.gd`) works with any theme.

## Opening it

1. Install the Godot 4.7 editor (one download from godotengine.org).
2. Copy the game's Godot library into `project/bin/`: `libantares.so` (Linux) or `antares.dll`
   (Windows), from the game folder. The editor needs it to know the game's node types.
3. Open `project/project.godot` in the editor.

## Changing the layout

- **A few panels:** make a package (see the modding guide, "UI packages") whose Godot zip holds
  just the files you changed at the same paths, for example
  `packages/antares-ui/views/side_tabs.tscn`. Enable it in the launcher; it replaces those files
  and the rest stays as shipped.
- **A new interface:** copy `packages/antares-ui/` to `packages/<your package id>/`, change what
  you like, and ship it as a package with its `frontend.tscn`. The game uses it in place of the
  built-in one when the legacy interface is off.

Either way, themes keep working as long as your panels use the theme names.
