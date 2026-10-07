# Monster kit: browser editor + converter for new creatures (ROADMAP 9.8)

Design note, 2026-10-07; **9.8a1 verified every [guess]/[verify] item the same day** (against the original `mog` binary and the
C++; the record is section 8, the data is `tools/monsterkit/ai_catalog.json`, the tools are `tools/fightscript.py` and
`tools/monsterkit/*`). Engine facts carry `file:line` cites (as of commit 49fdd47). Sources: ROADMAP (M8 8.8/8.10, M9), `docs/ARCHITECTURE.md`, `docs/ART.md`,
`docs/MODDING.md`, `docs/MEMORY.md`, `docs/PERF.md`, `tools/artconv.py`, `tools/cell_names.yaml`,
`include/game/{combat_script,creatures,fighters,knight,combat_load}.hpp`,
`src/game/{combat_script,creatures,fighters,fight_creatures,fight_ops,combat_load}.cpp`,
`src/rt/{arena,combat_load,loaders,soundbank,palette_glue}.cpp`, `src/engine/{loaders,display_fx,enhcarve}.cpp`.

Owner requirements (2026-10-07):
- **R1** every AI's required animations as a checklist; export is refused while one is missing.
- **R2** "Clone from..." any game monster (via the artconv export path, no original data in git) or any user kit.
- **R3** the AI is chosen per monster (`ai:` by name); checklist and tunables follow it; new AIs later as C++ rule
  handlers registered by name.

## 0. Summary

- A monster is: up to 5 **cel files** (frame-set slots `LAB_05E0`), optional **hit sets** (attack points per frame,
  `collide.hit`), **fight scripts** (bytecode, one tick per `$FF`), a **record set-up** (`init*`: script tables, damage
  table, type byte `+77`, HP, tunables `+116/+118/+120`), an **AI handler** chosen by the type byte (which also plays
  **hard-wired scripts by label**), a **loader**, an **arena row**, and a **sound bank**.
- **Hurt is pixel-perfect** (a defender is hit where its drawn frame has non-zero pixels, if drawn with the hurt flag);
  **attacks are points** on the attacker's frame. The editor's "hit box" tool is an attack-point tool plus a "hurtable"
  toggle per draw.
- **Kit = folder:** `kit.json` + PNG sheet(s); one schema for editor and converter; existing monsters clone into kits.
- **Converter tiers:** **T0 reskin** (art overrides of the original cel files + `collide.hit`, works today), **T1** new
  creature reusing the clone source's scripts (needs 9.4 + 9.5e1-e3), **T2** own animations/timing (needs 8.8
  `scripts.ini` + a role-script indirection).
- **Editor:** one static HTML+JS file: pixel canvas, palette snap, slicing, anchors, attack points, checklist, AI picker,
  Clone; animation preview through a JS port of the script VM and `contactTest` at the fight rate. No server needed.

## 1. What a monster consists of

### 1.1 Cel files

Layout (artconv.py:12-13, 208-216, 530-532): `BE16 frameCount, BE32 packedSize, BE32 8*decodedSize`, then per frame
`BE32 offset, BE16 w, BE16 h, byte8, byte9 plane_bits`, then an LZSS stream of each frame's present planes
(`ceil(w/16)*2*h` bytes each).

- **byte8 is runtime state:** the game flips frames in place on facing changes (`mirrorCelHeader`,
  include/engine/blit.hpp:127-139; bit 0 = not flipped, high nibble = padding). `loadFrame` tests `rec[8] == 1`
  (combat_script.cpp:95-104), `contactTest` reads bit 0 (creatures.cpp:128-130). **The converter always writes $01.**
- Limits: frame index is a byte (<= 256 frames per file, combat_script.hpp:13-14); at most 5 cel files per monster
  (`LAB_05E0`, draw opcodes `$00..$10` are offsets into it; combat_load.hpp:13-16).
- Originals (headers only): TROLL1/2 48/16 frames, up to 86x92; Balok1/2/3 70/26/60; Mudmen1/2 63/66; be1/be2 100/34.
- Index 0 is transparent (no mask). Most creature frames use indices 0..15; Troll and Mudmen use 16..31 too. 6-plane art
  needs MS_ENHANCED; in 64-colour templates sprites may use only 0..47.

Fight palette (`sceneColors`/`sceneTail`, display_fx.cpp:162-243, via `paletteScene`, palette_glue.cpp:81-99,
scene code `paletteMode(kind)`, arena.cpp:243):

| Index | Source | Kit class |
|---|---|---|
| 0 | forced black (display_fx.cpp:239) | transparent |
| 1-4 | backdrop, or region tweak for regions 8/C (226-229) | region-variable (warn) |
| 5 | backdrop | background-local (warn) |
| 6-8 | first fighter's colours by knight class (142-147, 214) | **forbidden** |
| 9-15 | creature colours per scene code (164-205); 15 forced red outside the dragon scene (240-242) | **own** (9-14 editable, 15 blood red) |
| 16-28 | region table or picture (236) | region-variable (warn) |
| 29-31 | picture (dragon tail 186-187) | background-local (warn) |

