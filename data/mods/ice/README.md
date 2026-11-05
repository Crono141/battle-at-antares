# ICE

ICE (version 35) is a complete alternative rule set for single-player games against a tougher AI. It rebuilds race picks, the technology tree, the galaxy map, the economy, combat and the Antarans, and should be played as a whole game profile rather than a small tweak.

## Rules

Without packages the game plays the 1.31 rules. ICE is layered on the Basic 1.50 package (v150), so you keep the 1.50 interface conveniences, and then replaces most of the balance with its own. It does not use the Improved (v150i) rules; it brings its own versions of the ideas found there, such as strict ship initiative and carried-over research. For the hardest AI and Antarans, enable ICE-X on top of ICE.

## What it changes

### Races and picks
- You have 25 picks instead of 10, can take negative picks down to -25, and Evolutionary Mutation grants 8 picks. All costs are rescaled to match.
- Relative to that larger budget, Lithovore, Tolerant, Creative, Democracy and Unification are expensive, while Lucky, Cybernetic, Stealthy Ships, Fantastic Traders and Large Home World are cheap. Uncreative refunds 17 picks.
- Ship Defense bonuses are smaller (+20% and +40%); Ship Attack is -25%, +25% or +50%.
- The 13 stock races are redefined for the new pick system.
- High-G races start on a Large home world, or a Huge one with Large Home World.
- Lucky races get Gold Deposits on their home world. With Artifacts Home World they get the stronger Orion artifacts bonus instead.
- AI races that are both Lucky and Low-G start on a Small Gaia home world.
- Uncreative races that cannot get any shield technology have Shield Capacitors, Multi-Phased Shields and Hard Shields swapped for other technologies.

### Starting conditions and galaxy
- Starting ships are kept, but there is no starting Outpost Ship.
- Every home system has at least 4 planets.
- Fewer wormholes are generated. White stars have their life-supporting zone in the outer orbit, and brown dwarfs have one in the inner orbit.
- Natives in monster-guarded systems are terraformed and the guarded planets' gravity is recalculated.

### Economy and colonies
- Planet size matters less for population: Tiny and Small planets hold much more than before, while Huge planets stay close to classic.
- The Subterranean population bonus is smaller on Large and Huge planets.
- Planets can produce more before they pollute, and Nano Disassemblers quadruple that limit instead of doubling it.
- Radiated and Barren colonies cost more building maintenance.
- Robotic Factories give more on poor planets. The Autolab gives 25 research instead of 30, and the Orion artifact bonus is +3 per scientist instead of +5.
- Food Replicators cost 1 production and no BC per food. Biomorphic Fungi work on every planet.
- The Weather Controller can be built on any planet that grows food except Radiated ones (with Biomorphic Fungi that includes dead worlds), and on a Toxic colony it makes the colony count as Barren.
- Aquatic races treat Tundra as Swamp instead of Terran. The Microbiotics and Universal Antidote growth bonuses stack. Natives and a colony's last population can be moved, and up to 50 population can be in transit.
- Outpost Ships cost 200 instead of 100, and Androids 100 instead of 50.
- Feudal ship-cost reduction is 25% (was 33%) and Confederation 50% (was 67%); both also cut freighter costs, and Feudal research is -25% instead of -50%. Federation gives +50% research and income instead of +75%.
- Dictatorship has a smaller morale penalty, Federation gets a morale bonus, and Psionics morale also applies to Democracy and Federation.

### Research
- Research is still random. Points above twice the cost carry over, and points are kept when the current technology arrives from artifacts, spying or capture.
- About 45 technologies move to different fields. For example, Colony Base comes with Nuclear Fission, Scout Lab and Security Stations with Electronics, Fighter Bays with Capsule Construction, and Phase Shifter becomes a Xenon technology.
- Anti-Matter Bomb is renamed Graviton Bomb, Stealth Field is Class I Cloak and Cloaking Device is Class II Cloak.
- Torpedoes, Tractor Beam, Stasis Field and several other systems shrink faster with miniaturization.

### Ships and combat
- Tactical combat ends after 40 turns (classic 50) and uses strict ship initiative: each ship may wait once per round.
- Ship statistics update during the round as damage and effects occur, and beams fire defensively at ships approaching within 12 squares.
- Strategic stealth is much weaker: stealth and cloaks add a limited concealment bonus instead of near-total invisibility. Colony, outpost and transport ships carry your best stealth device.
- Planetary defenses are tougher: better armor technology makes colonies harder to bomb, ground defenses have more hit points, and they use your Damper Field, Achilles Targeting Unit and other combat specials. Star bases get your racial defense bonus and Warlord crew bonus, and missile bases can fire torpedoes.
- The Capitol can be destroyed by bombing. Telepaths need at least a Titan at the star to mind-control a colony. Boarding range is 1 square without transporters.
- Jump Gates add +4 speed and ignore nebulae.

### AI and difficulty
- The AI gets an extra +1/4 BC per population at every difficulty above Tutor, and no production penalty on Tutor.
- The AI builds Colony Bases before it has warp travel, uses ICE's own ship designs, and Stealthy AI races may use Stealth Fields.
- AI empires may surrender to each other only after turn 200.

### Antarans and monsters
- Antarans strike earlier: on a pre-warp start the first attack comes between turns 125 and 145, otherwise between turns 100 and 120. Their raiding ships have elite crews, their fleets are larger and the Antaran Star Fortress is stronger.
- Monsters have more hit points, Amoebae regenerate, Crystals keep captured ships, wandering Crystals, Hydras and Dragons usually stay after a battle, and up to 6 Eels can roam. Bribing a Dragon costs 1000 BC.
- The Guardian and Avenger carry new weapon loadouts.

### Events
- Space monster events arrive earlier (Eel from turn 125, Crystal 150, Hydra 175, Dragon 200). Hyperspace Flux and Warp Beast wait until turn 100.
- Lucky players see events more often. Pirate Raids take 9 to 24% of the treasury instead of 29 to 49%, Donations are 100 BC smaller, and a Computer Virus can wipe up to 250 research points.
- Winning by Council vote is worth 200 points instead of 100.

### Interface
- The Info screen opens on the Turn Summary, and the build list is sorted by category with key buildings highlighted.
- Battle confirmation messages close by themselves after about 10 seconds. Help, leader, ship-name and design-dock texts are updated for ICE.

## Keys

| Key | Effect under ICE |
|---|---|
| W | Waits with the active ship; each ship can wait only once per round, and waiting ships act last in reverse initiative order. |
| H | Waits with all your ships. ICE always turns ship initiative on, so this works in every battle. |
| Alt+M, E, N, L, O | The cheat grants 100,000 research points instead of 1,000,000. |

## Needs and conflicts

- Requires Basic 1.50 (v150), which is enabled automatically.
- Cannot be combined with Improved (v150i), the Version 1.2 mod (v120) or VDC-GM3.
- ICE-X is an optional add-on that makes the AI and Antarans harder.
- ICE runs Lua scripts when a new game starts, so in multiplayer every player needs the same version of ICE.

## Saves

Saved games record this package, and it must be installed to load them.
