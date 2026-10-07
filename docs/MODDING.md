# Modding guide

Moonstone here is a C++ game, and most of what a player wants to change is **data**: prices, how many monsters a lair holds, how
tough the dragon is, a new monster. That needs **no rebuild and no tools**: you write a few small text files, put them in one
folder next to the game and start it. Sections 1-4b are about that. Changing the *logic* of a rule (a different formula, a new
behaviour) is C++ and is section 4c. `docs/MODULES.md` is the map of every source file and `docs/LABEL_INDEX.md` finds the C++ function
for an original label (`LAB_xxxx` of `mog.asm` / `program.asm`); both are generated (`py tools/label_index.py`).

## 1. What a mod is

A mod is **a few `.ini` text files in the `mods` drawer next to the game**, `PROGDIR:mods/` (on the hard disk install that is
`hd/mods/` next to the `moonstone` program). Nothing else: no program to patch, no original file to edit. Remove the files and you
have the original game again.

| File | What it changes | Section |
|---|---|---|
| `rules.ini` | wave scaling, the gold cap, the dagger cap (and the co-op switches) | 3.1 |
| `items.ini` | swords, armour and stat items: damage, hit points, prices | 3.2 |
| `shops.ini` | market prices, the dice house, temple, healer and the dagger price | 3.3 |
| `places.ini` | which map node is a village, a town, Stonehenge ... | 3.4 |
| `lairs.ini` | what each of the 24 lairs holds, and the loot odds | 3.5 |
| `encounters.ini` | the day the dragon comes, how often the computer knights fight | 3.6 |
| `creatures.ini` | the monsters: hit points, reach, damage, behaviour; new monsters | 3.7 |
| `arenas.ini` | the fights: which creature, how many; new fights | 3.8 |

You need only the files you use. The names are fixed and are read in this order: `arenas`, `creatures`, `encounters`, `items`,
`lairs`, `places`, `rules`, `shops`. A later file starts from what an earlier one set.

**The format.** `[kind name]` starts a section, `key = value` sets a key, `#` or `;` starts a comment. A value is a number (`-12`,
`$1E` for hex), a name (`ai = brawler`) or a list (`prices = 10, 16, 26`). You write only what you change: a missing file, section
or key keeps the original value. A row that does not exist yet is a *new row* and starts with `base = <an existing row>`, which
copies it; then you set what differs (an example is in 3.9).

**Every key is listed in `docs/MOD_KEYS.md`** (type, allowed range, meaning). The same list with every key at its built-in value,
as ready-to-edit `.ini` files, is written next to the game by the install: `mods/defaults/*.ini` (the drawer `defaults` is not
read by the game; copy a line from there into your own file).

**What happens with a bad file.** The game never stops for a mod. A file is read as a whole: if any line is wrong (a typo, a value
out of range, numbers that make no sense together) the **whole file is ignored** and the original values stay, the boot screen
turns red for three seconds, and the reason is written to **`mods.log`** (`PROGDIR:mods.log`, a text file next to the game; a boot
floppy is never written to):

```
mods/creatures.ini:14: [creature troll] hp = 2000: out of range -1..999 (file ignored)
```

The number is the line of the mistake. A file that is fine shows as `mods/<file>.ini applied`. Other `.ini` names in the drawer are
listed as unknown and ignored.

## 2. Quick start

1. Find the `mods` drawer next to the game (create it if it is not there).
2. Make the file `rules.ini` in it with this text, or copy `mods_examples/no_scaling/rules.ini` (the install puts the examples in
   `mods_examples/` next to `mods/`, switched off):

   ```
   [waves]
   scaling = none
   ```
3. Start the game and open `PROGDIR:mods.log`: it says `mods/rules.ini applied`. From now on a lair fight has the monsters the
   lair holds and no more, however strong your knight is.
4. To undo it, delete the file.

Make a mistake on purpose (`scaling = sideways`) to see what the game says: it plays as usual, and `mods.log` names the file, the
line and the allowed values.

## 3. The files, one by one

### 3.1 `rules.ini`: waves and limits

```
[waves]
scaling = none        # original | limited | none
[limits]
gold_cap = 300        # a knight's purse (original 150)
dagger_cap = 10
```

