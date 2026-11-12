# Antares UI

The Godot build's own interface (D27), shown when the setting `interface legacy_ui` is off (the
launcher's "Legacy interface", on by default; ticking a theme turns it off). The native galaxy, in place of
the original galaxy screen:
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
The classic build (`antares`) doesn't have it.

With the legacy interface on, the Godot build looks and plays exactly like the original.

It is also the starting point for people who want to make their own interface: everything in it
is a Godot scene or script you can open and change (the game ships it in `ui-source/`). See the
modding guide's "UI packages" (`docs/content/authoring-mods/ui-packages.md`).

## Keys

| Key | Screen | What changes |
|---|---|---|
| Ctrl+G | Galaxy | Switches between this package's galaxy and the original one, for comparing (no button). |
| Mouse wheel, drag | Galaxy | Zooms towards the pointer and moves the map. |
| Esc | Galaxy | Closes the system pane, the design list, the options or the game menu. |

The original galaxy's keys (F1 to F6, F9, Alt+V, Alt+F1 to Alt+F8, T, G) do what they do there,
and each also has a button on the map. Zoom is the mouse wheel.

## Themes and art

Its look comes from an enabled theme package (Chrome or Glass); without one it uses Godot's plain
look. It uses the HD art (Battle at Antares HD) when that package is enabled, and the original art
otherwise.

## Saves

Saves don't record the native interface or its themes: they change only how the game is shown.
