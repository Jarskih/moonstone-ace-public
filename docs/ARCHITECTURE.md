# Architecture: game API, data files and rules (ROADMAP 9.1)

Design note, 2026-10-07, for ROADMAP M9 (9.3-9.7); M8 (co-op 8.3a/8.4b, weapons 8.8/8.9, playable monsters 8.10)
builds on it. Owner decision: "abstract away all low level stuff and let the player change the game logic easily";
method: **data files + C++ rules**. Facts from `ROADMAP.md`, `AGENTS.md`, `docs/MOONSTONE2.md`, `docs/MEMORY.md`,
`docs/PERF.md`, `docs/MODDING.md`, `include/game/*.hpp`, `src/rt/arena.cpp`, `src/rt/files.cpp`, `tools/gen_data.py`,
`tools/{tables,data_types}.yaml`.

## 0. Summary

- **Four layers:** platform (`src/rt`), engine services (`src/engine` + the fight bytecode VM), the **game API**
  (`include/game/api/`: a `World` view, type objects, cues; no addresses), and **rules + data**
  (`src/game/rules/*.cpp` + `PROGDIR:mods/*.ini`). Scene/flow code sits on the API and calls the rules.
- **Data files** are line-based INI text and partial overrides: missing file or key = built-in default. Parsed once at
  boot into a fixed `GameData` in BSS (no heap, no STL). Errors carry `file:line` and go to `PROGDIR:mods.log` and the
  console; a file with errors is rejected as a whole and its defaults stay.
- **Defaults come from one schema** (`tools/mod_schema/*.yaml`, in git): `tools/gen_moddata.py` generates the compiled
  defaults (`build/gen/mod_defaults.cpp`), the parser field tables and reference `.ini` files (`hd/mods/defaults/`).
  Original-derived values stay in `build/`, never in git.
- **Parity at four levels:** schema defaults == asm tables; parsing the default `.ini` == `kDefaults`; the unicorn tests
  stay green (unchanged refs resolve to the original addresses); boot + play scripts identical with and without the
  defaults copied into `mods/`.
- **Budget:** at most ~16 KB (chip on a stock A1200: low point 264 -> ~248 KB). Nothing per frame: type objects are
  applied at spawn/scene entry.

## 1. Layers and boundaries

```
 L3  RULES (pure C++)      src/game/rules/*.cpp, include/game/rules/*.hpp   +  DATA  PROGDIR:mods/*.ini -> GameData
 L2b SCENES / FLOW         src/game/{scene_*,combat,overworld,placevisit,loot,mainloop,fighters,fight_creatures,fight_ops}
 L2  GAME API              include/game/api/*.hpp, src/game/api/*.cpp  (World, Party, Knight/Inventory views,
                           type objects, Fight pools, Places, Clock, Rng, Cues, GameData)
 L1  ENGINE SERVICES       src/engine/*, src/game/{combat_script,creatures,mogjobs,arena,combat_load}.cpp + their rt bindings
 L0  PLATFORM              src/rt/{display*,irq,input,files,audio,synth,sprites,palette_*,wipe,crash,serlog,system,perf,game}.cpp,
                           include/game/state_bind.hpp, build/gen/owned_data.*
```

### 1.1 Where current files go

| Layer | Today | Target |
|---|---|---|
| L0 | `src/rt/display*`, `irq`, `input`, `files`, `audio`, `synth`, `sprites`, `palette_*`, `wipe`, `crash`, `serlog`, `system`, `perf`, `game.cpp`, `hunk9`, `prg_*`, `noop` | unchanged; new `src/rt/modload.cpp` (reads `.ini`) and `src/rt/world_bind.cpp` (builds `World` from the owned cells) |
| L1 | `src/engine/*`, `src/game/combat_script.cpp` (fight VM), `creatures.cpp` (contact, daggers), `mogjobs.cpp`, `arena.cpp` (obstacle probe), `combat_load.cpp` | same files (listed as L1 in MODULES.md); new `src/engine/inifile.cpp` |
| L2 | none; ~25 `*Env`/`*Ops` structs hand pure code raw cell pointers | `include/game/api/*.hpp`; `*Env` stay as plumbing; rules see only `World` |
| L2b | `src/game/{combat,fighters,fight_creatures,fight_ops,overworld,mainloop,placevisit,scene_*,loot,progmain,prims}.cpp` | same files; decisions (prices, dice, gifts, loot, wave size, damage) move to L3; call order kept (tested) |
| L3 | `src/game/rules.cpp`, pure parts of `scene_town`/`scene_places`/`loot`, `contactDamage`, `rt/arena.cpp` `scaleWave`/`lairLoot`/`init0169..init019F` (rules sitting in L0 today) | `src/game/rules/<topic>.cpp`, `src/game/data/` (parser, checks) |
| Data | owned DATA tables, immediates in C++ | `GameData` (BSS) = `kDefaults` (generated) + `PROGDIR:mods/*.ini` |