`[waves] scaling` is how lair fights grow with the knight. `original`: the monster count grows with strength, maximum HP and damage
(HP >= 30 / >= 90 add one, ...), the adjusted total is written back into the lair so it compounds on every visit, and the number
of monsters alive at once grows too. `limited`: the total still grows, the number alive at once does not. `none`: nothing grows; the
lair's own count and the arena's base numbers are used. Creature stats never scaled in the original. Not touched: the day-based skill
of computer knights and the ratmen's moon-phase strength. Examples: `mods_examples/no_scaling`, `limited_scaling`.
`[coop]` holds the owner's co-op wave decisions (`enabled`, `alive_bonus`, `total_factor`, `writeback_divisor`); there is no co-op
mode yet, so they do nothing. Code: `src/game/rules/waves.cpp`. A knight's gold is cut to `gold_cap` wherever gold is added.

### 3.2 `items.ini`: swords, armour, stat items

```
[weapon broad]            # a built-in row (long, broad, claymore, sharpness): only the keys you name change
damage = 3                # added to every hit of the knight who wields it
price = 5                 # the smith's price, 0 = he does not sell it
[armour plate]            # padded, mail, plate, battle
hp = 25                   # maximum HP while worn; `endurance` is the endurance bonus
[item protection_ring]    # inventory items: `slot`, `hp` per item held, `forces_weapon`
hp = 30
```

A knight can only obtain the codes the game hands out (the smith, loot). A *new* row (`base = claymore`, its own `code`) is for the
code you give a knight yourself, in C++, or for the later weapon types (ROADMAP 8.8 / 8.9). The smith's and the computer knights'
shopping read the prices. Code: `src/game/damage.cpp` (`contactDamage`), `src/game/rules/stats.cpp`.

### 3.3 `shops.ini`: prices and odds

```
[market]
prices = 10, 16, 26, 20, 26, 18, 26, 26, 20, 12, 6, 10   # one per inventory slot, the potion first, the moonstones last
sell_shift = 1                                            # a sale pays the price >> 1 = half
[smith]
dagger_price = 1
[healer]
heal_price = 5
life_price = 8
[dice_row d555]           # the dice house: three sixes (the dice show 0..5, sorted) pay 40 times the stake
multiplier = 40
[dice_row d123]           # a new winning throw
base = d001
dice = 1, 2, 3
multiplier = 7
[temple]
stat_cost = 3, 2, 1, 1    # progress points per stat point for 1..4 human players
```

The armour and sword prices of the smith are in `items.ini` (`price`). The game refuses a price above `gold_cap` and unsorted or
repeated dice. The text and numbers drawn on the shop *pictures* are not data: the price list you see in the market does not
change with a mod, the price you pay does. Example: `mods_examples/cheap_shops`.

### 3.4 `places.ini`: what a map node is

```
[place extra]             # node $1D (29) becomes a town like HighWood
base = town_a
node_first = 29
node_last = 29
[map_node n1e]            # move a node on the map (x 0..351, y 0..199)
x = 120
y = 80
```

Arriving at a node inside `node_first`..`node_last` of a row visits that row's `kind` (`village`, `town_a`, `town_b`, `stonehenge`,
`valley`, `wizard`, `duel`, `none`); the first matching row wins. A node in no row is not a place. Built-in rows: `village`,
`town_a`, `town_b`, `stonehenge`, `valley`, `wizard`, `duel`.

### 3.5 `lairs.ini`: lairs and loot

```
[lair lair03]             # the 24 lairs are lair01 .. lair24 in the order of the game
count = 4                 # monsters it holds at the start of a game
arena = 10                # which fight it uses (slot of the arena table, 3.8)
[loot_odds band1]         # what a lair holds at the start: a percentile draw against `up_to`, first band it does not exceed
up_to = 30
loot = gold               # original | gold | items | mixed | none
```

The numbers of the original lairs and the loot odds are read from the game disks, so every key means "original" until you write it.
The lairs are fixed: you cannot add or remove one, you change what they hold. `x`, `y` and `region` move a lair on the map. The
game refuses falling loot bands (`up_to` must not decrease).

### 3.6 `encounters.ini`: the dragon and the computer knights

```
[encounters]
dragon_day = 5            # the dragon flies over the map from day 5 (0 or 1 = from the start)
ai_engage_odds = 127      # 0..127: a computer knight that could fight another picks the fight when a 0..127 draw is at most this
```

### 3.7 `creatures.ini`: the monsters

A creature row holds only what you change over the built-in creature it starts from: `be`, `mudmen`, `trogg_axe`, `trogg_axe_b`,
`trogg_spear`, `ratmen`, `balok`, `troll`, `demon`, `dragon`.

