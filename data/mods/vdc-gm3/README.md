# VDC-GM3

VDC-GM3 ("Very Difficult Choice", version X12-V72-B10-GM3) is a complete alternative rule set built for fair, competitive games, especially multiplayer. Every player gets an identical galaxy apart from the home systems, research has no random breakthroughs, and nearly every rule is rebalanced.

## Rules

Without packages the game plays the 1.31 rules. VDC-GM3 is layered on the Basic 1.50 package (v150) for its interface conveniences, then replaces the balance with its own. It is not built on Improved (v150i) and cannot be combined with it or with ICE. The galaxy is mirrored by the required Full Mirror except home systems package (mirror_exhw).

## What it changes

### Races and picks
- You have 35 picks, negative picks are limited to -12, and Evolutionary Mutation grants 5 picks.
- Population Growth, Industry and Research have no penalty choice. All three choices are paid bonuses: Industry +1, +2 or +3 per worker for 6, 12 or 18 picks; Research +1, +2 or +3 for 4, 8 or 13; Growth +65%, +85% or +105% for 6, 8 or 11.
- Ship Attack goes up to +75%. Lithovore and Tolerant cost 22 picks, Unification 18, Democracy 12 and Creative 12; Uncreative refunds 12.
- High-G races have every Swamp planet in their home system made Huge, and a Huge home world with Large Home World.
- Tolerant Subterranean races start on a Gaia home world.
- Lucky races get Gold Deposits on their home world, or the stronger Orion artifacts bonus with Artifacts Home World.
- For human Uncreative players, the starting choice of technologies is adjusted so that key ones (armor, computers, shields, weapons, farming, pollution control) are not all missing.

### Starting conditions and galaxy
- Players start with 75 BC and an Outpost Ship in addition to the normal starting ships. Stars near each home system are revealed at the start.
- Home systems have 5 planets in the fixed 1.50 layout, with an improved fourth planet. Every other system has exactly 3 planets, with no asteroid belts or gas giants.
- More stars per galaxy size: Small 32, Medium 48, Large 56, Huge 71.
- No wormholes, black holes, nebulas, Splinter Colonies or Orion (so no Guardian). Marooned heroes become Pirate Caches, and Ancient Artifacts planets give no free technology.
- Uncolonized Low-G planets become Normal-G, and monster-guarded planets are improved. Specials such as Gold Deposits and Natives are more common.

### Economy and colonies
- Gold Deposits give 10 BC and Gem Deposits 15. Trade Goods convert 100% of production to BC (150% for Fantastic Traders).
- Planets tolerate more production before polluting, and Nano Disassemblers multiply that limit by 8.
- A Core Waste Dump makes a Toxic colony count as Barren. The Weather Controller needs Desert or better, and Biomorphic Fungi add only +1/2 food and only on planets that already grow food.
- Androids are stronger (+4 production, +4 research or +4 food each), and worker and scientist androids cost 75.
- Colony Ships cost 650. Scrapping a ship returns 50% of its cost, refits cost less, and completed freighters give 5 BC.
- Research Labs, Supercomputers and the Galactic Cybernet give more research; Psionics gives +20% morale.
- Governments: Feudal cuts ship costs 27% and Confederation 30%, both also for freighters; Federation gives +75% research and +100% income; Unification gives +60% food and production (Galactic Unification +110%). Democracy and Federation get a morale bonus, and Dictatorship has no morale penalty.
- You may hire any number of leaders, move natives and a colony's last population, and have up to 60 population in transit. Transports have extended range.

### Research and diplomacy
- Research has no random breakthroughs: a technology completes when twice its cost is reached. Points above that are lost, but points are kept when the current technology arrives from artifacts, spying or capture.
- The tech tree is reorganized: fields are reordered and renamed, and technologies are regrouped and re-costed.
- Shields are renamed Class II, IV, VII, XII and XXVI to match their new strengths. Computers are renamed one step up (Optronic becomes Positronic, and so on up to Quantum). Death Spores become Phase Bombs and Stealth Field becomes Cloaking Field.
- The Races screen has no Report button, and technology cannot be exchanged, demanded or offered in diplomacy.
- The Council first meets no earlier than turn 75. An Antares victory is worth 300 points and a Council victory 200.

### Ships and combat
- Tactical combat ends after 25 turns. Ship initiative is always on.
- Defensive fire always triggers against ships within 15 squares and never beyond. Beam range penalties are gentler.
- More Tractor Beams are needed to stop big ships (1 for a frigate up to 32 for a Doom Star), and Black Hole Generators hold their targets continuously.
- Boarding reaches 2 squares without transporters, and marines cannot be transported onto a ship with a Damper Field. A Quantum Detonator always explodes.
- Star Bases, Battlestations and Star Fortresses are one size class larger, fire torpedoes, have more special-system choices and get the Warlord crew bonus. Planetary shields block biological weapons, and better armor technology makes colonies much harder to bomb.
- More command points from orbital bases and communications. Telepaths need at least a Battleship to mind-control a colony.
- Strategic stealth is much weaker than classic invisibility; colony, outpost and transport ships carry your best stealth device.
- Invading a colony has a better chance to capture technology from its buildings.

### AI and difficulty
- The AI gets an extra +1/4 BC per population at every difficulty above Tutor.
- On Hard and Impossible the AI races can roll any of five racial variants, each with extra traits.
- The AI builds Colony Bases before it has warp travel and uses VDC's own ship designs and personalities.

### Antarans and monsters
- Antaran resources are recalculated every 5 turns and they attack far more often; on an Average start the first attack comes around turn 100. Their reserves and the Antaran Star Fortress are much larger.
- Monsters are far tougher: system monsters have 3200 to 4000 hit points and event monsters about 25,000. Bribing a Dragon costs 5000 BC.

### Events
- Random events are much more frequent and no longer wait for turn 50; each event has its own earliest turn, from turn 25 for Earthquakes and Computer Viruses to turn 300 for Unstable Wormholes.
- Donations grow faster over the game.

### Interface
- The build list is colour-coded by building type, and the Colonies screen shows research remaining.
- New ship designs start with your best shield and computer.
- Battle and boarding pop-ups close by themselves, and the colony military pop-up shows rebel troops.

## Keys

| Key | Screen | What changes |
|---|---|---|
| H | Tactical combat | Waits with all your ships. Classic ship initiative is always on (the in-game Ship Initiative option can't switch it off), so H is there in every battle. |
| `+` / `-` | Galaxy map | Zoom further on small and medium galaxies: small maps get one zoom level (none without packages), medium maps two (one without packages). |
| Alt+A, Alt+D | Build | Auto Design still redesigns every ship, but follows VDC-GM3's AI design rules, so the designs differ. |

## Needs and conflicts

- Requires Basic 1.50 (v150) and Full Mirror except home systems (mirror_exhw); both are enabled automatically.
- Conflicts with Improved (v150i), ICE, ICE-X, Full Mirror (mirror) and Only Home Systems Mirrored (mirror_hw).
- VDC-GM3 and mirror_exhw run scripts when a new game starts, so in multiplayer every player needs the same versions of all three packages.

## Saves

Saved games record this package, and it must be installed to load them.