### 1.2 Crossing rules

1. Rules include only `include/game/api/*`, `include/game/rules/*`, `<stdint.h>`/`<stddef.h>`; never `state_bind.hpp`,
   `owned_data.hpp`, `rt/*`, `ace/*`; never cast integers to pointers; never touch `Knight` pointer fields; no `LAB_` addresses.
2. The API never calls L0; side effects leave as **cues** (Command pattern) or the scenes' `*Ops`.
3. The engine never includes `game/api` or `game/rules`.
4. The platform makes no game decisions (`scaleWave`, `lairLoot`, `init*` leave `src/rt/arena.cpp`).
5. Data never contains addresses: scripts, cel sets, sounds, texts by **name**, resolved at load (`world_bind.cpp`).
6. One writer per fact: `GameData` only by `modload.cpp`; stats only through rule functions; creature fields only via `creatureApply`.
7. Mods never write owned DATA (`imageEnter` restores it each overlay entry); overridden tables are BSS copies (3.6).

Enforced by `tests/test_layers.py` (include/pointer lint with a shrinking allow-list, in `check.py --quick`) and by
compiling rule host tests with only the API include paths.

## 2. Game API (namespace `ms::game`, `include/game/api/`)

Style as the pure headers: `stdint` types, ub/uw/sw/ul field prefixes, lowerCamelCase functions, SCREAMING_SNAKE
constants; POD tables with constant initialisers (no ctors).

- **ids.hpp:** `KnightIdx` (0..3, `KNIGHT_DRAGON = 4`), `CreatureKind`, `ArenaKind`, `WeaponId`, `ArmourId`, `LairIdx`,
  `PlaceIdx`, `ScriptRef` (0 = none), `CelSetId`, `SoundBankId`, `SoundId`, `TextId`, `ItemSlot`, `Stat`, `MoonPhase`.
- **world.hpp:** `Party {aRecords, aInv, ubCount, ubHumans}`, `Clock`, `Rng`, `World {party, pAct, aLairs, clock, rng,
  pData, aScriptAddr, pCues}`; `worldGet()` (L0 only).
- **party.hpp:** `partySize`, `knightAt`, `knightInv` (by index, never via `ulInventory`), `knightIndexOf`,
  `knightCurrent`, `knightIsHuman`, `knightIsAlive`, `statGet/statAdd` (byte wrap as the asm), `hpGet/hpMax/hpHeal`,
  (`knightRecalc` moved to `rules/stats.hpp` in 9.5d: the recalc reads `ItemDef`/`ArmourDef` rows).
- **items.hpp:** `itemCount/itemGive/itemTake`, flag slots (keys, moonstones), `knightWeapon/knightEquipWeapon`,
  `knightArmour/knightEquipArmour`, `goldGet/goldAdd/goldPay` (signed word compare, no cap, as the original).
- **creatures.hpp:** `CreatureDef {szName, ubBehaviour (+77), swHp, tunables +116/+118/+120, scripts idle/alt/walk/hurt/
  action/damage, ubCelSet, ubSounds, ubWaveRow, ubFlags, moon variants}`; `creatureDef`, `creatureApply` (replaces
  `init0169..init019F`, writes exactly the original fields; ref 0 = field untouched).
- **fight.hpp:** `ArenaDef {kind, backdrop mode, alive max, total, spawn style, palette, knight table patches}`,
  `WaveCounts`, `Fight` view (20 creature records, alive/total cells, first fighter); `fightSpawn`/`fightDespawn`/
  `fightAliveCount` over the existing `allocCreature`/`spawnScript`. Pools stay fixed (creatures 20, jobs 10 / 16 co-op,
  dagger slots 6).
- **places.hpp:** `PlaceKind`, `PlaceDef` (map sites LAB_069F), `LairDef` (LAB_07BD..07C0), `lairAt/lairHasLoot/
  lairHide/lairCount`.