```
[creature dragon]
hp = 250                  # hit points at the start of the fight (-1 = original); hp_max follows unless set
reach = 70                # how close it comes before it attacks; too_close, depth: the other two distances
damage = -1, 20, 60, -1, -1, 20, -1, -1, 60     # damage per action slot 0..8 (-1 = leave the slot)
[creature troll]
ai = brawler              # the behaviour, by name: flyer snatcher demon dragon brawler brawler_b spearman caster drake stalker
walk_table = dragon_walk  # scripts and tables of another creature, by name (the lists are in docs/MOD_KEYS.md)
```

Scripts and tables are shared by name: a creature that names another's `walk_table` walks like it, and creatures that share a damage
table share its changes. Animations of your own come with the script editor (ROADMAP 8.8) and the monster kit (3.9).
**The dragon** is special: its fight is set up by its own code, so for the dragon the numeric keys apply (`hp`, `hp_max`, `reach`,
`too_close`, `depth`, `damage`) and the table and script names, `ai` and `like` do not; the two bats that fly with it are not
changed. Example: `mods_examples/tough_dragon`.

### 3.8 `arenas.ini`: the fights

```
[arena troll]             # a built-in fight (be mudmen demon trogg_axe trogg_axe_b trogg_spear ratmen balok troll)
alive_max = 2             # monsters alive at once, before the wave scaling
total = 6                 # monsters in all, before the wave scaling (a lair fight takes the lair's own count)
creature = 7              # which creature row spawns (its number in creatures.ini; built-in rows 0-9 in the order of 3.7)
cels = troll              # the pictures it loads, by the name of the original monster
sounds = troll
palette = 24
```

A lair uses an arena through its **slot** in the arena table (`arena` of `lairs.ini`). The built-in fights keep their slots; a new
row (`base = <row>`) needs a free `slot`: 10, 11, 13 or 15. At most 16 creature rows and 13 arena rows exist in all. The `demon`
arena is the guardian's: it honours only `creature` and `palette`.

### 3.9 A new monster in a lair: three rows

A new monster is a **creature row**, an **arena row** that spawns it and a **lair row** that uses the arena. No C++. This is
`mods_examples/cave_troll`:

```
# creatures.ini                       # arenas.ini                          # lairs.ini
[creature cave_troll]                 [arena cave_troll]                    [lair lair07]
base = troll                          base = troll                          arena = 10
like = troll                          slot = 10                             count = 4
hp = 60                               creature = 10
                                      alive_max = 3
                                      total = 4
```

1. **The creature.** `base = troll` copies the troll row, `like = troll` says which built-in record it starts from, and `hp = 60`
   makes it tougher. Its row number is the next free one after the ten built-in rows: the first new row is **10**, the second 11.
2. **The fight.** `slot = 10` is a free place of the arena table, `creature = 10` is the row of step 1, `alive_max` and `total` size
   the wave. The pictures and sounds are the troll's (a variation borrows an original monster's set until it has its own).
3. **The lair.** `[lair lair07]` with `arena = 10` makes the seventh lair of the map use that fight; `count` is how many it holds.

Start the game, go to lair 7 and fight cave trolls. If the game ignores a file, `mods.log` says which line.

**Own pictures, sounds and animations** are made with the monster kit: `docs/MONSTER_KIT.md` describes the kit and its converter, and
the **web editor** `tools/monsterkit/editor_web/monster_editor.html` (one page, open it in a browser, no server and no game data
needed) lets you clone a monster, draw frames, set attack points and hit boxes, pick the behaviour (the editor shows the animations
that behaviour needs) and preview it at Amiga speed. The creature and arena rows of a kit are the same rows as above.

## 4. Example mods

`mods_examples/<name>/` holds ready-made mods (only our own values, no original data). Each folder has a `README.txt`. To use one,
copy its `.ini` files into the `mods` drawer; the hard disk install stages them next to `mods/` (`hd/mods_examples/`), switched off.

| Folder | Files | What it does |
|---|---|---|
| `no_scaling` | `rules.ini` | `[waves] scaling = none`: lair fights never grow |
| `limited_scaling` | `rules.ini` | `scaling = limited`: more monsters in all, never more at once |
| `cheap_shops` | `items.ini`, `shops.ini` | smith, market, healer and daggers cost half |
| `tough_dragon` | `creatures.ini` | the dragon has 250 HP (original 120) and hits twice as hard |
| `cave_troll` | `creatures.ini`, `arenas.ini`, `lairs.ini` | a new 60 HP troll, in lair 7 |

