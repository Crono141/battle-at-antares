# Antares UI

The native galaxy for the Godot build (`antares-godot`), in place of the original galaxy screen:
a map you can zoom and drag. Clicking a star flies the map to it, shows its planets on their
orbits and opens the system pane on the left (planets, colony potential, morale, governor and the
fleets in orbit). The empire's figures and Turn are along the top (rest the pointer on the
treasury for the tax rate; Game drops the game menu), the screens are tabs on the right edge, Ship
Design lists your designs and Options holds the map's influence view, merging relocations, the
distance tool, the game options, the music and sound volumes and the theme. The research and
command points figures drop their panels down from the bar: research is chosen there. The fleet and colony buttons are along the bottom.
Its look comes from a theme package (Antares UI: Chrome or Antares UI: Glass; enable either or
both, and switch in Options > Theme). Screens it
doesn't redraw yet (colonies, the ship designer, saving and loading and the rest) open as the
original screens.
It does nothing in the classic build (`antares`).

With this package off, the Godot build looks and plays exactly like the original.

It is also an example for people who want to make their own interface: everything in it is a
Godot scene or script you can open and change. See "Making a UI package" in the modding guide
(`docs/content/authoring-mods/ui-packages.md`).

## Keys

| Key | Screen | What changes |
|---|---|---|
| Ctrl+G | Galaxy | Switches between this package's galaxy and the original one, for comparing (no button). |
| Mouse wheel, drag | Galaxy | Zooms towards the pointer and moves the map. |
| Esc | Galaxy | Closes the system pane, the design list, the options or the game menu. |

The original galaxy's keys (F1 to F6, F9, Alt+V, Alt+F1 to Alt+F8, T, G) do what they do there,
and each also has a button on the map. Zoom is the mouse wheel.

## Needs and conflicts

No dependencies. Enable a theme package with it (Chrome or Glass); without one it uses Godot's
plain look. Uses the HD art (Battle at Antares HD) when that package is enabled, and the
original art otherwise.

## Saves

Saves don't record this package: it changes only how the game is shown.