- **clock.hpp, rng.hpp:** `clockDay`, `clockMoonIndex`, `clockMoon`; `rngNext`, `rngPercent` (only the original draw
  shapes, so moved rules keep the same RNG sequence).
- **cues.hpp (Command):** `CueKind {SOUND, TEXT, REDRAW, SCREEN, FADE}`, a 16-entry `CueQueue`, `cueSound/cueText/
  cueRedraw`; scenes drain it after the rule returns. Tested classic paths keep their result structs.
- **data.hpp:** `GameData {aCreatures[16], aArenas[12], aWeapons[8], aArmours[8], aItems, transfer order, aLairs[24],
  aPlaces, encounter bands, lair loot odds, smith, market, dice, temple, healer, wizard, rules}`; `extern const GameData
  kDefaults` (generated). `WeaponDef {uwCode $16..$19, damage bonus 0/2/3/5, rank, price, reach/speed/celSet/hitSet/
  actionMask for 8.8}`, `ArmourDef {uwCode $1B..$1E, hp bonus 0/10/20/30, endurance bonus, price}`.

## 3. Data files

### 3.1 Format

```
# PROGDIR:mods/creatures.ini : only the keys you change; everything else stays the original
[creature troll]
hp     = 60            # decimal
reach  = $5A           # $hex
[arena trolls]
alive_max = 2
spawn     = alternating
[creature cave_troll]  # a new row
base  = troll          # Prototype: copy the troll row, then override
hp    = 80
```

Lines up to 120 bytes, LF or CRLF; `#`/`;` comments; `[kind name]` sections (a new row needs `base =`); values: integer
(`-12`, `$1E`), name (schema enum or name table), list `a, b, c`, `"string"`. Fixed file names (no directory scan):
`rules.ini`, `creatures.ini`, `arenas.ini`, `items.ini`, `lairs.ini`, `places.ini`, `shops.ini`, `encounters.ini`
(`scripts.ini` later, 8.8).

### 3.2 Schema

`tools/mod_schema/<topic>.yaml` (one per topic): key, `GameData` member, type, min/max, default source
(`asm: LAB_xxxx[+n]`, `const: N` for immediates already in git source, `init: <fn>` for creature initialisers proven by
`test_arena_emu.py`), doc text, `aliases:` so renamed keys keep old mods working.

### 3.3 Generator and build

`tools/gen_moddata.py` (CMake, next to `gen_data.py`) writes `build/gen/mod_defaults.cpp` (`const GameData kDefaults`),
`mod_names.cpp` (script/cel/sound/text name tables from `tools/cell_names.yaml`), `mod_schema.cpp` (`FieldDesc` tables
for one generic parser), `build/gen/defaults/*.ini` (complete commented references, staged to `hd/mods/defaults/`), and
`docs/MOD_KEYS.md` (keys, types, ranges, docs; no original values).

### 3.4 Loading on the Amiga

`src/rt/modload.cpp`, once in `rtGameRun` before the arenas: `g_gameData = kDefaults`; for each file
`rt_file_open_exact("PROGDIR:mods/<f>")` read through the existing 32 KB buffer; the pure parser
(`src/game/data/modparse.cpp`) fills a scratch copy; the pure checker (`modcheck.cpp`) validates cross-field rules
(script refs exist, alive max <= job budget, counts fit pools, hp > 0, prices <= $7FFF, new rows have a base); a clean
file is committed, a bad one is dropped (defaults stay) with up to 16 messages to `PROGDIR:mods.log`, the console and a
3-second notice on the boot screen, e.g. `mods/creatures.ini:14: [creature troll] hp = 2000: out of range 1..999 (file ignored)`.

### 3.5 Memory

`GameData` + scratch ~6 KB, `kDefaults` ~3 KB, field/name tables ~3 KB, parser/checker/loader ~5 KB, cues + table pool
~1 KB: target <= 16 KB (row in `docs/MEMORY.md`).

### 3.6 Copy-on-write tables

