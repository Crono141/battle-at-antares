# Full Mirror except home systems

Builds a galaxy in which every player gets a mirrored copy of the same neighbourhood, while each home system keeps its own planets. For fair multiplayer games that still let races differ at home.

## Rules

Changes only how the galaxy is generated; combine it with any rule set.

## What it changes

- The galaxy is split into equal sectors: 2 side by side for two players, 2 by 2 for up to four, 3 by 2 for up to six and 4 by 2 for seven or eight.
- One player's surroundings, the one that best fits a sector, become the template: the home system and the nearest stars that fit in a sector.
- Every other player's sector gets a mirrored copy of the template's other stars, with the same planets, specials, wormholes and guarding monsters.
- Home systems are not copied: each player keeps the planets generated for their own home system. Home stars do take the template's star colour.
- Stars outside the template are removed, so the galaxy may end up with fewer stars than the size you chose.
- A marooned hero in the template is copied as a different hero in each copy.
- Nebulas are moved to symmetric positions.

## Needs and conflicts

Required by VDC-GM3. Can't be combined with Full Mirror, Home Systems Mirrored or Improved Multiplayer: only one package may generate the galaxy. Uses a script: in multiplayer the host generates the galaxy, every player needs the same package set, and the script must match on all machines.

## Saves

Saves record this package and need it enabled to load.