New colours = a new 7-word scene row (indices 9..15) in `ArenaDef` (9.5e2), not in the cel file.

### 1.2 Fight scripts (bytecode; combat_script.hpp:12-32, interpreter combat_script.cpp:326-516)

- **Draw `$00..$7F`** (the engine uses `$00..$10`, the byte AND `$1F` indexes the frame-set list), 6 bytes
  `[slot*4][frame][dy:s8][flags][dx:s16]` (drawEvent 245-308): x = `X+dx` facing right or `X-dx-w` mirrored;
  y = `Y+Z+dy`. Flags: bit0 hurt list, bit1 attack list, bit4 draw into background, bit5 text, bit6 no bounding box,
  bit7 skipped while Gore is off (see `$98`); bits 2/3 are unused. Several draws per tick = layers.
- **`$FF` is a TWO-byte tick marker** (endOfFrame 194-241): the interpreter skips the byte behind it (always `$00` in the
  original), except `$FF $FF` = the tick ends AND the script ends (the second `$FF` is not advanced over) and `$FF $FE` = the
  tick also closes a loop2 round. `tools/fightscript.py` spells them `end`, `done`, `end loop2`.
- Opcodes (sizes are exact, `tools/fightscript.py` is tested against the interpreter, `SIZE_OF`): `$80` facing (2; 1 right,
  3 left, `$FF` toggles), `$84` jump (6; byte1 = 3) else arm a "continue at" taken at the next tick end, `$88` loop count
  (2; 0 = random 1..31), `$8C` motion block (8; `param, count, flags, vy, vylimit, vx, vxlimit`, stepped per tick,
  155-185), `$94` loop2 (2), `$98` **jump when the Gore cell LAB_06DA is non-zero, i.e. Gore: Off / demo** (6; the original
  puts `$07` in its ignored byte 1), `$A0` move x/y/z (8; 407-422), `$A4` sound id (2; 423-426), `$A8` poke owner record
  (8; 427-440; **stores** the low byte / word / long, never ORs a bit; the original uses it only to re-point +22 (idle) at
  the next script of a chain: dragon, demon), `$AC` second script (6), `$B0` engine routine (6), `$B4` jump + reset when
  owner HP <= 0 (6; 451-458), `$B8` spawn object (6), `$BC` kill job (2; 464-469), `$C0` frame set (2; **1 = knights'
  list LAB_05E1, 2 = the creature's 5 cel slots LAB_05E0, 3 = map, 4 = effects**), `$C4` jump if same facing as the
  knight (6), `$C8`/`$CC` jump if owner field zero/non-zero (8), `$D0` reset work block (2), `$FD`/`$FE` jump to the
  return/loop2 point (1). Script addresses are absolute (relocated data). Text syntax: `tools/fightscript.py` docstring.
- **Rate:** one `$FF` = one fight frame; budget 6 VBLs (8.3 fps), measured 10 fps (docs/PERF.md; `mogFrameBudget`,
  arena.cpp:666).
- **Who picks scripts:** `creatureDispatch` calls the AI only on a hit link or when the script ended
  (creatures.cpp:643-668); the handler returns the next script (-1 keep, 0 free the creature). Each animation is one
  complete move ending in `$FF $FF`.
- **Death (verified):** the creature dies only through its scripts. Every hurt script contains `$B4` (`ifdead`), which jumps
  (and resets the work block) when HP <= 0, to a die sequence that calls **`engine LAB_0005`** (`fightCreatureDied`,
  combat.cpp:171-190: ends the fight when the first fighter is down, else counts the wave down and spawns the next creature)
  exactly once and ends with `$BC` `kill` (clears the record's in-use long) before `$FF $FF`. A hurt-table entry that is 0
  makes the handler return script 0: the record is freed WITHOUT the death accounting. `LAB_0006` (`fight_end`) and
  `LAB_000D` (first fighter HP := -1) are the other fight-level engine routines the scripts call.

### 1.3 Hit sets (attack points)

- `collide.hit` text (`hitParse`, loaders.cpp:172-240): a cel-name line, then per frame `NN` points (`00` = none); if
  `NN > 0` a `TT` type line (never read) and `NN` x `XXXYYY`; `99` ends the set. The parser computes a per-frame gate box
  `maxDx/maxDy`; binary record `{count, type, maxDx, maxDy, (x,y)*count}`, 1 byte when empty (`hitRecordAt`,
  creatures.cpp:111-120). The original holds 14 sets.
- `hitLoad(name, at)` loads the cel and appends `{cel table, hit data}` (rt/loaders.cpp:18-19); `contactScan` finds the
  set by the attacker's cel table (creatures.cpp:243-282); a cel without a set never hits.
- `contactTest` (creatures.cpp:122-225): gate box (shifted by `celW-maxDx` when mirrored), per-point window test with the
  mirrored x, then **QUIRK:** the bit test and the reported point use the raw unmirrored point (198-200); any defender
  plane bit under the point is a hit. Depth difference <= 10 (`depthClose`, 93-98). First hit per attacker per tick
  wins; links attacker +14 / defender +18, hit point at +122/+124.
- Pools: attack/hurt lists 8 entries per job (creatures.hpp:98-100; no bound check: a ninth draw overwrites the next job's
  slot); the pair table `LAB_0A51` is **84 bytes = 10 `{cel table, hit set}` pairs + one spare long and has NO terminator**
  (the asm compares keys without a bound; the C++ counts the pairs, rt/creatures.cpp:75); an 11th registration overwrites
  `LAB_0A52` and what follows (`rtHitLoad_mog` has no bound; the originals register 2-4 sets per fight, the 5 cel files of a
  kit + the knights' kn4 stay below 10); hit data region 9000 bytes (enhcarve.cpp:176-179); the `collide.hit` text buffer 9000
  bytes against an 8360-byte original, **only ~640 bytes headroom**. Attack point x / y are 3 decimal digits stored as
  BYTES (0..255, wraps above), the gate box is the byte of the largest x / y; the Python ports in
  `tools/monsterkit/engine_ports.py` are tested against the C++ (`hit_parse`, `contact_test`, ...).

### 1.4 Anchors

No anchor in the cel file: each draw places the frame's top-left at `(dx, dy)` from the job position; mirroring flips
around `X - w` (combat_script.cpp:250-260). In the kit the **anchor** is the frame's ground point; the converter emits
`dx = -anchorX`, `dy = -anchorY` (s8). Clone recovers anchors from each frame's most common `(dx, dy)`.

### 1.5 Record set-up (`Knight`, 132 bytes, knight.hpp:65-170; init* arena.cpp:503-658)

| Field | Offset | Meaning |
|---|---|---|
| idle script | +22 | default |
| after_hit | +26 | after a successful hit |
| hurt scripts | +30 | 9 longs by the **attacker's** action/4 |
| action scripts | +34 | 9 longs by own action/4 |
| cel slot list | +38 | the 5-slot cel list |
| damage table | +42 | 9 longs by own action, read by the defender (creatures.cpp:359-397) |
| walk scripts | +46 | `phase*4 + block` (0 side, 32 up, 64 down; `advancePhase`, fighters.cpp:664-674) |
| type | +77 | AI/behaviour (1.6) |
| hp / hpMax | +80 / +84 | |
| +11 | | 4 for AI |
| reach | +116 | stop-and-attack x distance (`approach`, fighters.cpp:744-750; caster: dagger speed 951) |
| keep-away | +118 | back off when closer (734-740, 797) |
| depth | +120 | lane tolerance (`depthNear`, creatures.cpp:680-683) |

Ratmen moon-phase overrides: arena.cpp:626-658. Arena (660-806): `commonSetup`, loader `rtCl*`, `waveCells`,
`scaleWave`, spawn list, `paletteMode`; some arenas patch the knight's tables (Be arena 701-713).

### 1.6 AI types (handler table `fightTablesInit`, fight_ops.cpp:90-109)

The type byte also selects the knight's reaction (`knightHurt`/`knightHit`, fighters.cpp:407-443) and is the palette
scene code.

| Type | Handler | Original | In the kit |
|---|---|---|---|
| `$00` | flyer (fighters.cpp:538) | Be | `flyer` |
| `$04` | snatcher (fight_creatures.cpp:70) | Mudmen | `snatcher` |
| `$08` | demon (fight_creatures.cpp:335) | guardian | advanced |
| `$0C`/`$38` | knight | player | no |
| `$10` | AI knight (fight_creatures.cpp:606) | black knight | advanced |
| `$14`/`$2C` | dragon / part | dragon | no |
| `$18`/`$1C` | brawler (fighters.cpp:875) | TroggAxe A/B | `brawler`, `brawler_b` |
| `$20` | brawler, spear path | TroggSpear | `spearman` |
| `$24` | caster (fighters.cpp:1192) | Ratmen | `caster` |
| `$28`/`$34` | idle / dagger | objects | no |
| `$30` | drake (fighters.cpp:1595) | Balok | `drake` |
| `$40` | stalker (fight_creatures.cpp:226) | Troll | `stalker` |

**Required roles per AI (R1/R3).** "Field" = a record table the kit sets; "label" = a script the handler loads by fixed
label (under T0/T1 that is the clone source's script, so frame indices must be kept; under T2 a role script, 3.4).

- **Verified by 9.8a1 (all in `ai_catalog.json`, with cites):** `hurt_by_action` entries that must be filled are the knight
  actions whose script has attack draws, indices **1, 2, 3, 5, 6, 8** (actions 4, 8, $C, $14, $18, $20; indices 4 and 7 =
  $10 / $1C are stances; the ratmen and spear arenas replace knight actions 4 / 7); an empty entry frees the record when
  hit by that action. The knight's reaction to each type decides which **damage** entries exist at all: flyer flat 5, spear 3,
  drake 5, stalker 7 (tables never read), snatcher damage[0], brawlers damage[2] / damage[8] (raw), caster damage[1] / damage[2].
  Scripts never poke the AI flag bytes (+104 / +105 are handler-owned): the `events` of the 8 AIs are empty.
- **flyer `$00`** (538-612): action forced to `$20`; idle, `hurt_by_action`, after_hit +26, `walk[0..3]` side (block 0 only);
  labels kill_same_facing s0849 / kill_opposite s084E (also started by the knight's hurt handler); damage unused (flat 5);
  no tunables (step table 33/27/17/33, screen wrap).
- **snatcher `$04`** (fight_creatures.cpp:33-200): labels hurt s08A3, carry s089F, break_free s08A0, free s08A1,
  appear s0897, grab s08A2, grab_miss s0899, strike_near s0898, strike_far s089E; idle, walk (up to 8 phases, the original
  has 4; the hurt table +30 and after_hit +26 are NOT read); tunables via `approach`; SN flags in +104 are written by the
  handler itself (fight_creatures.cpp:31, 139, 158), not by scripts.
- **brawler `$18`/`$1C`** (fighters.cpp:770-930): idle, after_hit, hurt_by_action, attack = action[2], heavy =
  action[8], walk side/up/down; damage[2], damage[8]; reach, keep-away (797), depth; gloat-or-attack against knight
  action `$10` (833-850).
- **spearman `$20`**: brawler without the action table; labels thrust s081B (829-832), kill s081C (916); reach gate 825;
  knight reaction hurt01F6 (418, fixed 3 HP); the arena patches knight actions 4/7 (arena.cpp:688-699).
- **caster `$24`** (937-1275): idle, hurt_by_action; labels finished_off s0860, hurt_held s086B, hurt_plain s0862,
  strike_close s0859, strike_mid s0857, hit_landed s085A/s0858, throw_out s0850, hold_near/far s0864/s0863, grab_hold
  s085B, grab_release s085C, grab_broken s085D, throw_knight s0851, slam_hit/miss/slam/end s0867/s0868/s0866/s0869,
  after_grab s0865; uses the dagger object and +116 as dagger speed; flags +104 bits 0/2/3/4/5/7, +105 bits 0/2 are
  handler-owned (1141-1160); damage[1] / damage[2]; the walk table **is read** (the flight / leap animation, `casterFlight` 1013-1036: block 0 near the ground,
  block 1 = +32 high in the air, the original's attack draws are in block 1), never for walking.
- **drake `$30`** (1546-1740): idle, after_hit; labels hurt s0891, grab s0893, hit s088E, land s0894, gloat_a/b
  s0895/s0896, leap_near s088D, leap s0890, jump s088A, fly s088B; hard-wired landing sounds `$2F` (arc < 8) or `$2D` + `$2E`
  (bank ids: the Balok bank must provide them); reads the first fighter's dagger count; knight reaction fixed 5 HP.
- **stalker `$40`** (fight_creatures.cpp:226-270): idle, after_hit, walk side (4 phases, steps 16/26/13/26; the table's
  hurt cells point at themselves and are never read); labels hurt s08AC, strike s08AA (action 8), heavy s08AB (action
  `$20`, chosen at x-distance 100..149); knight reaction fixed 7 HP (damage table unused).
- **Every AI** needs `die` (the `$B4` target of each hurt role, verified: see 1.2); the cel slots must hold every frame the roles
  draw.

**New AIs (later):** a C++ rule handler `src/game/rules/ai_<name>.cpp` with a `const AiProfile` (name, roles, events,
tunables, description, knight reaction to reuse), registered next to `fightTablesInit`; selected by `ai = <name>`;
gets a free type byte (`$3C`, `$44+`). The knight reaction tables must learn the new type, otherwise the monster is
harmless (fighters.cpp:415-425).

### 1.7 Loaders and memory

`rtCl*` loaders (combat_load.cpp:148-299) fill the 5 slots with `celAdv` or `hitAdv`/`hitLoad`, then `music(...)`
loads the sound bank (rt/combat_load.cpp:276-292). Creature cel arena 80,000 bytes of chip (enhcarve.cpp:126-130);
the largest original (Balok) is ~74,900 bytes decoded (77,562 by the loader's size estimate `decoded + $168 + 10 per frame
+ 10`, which is what must stay <= 80,000: `cel_size_estimate` in the Python ports). **The second area** (`LAB_05B9[11]`) is
`b9_44 == b9_0 == LAB_05C2`, the 50,000-byte (`$C350`) picture unpack / spare screen buffer in fast RAM (enhcarve.cpp:141-143):
Mudmen2 (est. 38,784) lives there, Balok / dragon / troll put `kn5.ob` there (est. 32,000), the demon its picture; a kit cel
set in that area shares it with kn5 (18,000 bytes left). Pools: 10 jobs, 20 creature records, 45 dirty rects. Cel draws are 85-90% of a fight frame (8.4a).

### 1.8 Sounds

`$A4 id` and the `$B0` sound pickers (`soundPick`, fighters.cpp:1819-1860) start a global synth sequence; samples come
from the creature's bank (11 banks, soundbank.cpp:47-77). A kit picks an existing bank and ids it serves. **The id-to-bank
map (verified, `tools/monsterkit/sounds.py`, catalog `sound_banks.id_ranges`):** the id IS the synth sequence index
(`rt::sfxRequest`); a sequence's `$D0` instrument selects (following `$B0` calls / `$D4` jumps) decide the buffer of its
samples: ids 1-19 and 79 / 109 the knights' bank (always loaded in a fight), 110-127 and 153-166 the campaign bank
(resident), 91-108 the ratmen bank (only with `Ra.a`), 128-133 the wizard bank (never in a fight), **20-90 and 134-152 the
creature buffer C8: right only with the creature file that holds those samples** (ids the original creature plays are right
by construction: catalog `creatures[].script_sounds`, `engine_ops`, `hard_wired_sounds`), 167 silence. Sound pickers
(`$B0` `LAB_02DC`..`LAB_02E9`, `LAB_0EB8`..) and their id sets are catalog `engine_ops` (tested against `fightOpRun`).
New samples are out of scope for v1.

## 2. The kit format

```
<kit>/kit.json        schema tools/monsterkit/kit.schema.json (kit: 1)
<kit>/sheet*.png      indexed or RGB sheets (the editor snaps RGB to the palette)
<kit>/frames/NNN.png  optional per-frame PNGs (what a clone writes)
<kit>/preview.gif     optional
```

Kits live under `build/kits/` (git-ignored) or outside git. Game clones contain original pixels and are never committed
(`monsterkit.py` refuses tracked paths).

```json
{
  "kit": 1,
  "name": "frost_troll",
  "title": "Frost troll",
  "cloned_from": {"kind": "game", "creature": "troll"},
  "ai": "stalker",
  "planes": 5,
  "palette": {"scene": "troll", "colors": ["#000000"], "own": {"9": "#5f5faa"}, "allow": [9,10,11,12,13,14,15]},
  "sheets": [{"file": "sheet.png", "grid": {"w": 96, "h": 96, "pad": 0}}],
  "celsets": [{"slot": 0, "file": "auto", "hits": true,
               "frames": [{"id": "stand0", "src": {"sheet": 0, "rect": [0, 0, 82, 92]},
                           "anchor": [41, 90], "attack": {"type": 0, "points": [[70, 30]]}}]}],
  "animations": {
    "stand": {"loop": 0,
              "steps": [{"ticks": 2,
                         "draws": [{"frame": "stand0", "slot": 0, "dx": 0, "dy": 0, "hurt": true, "attack": false}],
                         "move": [0, 0, 0], "sound": null, "events": []}],
              "motion": null, "on_dead": "die", "asm": null}
  },
  "roles": {"idle": "stand", "after_hit": "stand", "hurt": "ouch", "strike": "club", "heavy": "slam",
            "walk.side": ["walk0", "walk1", "walk2", "walk3", null, null, null, null], "die": "fall"},
  "hurt_by_action": ["ouch", "ouch", "ouch", "ouch2", "ouch", "ouch", "ouch2", "ouch", "ouch"],
  "damage": [0, 0, 6, 0, 0, 0, 0, 0, 10],
  "stats": {"hp": 40, "reach": 150, "keep_away": 90, "depth": 5, "wave_row": "troll"},
  "sounds": {"bank": "troll"},
  "arena": {"backdrop": "cave", "alive_max": 1, "total": 1, "spawn": "alternating", "frame_budget": 6},
  "tier": "auto"
}
```

Round-trip rules: every original draw becomes one `draws[]` entry; opcodes the steps cannot express go into `asm`
verbatim (a clone converts back byte-identical); `ticks` counts `$FF`s; anchors per frame, `dx/dy` per draw are deltas;
a T0 reskin pads/crops new art so the anchor lands where the original script expects it.

`tools/monsterkit/ai_catalog.json` (in git: names, roles, cites, summaries, tunable meanings, events as named pokes such
as `{"name": "retarget", "poke": {"off": 22, "size": "long", "op": "store", "value": "<animation>"}}`: `$A8` stores, it never
ORs a bit, and the 8 offered AIs have no events at all, see 1.2). Switching AI keeps the
role mappings that still apply; the checklist (R1) comes from the catalog; missing roles/`die`/events or attack roles
without attack points are **errors**.

## 3. The converter `tools/monsterkit.py`

```
py tools/monsterkit.py clone --game troll --out build/kits/frost_troll
py tools/monsterkit.py clone --kit path/to/kit --out path/to/new --name new_name
py tools/monsterkit.py check build/kits/frost_troll
py tools/monsterkit.py build build/kits/frost_troll --out build/kits/out [--tier t0|t1|t2] [--planes 5|6]
py tools/monsterkit.py catalog
py tools/monsterkit.py serve [--port 8765]
```

- **Clone from the game:** artconv export of the creature's cel files (anchors from the scripts), hit sets via a Python
  `hitParse`, scripts via a new disassembler `tools/fightscript.py` (reads mog DATA bytes + relocations like
  `gen_data.py`, names from `cell_names.yaml`), stats from the `init*` values, the bank from the loader, a fight-palette
  preview from a Python `sceneColors`/`sceneTail`. Output only under `build/kits/`. **From a kit:** copy, rename, set
  `cloned_from`.
- **Check:** palette classes (6..8 error, region-variable warn, 48..63 error at 6 planes); sizes (w <= 336 and (2 * ceil(w/16) + 2) * h <= 4800 per temp plane - the cel header holds 16-bit w/h, there is no 8-bit limit; attack points 0..255,
  hit points inside the frame, <= 99 points, dy fits s8, <= 256 frames, <= 5 slots); memory (cel totals <= 80,000, hit
  records <= the free region, T0 `collide.hit` <= 9000); pools (<= 10 hit pairs, <= 8 attack/hurt draws per tick,
  warn above 3 draws per tick, alive_max x draws within jobs/rects); frame-rate estimate vs the clone source (warn
  above 120% via the 8.4a model); the R1 checklist; tier detection.
- **Build T0:** `art/<ORIGINAL>.CEL` (artconv writer, byte8 = 1) + `art/collide.hit` with this creature's sets replaced
  (written to `build/` only), copied into `hd/art/`. **T1:** `<name>N.cel`, `<name>N.hit` (same text format, one set per
  file; `hitLoad` learns to try `<stem>.hit` in 9.5e3), a `[creature <name>]` row (`base = <source>`, stats, damage,
  scripts by name of the source, cel set, sounds, palette row) and an `[arena <name>]` row. **T2:** `mods/scripts.ini`
  in 8.8 text assembly, e.g.

  ```
  [script frost_troll.club]
  ifdead  frost_troll.fall          ; $B4
  draw    0 12 dx=-41 dy=-90 hurt   ; $00 slot 0 frame 12
  end                               ; $FF
  draw    0 13 dx=-41 dy=-90 hurt attack
  sound   $2F                       ; $A4
  end
  move    x=6                       ; $A0
  draw    0 14 dx=-41 dy=-90 hurt
  end
  done                              ; $FF $FF
  ```

  and the creature row names role scripts (`role.strike = frost_troll.club`).
- **Engine hooks needed (not part of the tool):** cel/hit sets by name + per-kit `.hit` lookup (9.5e3); **role scripts**
  `CreatureDef.aRoleScript[]` defaulting to the original labels, handlers call `roleScript(e, k, ROLE_x)`, found through
  a BSS side table by record index (9.8d); palette row in `ArenaDef`; `ai = <name>` mapped to the type byte.
- **Tests (host, no original data in git):** `test_monsterkit.py` (schema, cel round trip, Python vs C++ `hitParse` and
  `contactTest`, checklist, tier detection), `test_fightscript.py` (assemble/disassemble round trip, VM equivalence);
  with `build/disks` present: clone every offered creature, rebuild T0 byte-identical.

## 4. The browser editor `tools/monsterkit/editor.html`

One static file (HTML + inline JS/CSS), no build, no dependencies, no original data; runs from `file://`,
`monsterkit.py serve`, or as a published artifact. Opens a kit folder via the File System Access API (Chrome/Edge),
fallback file inputs + "Download kit.zip".

| Panel | Contents |
|---|---|
| Top bar | kit name, **AI picker**, **Clone from...**, Save, Export, tier badge |
| Left | **Checklist** for the chosen AI: role -> animation or "missing" (red), frames, duration; missing events and attack points flagged |
| Centre | pixel canvas (1-16x, grid, onion skin): slice, anchor, attack point, pencil/fill/picker, "hurtable" overlay |
| Right | palette (own 9..14 editable as 12-bit RGB, forbidden classes greyed), frame list, stats/tunables with the AI's meanings |
| Bottom | animation timeline: steps with ticks, layers, move, sound and event chips |

- **Palette snap** of RGB sheets to the nearest allowed index (12-bit rounding as artconv), diff overlay, forbidden
  nearest colours marked red.
- **Slicing** by grid or auto islands; **anchors** by click, aligned across an animation; **attack points** by click
  with the computed gate box.
- **Contact preview:** a knight silhouette at draggable distance/depth, JS `contactTest` incl. the mirrored-x quirk,
  both facings.
- **Animation preview:** JS VM subset (draw, `$FF`, loops, `$A0`, `$8C`, `$80`, `$A4` as a sound tick, `$B4` dead
  toggle, `$FD`/`$FE`) on a 50 Hz clock at 6 or 5 VBLs per tick, step mode, facing toggle, motion trail, depth band,
  reach/keep-away markers; AI walk preview by the AI's step table.
- **AI picker (R3):** catalog AIs with summary, cites, tunables, knight reaction, "advanced" badge; switching recomputes
  the checklist.
- **Clone from... (R2):** game monster (shows the `monsterkit.py clone` command, or calls `serve`'s `/clone`), or kit
  (copy + rename).
- **Validation** live (cheap checks in JS); errors block Export. ~2,500-3,500 lines; the VM/contact ports are tested by
  a `?selftest` page against vectors from `test_monsterkit.py`.

## 5. Steps

| Step | Work | Proof | Size | Needs |
|---|---|---|---|---|
| 9.8a1 | `tools/fightscript.py` (disassembler + text assembler), Python `hitParse`/`contactTest`/cel-size ports, `ai_catalog.json`; every [guess] verified (section 8) | host oracle tests | M | - |
| 9.8a2 | `monsterkit.py clone/check/build --tier t0`, schema, palette preview | clone all offered creatures, T0 rebuild byte-identical; one reskin shot | M | 9.8a1 |
| 9.8b1 | editor core: load/save, snap, slicing, anchors, attack points, pixel tools, checklist, AI picker, clone-from-kit | manual + `?selftest` | M | 9.8a1 |
| 9.8b2 | editor preview: VM port, fight clock, facing, motion, contact preview, walk preview | self-test vectors equal the C++ | M | 9.8b1 |
| 9.8a3 | `build --tier t1` | boot + play script fights the new creature | S | 9.4, 9.5e1-e3 |
| 9.8d | role-script indirection, `ai =` by name (defaults keep the original labels) | fighter tests unchanged; parity legs | M | 9.5e1 |
| 9.8a4 | `build --tier t2` (`scripts.ini`, events, motion, loops) | assembler round trip; a T2 kit boots | S | 8.8, 9.8d |
| 9.8c | example "Frost troll" T0 -> T1 -> T2 + tutorial | boot + play script, shots per tier, perf line | S-M | 9.8a4, 9.8b2 |

Order: 9.8a1; then 9.8a2 and 9.8b1 in parallel; then 9.8b2. After M9 9.4/9.5e: 9.8a3, 9.8d, then (after 8.8) 9.8a4 and
9.8c. T0 gives a usable reskin tool after 9.8a2 + 9.8b1, before any engine work.

## 6. Owner decisions (2026-10-07)

- T0 reskin is the first deliverable (9.8a1, 9.8a2, 9.8b1, 9.8b2), new creatures follow with M9.
- New monsters are placed by assigning them to lairs (`lairs.ini`); no new map lairs for now.
- Kits target 32 colours (5 planes); 64-colour kits come with the enhanced backlog.
- The editor opens through the local helper server (`py tools/monsterkit.py serve`), so "Clone from game" is one click.

## 7. Remaining open questions

1. AIs in v1: flyer, snatcher, brawler, brawler_b, spearman, caster, drake, stalker; demon and AI knight "advanced" or
   later; dragon/dagger/idle not offered?
2. T0 reskin as the first deliverable, or only new creatures (T1+)?
3. Where do new monsters appear: replace a lair's creature, new lairs/map nodes, or a debug key in v1?
4. Colours: a new 6-colour creature row per arena, or reuse an existing creature's colours? Region-variable indices:
   warnings or errors?
5. Sounds: existing banks and ids enough for v1?
6. 5 planes only, or also 6 planes / 64 colours (MS_ENHANCED) from the start?
7. Editor delivery: `file://` (Chrome/Edge) enough, or the local Python server by default (one-click clone from game)?
8. Frame-rate guard on a stock A1200: warn above the original troll's per-tick draw area, error at 150%?
9. New AIs: C++ rule handlers registered by name, or also a data-driven state table later?
10. Sharing: game clones stay in `build/`, only fully redrawn kits may be shared?

## 8. 9.8a1 verification record (2026-10-07)

Done in `tools/fightscript.py` (disassembler + text assembler, round trip over every creature script reachable from the 8 AIs,
the demon and every decodable script of the original, tested against the interpreter's host build), `tools/monsterkit/` (Python
ports `engine_ports.py` tested against the host C++, `sounds.py`, `ai_catalog.json`, `kit.schema.json`, `jsonschema_lite.py`)
and `tests/test_fightscript.py`, `tests/test_monsterkit.py`. Each former [guess] / [verify]:

| Item | Result |
|---|---|
| Death via `$B4` | **Confirmed**, with a correction: the die sequence must also call `engine LAB_0005` (fightCreatureDied) exactly once and end with `kill` (`$BC`) before `done`; a hurt entry of 0 frees the record without accounting (1.2) |
| Event pokes byte vs bit | `$A8` **stores** (the low byte of the long, `MOVE.B D1`); no original script pokes `+104/+105`: those flags are handler-owned, the 8 AIs have no events. The only pokes in the binary re-point `+22` (idle script) with a script pointer (dragon, demon) |
| Caster walk table | **Is read** (flight animation, blocks 0 / +32), contrary to the design note |
| Pair table terminator | **None**: `LAB_0A51` = 84 bytes = 10 pairs + a spare long, the asm compares without a bound, the C++ counts pairs (1.3) |
| Second cel area | `LAB_05B9[11]` = `LAB_05C2`, the 50,000-byte spare buffer, shared with kn5 for Balok / dragon / troll (1.7) |
| Max frame w/h | **No 8-bit limit** (16-bit header fields); the limits are the temp planes: w <= 336, `(2*ceil(w/16)+2)*h <= 4800` (`CEL_TEMP_STRIDE`), BLTSIZE h <= 1023 / words <= 63 |
| Sound id -> bank | id = sequence index; map from the synth tables (1.8, `sounds.py`) |
| Cel size formula | `decoded = sum(ceil(w/16)*2*h*popcount(planes))`, header `bits = 8*decoded` (exact on every original cel), loader estimate `decoded + $168 + 10*frames + 10` (host-tested) |
| `$98` | jumps when the Gore cell `LAB_06DA` is non-zero (Gore: Off / demo), not "if gore"; the assembler calls it `ifnogore` |
| `$C0` | frame-set list 1 = knights, 2 = the creature's 5 slots, 3 = map, 4 = effects |
| Tick marker | `$FF` is two bytes (the byte behind it is skipped); `$FF $FF` / `$FF $FE` are `done` / `end loop2` |
| Damage | the creature's own damage table is read only by the knight reaction of its type: flyer 5, spear 3, drake 5, stalker 7 fixed; snatcher [0], brawlers [2] / [8] raw, caster [1] / [2] |
| Hurt entries | required indices 1, 2, 3, 5, 6, 8 = the knight actions with attack draws (derived from the original in the test) |
| Tunables | reach / keep-away only where `approach()` runs (snatcher, brawlers, spearman, stalker); the caster uses +116 as leap speed and ignores +118; flyer and drake ignore all three (depth only for those that call `depthNear`) |
| Attack points | x / y are bytes (0..255, wrap above), 98 points per frame at most |

## 9. 9.8b as built (2026-10-07)

The editor is a folder of static files, `tools/monsterkit/editor/` (`index.html`, `editor.css`, `core.js`, `frame.js`, `anim.js`,
`app.js`; plain JS, no CDN, no original pixels or text), served with a JSON API by `tools/monsterkit/serve.py`:
`py tools/monsterkit.py serve [--port N] [--kits DIR] [--disks DIR] [--hd DIR] [--open]` (127.0.0.1 only, stdlib `http.server`;
default page http://127.0.0.1:8765/, `?kit=<name>` opens a kit directly). API list: the docstring of `serve.py`. Guards: Host /
Origin must be the local origin, writes need `X-MonsterKit: 1`, kit names are `[a-z][a-z0-9_]{1,31}`, static paths cannot leave
`editor/`. Tests: `tests/test_monsterkit_serve.py`.

- Home: one-click "Clone from game" cards (the 8 creatures, name field, default `<creature>_kit`), existing kits (Open, Copy).
- Frame editor: anchor, attack points (click / drag / right-click removes, gate box shown), pencil, fill, eyedropper, undo, zoom + Fit,
  onion skin, hurt tint, sprite sheet view per cel set, PNG import (server snaps to the palette, changed pixels shown in magenta,
  apply to the frame or the whole sheet), palette with the colour classes (6..8 red, 9..14 editable by double-click).
- Left: AI picker (catalog), the required-animation checklist (live check of the edited kit, green / red per role, a select per role
  and per required table index, Build is disabled while anything is red), stats / tunables (unused ones greyed) and damage fields,
  sound bank.
- Bottom: animation preview (50 Hz clock, 6 / 5 VBL per tick, left / right facing, hurt boxes, attack points, motion), Check output
  (check_kit messages), Build T0 + Install into a HD folder (default `build/autoplay-hd`).
- Not in 9.8b: animation step editing (a clone's verbatim `asm` stays authoritative; T2), contact-test preview with a knight
  silhouette, AI walk preview, `?selftest` JS vectors.

## 10. Web edition (9.8w)

`py tools/monsterkit/build_web.py` builds `tools/monsterkit/editor_web/monster_editor.html`, one self-contained Artifact page fragment
(title, style, markup, scripts; no doctype / html / head / body) from the same `editor/*` sources plus `editor_web/*`. No server: kits open
from the viewer's disk (a .zip of the kit folder, kit.json + sheet PNGs, or a dropped folder) and live in the page; `MK.api` is an in-page
store, the check is `WCHK.check` (a JS port of `check_kit` without the original-data parts: sound ids are not checked), PNG / zip use the
browser's Compression streams. The page opens with a code-drawn example kit ("Example kit (not from the game)", neutral palette).
Artifact sandboxes block page-initiated downloads, so "Save / export" offers Copy kit.json (clipboard, textarea fallback), Copy PNG as data
URL per sheet, the sheet as an `<img>` to right-click save, and a .zip link as an unreliable extra. Tests: `tests/test_monsterkit_web.py`
(file current, fragment contract, leakscan, node checks). `serve.py` binds with SO_EXCLUSIVEADDRUSE (no SO_REUSEADDR), so a second
`serve` on a used port fails with "port N is in use" instead of silently sharing it with an orphaned server.
