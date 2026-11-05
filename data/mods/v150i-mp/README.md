# Improved Multiplayer

The 1.50 Improved rules tuned for network games between human players: multiplayer race-pick costs, a fairer map with identical home systems, and no technology trading.

## Rules

Everything from Improved (v150i) and Basic (v150), plus the multiplayer changes below. See the Improved package for the full list of rule changes from 1.31.

## What it changes

Races:
- Multiplayer race-pick costs replace the single-player ones. Negative picks are limited to 10 again (Improved allows 12).
- Compared with Improved, most bonuses cost less and most penalties give more, for example Uncreative -10, Feudal -10, Low-G -7, Telepathic 5, Creative 7, and +50% population growth for 2.
- Democracy costs 6 instead of 7; Unification stays at 8.

Galaxy:
- Every home system has at least 5 planets.
- Home systems are mirrored: one player's home system, chosen at random, is copied to every other player (this comes from the Home Systems Mirrored package).
- No wormholes, no Orion system (and no Guardian), and no Splinter Colonies.

Diplomacy:
- The Report button on the Races screen is hidden, and technology can't be exchanged, demanded or offered in an audience.

Interface:
- Combat confirmation messages ("X attacks Y", outpost destroyed, colony taken by mind control) close after about 5 seconds.
- Raid and capture results after boarding close on their own.

## Keys

Same as Improved: W lets each ship wait once per combat turn, and H (wait with all ships) is available in every battle.

## Needs and conflicts

Needs Improved (v150i, which brings Basic) and Home Systems Mirrored (mirror_hw); enabling this package enables them. Can't be combined with Full Mirror or Full Mirror except home systems, because only one package may generate the galaxy. All players need the same package set, and the scripts must match on all machines.

## Saves

Saves record this package and need it enabled to load.
