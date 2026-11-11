# Antares UI

The native galaxy for the Godot build (`antares-godot`), in place of the original galaxy screen:
a map you can zoom and drag, with the star system, fleet and empire panels over it. Screens it
doesn't redraw yet (colonies, research, ship design and the rest) open as the original screens.
It does nothing in the classic build (`antares`).

With this package off, the Godot build looks and plays exactly like the original.

It is also an example for people who want to make their own interface: everything in it is a
Godot scene or script you can open and change. See "Making a UI package" in the modding guide
(`docs/content/authoring-mods/ui-packages.md`).

## Keys

| Key | Screen | What changes |
|---|---|---|
| Ctrl+G | Galaxy | Switches between this package's galaxy and the original one, for comparing. |
| Mouse wheel, drag | Galaxy | Zooms towards the pointer and moves the map. |

The original galaxy's keys (F1 to F6, Alt+V, + and −) do what they do there, and each also has a
button on the map.

## Needs and conflicts

No dependencies. Uses the HD art (Battle at Antares HD) when that package is enabled, and the
original art otherwise.

## Saves

Saves don't record this package: it changes only how the game is shown.
