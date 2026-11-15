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
The Colonies tab (or C) opens the colonies as a table on the right with the whole galaxy beside
it: click a column's title to sort, move colonists between jobs or to another colony as on the
original screen, buy, and click a colony's Building slot for its build list (the system's button
then reads RETURN TO COLONIES). A click on a star picks its colony's row.
The Planets tab (or P) lists the planets you could settle, with the original's five toggles and
sortable columns; arm SEND COLONY SHIP or SEND OUTPOST SHIP and click a planet to send the
nearest free ship, and click a ship's marker in a row to turn it back.
The Fleets tab (or F) shows your fleets one at a time (◀ ▶), their ships with the Support and
Combat filters and the hovered ship's details; pick ships (ALL picks them all) and click a star to
send them, SCRAP scraps them, RELOCATE takes the star to relocate from and then where to.
Its look comes from a theme package (Antares UI: Chrome or Antares UI: Glass; enable either or
both, and switch in Options > Theme). Screens it
doesn't redraw yet (leaders, races, the ship designer, saving and loading and the rest) open as the
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
| Esc | Galaxy | Closes the system pane, the Colonies, Planets or Fleets panel, the design list, the options or the game menu. |
| C | Galaxy | Opens or closes the Colonies panel. |
| P | Galaxy | Opens or closes the Planets panel. |
| F | Galaxy | Opens or closes the Fleets panel. |

The original galaxy's keys (F1 to F6, F9, Alt+V, Alt+F1 to Alt+F8, T, G) do what they do there,
and each also has a button on the map. Zoom is the mouse wheel.

## Themes and art

Its look comes from an enabled theme package (Chrome or Glass); without one it uses Godot's plain
look. It uses the HD art (Battle at Antares HD) when that package is enabled, and the original art
otherwise.

## Saves

Saves don't record the native interface or its themes: they change only how the game is shown.