Unchanged refs resolve to the original addresses (records hold today's bytes; unicorn tests stay valid). Overridden
tables (e.g. `damage = 4, 6, 8`) are copied into a fixed BSS pool and the ref points there. Runtime in-place writers
(ratmen moon damage, arena knight-table patches) keep working; co-op/weapons get per-knight copies.

### 3.7 Parity

1. `tests/test_mod_defaults.py`: every default equals the asm table / transcribed C++.
2. Parsing `build/gen/defaults/*.ini` with the real parser == `kDefaults` byte for byte.
3. Emulator tests unchanged; identity cells keep the `rt_ar_init_*`/`rt_ar_swap_*` stub addresses, which now call
   `creatureApply`; `scaleWave` picks its row from `def.ubWaveRow`.
4. `integrate.py --boot` + play scripts run twice: no `mods/`, and defaults copied into `mods/` (`--mods-defaults` leg).

## 4. Rules

### 4.1 One file per topic (`src/game/rules/`)

| file | content (from) |
|---|---|
| `stats.cpp` | HP/endurance recalc from `ArmourDef`/`ItemDef` (rules.cpp) |
| `settle.cpp` | gold settle, fight settle, defeats, knight death |
| `clock.cpp`, `turns.cpp` | daily upkeep, day turnover, turn setup/advance (8.3 party turns hook here) |
| `damage.cpp`, `ai_fight*.cpp` | 9.6g: `contactDamage` (with `WeaponDef.ubDamageBonus`), `protectedDamage`, `blockDecide` and the knight's hurt table `kHurtRules`; 9.6h: the fight AI table `kFightAis` and one decision file per AI (`ai_fight_<name>.cpp`); the handlers in `fighters.cpp` / `fight_creatures.cpp` apply the answers |
| `waves.cpp` | `waveSize(...)` from `scaleWave`; `RulesDef.ubWaveScaling` replaces `MS_MOD_NO_SCALING`; co-op +1 / x2 / halved write-back |
| `loot.cpp` | lair loot roll, gifts, loot moves |
| `shops.cpp`, `dice.cpp`, `temple.cpp`, `healer.cpp`, `wizard.cpp` | smith, market, exchange, dice house, stat purchase, healing, Math's gifts |
| `encounters.cpp`, `ai_knight.cpp` | encounter bands, black knights, lair choice; map AI, fight AI odds from data |
| `creature_apply.cpp` | `creatureApply`, moon variants |

Example (smith, LAB_0559):

```cpp
bool shopBuyArmour(World &w, Knight &k, uint8_t ubOffer) {
	const SmithOffer &o = w.pData->smith.aArmour[ubOffer];
	if(!goldPay(k, o.uwPrice)) return false;
	knightEquipArmour(k, w.pData->aArmours[o.ubArmour]);
	knightRecalc(w, knightIndexOf(w, k));
	return true;
}
```

### 4.2 Data vs rule changes

Data (no rebuild): edit `PROGDIR:mods/*.ini`, restart. Rule (rebuild): edit `src/game/rules/<topic>.cpp`, run
`py tools/integrate.py`, `cmake --build build --target hdinstall`. New selectable behaviour = a `RulesDef` variant
(enum + schema name + branch); the original stays the default.

### 4.3 Tests

Rules build with host clang++; with default data they are compared against the Python asm models, the lifted oracle and
(for rt glue) the unicorn tests. Modded behaviour is tested with mod fixtures, never by weakening the classic tests.

### 4.4 Patterns

| Pattern | Where |
|---|---|
| State | mog main-loop scene table (one writer of the scene id, `{enter, update, draw}` per scene); fighter dispatch by type; fight phases setup -> run -> settle |
| Type Object | `CreatureDef`, `ArenaDef`, `WeaponDef`, `ArmourDef`, `ItemDef`, `LairDef`, `PlaceDef`; Prototype rows via `base =` |
| Bytecode | the fight script VM (`combat_script.cpp`); scripts referenced by name; text-assembled `scripts.ini` in 8.8 |
| Object Pool | knight records, creature records (20), jobs (10/16), dagger slots (6), cue ring, mod table pool |
| Command | pad -> fighter input bits per knight (AI, player and playable monster produce the same bits); cues from rules |
| Update Method | per-fighter handler, per-job script tick |
| Dirty Flag | derived stats recalculated once before use (new code); dirty rectangles in drawing |

## 5. Migration plan (each step green: `check.py`, `integrate.py --boot`)

| step | work | proof | size |
|---|---|---|---|
| 9.3a | API headers + accessors, no callers; empty `kDefaults` stub | `test_game_api.py` | S |
| 9.3b | `world_bind.cpp`: `worldGet()` from owned cells, rebuilt at `imageEnter` | boot regression | S |
| 9.3c | `test_layers.py` lint with allow-list in `check.py --quick` | lint | S |
| 9.3d | replace pointer-field casts in `src/game/*.cpp` with API calls, one file per task | host + emu tests | M (parallel) |
| 9.3e | split `rules.cpp` into `rules/{stats,settle,clock,turns}.cpp` over `World` | `test_game_rules.py` | S |
| 9.4a | `inifile.cpp` tokenizer, `modparse.cpp`, `modcheck.cpp`, `FieldDesc` | host tests incl. messages | S |
| 9.4b | `mod_schema/rules.yaml` + `gen_moddata.py` + CMake | `test_gen_moddata.py` | S |
| 9.4c | `modload.cpp`, `rt_file_open_exact`, `mods.log`, report; `hdinstall` stages `mods/defaults/` | boot with a broken `.ini` still plays | S |
| 9.4d | parity tests + `--mods-defaults` boot leg | green | S |
| 9.5a | `waves.cpp` out of `rt/arena.cpp`, `[waves] scaling`, co-op wave rules | `test_arena_emu.py`; "no scaling" shot | S |
| 9.5b | shops/prices (smith, market, dice, temple, healer, caps) | `test_scene_town.py`, places emu | M |
| 9.5c | places/lairs/encounters | `test_overworld.py`, arena emu | M |
| 9.5d | items/weapons/armour; `damage.cpp`, `stats.cpp` read defs | `test_creatures.py`, `test_game_rules.py` | M |
| 9.5e1 | creatures: `creatures.yaml`, `creatureApply`, `init*` become stubs | `test_arena_emu.py` byte-identical | M |
| 9.5e2 | arenas: `ArenaDef` rows, generic arena runner (call order kept) | arena emu call logs | M |
| 9.5e3 | cel sets / sound banks by name | `test_combat_load.py`, soundbank emu | S |
| 9.6a-h | readable rules per topic | host oracle tests + a play-script shot each | L total |
| 9.7 | MODDING.md around mods, MOD_KEYS.md, example mods (no-scaling, cheap shops, tough dragon) | one shot per example | S |

Order: 9.3a -> 9.3b -> 9.4a -> 9.4b -> 9.4c -> 9.4d serial (9.3c, 9.3e beside 9.4a). Then parallel on disjoint files:
9.3d per file, 9.5b, 9.5c, 9.5d. Serial: 9.5a -> 9.5e1 -> 9.5e2 (all edit `rt/arena.cpp`); 9.5d before 9.5e1; 9.6x after
its 9.5 step. M8 hooks: 8.3a uses `Party`; 8.4b generalises `Fight` after 9.5e; 8.8 extends `WeaponDef` + per-knight
tables; 8.9 adds `ProjectileDef`; 8.10 adds `CREATURE_PLAYABLE`. Finish 9.5 before 8.4b.

### Risks

| risk | mitigation |
|---|---|
| fight frame on stock (83 % used) | no API calls in draw/contact loops; type objects at spawn; measure with `perfx.py --model` before/after 9.5e (+0 beam lines) |
| chip memory on stock | 16 KB cap, a size line in `bootLogMemory`, row in MEMORY.md |
| parity drift (address identity, store/call order) | unchanged refs = original addresses; identity stubs kept; unicorn call logs; two boot legs |
| owned DATA reset at overlay entry | `GameData` and mod pools in BSS outside the reset rows |
| no global constructors | constant-initialised POD; `g_gameData` copied at run time |
| unknown field roles (+116/+118/+120) | provisional keys with `aliases:`, `[verify]` in MOD_KEYS.md |
| bad mods | range + cross checks, bad file falls back to defaults, scripts only by name until 8.8 |
| original data in git | defaults generated into `build/`; git holds schema, key docs, example mods with new values only |

## 6. Owner decisions (2026-10-07)

- One `PROGDIR:mods/` folder; everything in it is applied.
- A file with an error is skipped (its defaults stay), with a 3-second boot notice and `mods.log`.
- `MS_MOD_NO_SCALING` became the data switch `[waves] scaling = none` in `rules.ini`; the CMake option is removed (ROADMAP 9.5a).
- Fight scripts are referenced by name only in M9; editable scripts (`scripts.ini`) come with 8.8.
- Open: creature names in files (`be` = ?), mods on floppy (proposed: HD only), example-mod numbers (tough dragon HP, cheap-shop factor).