Each is proved by a headless boot (`py tools/modsboot.py <name>`: the log shows the loaded values, the dragon and the cave troll
log a line from the fight set-up) and by `tests/test_mod_examples.py` (every example parses clean against the schema).

### 4a. Art and monsters (pictures and sounds)

`tools/artconv.py` exports every original `.cel`/`.ob`/`.piv`/`.stile` to indexed PNG templates and imports redrawn PNGs back into
game files (5-plane original or 6-plane/64-colour redesign): see `docs/ART.md`.

```sh
py tools/artconv.py export                       # build/disks -> build/art/export/<disk>/<file>/
py tools/artconv.py import --all --planes 6      # redrawn PNGs -> build/art/import/<disk>/<file>
```

Drop the imported files into `art/` next to the exe on the HD (`PROGDIR:art/<name>`, plain names; 6-plane pictures also need their
`.pal` sidecar): the game looks there before the original disk. Enhanced (64-colour) mode is the `MS_ENHANCED` build option. New
monsters: the monster kit (3.9).

### 4b. Building and testing (for code changes)

```sh
py tools/integrate.py                      # regenerate, build Release + Debug, run the tests that touch changed files
py tools/integrate.py --boot               # headless boot regression in WinUAE (docs/AUTOPLAY.md)
py tools/check.py --quick                  # unit tests and tool checks without replay and builds
py tools/modsboot.py cave_troll            # boot one example mod and check the log
py -m unittest tests.test_game_rules       # one module
```

Toolchain PATH and configure lines: `AGENTS.md`, `BUILD.md`. Work in a worktree (`py tools/worktree_setup.py --path D:/Amiga/wt/<task>`,
`docs/AGENT_BRIEF.md`). New game logic goes in `src/game` or `src/engine` (pure, host-testable with clang++, no STL, no new/delete);
hardware glue goes in `src/rt`. The original data is C++-owned and generated at build time into `build/gen/owned_data.{hpp,cpp}` by
`tools/gen_data.py` (`tools/tables.yaml`, `tools/data_types.yaml`); no original data is in git. Where the code lives:

| What | Where |
|---|---|
| Game state (knight/actor record 132 B, inventory, lairs, map nodes, jobs) | `include/game/{knight,world,state}.hpp`, `docs/GAME_STATE.md` |
| Stats, HP, fight settlement, lunar clock, shops, dice, rituals, healing, the black knights | `include/game/rules/*.hpp`, `src/game/rules/*.cpp` (section 4c) |
| Combat: fight loop, per-fighter handlers, script engine, contact/daggers | `src/game/{combat,fighters,fight_creatures,fight_ops,combat_script,creatures,arena}.cpp` |
| Overworld map, turn UI, AI knights, dragon flight | `src/game/overworld.cpp`, `src/game/mainloop.cpp`, `src/rt/overworld.cpp` |
| Towns, places, loot | `src/game/{scene_town,scene_places,placevisit,loot}.cpp`, `src/rt/screens.cpp`, `src/rt/combat_ui.cpp` |
| Intro, ending, animations, sound | `src/engine/{scenes,intro,anim,sfx,synth}.cpp`, `docs/AUDIO.md` |
| The mod loader and its data | `src/rt/modload.cpp`, `src/game/data/*`, `tools/mod_schema/*.yaml`, `docs/ARCHITECTURE.md` section 3 |

Each ported function cites the original labels it transcribes; deliberate differences and kept original quirks are commented in
place. Behaviour changes end with a `build/shots/` screenshot or a replay log.

## 4c. Changing the rules in C++ (ROADMAP 9.6)

Numbers are data (section 3, no rebuild); a rule that needs different logic is C++, and each rule topic is one short file in
`src/game/rules/` with its header in `include/game/rules/`. A rule file works on the records and the game API only
(`include/game/api/*`, `GameData` rows): no ACE, no hardware, no scene code (`tests/test_layers.py` enforces it), so it builds and tests on
the host. The doc block at the top of every header says what a modder can change there. Every function cites the original `LAB_`
labels, and the quirks the original has are marked `QUIRK`; the result must stay bit-identical unless you mean to change it. The scenes
(`src/game/scene_*.cpp`, `src/rt/*`) keep the screens, sound and text and ask the rules.

| Topic | Rule file (`src/game/rules/`) | Change here |
|---|---|---|
| Levelling, stats, progress points | `stats.cpp`, `levelling.cpp` | points per victory (`PROGRESS_*`), max HP and endurance formulas, what a stat point costs, which stat a gain picks and the stat limit |
| Healing | `healing.cpp` | overnight regeneration and curse decay, rest, the castle's life, the healer's order of cures (prices: `[healer]`) |
| Day, moon, turns | `clock.cpp`, `turns.cpp` | turns per day, moon frames, what happens at dawn, who plays next, the turn budget |
| Fight settlement | `settle.cpp` | which goods and gold change hands after a fight |
| Smith and market | `shops.cpp` | what is sold and when a purchase is allowed (`canAfford`), sell-back and the gold cap (`goldAddCapped`), the dagger rule |
| Dice house | `dice.cpp` | stake limit, the dice, how a throw is looked up; the odds and payouts themselves are `[dice_row]` data (section 3.3) |
| Rituals: Math, Mythral, Stonehenge, the Valley | `rituals.cpp` | gift bands and tables, the mystic's odds and bonus table, the moon-phase stones and ending codes, Danu's blessing, the guardian's reward |
| Black knights on the map | `ai_map.cpp` | whom they fight (`aiFeelsLikeFighting`, `aiPickOpponent`), which lair they roam to, potion / scroll / speed use, what they buy (`aiShopWish` steps, `aiShopApply`) |
| Wave sizes | `waves.cpp` | creature counts per fight (`[waves]`, section 3.1) |
| Damage and contact | `damage.cpp` | the damage formula (`contactDamage`: table value + strength + sword bonus, doubled for a heavy blow and a moonstone), the shield (`protectedDamage`), blocking (`blockDecide`), what a knight takes from each creature type (the `kHurtRules` table and the flat `DAMAGE_*` values: flyer 5, drake 5, spearman 3, demon 10 / 8, troll 7, the dragon's 20 / 30 / 34 through the shield) and the script it plays next |
| Fight AI (creatures and the computer's knight) | `ai_fight.cpp` (the `kFightAis` table: ActorType -> AI -> the monster kit's AI name), `ai_fight_flyer.cpp`, `ai_fight_snatcher.cpp`, `ai_fight_demon.cpp`, `ai_fight_knight.cpp`, `ai_fight_dragon.cpp`, `ai_fight_brawler.cpp`, `ai_fight_caster.cpp`, `ai_fight_drake.cpp`, `ai_fight_stalker.cpp` | which AI a creature type runs; the ranges, cool-downs and odds each AI decides by (brawler: gloat odds 30 %, close 100, heavy swing 120; demon: lunge 100, strike 130, jump 140; caster: strikes at 40 / 50; stalker: heavy swing at 100..149), the state order of the flag machines (caster, snatcher, demon, drake, dragon) and what hurts a creature (`brawlerHitTaken`, `dragonHitTaken`, `flyerHitTaken`) |

A small example, in `rules/stats.hpp`: a creature fight is worth two progress points instead of one:

```cpp
enum : uint16_t { PROGRESS_CREATURE_FIGHT = 2, /* ... */ };
```

Then `py -m unittest tests.test_game_rules tests.test_scene_town tests.test_scene_places` (the tests compare against the original
behaviour, so a deliberate change shows up as a failing oracle: update the expectation in the same commit). The two fight topics work the same way: the
handlers in `fighters.cpp` / `fight_creatures.cpp` gather the facts of the moment (a `HurtFacts`, a `BrawlerFacts`, ...) from the two
records, ask the rule, and apply the answer (hit points, flags, the next script), so a change to a number or a decision never touches the
plumbing. The checks are `tests.test_rules_fight` (the AI table against the monster kit's `ai_catalog.json`, the flat damage values, the
range boundaries), and the oracles `tests.test_fighters` / `test_fighters_emu` (the handlers against the original). To give a new creature
the AI of an existing one, add a row to `kFightAis` and give the creature that type byte; the monster kit's AI names are the `pName` column.

## 5. Not done yet (for modders)

- New scripts, hurt/walk tables and cel sets for a brand-new creature (`ROADMAP 8.8`-`8.10`, 9.8); rows can combine the original's.
- A data-only override of texts/tables without regenerating (tables come from the original hunks at build time).
- Per-label names for the typed state: the `prg_/mog_LAB_*` aliases are being replaced by C++ names (`ROADMAP 7.1s`).
