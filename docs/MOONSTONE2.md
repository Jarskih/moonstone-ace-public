# Moonstone 2: co-op party design note (ROADMAP 8.1)

Status: design, 2026-10-07. Documentation and code reading only; nothing here is implemented. Written against the C++ game at
HEAD `34db0bc` (`src/game`, `src/rt`, `include/game`). Authority for how the original behaves is `mog.asm`; every engine fact below
cites the C++ transcription as `file:line` (paths relative to `moonstone-ace/`). Tags: **[fact]** read in the code, **[inferred]**
follows from code but was not run, **[unknown]** needs a measurement or the owner.

Owner decisions of 2026-10-06 (ROADMAP M8), taken as given:

1. Up to four human knights play together as a **party**.
2. The party moves as **one token** on the map (one token, one move per turn).
3. **Loot is not split**: every knight receives the same loot, a full copy each.
4. Black knights met = **number of player knights**.
5. Stretch: Golden-Axe style scrolling lairs.
6. New weapons (poleaxe, spear with reach), bows through the existing dagger path, playable monsters (creature AI controller replaced by joystick input).
7. The original single-player rules stay intact behind an option (parity tests keep working).

Owner hardware: A1200, 68020, 8 MB fast + 2 MB chip. Fights run at 10 fps today (`docs/PERF.md`).

---

## 0. Summary for the impatient

* The original already has the *plumbing for four humans* but only as **hot-seat turns**: up to 4 knights, one on the map at a time,
  picked in the knight screen (`src/game/scene_menu.cpp:30-33`, `:159`, `:203-216`). A fight has **exactly two named fighters**
  (`ActiveKnights::ulCurrent` / `ulOpponent`, `include/game/world.hpp:15-17`), two joystick ports, and every creature has one
  target, the "first fighter" (`*e.pTarget = *e.pFirst`, 10 sites, section 4.2). Co-op fights are therefore **not a UI change**: they
  need a team model in the contact scan, a target chooser in ten handler heads, a controller abstraction for `Knight +11`, and more
  than ten job slots' worth of headroom.
* The hard limits, in order of how likely they bite: **10 job slots** (`COMBAT_JOB_COUNT`, `include/game/combat_script.hpp:122`; a dagger
  or effect needs one too), **45 dirty rects per frame** (`COMBAT_RECT_MAX`, `:123`, with a silent cap at `src/game/combat_script.cpp:277`),
  **blitter budget** (ROADMAP 8.4a, measured in 4.6: a fighter costs 390-460 beam lines, 85-90 % of it cel blits; 4 knights + 4 enemies run at about 4.4 fps on a stock
  A1200 and 7 fps with fast RAM, 10 fps needs the draws roughly halved; 12 job slots would be needed for 4 + 4 casters), **two knight palette triplets** (section 4.5), and **no HUD in fights** (section 4.4).
* There is **no spare knight record for black knights** when four humans play: the original's black knights are the *unused* knight
  slots (`ulKind == 4`, `src/rt/arena.cpp:1037-1038`), so with 4 humans there are none. Decision 4 needs a new pool of up to four
  black-knight records; the dragon is hard-wired as `mogKnights[4]` in at least 13 places in `src/rt` so the new records must go
  **after** it (indices 5..8), never in front.
* The owner's note on bows is partly off: the **player's and the AI knight's thrown dagger does not use `daggerAim`/`daggerStart`**.
  It is a plain scripted projectile (type `$34`, `fighterDagger`, `src/game/fighters.cpp:1721-1738`; spawned by `daggerRelease`,
  `src/game/creatures.cpp:631-640`). The ballistic six-slot trajectory table (`daggerAim`/`daggerStart`/`daggerStep`) belongs to the
  caster, the drake and the dragon's fireball. Both exist; section 5.2 says which one a bow should extend.
* ROADMAP 8.10 says a monster's stats come from `creatureTypeDef`. They do not: `creatureTypeDef` is the 24-row **lair** table
  (arena handler offset + creature count, `tools/tables.yaml:24-37`, consumed in `resetGame`, `src/rt/arena.cpp:1107-1109`
  `st32(a0 + 4, rd32(p7BD + 4 * i))`). Creature stats (HP, scripts, damage tables, reach tunables) are hard-coded in the per-creature
  initialisers `init0169`...`init019F` (`src/rt/arena.cpp:487-640`). Section 5.3 uses those.

---

## 1. What the original already supports

### 1.1 Players, knights, turns

| Fact | Where |
|---|---|
| Number of humans 1..4, menu item "Players" | `src/game/scene_menu.cpp:29-34` (`menuSetPlayers`), `include/game/scene_menu.hpp:69` |
| Knight select: each human picks one of four portraits, name entry; the pick sets `ulKind = portrait (0..3)` and `+11 = 2` | `src/game/scene_menu.cpp:203-216` (`finishName`, `:209`) |
| The humans occupy record slots 0..n-1; the remaining slots keep `ulKind = 4` = AI knight = **the black knights** | `src/rt/arena.cpp:1037-1038` (`resetGame`), `src/rt/arena.cpp:1120-1143` (`startKnights` loops `until d0 == mogHumanPlayers`) |
| Knight records: 5 x 132 bytes (`mogKnights[5]`: four knights, then the dragon at index 4), 5 inventories of 24 bytes | `include/game/state_bind.hpp:16-19`, `include/game/knight.hpp:73,132` (`sizeof == 132`) |
| Four-slot round-robin, hard-coded mask: `turn = (turn + 1) & 3`; a round ends when it wraps to 0 (daily upkeep) | `src/game/rules.cpp:207-212` (`advanceTurn`), `src/game/overworld.cpp:694` |
| A turn: budget = `derivedEndurance << 4` movement steps, one per frame on the joystick; Esc ends the turn | `src/game/rules.cpp:188-199` (`setupTurn`), `src/game/overworld.hpp:19` (frame sequence) |
| A human knight on the map always reads **port 1** (`+11 = 2` is written by `setupTurn`) | `src/game/rules.cpp:190`, `src/game/fighters.cpp:115-118` (`inputFor`) |
| Game over when every human was passed over (no lives) in a row | `src/game/rules.cpp:218-222` |
| Four fixed-4 loops that matter for a bigger table: `knightsRecalcAll`, `dayTurnover` | `src/game/rules.cpp:61`, `:153` |
| Rounds: 4 rounds = one day (sub-counter wraps), the moon/lunar index steps per day | `include/game/world.hpp:24-25`, `src/game/rules.cpp:196-215` |
| Player count scales one rule: `uwDamageDiv` = 3,2,1,1 for 1..4 players (the black knight's stat-purchase cost) | `src/game/scene_menu.cpp:14,33`, `include/game/scene_places.hpp:69-71` |
| There is **no save game**; all state is RAM | [fact] no save code in `src`/`include`; `docs/GAME_STATE.md:10,64` only mention emulator save-states used for research |

### 1.2 Fights

Three entry points, all two-sided at the engine level (`src/game/combat.cpp`):

| Fight | Entry | Fighters | Ports |
|---|---|---|---|
| Knight vs knight (map duel; human vs black knight) | `fightMeet` `:235-300` | `ActiveKnights::ulCurrent` (first), `ulOpponent` (second) | the first non-AI knight gets `+11 = 2` (port 1), the second `+11 = 1` (port 0); an AI knight gets no port (`:256-265`) |
| Knight vs creatures (lair, wave of creatures) | `fightCreature` `:319-340` | one knight (`mogCurKnight`) vs a wave spawned by the lair's arena routine | the knight keeps `+11 = 2` (port 1) |
| Knight vs dragon / valley guardian | `fightDragon` `:343`, `arenaDragon` `:470-495`, guardian `arena01A0` (`src/rt/arena.cpp:754`) | one knight vs dragon + 2 bats / one guardian | port 1 |
| Practice (menu) | `arenaPractice` `:447-466` | knight 0 (port 1) vs knight 1 (port 0), both human | both ports |

* Joystick selection is a **single comparison**: `Knight +11 == 1 ? port 0 : port 1` (`src/game/fighters.cpp:115-118`); the same field
  chooses the pad that drives the cursor on loot/town screens (`src/rt/combat_ui.cpp:1037-1045`, `rd8(mogUiKnight + 11) == 1`). The other
  writers of `+11`: `setupTurn` (`rules.cpp:190`), `finishName` (`scene_menu.cpp:210`), `scenePractice` (`src/game/mainloop.cpp:49,52`),
  `resetGame` (`+11 = 4` for AI map knights, `arena.cpp:1037`), `fightMeet` (`combat.cpp:256-265`).
* The joystick read produces **two** words, one per port (`ms::JoyBits { uwPort0, uwPort1 }`, `include/engine/input.hpp:28-31`;
  `joyPoll` at `src/rt/input.cpp:70-78`; `rtMogJoyReadC` returns them packed, `:210-213`). Bits: 1 right, 2 left, 4 down, 8 up, `$10` fire.
  Port 0 filters impossible left+right / up+down combos (mouse-counter glitch, `src/engine/input.cpp:33-38`).
* The fight loop (`fightRun`, `src/game/combat.cpp:115-160`) is one frame = `frameStart, jobPass (AI handlers + script engine), tick,
  flip, contactPass, drawPass, lowHpWarnings, endAndPause, frameWait`. It ends when **the first fighter's HP is negative**
  (`endAndPause`, `:87`), or when the creature counters run out (`fightCreatureDied`, `:172-191`). `settleDefeats(first, second)` then
  gives each fighter at HP <= 0 full HP and **-1 life** (`:150`, `include/game/rules.hpp` `settleDefeats`). A loss in a creature fight
  shows the temple screen and returns to the map (`:328-331`).
* A knight's arena entry is `placeAttacker`: the current knight enters at x `$FA`, depth `$64`, facing left (`src/rt/arena.cpp:345-354`).
  `arenaMeet` puts the second knight at x `$1E`, depth `$4B`, facing right (`src/game/combat.cpp:429-443`).
* Creature waves: `LAB_05ED` = most alive at once, `LAB_05EC` = total; both grow with **the one fighter's** strength / max HP
  (`scaleWave`, `src/rt/arena.cpp:417-433`); the lair's own count replaces the total (`:434-436`). `spawnOne` allocates one record + one
  job per creature (`:372-385`).

### 1.3 Fighter, job and record limits

| Resource | Size | Where | Consequence |
|---|---|---|---|
| **Job table** (`CombatJob`, one per drawn actor: knight, creature, thrown dagger, effect object) | **10** | `include/game/combat_script.hpp:122`, `src/game/creatures.cpp:573-597` | A full table makes `jobCreate` return 1; `creatureSpawn` has already allocated a record and nothing retries (`creatures.cpp:596`, `:612-628`). A 4-vs-4 fight leaves **2** jobs for every thrown dagger, spark and dragon object. |
| Creature/projectile record heap | 20 records x 132 B (a 21st alloc returns one record past the heap) | `include/game/creatures.hpp:96`, `src/game/creatures.cpp:600-609` | Fine for 4+4 plus projectiles. |
| Ballistic dagger slots | 6 (20 B each, keyed by the thrower's address) | `include/game/creatures.hpp:79-95` | Caster/drake/dragon only; see 5.2. |
| Attack / hurt lists | 8 entries x 10 B per job, 800 B per list | `include/game/creatures.hpp:98-100`, `src/game/combat_script.cpp:268-300` | Per-frame cel overlays (a fighter's body parts) share one 8-entry slot. [unknown] whether any frame exceeds 8 flagged draws; no bound check in the writer. |
| Dirty-rect list | 45 entries/frame, further rects are silently **not listed** (count keeps growing) | `include/game/combat_script.hpp:123`, `src/game/combat_script.cpp:272-283` | **Risk**: each draw event adds a rect. Measured (8.4a): 3.5-3.7 rects (3.8-4 cel draws) per fighter per frame, 26-34 for 4 + 4 plus 1-2 per dagger or spark, peaks 4-8 above the average, never above 45 in any run: thin margin for 4 + 4 casters, raise the cap to 64. |
| Click pool (loot/town screens) | 2,400 B = **100** 24-byte records | `src/rt/combat_ui.cpp:975-978` (`clearBytes(mogRecordTable, 0x960)`) | Each icon is one record (`registerRegion`, `:220-240`). A 4-knight screen draws up to 4 inventories of icons. [unknown] current peak; four panels of 12 slots with counts need up to ~4 x 40 icons. |
| Knight/actor record | 132 B, fixed stride in 10+ places | `include/game/knight.hpp:132`, `include/game/overworld.hpp:88-93` (`RecordSet`, stride 132, 5 records) | Do not widen the record; use spare bytes (+87 and +131 appear unreferenced: no `87(An)` / `131(An)` in `mog.asm`, no C++ use; [inferred], confirm with a grep of the lifted code before use). |
| Draw sprites | one blit per cel into a 320x200, 5-plane screen, 40-byte rows | `include/engine/bgblit.hpp:20-29` (`BG_PLANE_BYTES = 8000`) | Scrolling lairs (8.6) need a wider bitmap and a different background restore. |
| Handler table | longs indexed by the **type byte** (`rd32(table + type)`), types `$00..$44` used | `src/game/creatures.cpp:653`, `include/game/fighters.hpp:22-33` | New fighter types (player monster) need a new type value and a table slot beyond `$44`/`$48`; check the BSS cell's extent first [unknown]. |

### 1.4 How a fighter acts (what co-op and playable monsters hook into)

* `fighterKnight` (type `$0C` / `$38`): reads its pad (`inputFor`), turns direction+fire into an **action** (a byte offset into the action-script
  table at `Knight +34`, `kActionRight/Left`, `src/game/fighters.cpp:25-28`, `:446-478`), or walks (steps `kStepX/Up/Down`, `:18-20`),
  clipped by `clipToArena` (x +-25 within 10..320, depth 30..155, `:171-190`), by other fighters (`blockedMask`, `creatures.cpp:294-340`)
  and by the arena wall table (`e.obstacles`, `src/game/arena.cpp`).
* Action numbers: `4` thrust, `8` cut, `$0C` **dagger throw** (fire + up + the key opposite to the facing: `kActionRight[10]` / `kActionLeft[9]`),
  `$10`/`$1C` guards, `$14`/`$18` other strokes, `$20` the big blow (damage doubled), `$40` a fire+all-directions entry. The AI knight uses
  the same numbers directly (`src/game/fight_creatures.cpp:561-600`: action `4` and `$20` by distance, `$0C` when `ubDaggers != 0`).
* Damage: base from the attacker's damage table (`Knight +42`) indexed by action, + strength (+70), + sword bonus 0/2/3/5 for `$16..$19`
  (`+88`), doubled for action `$20` (`include/game/creatures.hpp:173-178`, `src/game/creatures.cpp:359`).
* **Contact is team-blind and one-to-one.** `contactScan` tests every active job's attack list against every *other* active job's hurt
  list within 10 depth (`depthClose`) and stores **one** hit per attacking job per frame (`ulHitTarget` / `ulAttacker`, first hit wins,
  job-table order decides) (`src/game/creatures.cpp:243-291`, filter spot `:256-259`). A defender can only remember one attacker.
* **Bodies block each other** (`blockedMask` over all other jobs with HP > 0, `creatures.cpp:294-340`): four knights will jam each other
  in a 310 x 125 px arena (x 10..320, depth 30..155) unless allies are skipped in that loop.
* Reaction tables and arena set-ups **mutate shared global script/damage tables in place** (`arenaKnightTables` points every knight at the same
  tables, `src/game/combat.cpp:373-384`; `arenaDragon` pokes hurt/damage entries, `:476-484`; `arena0175`/`arena0188` patch the current knight's
  action/hurt tables, `src/rt/arena.cpp:672-698`). That is fine with one knight at a time and wrong for four knights with different weapons.

---

## 2. Per-screen changes for co-op

Terms: **party** = 1..4 player knights (records 0..3, `ulKind` 0..3). **BK** = black knight (AI knight). Classic mode = the original rules, untouched.

### 2.1 Title menu and knight select

* New menu item "Mode: Classic / Co-op" directly under Players (the Gore row is gone, owner 2026-10-11; Players keeps the count in both modes) (`include/game/scene_menu.hpp:53` `MenuItem`, `src/game/scene_menu.cpp`). In co-op mode the player count means
  "knights in the party" (1..4); in classic mode nothing changes. Practice stays as it is.
* Knight select (`SceneKnights`, `scene_menu.cpp:154-300`): after choosing a portrait and name, a **controller** is chosen for each knight
  (joystick port 0/1, adapter port 3/4, keyboard set A/B; section 3). Today the pick writes `+11 = 2`; in co-op it writes the controller
  index (section 3.3). Controller conflicts (two knights, one pad) are refused on the screen.
* Duplicate portraits are impossible (one per knight, `ubFree` bit mask, `scene_menu.cpp:163-165`), which keeps the four palette
  triplets distinct (section 4.5).

### 2.2 Map (`src/game/overworld.cpp`, `src/rt/overworld.cpp`)

Decision 2: one token, one move per turn.

* **Party token.** Represent the party by a **leader record** (any of the four, the one whose turn the slot is) that carries the map position; the
  other party records mirror `uwMapX/uwMapY/uwTileX/uwTileY` (`knight.hpp:126-128`, `:96-97`) from it at the end of every step (new
  `partySyncMap()` in `src/game/party.cpp`). Drawing (`mapDrawSelf`, `mapDrawOthers`, `src/game/overworld.cpp:753-790`) draws one party sprite;
  the status screen (Space) cycles the party through the exchange-screen "next knight" logic (`lootNextKnight`, `src/game/loot.cpp:43-52`).
* **Turn slots.** Replace the hard-coded `& 3` by a slot count (`TurnState`, `rules.hpp`): slot 0 = the party, then each BK group slot. A round is every
  slot having moved once, a day = 4 rounds as now (`include/game/world.hpp:24-25`), so days/moon phases keep their pace relative to the number of
  turns. [decision, section 7 Q4] Party budget = **leader's** `derivedEndurance << 4` (simplest, parity with a 1-knight game) vs. the **minimum** of
  the party (nobody is left behind). Recommended: minimum, because the party is one token.
* **Input on the map**: any party member's pad moves the token? Recommended: **the leader's pad only** moves; the leader rotates each round (so everyone walks
  in turn) and any member's fire button opens the node menu. Alternative: "majority/any pad" movement is jittery on a 25 fps loop and invites fights over the stick.
* **Proximity scan** (`scanMap`, `overworld.hpp:293-299`) is per-record: run it once for the leader; the "other knights near" rows become
  "BK group near" rows (`{record, 1}` rows are today the arrival-menu entry "Battle with <name>", `overworld.hpp:201`, `AR_DUEL`).
* **Dragon**: it engages one random living knight (`dragonPickTarget`, `src/game/overworld.cpp:619-628`) and has a four-slot touch list
  (`aulTouch[4]`, `:561-575`). In co-op it engages the party (target = leader for flight direction, `src/rt/overworld.cpp:662`), and the
  fight is a party fight (2.3).
* **Home villages** `$15..$18` accept only the knight with the matching index (`include/game/world.hpp:63-65`, `LAB_006C..006E`). [Q7] Any member of the party
  or only that knight's own village?

### 2.3 Encounters and black knights (decision 4)

Original: a duel starts when a knight stands on another knight's map box and picks "Battle with <name>" (`AR_DUEL`, `overworld.hpp:235`,
`fightMeet`); the AI knights roam, shop, and pick one human opponent (`aiPickOpponent`, `overworld.cpp:229-262`, skipping kind 4 and itself).

Co-op:

* **Fight-extra pool** (the first design's black-knight pool; the map's black knights are now the original records 2, 3, next bullet). Add up to four BK records (`mogKnights[5+i]`, inventories in a new array; do **not** widen or renumber `mogKnights[0..4]`: the dragon is index 4 in
  13 places, `src/rt/overworld.cpp:636,662,783`, `src/rt/arena.cpp:264`, `src/game/overworld.cpp:570,630`...). `ulKind = 4`, `ubType` `$10`, own gold/progress/shopping via the
  existing `aiShopWish/aiBuyStat/roamPick` code paths, which are record-indexed and 4-slot-bound (`overworld.cpp:229-262`, `RecordSet` is 5 records).
* **How many fight (owner decision 2026-10-09, replaces the one black-knight group token).** The map's black knights are the ORIGINAL AI knights: the knight records the party does not use (records 2, 3 of a two-knight party, `ulKind` 4, original lives / stats / names, at their normal `LAB_01AE` start spots, each its own token, own turn slot and the original AI; a party of four has none on the map).  On contact the fight is party size N vs. N black knights: the engaged map knight plus N - 1 FIGHT EXTRAS from the pool (records 5.., never on the map; `fight_seed.cpp`, the engaged record first), and after a won fight the extras hand their belongings to the engaged knight, whose pile the loot screen shows.
* **Difficulty.** BK stats come from the BK pool (they level with `aiBuyStat`, cost `uwDamageDiv`, `scene_places.hpp:69`); N BK vs. N knights is
  already "fair" by construction. A balance knob (HP scaling by party size) lives in 8.7.
* **Loot from a won BK fight.** `settleFight` moves **one item** (or everything if the loser has no lives) from a loser to the winner (`include/game/rules.hpp:52-67`,
  transfer table `LAB_0028`). In co-op each defeated BK's transfer is computed once into a "pot" (a scratch inventory) and then **copied in full to every knight** (decision 3, section 2.7).
* **AI targeting** in a BK fight: `fighterAiKnight` targets `pFirst` or `ulOpponent` (binary, `src/game/fight_creatures.cpp:611`); see 4.2.

### 2.4 Lairs (creature fights)

* The lair's arena routine (`LAB_0168`..`LAB_01A0`, `src/rt/arena.cpp:654-760`) loads one creature cel kind, sets the wave size (`waveCells`/`scaleWave`) from **one** knight's
  strength and max HP and spawns from a list at the arena's right edge. Co-op: `scaleWave` takes the **sum/max over the party** (`src/rt/arena.cpp:417-433`) so a four-knight party
  faces a bigger wave; the cap `LAB_05ED` (alive at once) rises with party size (it is 1..3 today: `waveCells(1, 3, ...)`, `:657`). The exact formula is a balance question (8.7); the seam is `scaleWave`.
* Knights enter at the right edge staggered in depth (today all at `y = $64`, `placeAttacker`, `arena.cpp:345`): a new `placeParty()` spawns N knight jobs.
* Fight end: win when the creature counters reach zero (`fightCreatureDied`, `combat.cpp:172-191`), lose when **all** party knights are down (new rule, replaces "first fighter negative HP", `combat.cpp:87`).
* Creature cel set: one kind at a time (`LAB_05DF`, `include/game/combat_load.hpp:20-25`); unchanged.

> **State 2026-10-09 (ROADMAP 8.5, branch wt-8_5):** loot copies, the shop queue, valley copies, the stone owner's ending and the N-generic lives rules are implemented for 2..4 knights; see ROADMAP 8.5 and `include/game/party_loot.hpp`.  The sections 2.5-2.8 below are the design they follow, with two differences: the copy is "replay the original take on a snapshot of the pile" (not a new `partyReplicate(slot, delta)`), and the ending is the stone owner's only (Q8 decided).

### 2.5 Valley of the Gods and the dragon

* Valley (`placeVisit` `PK_VALLEY`, `src/game/placevisit.cpp` `case PK_VALLEY`): needs all four keys in **the visiting knight's** inventory (`valleyKeysComplete`, `scene_places.hpp:165`).
  With loot copies every knight holds the same keys, so any member can trigger it; the guardian fight becomes a party fight (`arena01A0`, HP `$8C`, `arena.cpp:754-775`). A win gives
  +3 progress and takes the keys (`valleyVictory`), a loss costs 2 lives and a stat point (`valleyDefeat`) **per knight who is down**; the guardian's random moonstone
  (`valleyMoonstone`, `scene_places.hpp:174`) is part of the loot -> copied to all.
* Dragon: party fight, dragon + 2 bats (`arenaDragon`, 3 jobs besides the knights); dragon loot screen (`SCENE_DRAGON`) uses the same copy rule. The dragon fight needs the dragon's
  `pTarget = pFirst` single target generalised (4.2) and its touch list (`aulTouch[4]`) read for the party.

### 2.6 Stonehenge and the ending

* `stoneMatches(frame, stones)` tests the **current** knight's moonstones (`scene_places.hpp:151`); with copied loot all knights agree.
* The ending code depends on the knight's kind: `stoneEnding(frame, kind)` sets one of four knight bits (`scene_places.hpp:155`, `src/game/placevisit.cpp` `PK_STONEHENGE`), then the overlay switch
  runs `program` (the ending) and **never returns** (`pfnRunProgram`). In co-op: the knight who steps on the stone (the leader) picks the ending; [Q8] the ending is
  per knight, so either play the leader's ending once, or chain the four endings (the re-entry mechanism from ROADMAP 2.12 would have to loop; not trivial).
* **Colours of the ending (2026-10-11).** The original ending is already per knight: the kind bits of the ending code (3 red, 0 blue, 1 orange, 2 green) are the boot flags the program overlay reads, and its loader (`engine/scenes.cpp` `tweakPalette`) overwrites the class ramp (palette entries 8..11) of bg5 / bg5a / bg3 / bg2a with that knight's four colours (the cloak in li1.cel, co1 and da1 draw with those entries; bg7 / bg8 have no class colours: bg7 is the galaxy, bg8 the villager). The flags word (`rt_boot_flags`) is the cell that survives the mog -> program switch, so nothing new crosses. Moonstone 2 only chooses the kind: local game (classic or co-op) = the knight on the stone, unchanged (the classic ending stays byte-identical); network game = each machine its own knight (the stone owner when local, else the first record bound to this node), by `endingKnightRecord` / `endingFlagsForKind` (`game/ending_knight.hpp`, test `tests/test_ending_knight.py`), applied in `pvRunProgram` (`src/rt/overworld.cpp`) before the session closes. The ending code in the quest text is unchanged. A node playing on alone has no lockstep and keeps the stone owner's colours.
* **Colours of the other non-fight scenes (2026-10-11).** Same rule, one pure choice (`game/local_knight.hpp` `localKnightRecord` / `localKnightKind`, test `tests/test_local_knight.py`; the ending's `endingKnightRecord` delegates to it): in a network game every machine runs the whole game, so a screen of another node's knight is also watched here; a screen that shows "the player's knight" draws THIS machine's knight (the shown one when local, else the first record bound to this node). Classic, local co-op and a node playing on alone: unchanged. Display only (a palette, never state, RNG, timing or the state hash; `mogUiPalette` is not hashed), so every machine runs the same ticks.

  | scene | what it shows in a network game |
  |---|---|
  | ending (bg5 / bg5a / bg3 / bg2a, li1 / co1 / da1 cloaks) | this machine's knight (2.6 above, boot flags) |
  | dice house, ritual (hen) screen, wizard (`colourRamp`, `src/rt/screens.cpp`: UI palette entries 6..10, five / four shades) | this machine's knight; unchanged elsewhere |
  | ending lift (Klift1 on the bg7 galaxy palette `01CB`) | unchanged: that palette is the galaxy's own (entries 8..11 are galaxy colours of the picture too), there is no class ramp to swap without tinting the background |
  | map tokens, the map menu (knight sprite + name), the party row, the corner box of whose turn it is | unchanged: they name a specific knight, and the other players must see who it is |
  | status sheet, temple, mystic, healer, town / lair / Stonehenge text screens, Next Day | nothing in them depends on the knight's kind (no class ramp, no per-kind picture), so nothing to change |
  | fights, duels, meeting / exchange screens (`screenPalette`) | unchanged: they show specific knights and their colours tell them apart |
* Danu's offer (no matching stone): `danuBlessing` +1 life for the knight who offers an item (`scene_places.hpp:159`); in co-op the offering knight's life, or all knights'? Default: the offering knight (it is a trade).

### 2.7 Loot, gold, lives (decision 3)

* **Loot screens** (`SCENE_CREATURE`, `SCENE_DRAGON`, `SCENE_MEET`): one click handler (`src/game/loot.cpp`, `include/game/loot.hpp:9-12`) moves one item from the pile to **the screen's knight**
  (`mogUiKnight`, set by `LAB_058A`). Rule: when a party member **takes** item X (any take button, including the gold pile `LAB_0553`/`goldPile`, `src/rt/combat_ui.cpp:327`), the same amount is **added to every other party
  member** and removed once from the pile. Counted slots (`Inventory +n`, bytes) add; `+20` keys and `+22` moonstones are flag bytes and are OR-ed (`LAB_053F` rules, `loot.hpp` comment on `$14/$16`). The cap rules (dagger count 10,
  `src/game/scene_town.cpp:72`) apply per knight independently. Implementation seam: one `partyReplicate(slot, delta)` called from the loot-move and gold-take branches (pure, in `src/game/party.cpp`).
* **Gold found by rolls** (`lootRoll046C/0471` at game reset, `src/rt/arena.cpp:1067-1073`): the pile is given to everyone in full (decision 3). Expect gold inflation x party size; the shop prices are not scaled (balance, 8.7).
* **Lives/death.** `settleDefeats` today: first/second fighter at HP <= 0 -> full HP, -1 life (`include/game/rules.hpp`). Co-op proposal: **a knight who is down when the fight ends loses a life** (as now), whether or not the party won; if every
  knight is down the party loses (temple screen). [Q5] Alternative: a knight down in a won fight just recovers. Game over = all party knights at 0 lives (`advanceTurn`, `rules.cpp:218-222`, generalised to the party).
* **Frog curse / Math / daily upkeep** apply per record already (`knightDailyUpkeep`, `rules.hpp`); frog days skip a *turn* (`rules.cpp:218`): in co-op the party turn is skipped only if **all** members are frogged.

### 2.8 Towns and temples: who shops

Town screens work on one knight (`LAB_0633`/`mogUiKnight`): smith, market, healer, dice, temple stats. Co-op flow: entering a town enters a **shop queue**: each member shops in turn with
**their own** gold and inventory (the same screens, a "next knight" button already exists on exchange screens, `nextKnightButton`, `src/rt/combat_ui.cpp:311-326`), with that knight's controller as the cursor
(`cursorTick` already selects the pad by `Knight +11`, `:1043`). Time cost: the party's turn is spent once (`exitSpent`, `placevisit.cpp`), not per knight. The dice house is per knight. Healer/temple: per knight.
The knight-to-knight **exchange** screens (`SCENE_EXCHANGE_8/11`) become "party exchange" between members and cost no turn.

**Owner change 2026-10-10 (ROADMAP 8.5 / 8.5a): no turn queue, arrows instead.**  A town, the home village's temple sheet and the wizard run once, the leader first; the corner carries a `<` and a `>` arrow (two or more living members) that switch the active knight to the previous / next living member, wrapping (`shopQueueStep`, `rt::shopSwitch`): the corner icon and name, the pad that moves the cursor, the gold / inventory / shops and the banner "SIR <NAME>'S TURN" follow the new knight.  One Exit ends the visit for the whole party.  The corner is only the name between the two arrows, drawn straight on the picture (no box, no token; owner 2026-10-26): the small menu font in its own colour with a one-pixel black outline, so it reads on every picture.

**Whose turn it is (ROADMAP 8.5a, owner-approved 2026-10-09, revised the same day; `include/game/party_turn.hpp`, `src/rt/party_turn.cpp`).** The place screens of the queue look the same for every knight, so two cues, co-op only (classic stays byte-identical; the mouse pointer keeps its NORMAL colours, the first version's class-coloured pointer was dropped by the owner): (1) the active knight's name (without `SIR `, 8 characters) between the `<` `>` arrows in the UPPER-LEFT corner of the picture for the whole turn, drawn straight on the picture in the small menu font with a one-pixel black outline (owner 2026-10-26: the first versions had a parchment / black box and the knight's map TOKEN beside the name; the token was drawn with the picture's own palette entries 29..31, which hold the map's class colours only on the map: on the town and shop palettes it came out purple or white, whoever the knight was, so it went); on the town pictures it is drawn into the clean background (the pixels under it are saved once and put back before the next knight's name, so a switch does not smear), on the black shop / temple / wizard sheets after every redraw of the sheet (`ScreenOps::turnCorner`; the redraw blits the clean sheet first); (2) a banner `SIR <NAME>'S TURN` (a name that already starts with `SIR ` is not prefixed; the name buffers keep a space as `_`) over the picture when a member's turn starts, in the big font on a parchment box on the town screens (small centred text on the sheets), removed at the first input of any pad (a press still held from the previous turn's click does not count) or after 75 frames (1.5 s), the picture restored from the clean background. Applied to town A / B, the home village's temple sheet and the wizard's temple sheet (screen 9, through `ScreenOps::turnBanner`) and to the smith / market screens of a town visit. Not covered: the creature loot screens (the map's cel table is not guaranteed there), the wizard's Math speech screen and the dice / healer / mystic screens.
---

## 3. Input plan

### 3.1 What exists

| Item | Where |
|---|---|
| Port 0 = `JOY0DAT` + CIAA PRA bit 6, port 1 = `JOY1DAT` + PRA bit 7 (both fires active low) | `src/rt/input.cpp:66-78` |
| Joystick sampled by `rtMogJoyReadC` (also from VBL hooks for the cursor) | `src/rt/input.cpp:210-213`, `src/rt/combat_ui.cpp:1039-1045` |
| Keyboard ISR feeds `mogKeyDown[128]` + last-key cell; ACE owns the CIA-A serial handshake | `src/rt/input.cpp:22-36`, `:138-170` (`deliverKey`), `src/engine/input.cpp` (`keyDecode/keyApply`) |
| `autoplayJoy(bits)` injects scripted joystick state into the same two words | `src/rt/input.cpp:74` |
| Menu/fire waits use **port 1 only** (`rtMogFireWait`) | `src/rt/input.cpp:215-224` |

### 3.2 Four controllers

Introduce `struct Pad { uint16_t bits; }` and `Pad g_pads[4]` filled once per `joyPoll` (`src/rt/input.cpp:70`, rename/extend; `JoyBits` stays for classic callers):

| Controller | Source | Notes |
|---|---|---|
| 0 | Port 1 joystick (`JOY1DAT`, PRA bit 7) | what P1 uses today |
| 1 | Port 0 joystick (`JOY0DAT`, PRA bit 6) | the mouse port; keeps the existing glitch filter (`engine/input.cpp:33-38`) |
| 2, 3 | **Parallel-port 4-player adapter**, joysticks 3 and 4 | Direct hardware read in `joyPoll`; see below |
| K0 | Keyboard set A: arrows + Right Ctrl (fire) | from `mogKeyDown[]` |
| K1 | Keyboard set B: W A S D + Left Alt (fire) | from `mogKeyDown[]` |

* **Parallel adapter.** A four-player adapter reads the parallel port's data lines (CIA-A PRB, `$BFE101`, DDRB `$BFE301` set to input) for the extra directions and the BUSY/POUT/SEL lines
  (CIA-B PRA bits 0..2, DDRA bits cleared) or the data lines for the fires; the exact line-to-button assignment is **adapter specific** and must be taken from the documentation of the
  adapter the owner has (Dyna Blaster style). [unknown] I did not verify the pinout here; do not code it from memory. The routine is a pure function `adapterRead(prb, ciabPra) -> {bits3, bits4}`
  in `src/engine/input.cpp` with a table-driven mapping, host-tested; the hardware read stays in `src/rt/input.cpp`. M2 ownership rule (CLAUDE.md): the game already owns CIA-B disk bits;
  only bits 0..2 of CIA-B PRA are parallel-port status lines, and the OS parallel device is not in use while the game owns the machine; `files.cpp`' `systemUse` must not touch
  DDRB/DDRA of those registers across a disk read (re-set them after every OS give-back).
* **Keyboard sets.** Amiga keyboards report multiple simultaneous keys, but the A1200 matrix can **ghost** when 3+ keys in a rectangle are pressed together: two players on one keyboard
  are fine for walking+fire, four-key chords (fire + up + back for the dagger throw, `kActionRight[10]`) may drop. Offer fire/dagger as separate keys in the keyboard sets (see 5.2) instead of chords.
  **Ghosting check for two sets at once (owner 2026-10-09: arrows + Ctrl / RShift together with WASD + Alt, Coop 3 / 4 uses both):** an Amiga raw key code is the key's matrix
  position (row = code >> 3, column = code & 7); a diode-less matrix shows the 4th corner of any L of three pressed keys. [unknown] whether the A1200 matrix has diodes and whether
  the modifiers ($60-$67) are scanned inside it: it cannot be measured in an emulator (WinUAE ghosts like a PC keyboard, not like the Amiga one), so `tests/test_keyghost.py` models the
  worst case (everything in the matrix at its raw code). Result of that model: (1) any two keys never ghost; (2) the direction keys sit in columns 4-7 (arrows, row 9) and columns 0-2
  (W $11 row 2; A S D $20-$22 row 4), so no two keys of the two sets share a column; (3) the fire keys are the only link: Ctrl $63 / RShift $61 / Alt $64 share row 12 with the other
  modifiers and Alt shares column 4 with Up, so some combinations show a phantom - always one of LShift $60, CapsLock $62, LAmiga $66, RAmiga $67, T $14, F $24, $49, $4B, none of which the game
  reacts to; (4) **no phantom is E (end turn), Space (status sheet), Esc, Return or Backspace** for any combination a player can hold on purpose (no opposite directions of one set,
  one fire key per set), so the chosen keys stay. One transient outside that rule: W + S + D (S pressed while W is still down) makes **E** (the end-turn key) in the model, and W + A + S makes Q
  (no function). The pad layer already cancels W + S (they move nothing); if the owner's keyboard ends the turn by itself while walking diagonally on WASD, swap the up key
  (candidates and procedure: kbdtest in `build/`, key table `kPadKeysWasd` in `include/engine/pad.hpp`). The real check is the owner's A1200 keyboard (ROADMAP 8.7): hold
  W D Alt + Right Up Ctrl in `kbdtest` and see whether extra codes arrive.
* **Joystick and mouse share port 0.** The map/menu mouse pointer reads `JOY0DAT` (`src/rt/input.cpp:110-129`); a joystick in port 0 is already what the original calls "player 2 of a duel".
* **Fire detection for "any controller"**: menu "press fire" waits (`rtMogFireWait`) accept any pad.

### 3.3 Binding pads to knights

> Implemented 2026-10-07 (ROADMAP 8.2, phase P1, two knights): `include/engine/pad.hpp` (`PadSource`: joystick 1 = port 1, joystick 2 =
> port 0, keys arrows = cursor keys + Ctrl / Right Shift, keys WASD = W A S D + Alt; the adapter is a stub returning 0 and is not offered),
> `include/game/party.hpp` (`PartyConfig g_party { active, n, aubPad[4], ubFocus }`, zero = classic), title menu "Players ... Coop 2",
> a controller line per knight on the knight screen. The binding lives in `g_party.aubPad[record]`, not in a record byte. `rt/input`
> `joyPoll` replaces only the port 1 word in co-op: the current knight's controller (`ubFocus = PARTY_FOCUS_TURN`: map, fights, the loot
> cursor) or every controller (`PARTY_FOCUS_ANY`: knight screen, `rtMogWaitFireC`); the port 0 word stays the raw port 0 stick, so
> `inputFor` (fighters.cpp) is unchanged until 8.4b gives each fighter its own controller. Keyboard note: the game's key table merges
> both Alt keys ($38), so the two sets cannot use LAlt / RAlt separately; there is no right Ctrl on an Amiga keyboard. Tests:
> `tests/test_coop.py`, boot `tests/boot/play_coop.txt`.
>
> 2026-10-09 (Coop 2..4, ROADMAP 8.2): the title menu steps Players 4 -> "Coop 2" -> "Coop 3" -> "Coop 4" (`PARTY_COOP_MAX = 4`; left goes back down and then to 4 players).
> The knight screen offers, per knight, the first free controller in the order Joystick 1, Joystick 2, Keys arrows, Keys WASD; left / right cycle only the controllers no earlier
> knight uses (`partyNextPad`), so two knights can never share one and the fourth knight has exactly the one that is left. `tests/boot/play_coop4.txt` (Coop 4 from the menu,
> four knights with the four controllers, the leader rotation, every knight steering the token with its own controller while the other three are held and ignored, four status sheets).

Replace the port test with a lookup, keeping the old encoding for classic mode:

```
struct KnightInput { uint8_t controller; }   // stored in a spare record byte (+131), 0xFF = classic encoding in +11
uint16_t inputFor(env, k) { classic: k.ubInputPort == 1 ? port0 : port1   (unchanged, fighters.cpp:115-118)
                            coop:    env.pads[k.ubController] }
```

and the same in `cursorTick` (`combat_ui.cpp:1043`) and the knight select (`scene_menu.cpp:209-210`). `FighterEnv::joystick` (`include/game/fighters.hpp:239`, a callback returning two words) is
replaced by `pads` + count so the host test (`tests/test_fighters.py`) keeps its deterministic stand-in.

The caster's hold break uses "the second joystick" (`port 1`) as the **held fighter's** pad (`src/game/fighters.cpp:1037-1041`, `:1109-1113`): this must read the held knight's controller.

---

## 4. Engine limits found and what must change

### 4.1 Fighter slots

* Record layout and the 4 + 1 knight table stay. Fight roster becomes a list: new `FightRoster { Knight* party[4]; uint8_t nParty; Knight* bk[4]; uint8_t nBk; }`
  in `src/game/party.hpp`, with `ActiveKnights::ulCurrent/ulOpponent` kept as "party leader / first BK" so every untouched caller (`src/game/combat.cpp:87,124,150`, `src/rt/arena.cpp:345`) still works in classic mode.
* Jobs: with 4 knights + 4 BK = 8 jobs. Two spare for daggers/arrows and the dragon's objects. Options: **(a)** raise `COMBAT_JOB_COUNT` to 16. This touches `mogJobs[10]` (`include/game/state_bind.hpp:24`),
  the work blocks (36 B each), the attack/hurt list slots (80 B each, `include/game/creatures.hpp:98-100`), `MOGJOB_POOL_BYTES` (`include/game/mogjobs.hpp:37`) and the contact scan loops (O(jobs^2 x entries), fine at 16). **(b)** cap the BK fight at N <= 3 + creatures.
  Recommended: (a), as a size-parametrised build constant so classic mode keeps 10 (`#if MS_COOP`/runtime value), proven first by the replay tests with the constant unchanged.
* Records: 20-record heap is enough (8 + 6 projectiles + dragon objects).
* `LAB_05F2` (first fighter) and `LAB_05F4` (second) cells: keep as leader/first BK; do not add a third.

### 4.2 AI targeting (ten handler heads)

All creature handlers start with `*e.pTarget = *e.pFirst` (`src/game/fighters.cpp:544, 879, 1196, 1311, 1468, 1514, 1599`; `src/game/fight_creatures.cpp:74, 232, 366`) and then read `pFirst` for positions, pauses, grabs
(`jobTogglePause(e.pJobs, *e.pFirst)` etc.: `fighters.cpp:1079-1100, 1634-1646`; `fight_creatures.cpp:328-329`; `fight_ops.cpp:228-260`). The AI knight chooses `pFirst` or the opponent (`fight_creatures.cpp:611`).
A one-call seam, `*e.pTarget = pickTarget(e, k)`, covers the heads; the second kind of read (`*e.pFirst` as "the" player) is a closed list of ~25 call sites that become `*e.pTarget`
(the target of this handler's frame). `pickTarget`: classic = `*e.pFirst` (identical); co-op = nearest living knight in x+depth distance, **sticky** for N frames (so a creature does not dance between two knights),
with a bias for knights who hit it last. A fresh target also needs its `LAB_0634` neighbours (`LAB_0F3A` move scratch) reset; the existing code already recomputes them per handler call.

Grabs (snatcher, caster hold, drake) pause one job and restart its script (`fighters.cpp:1079-1146`, `fight_creatures.cpp:328`): they generalise per target record; two creatures grabbing the same knight needs a "grabbed" latch on the knight (`ubBehaviourFlags2` bit 2 is the caster's, `fighters.cpp:1109` area).

### 4.3 Hit tests

* **Friendly fire**: add `teamOf(Knight&)` (party = 0, enemies = 1) and skip pairs with equal team in `contactScan` (`src/game/creatures.cpp:256-259`) unless the owner wants friendly fire [Q6]; skip allies in `blockedMask` (`creatures.cpp:303-307`) so knights walk through each other (or keep them solid but not at the same depth; recommended: pass-through).
  Teams need no new data if derived from `ulKind`/`ubType` (knights 0..3 and their daggers `$34` thrown by them vs everything else): a thrown dagger's team = its thrower's team, which the record does not store: store the thrower's team in a spare byte of the projectile record at `daggerRelease` (`creatures.cpp:631-640`, new byte +131).
* One hit per attacking job per frame, one remembered attacker per defender: with 4 enemies on a knight, simultaneous hits collapse to the last writer (`creatures.cpp:274-277`), same as today with 2 creatures. Acceptable.
* Cost: `contactScan` is `jobs x attackEntries x jobs x hurtEntries x contactTest` (`creatures.cpp:246-283`); the depth gate prunes most pairs. At 8 fighters it is cheap next to the blitter [inferred].

### 4.4 UI panels (fight HUD and screens)

* **No HUD exists in a fight**: grep finds no health bar or panel code in `src/game`/`src/rt`; the only health feedback is the low-HP palette pulse (`lowHpWarnings`, `src/game/combat.cpp:42-64`, three palette-ramp jobs per fighter: first fighter always, second only on arena kinds `$0C/$10`, `:53`).
  Co-op needs a HUD: per knight a small bar + name tag drawn in the status strip (screen is 320 x 200, arena depth 30..155 leaves the top 30 and bottom 45 px free, [inferred] check the art) with the knight's palette colour, drawn **after** the dirty-rect restore so it is not erased by the 45-rect cap.
* The low-HP warnings are per fighter and use 3 palette ramp jobs each (`opSpawnWarning`, `src/rt/combat.cpp:119-121`); four knights = 12 ramps. Check the ramp table size (`rt_palette_ramp_add_mog`) [unknown]; recommended: one shared pulse for "any knight low".
* **Loot/town/meeting screens** show two stat sheets side by side (`pKnightA`/`pKnightB`, wide-layout scenes, `src/game/combat.cpp:528-590`). A party needs a compact roster strip (4 faces + HP) plus the existing sheet for the active shopper; the click-pool budget (100 records, `combat_ui.cpp:976`) caps the number of simultaneously drawn icons.
* **Map**: one party token sprite and a roster bar.

### 4.5 Palette / identity (fits in 32 colours)

Knight identity is **colour only**: all knights share one cel set (`loadKnights`, `src/game/combat_load.cpp:116-124`: `kn1..kn3` + hit set `kn4`), and the knight class colour is **one 3-colour triplet** per fight screen: first fighter in `pal[6..8]`,
the second only in scene `$0C` (knights' duel) in `pal[9..11]` (`src/engine/display_fx.cpp:162-170,213-214`, class table at `:140-145`). In all creature scenes `pal[9..]` hold the creature colours. [fact] Consequence: **at most two simultaneous distinct knight colours**,
and none in creature arenas for the second. For four knights: (a) the 6-plane **enhanced** mode (`MS_ENHANCED`, `CMakeLists.txt:59`, 64 colours) can hold four triplets; (b) in the 5-plane mode use two triplets and distinguish knights 3/4 with a tag/arrow in the HUD; (c) draw shield/helmet overlay cels in a distinguishing palette entry (needs art). Decide in 8.4 [Q9].
Black knights share one triplet (kind 4 -> the last set, `fighterColors`, `display_fx.cpp:140-145`).

**Update 2026-10-26 (branch wt-kcolours, the duel's colours):** in a party duel the palette's two knight triplets belong to the FIRST and the SECOND FIGHTER of `ActiveKnights`, not to a team: a black knight that walks onto the party is the first fighter (knight list, `pal[6..8]` = the black triplet) and the engaged party knight the second (creature list HE1..3, `pal[9..11]` = its class), so the second black knight, which also draws from the creature list, came out in that party knight's colour, and the second party knight kept only its own copy. `rt::partyColoursApply` now builds one skin (copies of the list's three tables, `rt/knight_colours.cpp`) for every fighter whose class differs from the one its list's palette entries hold, keyed by (list, class) so fighters of one class share it (`CelAlias::ulOwner`, `rosterHasSkin`); a class that is the OTHER list's palette class borrows those entries (the second black knight on the creature list uses 6..8, the first fighter's black triplet) and needs no free entry; the others take the free entries 16..31 (three in the duel pictures, so with 3 or 4 knights the first party skin takes them and later classes share, logged). A creature scene is unchanged (only the second party knight has a skin). Reproduce: `tests/pc_play/pc_play.sh coopbk <out> 3300` (`--warp-knight 2 2850` of the PC build: the knight stands on the black knight). The Amiga has the same fix (shared code).

**Map and status (ROADMAP 8.3, 2026-10-09, N = 2..4):** no colour budget problem outside the fights. The four map tokens are four distinct sprites (kind 0..3, own colours), the status sheet shows the knight's own portrait, and the knight screen has four distinct portraits, so nothing needs a new colour. What 3-4 players do miss is *who leads this round*: the one token on the map is tiny and takes the leader's colour, so `mapDrawRoster` (`src/game/overworld.cpp`, in `mapDrawOthers`) draws a row of the party's tokens at the top edge of the map (x $70 + $14 * member, y 8): every member in its own bright map sprite (kind + 5), the leader raised to y 2, a frog as the frog sprite, a knight without lives as the dead sprite (the plain kind sprite was tried first and is too dark against the terrain). It is a HUD tag made of the map's own sprites: no palette entry, no new art. The fight HUD tags for knights 3/4 (the two-triplet limit above) stay with 8.4.

### 4.6 Memory and CPU budget

**CPU: measured (ROADMAP 8.4a; method and all bins in `docs/PERF.md` "Fight frame split", model in `py tools/perfx.py --model`).** WinUAE 6.0.3 cycle-exact 68020 at
14.19 MHz, Release + `MS_AUTOPLAY` build; "stock" = 2 MB chip, no fast RAM; "owner" = the same plus 8 MB fast. A fight frame was split with a colour-clock timer (beam lines, 312 per VBL)
into `B1` (job pass, handlers, script, cel draws) -> the **flip** (idles until beam line `$F5`, once per VBL: 140-200 lines of waiting, *not* work) -> `B2` (contact scan, background
restore, low-HP warnings). `W` = the whole stretch up to the frame wait (flip idle included). Measured fights (one row = the busiest bins, 62-528 frames each):

| fight | fighters / jobs | stock W | stock fps | owner W | owner fps | cel draws / frame | dirty rects avg (max) |
|---|---|---|---|---|---|---|---|
| duel, knight vs black knight | 2 / 2 | 1082 | 10.0 | 693 | 10.0 | 7.6 | 7.0 (11) |
| knight + one creature (lairs 7, 15, 16) | 2 / 2 | 955-1224 | 10.0 | 608-750 | 10.0 | 7-8 | 6-8 (11-16) |
| knight + 3 creatures (lairs 1, 11, 19) | 4 / 4 | 1328-1755 | 10.0 / 8.9 | 1061-1152 | 10.0 | 13-16 | 12-15 (17-20) |
| knight + 4 ratmen (lair 0) | 5 / 6-9 | 2049-2377 | 6.6-7.6 | 1331-1522 | 10.0 | 16-20 | 15-19 (20-25) |
| knight + 5 ratmen (biggest wave reached) | 6 / 9-10 | 2499-2542 | 6.2 | 1675-1738 | 9.0 | 22-23 | 18-19 (21-25) |
| dragon + parts, one knight | 1 + dragon / 4-7 | 1372-1516 | 10.0 | 1087-1185 | 10.0 | 9-12 | 9-12 (13-15) |

fps is 10.0 while `W < 1626` and `15600 / W` above that (PERF.md "Pacing"); the PERF lines of the runs agree (lair 0 on stock: 6.4-6.6 fps).

**What one fighter costs** (beam lines per frame; knight = duel / 2, creature = mean over seven lairs of (bin - knight) / (fighters - 1)):

| | handler | job pass | script | **cel draws** | contact | **restore** | draws / rects | total (c1 + c2) |
|---|---|---|---|---|---|---|---|---|
| knight, stock | 2.4 | 1.9 | 32 | **321** | 11 | 83 | 3.8 / 3.5 | 460 |
| creature, stock (mean; 196-465 by kind) | 4.4 | 1.7 | 31 | **279** | 7 | 60 | 3.9 / 3.7 | 388 |
| knight, owner | 1.3 | 0.9 | 27 | **174** | 6 | 33 | 3.6 / 3.4 | 247 |
| creature, owner (mean; 193-321 by kind) | 1.6 | 3.5 | 27 | **201** | 5 | 31 | 4.1 / 3.9 | 275 |

* **The CPU is not the problem; the cel blits are.** All handlers together (the per-fighter "update") are 1-35 lines per frame (0.3-2 % of the frame; casters the most), the job pass
  4-12, the script interpreter plus z-sort about 30 per fighter (8 %). The **cel draws are 85-90 % of `B1`** and the background restore about 80 % of `B2`. One cel costs 46-109 lines
  on stock (34-81 owner) by size, 3-7 ms: five planes of cookie-cut blits against the chip bus that the five display bitplanes also use.
* Fast RAM helps more than the old "12 %" of `PERF.md` (that compared `work`, which contains the idle flip): `B1` -20 to -45 % (duel 717 -> 411, lair 15/7/16 -37 to -42 %, lair 1/19 -19 to -23 %),
  restore -50 to -60 %, i.e. a fighter's draws -46 % (knight) / -28 % (creature).
* Each fighter is **3.8-4 cel draws and 3.5-3.7 dirty rects** per frame (body, shadow / weapon parts): rects = draws within 5 %. Four knights + four enemies = **26-34 rects on average**, peaks
  4-8 above the average: ~31-42 against the cap of 45, plus 1-2 per dagger or spark in flight, so the heavy case can touch the cap. No frame in any run exceeded 45 (`rover` 0 everywhere).
  Raise `COMBAT_RECT_MAX` to 64 together with the job table (4.1).
* **Job slots**: one knight + five ratmen already used **9-10 of 10** (casters throw daggers: `jobs - fighters` was up to 4). Four knights + four casters need 8 + ~4 = **12 jobs, two more than the
  table has**, so for casters the job cap bites before the CPU does; brawlers (lairs 1, 11, 19: `jobs = fighters` or +1) fit exactly with 4 + 4 = 8 (+1).

**Cost model.** `B1 = sum of c1 per fighter`, `B2 = sum of c2 per fighter + ~6 per fighter beyond two` (the contact scan), `W = 312 * ceil(B1 / 312) + B2` (the flip rounds `B1` up to a
VBL, `B2` runs after it), frame period 5 VBLs (10.0 fps) while `W < 1626`, else `W` (fps = 15600 / W). Per-fighter `c1 / c2`, stock: knight 359 / 101, creature 318 / 70 (cheapest kind 196 / 33,
dearest 465 / 139); owner: knight 205 / 42, creature 237 / 38 (193 / 24 .. 321 / 50). The frame's own base cost (frame start, job pass, keys) is ~10-15 lines: it is per fighter all the way
(a fit with a base term gives a base of about 0). Checks: duel predicted W 1138 (measured 1082); knight + 5 mean creatures 2659 (measured 2499-2542); lairs 1 and 19 within 5 %.

| case (model) | stock B1 / B2 | stock W | **stock fps** | owner B1 / B2 | owner W | **owner fps** | rects avg |
|---|---|---|---|---|---|---|---|
| 2 knights (duel, measured) | 717 / 202 | 1138 | 10.0 | 411 / 83 | 707 | 10.0 | 7 |
| 4 knights + 3 creatures (mean) | 2389 / 644 | 3140 | **5.0** | 1532 / 312 | 1872 | **8.3** | 25 |
| **4 knights + 4 creatures, light kinds** | 2220 / 571 | 3067 | **5.1** | 1592 / 299 | 2171 | **7.2** | 26 |
| **4 knights + 4 creatures, mean** | 2707 / 720 | 3528 | **4.4** | 1769 / 356 | 2228 | **7.0** | 29 |
| 4 knights + 4 creatures, heavy kinds | 3295 / 998 | 4430 | **3.5** | 2106 / 402 | 2586 | **6.0** | 34 |
| **4 knights + dragon** (+ its parts and bats) | 2199 / 414 | 2910 | **5.4** | 1442 / 281 | 1841 | **8.5** | 20 |
| 2 knights + dragon | 1482 / 200 | 1760 | 8.9 | 1031 / 185 | 1433 | 10.0 | 13 |

The earlier two-point extrapolation (2040 lines of 1872, "at the edge") was far too optimistic: it read `work` (flip idle included: 1.1-1.4 ticks for two fighters) as 208 lines per fighter, while a
fighter is 390-460 lines of real work and the flip rounds `B1` up to a whole VBL. **4 + 4 does not run at 10 fps on either machine as the engine is**: about **4.4 fps on a stock A1200, 7.0 on
the owner's** (the fight still works; game speed follows the frame rate because every step is per frame). The original's own fights ran at most 8.3 fps (6-tick budget); 10 fps needs `W < 1626`.

**Cheapest ways to make 4 + 4 fit** (model estimates, nothing coded; the factors scale `B1` and `B2` of the 4 + 4 mean case):

| lever | stock fps | owner fps | note |
|---|---|---|---|
| nothing | 4.4 | 7.0 | |
| fast RAM (the owner row) | - | 7.0 | gate the *enhanced* 6-plane mode to fast-RAM machines: a sixth plane is +20 % blit work (3.7 stock / 6.0 owner) |
| cap the enemies at 3 / 2 | 5.0 / 5.7 | 8.3 / 8.5 | does not reach 10 on stock; also relieves the job table |
| **halve the cel draws**: drop the shadow / secondary part cels (2 draws per fighter instead of ~3.8; `B1` x 0.56, `B2` x 0.6) | **7.8** | **10.0** | one change in the cel scripts or a "part" flag; halves the rects too (29 -> ~15) |
| skip draw + restore for sprites that did not change (same frame and position, nothing overlapping; `B1` x 0.75) | 5.7 | 8.5 | needs a rect-overlap test; worth more the more fighters stand still |
| halve the draws **and** skip unchanged sprites (x 0.42) | **10.0** (W ~ 1570, no margin) | 10.0 | the combination that reaches 10 fps on stock |
| lower or float the frame budget (`LAB_05BA`) | no change | no change | the loop already runs at `W` once `W` exceeds the budget; the budget only matters while `W < 1560` |
| blitter nasty (`BLTPRI`, the CPU only waits for the blitter in a draw) | [unmeasured] | | cheap to try; measure with the PERFX lines |

Recommendation for 8.4 / 8.7: plan the party fight for **~7 fps on stock and 10 fps on fast RAM**, and spend the effort on the draw count (shadow / part cels, unchanged sprites), on raising
the job table to 16 and the rect list to 64 (4.1), and on keeping the number of simultaneous enemies at 3-4; do not promise 4 + 4 at the original rate on a stock A1200. Caveats: the model is
linear in the fighters and used a strong knight (strength 9, 310 HP) against lairs 0-19 (the lair-0 wave is the biggest reachable, 5 ratmen at once); the dragon row assumes the dragon costs the same
with more knights; the owner's accelerator clock is taken as 14.19 MHz, the conservative case.

Memory (`docs/MEMORY.md`): owner machine has 7.2 MB fast and 1.43 MB chip free after everything: **memory is not a constraint**. Stock A1200: low point **264,584 B free chip**, so any chip use in co-op (HUD cels, scroll buffer: a 640 x 200 x 5-plane screen is 80 KB x 2 for double buffering, plus background restore) must either fit
the 258 KB or be gated to a config with fast RAM / fewer planes. Recommendation: co-op **requires** (checks at start-up) enough free chip for the extras, but does not require fast RAM.

---

## 5. Weapons as data, bows, playable monsters

### 5.1 Weapons as data (8.8)

Today a weapon is `Knight +88` (`ulSword`, long `$16..$19`, signed compare "better than"), the sword bonus is a fixed `+0/+2/+3/+5` branch (`include/game/creatures.hpp:173-178`, `src/game/creatures.cpp:359`), the shop sells `$17/$18` (`src/game/scene_town.cpp` `shopBuySword`), and the animation/hit data is the knight cel set `kn1..kn3` and hit set `kn4`
(`src/game/combat_load.cpp:116-124`). All 12 inventory slots are taken (`include/game/knight.hpp:29-54`), so do not store weapons in the inventory.

```
struct WeaponDef { uint8_t id; uint8_t damageBonus; uint8_t reach; uint8_t speed; uint16_t actionMask;
                   uint8_t celSet; uint8_t hitSet; uint8_t ammoKind; uint16_t price; const char* name; };
```

* `WeaponDef weaponTable[]` in `src/game/weapons.cpp`, indexed by `ulSword`; the original four rows reproduce today's behaviour exactly (classic parity tests compare the old branch with the table).
* **Action tables are per weapon, not global.** The shared `LAB_05F5..` tables are mutated in place by arenas (section 1.4); a weapon with its own attack animation needs its own action-script/damage tables per knight
  (`Knight +34/+42`, currently the same pointers for all four, `combat.cpp:373-384`). Weapons give the table; arenas' in-place patches (`arena.cpp:672-698`, `combat.cpp:476-484`) must patch copies.
* **Reach.** Reach is the hit record of the attack cel (`{count, ?, maxDx, maxDy, points}`, `src/game/creatures.cpp:122` `contactTest`, `hitRecordAt`) plus the depth gate (`depthClose` +-10). Poleaxe/spear = new attack cels with long hit records, or **synthetic** hit records generated from the sword's by a reach scalar (first step, no new art; the cel still draws a normal sword, only the hit box is longer: useful for balance only).
* **Art and files.** New knight cel sets are separate `.ob` files with hit sets registered through `hitLoad` (`combat_load.cpp:121`, pairs `LAB_0A51`: cel table address -> hit set, `hitSetFind`). With four knights wielding different weapons, up to four knight cel sets are resident (20,760 B for `kn1.ob`, `docs/FILES.md:83`; chip arena `LAB_05B8[1]` is sized for one set, [unknown] free room) - assume **at most two weapon cel sets per fight** in the first version.
* Shop/loot: a new `SCENE_SMITH` row per weapon (`scene_town.cpp` smith buttons, `scene_town.hpp:36-44`) and a loot slot; `ulSword` ordering (`shopBuySword` refuses "that sword or a better one") needs a `rank` field in `WeaponDef`.
* Owner art: the poleaxe/spear attack frames are the critical path (owner-drawn or derived).

### 5.2 Bows (8.9) and the dagger path

What the code does (**[fact]**):

| Path | Used by | Mechanism |
|---|---|---|
| **A. Scripted projectile** | player knight (action `$0C`), AI knight (`fight_creatures.cpp:596-600`) | The knight's `$0C` script calls fight-op `$02CA` -> `daggerRelease`: `ubDaggers--`, a creature record of **type `$34`** running `sDaggerScript`, damage table `tDaggerDamage`, `uwAction = $0C` (`src/game/fight_ops.cpp:145-147`, `src/game/creatures.cpp:631-640`). The flight is the script's own motion; `fighterDagger` ends it off-screen (x >= `$14A` / <= -10) or on a hit (`src/game/fighters.cpp:1721-1738`). 1 job + 1 record, no aiming, no ballistics. |
| **B. Ballistic trajectory table** | caster (type `$24`), drake, dragon fireball | `daggerAim` (`creatures.cpp:406`) fills a `DaggerBlock` (start, target, steps, arc), `daggerStart` (`:445`) registers one of **6** slots keyed by the thrower, `daggerStep` (`:538`) steps position/height/gravity each frame; the caster re-throws after a landing hold (`fighters.cpp:949-1030`). |

The owner's note ("`daggerAim`/`daggerStart`, `DaggerBlock`; caster states 0255/0256/0259/025E") describes **B**; the player's dagger is **A**.
Bow design:

1. **Player bow = variant of A.** Data-driven projectile: `ProjectileDef { script, damageTable, speed, arcKind, ammoField }` selected by the equipped weapon; `daggerRelease` takes a `ProjectileDef` instead of the two global scripts (`fight_ops.cpp:145`). Ammo: `ubDaggers` (`+76`, max 10, `scene_town.cpp:72`) becomes "missiles of the equipped ranged weapon" (arrows share the counter in v1; the shop sells "arrows" like daggers at 2 gp). Needs an arrow cel and its hit record (`hitSetFind`) and a script, art/data work.
2. **Aim/draw animation** = a new action number (e.g. `$44`... entries beyond `$20` need a table slot in `kActionLeft/Right` and the action-script table length [unknown]); "hold then throw again" = the caster's hold logic (`casterHold`, `fighters.cpp:972-990`) generalised.
3. **Lobbed arrows for archer enemies** = path B (`casterAim` already computes `steps/arc` from range, `fighters.cpp:951-989`); an `archer` creature type (new initialiser, 5.3) reuses it. The 6 slots are shared with the dragon fireball, so ≤ 6 arrows in flight.
4. **Friendly fire and jobs**: an arrow is one job; with 8 fighters the 10-job table leaves 2 (4.1 -> raise the constant). Arrow team = thrower's team (4.3).
5. **Controls**: dagger/bow = dedicated key on keyboard sets (4-key chord problem, 3.2); on a one-button joystick keep fire + up + back.

### 5.3 Playable monsters (8.10)

* A monster's definition today is **its initialiser** (`init0169, init0170, init0176, init018B, init0195, init0198, init019D, init019F, init018F`, `src/rt/arena.cpp:487-640`): `swHp/swHpMax`, `ulActionScripts +34`, `ulHurtScripts +30`, `ulDamageTable +42`,
  `ulWalkScripts +46`, `ulIdleScript +22`, `ubType` (the handler), and the tunables `+116/+118/+120` (speed / depth reach). The creature cel set is one kind at a time (`LAB_05DF`, `combat_load.hpp:20-25`), loaded by the arena's loader call (`rtClTroggAxe`, `rtClBe`, ...).
  `creatureTypeDef` (ROADMAP 8.10) is the *lair* row (`tools/tables.yaml:24-37`) and carries no stats; the data model for monsters should be a new `CreatureDef` table generated/hand-written **from these initialisers** (`src/game/creature_defs.cpp`), with `init*` reduced to `applyDef(def, record)` (classic parity: identical record fields, proven by `tests/test_arena_emu.py`).
* **Controller swap.** A creature is an AI handler keyed by `ubType` (`fighterBrawler`, `fighterCaster`, ... `include/game/fighters.hpp:22-33`). A *player monster* = a new handler type that does what `fighterKnight` does (read pad, map direction+fire to action, walk with the creature's own walk-script table) but with the creature's tables.
  Differences to handle: knights index walk scripts by `animPhase*4 + (0/0x20/0x40)`; the brawler indexes phases 0..7 and uses `kMoveTab` (`fighters.cpp:30-40`); each creature type has its own set of action slots (3 attacks for the brawler). So the mapping pad->action is **per creature** (data: `PlayerMoves { walkStyle, actionForDir[16] }` in `CreatureDef`).
* **Where the monster lives.** In fights as a party member on the player side (team 0, `controller` byte set, handler `fighterPlayerCreature`). Playable creature cel set vs enemy creature cel set: if the lair's creatures are another kind, **two creature cel sets** are needed at once, and the creature arena holds only one (`LAB_05DF`); first version: a monster knight can only enter lairs of its **own kind** or the knights' arenas, or both cel sets are carved from the (large) fast/chip arena on the owner machine [unknown free room].
* **Outside fights.** A "party slot with a creature portrait": the record still has to be a `Knight` (map sprite kind index, stat sheet, inventory), plus `species` in a spare byte (+87). Shops: sell only armour/items that fit? (data flag in `CreatureDef.canUse`). Level-up: temple stat purchase by progress (`LAB_0544`) applies to strength/constitution/endurance of the record: reuse as is. Monster identity in the knight-select screen needs portrait art. Balance per creature in 8.7.
* Low priority risks: a monster in the party changes **enemy AI** (they target by position, not species, so no change), and the ending code is a knight kind (`stoneEnding`, ending selection must handle non-knights: use the leader's species -> a default ending).

---

## 6. Implementation plan mapped onto ROADMAP 8.2-8.10

Sizes as in ROADMAP (S/M/L); order matters. Suffix rule: new sub-tasks get `Na` suffixes, nothing is renumbered.

**Mode switch and parity.** All co-op code is behind `ms::game::PartyConfig g_party { bool active; uint8_t n; ... }` (default `active = false`) plus a CMake switch `MS_COOP` (default ON, but runtime
default off) so the Release exe can still be built without it. Classic mode must produce **bit-identical** results to today: every changed function keeps its original branch as the `!g_party.active` path (e.g. `inputFor`, `pickTarget` = `*e.pFirst`, `teamsEqual` = false, `COMBAT_JOB_COUNT` = 10).
`tools/check.py`, `tests/test_fighters.py`, `tests/test_creatures.py`, the `run_host` replays and `py tools/integrate.py --boot` stay the parity gate; add a `play_coop*` autoplay tier next to the existing `play_*` scripts (`docs/AUTOPLAY.md`).
New logic lives in new pure files (`src/game/party.cpp`, `src/game/weapons.cpp`, `src/game/creature_defs.cpp`, host-tested with clang++); `src/rt` only wires it.

| Phase | ROADMAP | Work | Size | Risk |
|---|---|---|---|---|
| P0 | **8.4a** (new) | **Perf probe first**: a synthetic 8-fighter fight in the original engine (autoplay script spawning extra creature jobs, `src/rt/perf`) to measure beam lines, draws/fighter, dirty-rect count (45 cap), job use, in WinUAE exact for stock and owner configs. Retires the biggest unknown (4.6) before any design is built on it. | S | the answer may force a smaller roster (<= 3 + 3) |
| P1 | **8.2** | Controller abstraction: `Pad[4]`, `inputFor`/`cursorTick`/`fireWait` through it, knight-select binding screen, keyboard sets, adapter reader (pure mapping + host test, hardware path guarded by a diagnostics screen "show raw PRB/PRA" so the owner can confirm the pinout). Classic encoding untouched. | M | adapter pinout (needs owner's adapter), keyboard ghosting |
| P2 | **8.3a** (new) | Record pool: BK records 5..8 + inventories, generalised `RecordSet`, turn slots (`& 3` -> slot count), `aiPickOpponent` over the party, `startKnights`/`resetGame` for co-op. | M | many fixed-4 loops (`rules.cpp:61,153,207`, `overworld.cpp:229-262,561-575,597`, `loot.cpp:43-52`, `arena.cpp:272`); dragon index 4 must not move |
| P3 | **8.3** | Party token on the map: leader/mirrors, one budget, party sprite and roster bar, status screen cycling, village rule, dragon engages the party, arrival rows "Battle with <BK group>". | M | UI art (token sprite) |
| P4 | **8.4b** (new) | Fight engine generalisation: roster, `placeParty`, team model (`contactScan`, `blockedMask`), `pickTarget` in the ten handler heads, grabs per target, end-of-fight rules (all party down / all enemies down), `settleDefeats` for N, `COMBAT_JOB_COUNT` as a parameter (16 in co-op). Dagger/projectile carries a team byte. | L | the heart of the project; keep the classic path byte-identical; list of ~25 `pFirst` sites (4.2) |
| P5 | **8.4** | Co-op fights: knight vs knight (N vs N BK), creature lairs (`scaleWave` by party), dragon, guardian; HUD and palette plan (4.4, 4.5); camera/arena crowding (entry staggering); sound channel pressure [unknown]. | L | perf (P0), 45-rect cap, palette triplets |
| P6 | **8.5** | Co-op loot (`partyReplicate` in loot-move/gold-take, BK pot), shop queue per knight, exchange screens as party tools, valley/Stonehenge/ending, lives/game-over rules. | M | click-pool 100 records; ending rules (Q8) |
| P7 | **8.7** | Balance and playtest on the owner's machine: wave size, BK stats, gold inflation, prices, damage, difficulty by party size (`uwDamageDiv`-style table). Needs the P0 numbers and real play. | M | taste |
| P8 | **8.8** | `WeaponDef`/`ProjectileDef` data tables (classic rows first, parity proof), per-knight action tables instead of global patches, synthetic-reach spear/poleaxe for balance, then art. | L | art; shared in-place table patches (`arena.cpp:672-698`) |
| P9 | **8.9** | Bow as projectile variant of path A (arrow, ammo, draw animation, shop), enemy archers on path B; job/dirty-rect headroom. | L | needs P8 tables and art |
| P10 | **8.10** | `CreatureDef` table extracted from the initialisers (parity first), `fighterPlayerCreature`, party slot with a creature, per-species move map, ending/shop rules. | L | second creature cel set residency; balance |
| P11 | **8.6** (stretch) | Scrolling lairs: wider 5-plane buffer (80 KB x2 double-buffered; fits only with the owner's chip), tile-script background for 2-3 screens (`bgblit` composes 32x25 tiles from a script, `include/engine/bgblit.hpp:15-26`), dirty-rect restore against a scrolling buffer, wave tables, camera follows the party, guardian at the end. | L | biggest engine change (display, background restore, obstacles `LAB_0A71` in world coordinates); do last |

Dependencies: P0 -> P5 sizing; P1, P2 -> P3 -> P4 -> P5 -> P6/P7; P8 -> P9; P10 needs P4; P11 needs P5.

### 6.1 State of P2 / P3 (ROADMAP 8.3a, 8.3; 2026-10-07)

Done, behind the co-op switch (`g_party.active`; provisional `include/game/party_config.hpp` until 8.2's `PartyConfig` lands). The
rules are pure (`include/game/rules/party.hpp`; turn rules in `src/game/rules/turns.cpp`, map rules in `rules/ai_map.cpp` and
`src/game/overworld.cpp`, each as a co-op branch beside the untouched classic code); `src/rt/party.cpp` owns the state,
`src/rt/party_start.cpp` sets up a new co-op game after LAB_01BE. Host test `tests/test_party.py`; boot `tests/boot/play_coop.txt`.

* **Records**: 0..n-1 the party, n..3 out of play (lives 0, never scheduled or drawn), 4 the dragon (unmoved), 5..5+n-1 the black-knight
  group (`RecordSet::aPool`, its own array and base address; `rec(i)` / `addr(i)` / `index()` cover both ranges, `aRec[i]` is still
  the classic record). The BK records are AI knights made as LAB_01AE / LAB_01BE make them, named "BLACK KNIGHTS".
* **Turn slots**: the turn word is a slot in co-op: 0 = the party (its leader's record), 1 = the BK group (record 5). A round = both
  slots; the daily upkeep (records 0..4 plus the pool) runs at each round's end, a day is 4 rounds as before.
* **Party token**: the leader's record walks; the members (and the BK records behind record 5) mirror the token after every step;
  only the two tokens are drawn (the leader's own knight sprite). The leader rotates at each round's end to the next member with lives
  left and steers with its own pad (every member is "port 1", the player stick, which in co-op is the current knight's `g_party.aubPad`); the budget is the party minimum. The party turn is
  skipped only while every living member is a frog; game over when no member has a life.
* **Encounters**: the scan lists the other group's token ("Battle with BLACK KNIGHTS"); the BK group's AI picks the party token;
  the node menu, the places and the fights act with the leader as the current knight (the fights stay one knight vs one knight / the
  wave until 8.4b; per-knight shopping waits for 8.5). The dragon engages the party (its target is the leader, re-pointed when the
  leader changes; the touch list holds the token). Home villages: the leader's own village (the original rule applied to the token).
* **Status** (Space): one sheet per member, the leader first.

### 6.2 Party of 2..4 (ROADMAP 8.2 / 8.3 / 8.3a; 2026-10-09)

The rules were written for N = 1..4 from the start (`PartyMap::ubSize / ubBk`, `REC_BK_POOL = 4`, leader rotation modulo `ubSize`); 2026-10-09 added the title menu
(Coop 2 / 3 / 4), the host tests for N = 3, 4 (`tests/test_party.py` `n3_*`, `n4_*`: start, token, leader cycle 0..N-1 twice, BK upkeep, minimum budget, dead members skipped,
frogs, mirrors of the N members and the N black knights, the scan row, the BK engagement follows the leader) and the boot run `tests/boot/play_coop4.txt`.

Fixed-4 loops of the P2 row, audited for a party of 4 (only the record pool matters here; fights / loot are 8.4 / 8.5, not touched):

| Loop | Co-op 3/4 |
|---|---|
| `knightsRecalcAll`, `dayTurnover` (`rules/stats.cpp:59`, `rules/clock.cpp:20`: records 0..3 + the dragon) | fine: the party is records 0..N-1; the black knights are covered by `partyPoolUpkeep` and `rtPartyKnights` (HP / endurance recalculated there) |
| `advanceTurn` `& 3` | replaced in co-op by `partyAdvanceTurn` (slots: party, BK group) |
| `scanMap` other knights / dragon touch list `aulTouch[4]` (`overworld.cpp:334-345`) | co-op branch `scanTokensParty`: two tokens, the dragon touches the leader only |
| `aiPickOpponent` (4 rows) | co-op branch `partyAiPickOpponent`: one row, the party token |
| `partyStart` (`rules/ai_map.cpp`) | members 0..N-1 on the player stick, slots N..3 out of play (none for N = 4) |
| `clearKnightFlags` (`rt/arena.cpp:299`) | records 0..3 plus the pool in co-op |
| `lootNextKnight` (`loot.cpp:43-52`, `& 3` over `mogKnights[0..3]`) | NOT changed: for N < 4 it would still visit the out-of-play slots; belongs to 8.5 (per-knight shop queue) |
| `arena.cpp:272` (`mogKnights[4]` dragon) | untouched, the dragon stays record 4 |

The status screen runs once per member, the leader first, each closed with its owner's fire (`statusParty`, `src/rt/overworld.cpp`); the map shows the roster strip (section 4.5).

### 6.3 State of P4 / P5 (ROADMAP 8.4b, 8.4; 2026-10-09)

Done for two knights (the code is N-generic up to four, only two are played and measured):

* **Roster** (`include/game/roster.hpp`, `src/game/roster.cpp`, pure, `tests/test_roster.py`): `FightRoster` = party fighters (the living members, the leader first), foe knights (the black
  knights of the encounter), a team per record (party / creatures / foe knights; a thrown dagger carries its thrower's team in `Knight::ubTeam`, +131), nearest-living sticky targeting
  (`ubTargetSlot`, +87), settle bits. It lives for one fight: `RosterScope` (src/game/combat.cpp) in `fightCreature` and `fightMeet`, seeded by `rt/fight_seed.cpp`; inactive = classic.
* **Pads**: nothing new was needed in the fighter: every member is port 1 and `rt/input.cpp` answers with the pad of the record the running handler works on (focus TURN, `mogCurKnight`).
  Keyboard pads need held keys: in a party fight `opKeyReset` keeps the key-down flags (it cleared them every frame).
* **Targeting** (4.2): the ten handler heads call `targetFor`; reads of "the first fighter" inside a handler are `firstOf`, script ops of a creature use `heldOf`; grabs (caster, snatcher, drake) keep their knight.
* **Hit tests** (4.3): allies never hit (`contactScan`) and walk through each other (`blockedMask`); creatures keep hitting each other as before.
* **End / settle**: the fight is lost when every party fighter is marked dead (`endAndPause`, the death scripts LAB_0005 / 0006 / 000D settle the OWNER: `fightOpPartyRun`), won when the creatures are
  gone / the foe knights are down; each fighter down gets its hit points back and loses a life (`rosterSettle`), the party wins with a fallen member and that member still loses the life.
* **Creatures released late**: the enter script of every knight ends with LAB_000A (toggle the creatures' pause); with two knights the second toggle froze the wave again. The creatures are
  released when the LAST party knight has entered.
* **Limits** (4.1 option a, as parameters): `game::g_combatLimits` = 10 jobs / 45 rects (classic, unchanged) or 16 / 64 in a party fight; `rt/combat_pool.cpp` switches the arrays (job table, work blocks, attack / hurt lists, the two rect lists) with the limits.
* **Scaling**: `[coop]` numbers are for two knights; each further knight adds the same step (`coopAliveBonus`, `coopTotalFactor`, `coopWritebackDivisor`); the strongest of the party sizes the wave.
* **Black knights**: N party knights against N BK records (5..), the BKs choose among the party (`rosterFoeTarget`), end and loss rules as above; a lost party is robbed by BK 0.
* **A knight killed BY a creature's kill script (owner report 2026-10-11, fixed)**: the finishing sequences of the creatures (the ratmen's squeeze `LAB_0869`, the spear `081C`, the flyer `0849` / `084E`, the Be's carry `0895` / `0896`, the snatcher `08A1` / `08A2`,
  the dragon part `0884`) run on the CREATURE's job and end with `LAB_000D` ("the current knight is dead") and `LAB_0006` ("the fight is over"), written for one fighter: `LAB_000D` marked the leader dead whoever had been struck and `LAB_0006` ended the fight, so
  one dead knight took the survivor with it (the survivor's job followed it into its death script) and the fight ended with a knight still standing. Rule now (`rosterScriptVictim`, `rosterScriptEndsFight`, `fightOpPartyRun`): a creature's `LAB_000D` marks the knight
  it holds (its sticky target); its `LAB_0006` ends the fight only when every party knight is down (a creature that is itself down, a boss's death script, still ends it: the party has won). The ratmen's squeeze (`casterSlam`) additionally lets go
  of the dead knight while another knight stands (`casterDropVictim`: its job runs again from `LAB_07FA`, whose `ifdead` plays the fall `LAB_07F7` / `07F8`, i.e. the corpse is drawn by the knight's own job in its own colours), instead of holding the corpse
  for ever. The other kill sequences (spear, flyer, snatcher, drake, dragon part) still remove the victim's job: the victim vanishes after the creature's animation (no corpse), the creature goes back to the fight. Not verified in the emulator: the drake / snatcher /
  dragon-part sequences with a surviving knight (dragon and valley fights). The fallen knight loses its life at the end of the fight as for any fallen member (`rosterSettle`). Classic fights are untouched (`partyFight(e)` false: the original routines run).

* **Kill-path audit of every creature (2026-10-26, branch wt-coop-kill; PC play with a debug warp, frames named in the commit notes)**: every site that kills, knocks out, releases a job or ends a fight was read against "which record does it act on". Bugs found and fixed (party fights only, classic unchanged): (1) the **spear trogg's** finishing blow (`LAB_024B` -> `LAB_081C`): the script's `LAB_000D` marks the creature's sticky target, but the handler's pick at its head had already moved past the struck knight (it is down) to the other knight, who was marked dead and ended the fight (`bindVictim` now points the slot at the struck knight, as for the dragon's bite `LAB_028D` -> `LAB_0884`; the flyer's scripts have no `LAB_000D`, its direct kill was fixed by 515d02a); (2) the **mudmen's grab** (`LAB_08A2`, which ends by `kill`): the mudman left the arena for good while the wave counter still held it (a fight that could never be won) and the shared `SNATCH_GONE` flag kept the other mudmen frozen: its `LAB_0006` is now a `LAB_0005` ("a creature died") while another knight stands (`fightOpRemap`), the flag is cleared; (3) a **mudman killed by the partner while it carries a knight** now lets that knight go (its job stayed paused for ever); (4) the **balok drake** carrying a knight (flag `0x20`) kept its knight only while flying / grabbing: the pick could swap to the nearer knight and release / gloat over the wrong one (`0x23` now); (5) PC only: `opKillJob` cleared the long at a warning job's TYPE (a ramp-slot miss returns 6..11, harmless vector write on the Amiga, an access violation on the PC) when both party fighters ran low in a crowded fight. Checked and fine: the snatcher's swallow `LAB_08A1` / grab `LAB_0EB0` (hold keeps the slot), the caster's throw / slam / drop (the held knight), the demon's hit, the drake's landing (`knockOut(tgt)`), the dragon's bats / part / flight, the troll, the axe troggs, the black knights (their own death scripts), `rosterSettle` (hit points <= 0 lose a life). Host tests: `tests/test_coop_script_ops.py`.

**Colours (4.5, measured)**: the arena pictures never use palette entries 23, 28..31 (bg1a, 1b, 2, 2a, 3, 5a; bg4 only 23, 24, 31), and nothing but KN5 / ki.cel draws with 29..31. The knight cels (KN1..3) use colour
indices 0..8 (+15): 1..5 are shared (skin, steel), **6, 7, 8 are the class colours** (`pal[6..8]` = the first fighter's class, `fighterColors`); the creature cels use 9..14 (+16..28 for the troll, ratmen effects); the duel scene has the second class in 9..11 (HE1..3 cels). In a
creature arena entries 0..15 and 16..28 are taken, so the second knight cannot share the 32-colour palette with a copy that still uses 6..8: `src/rt/knight_colours.cpp` finds three entries of 16..31 that the arena picture and the creature cels do not use, remaps the
knight cels once into a chip copy (`engine/celremap.cpp`, 87 KB, planes 0..4, byte-wise logic) and writes the second knight's class triplet there; the draw event of that knight's jobs takes the copy (`rosterCelFor`), the hit sets are found through an alias. Cost: ~87 KB chip while the fight lasts
(stock A1200 has ~258 KB spare, the owner machine ~1.4 MB) and one pass over the cels at the start of the fight; none per frame. Without three free entries (or in the duel / dragon scenes) the knights share the colour as before. Four knights would need three more entries each (not available in 5 planes: enhanced mode or HUD tags).

**Update 2026-10-10 (hurt frames, branch kcol_hurt):** the second knight turned into the first knight's colour while some hurt animations played. The knight cels are not only KN1..KN3: a knight script can switch its job to the creature cel list (`$C0 $02`, `LAB_085E` / `085F` the ratmen's struck / cursed reactions, `LAB_084A` / `084B` the Be's grip) and then draws the knight figure out of the creature's set (`RATMEN2` frames 42..62, `be1`, `TROGGSpear1`, Balok, Mudmen, Demon, Dragon cels carry class-colour pixels 6, 7, 8), and the creature's own job draws the knight it holds the same way; KN4 (weapons) and KN5 were not copied either. Now every cel set of the arena's two lists (`engine/celremap.cpp celSkinCandidates`) that has class-colour pixels gets a copy (KN1..KN3 mandatory as before, the rest best effort: a failed chip allocation leaves that set in the shared colour), and `rosterDrawSkin` (`include/game/roster.hpp`) selects the copy for the skinned knight's jobs AND for the job of a creature whose sticky target is that knight (`ubTargetSlot`). Cost: +8 KB (KN4) and the carrying set(s) (RATMEN2 38 KB, Balok2/3 43 KB + KN5 36 KB, Mudmen 86 KB worst case) while the fight lasts. Host test `tests/test_skin_cover.py` (script control-flow walk over the original knight scripts vs the copied tables per arena).

**Update 2026-10-09 (ROADMAP 8.4, branch wt-8_4h, emulator pass pending):** the duel (`$0C`) and dragon (`$14`) scenes now search free entries too. Measured from the exported originals: the duel's cels (HE1..3) only use low entries, so 31 / 30 / 29 are free like in the lairs; the dragon's cels (DRAGON1/2/5) and KN5 (the fifth knight set, loaded in the dragon, balok and troll arenas) draw with 29..31, so those count as used (KN5 is scanned in those three arenas, which also fixes the balok / troll lairs); the arena pictures leave 17 / 23 / 24 / 28 and so on free (bg1a, 1b, 1c, 3: enough), bg2 / bg5a leave only 23 / 28 (fallback: shared colour, logged). The valley guardian's cels use 15, 17..18, 21..23, 28..31 and the valley picture is unmeasured: it may fall back too. **Fight HUD:** the only per-fighter indicator in a fight is the low-hp palette pulse (`lowHpWarnings`: three ramp jobs on three palette entries); knight 2 pulses its own entries (kept in `FightRoster::aubSkinEntry`); the palette has six ramp slots, so in a party duel the black knights' own warning gives way; knights 3 / 4 share a colour and have no pulse (a HUD tag would be needed, 4.4). **Placement:** the N black knights enter on `rosterFoeSlot` (x alternating $14 / $44, depth evenly $4B..$80), one black knight keeps the original spot. **Valley / dragon:** see ROADMAP 8.4; the guardian's hit points are not scaled with the party size (8.7).

---

## 7. Open questions for the owner (short)

1. **Black knights**: do you want **one BK group token** that spawns N fighters (recommended), or N individual BK tokens on the map?
2. **BK count with 1 player**: N = party size means 1 knight fights 1 BK (the original's duel). OK?
3. **Who moves the token**: the leader's pad only, leader rotating each round (recommended), or any pad?
4. **Party move budget**: leader's endurance or the party minimum (recommended)?
5. **Death**: a knight who is down when a fight ends loses a life even if the party won (matches the original), or only when the whole party loses?
6. **Friendly fire**: off (recommended; allies pass through each other) or on (more chaos, needs team handling for blocks and daggers)?
7. **Home villages**: does any party member use any village, or only a knight's own village (the original rule)?
8. **Ending**: only the stone-owner/leader's ending, or all four endings in sequence?  **Decided 2026-10-09 (owner): the stone owner's ending only** (ROADMAP 8.5; `rt::partyStoneOwner`: the first member holding the stone of the moon phase, the leader first, is the visiting knight, so `stoneEnding` plays its ending; the original rule for the knight concerned).
9. **Colours**: accept two knight colour triplets plus HUD tags in the 5-plane mode, or require the 6-plane enhanced art mode for four distinct knights?
10. **Gold**: copied loot means 4x gold in the party. Keep (decision 3), or scale shop prices by party size?
11. **Adapter**: which parallel-port 4-player adapter do you own (pinout/documentation), and is a 4-player test setup available for the P1 diagnostics?
12. **Hardware**: stock 68020 at 14 MHz (the measured configuration) or an accelerator? The CPU question decides how many fighters 8.4 can afford (4.6).

---

## 9. Computer knights ("AI partners", ROADMAP 8.3b; owner request 2026-10-25)

One human can play co-op with computer team-mates.  On the knight screen the controller line of knights 2..4 offers **Computer** as one more
stop after the last controller (left / right cycle it; knight 1 is always human, netplay has no computer knights yet).  The offers stay as
they were: the screen still proposes a real controller first, a computer knight only exists when the player picks it.  Classic mode and
all-human co-op do not change (`g_party.aubPad[m] == ms::PAD_COMPUTER` is the whole switch; `ms::game::partyIsComputer`, `partyComputerMask`).
The choice is not persisted, like the other controller picks (the knight screen asks every game).

**Owner rules (2026-10-25), and where they are in the code**

* **The map is the human's.**  A computer knight never leads a round: `partyRotateLeader` skips it (`PartyMap::ubComputer`, filled by
  `rtPartyKnights` from the knight screen), so the human knights alternate as before and one human always walks the party.  Only when no
  human is left alive does a computer knight take the lead (the first human's pad then steers the token), so the game can end normally.
* **Screens that wait for a pad** (status sheets, loot screens, "Next Day", the fire that dismisses a message): a computer knight's turn is
  read from the first human member's pad (`partyHumanFor`, `partySlot` / `partyPadBits` / `partyStick` do it for every caller).  Nothing
  else was needed per screen: the leader is always human, so the screens that belong to "the current knight" are the human's.  The status
  sheet of a computer knight is still shown (the human closes it) so its gear can be inspected.
* **Fights.**  A computer party knight runs the original black knights' brain (`rules/ai_fight_knight.cpp`, `fighterAiKnight`) on the party's
  side.  `FightRoster::ubPartyAi` marks it (`rosterIsComputer`; set from `FightRoster::ubMemberAi`, which `rt/fight_seed.cpp` fills from
  `PartyMap::ubComputer`); `fighterKnight` hands such a record to `fighterComputerThink` after the hurt / hit handling that every knight
  shares (so the creatures' blows hurt it by their own rules, not the black knights' knight-vs-knight rule).  The target is chosen by
  `partyAiPickFoe` (`game/party_ai_fight.hpp`, pure; the brain gets its enemy through the shared "current target" cell LAB_0634, which is put back
  afterwards: the next arena's set-up edits the script tables of whatever record it holds, and only a knight has them - found by the soak, seed 320):
  the nearest living enemy (foe knights of a black-knight fight, the creatures of the
  heap, the dragon while its job runs), sticky within 40 px, no random draw (the brain's own odds use the game RNG exactly where a black
  knight would, so netplay / replays stay deterministic).  Daggers, effect objects and body parts are not targets.
  **Design call (not an owner rule): the odds cap.**  The brain rolls a percentage per decision and walks on instead of attacking while the
  roll does not beat the day's odds byte (70 on day 0, 5 from day 8, the black knights' ramp).  A partner that attacks one decision in
  three on day 0 was too passive to win a lair beside a human (soak: no lair won in 48 runs), so a computer knight never uses an odds byte
  above 30 (`PARTY_AI_ODDS_CAP`, `partyAiOdds`): from the first day on it attacks at least 70 % of its decisions.  Tune the constant if the
  partner feels too strong or too weak.  Friendly fire stays off
  (`rosterAllies`): the computer's strikes pass through its team-mates as before.  A fallen computer knight loses its life at the end like
  any member.
* **Towns: the computer knight does not shop on its own; it echoes the human.**  A town, village, temple or wizard visit is run by the
  human members only (the corner arrows skip computer knights: `shopQueueStep`, `shopQueueHumans`; they stay in the queue, so the village's
  castle blessing still reaches them and a computer knight that holds the stone can be the Stonehenge owner).  Whatever the human buys, every living computer
  knight buys the SAME thing at the moment the human's purchase completes, from its own gold or progress points, through the same rule
  function (`partyEchoBuy`, `game/party_ai_shop.hpp`, pure): smith armour / sword / dagger, market items, the temple's stat points and the
  healer's donation.  If it cannot pay, already has it (or better: the echo never downgrades armour, whatever the human's quirk allows),
  the item is not on offer, or the rules forbid it (dagger cap, stat at its limit), it simply gets nothing: no debt, no substitute.
  Using a potion (the map's, the loot screen's rest button) is not a purchase and is not echoed.
  The market's stock is not reduced by the echo (it runs on the stock as it was before the human's click, like the loot copies); selling,
  the dice house and the mystic's gamble are not echoed; the QUIRK of the human's market click (the stats recalculation on the wrong
  record) is not reproduced for the computer knights.  Hooks: `rt/scene_town.cpp` (smith, market, temple), `rt/screens.cpp` (healer),
  `rt::computerEchoBuy` in `src/rt/party_ai.cpp`.
* **Loot** is copied to every living member already (8.5), so a computer knight takes whatever the human takes.  The human's choices
  on the loot screens (which item, gold, armour, sword) are the only choices.  Stonehenge / the valley victory go to the human's side as
  in 8.5 (the stone owner's ending; a computer knight that holds the stone is the owner when the human has none).

**Shared-code deviation found by the soak (deliberate).**  `rt::diwShakeStart` (the screen shake of a fight hit) took a second hook slot
when a hit landed while the previous shake still ran and freed only the newest, so every overlapped hit leaked a tick hook; after nine of
them the VBL hook list ran past its terminator and the PC build jumped into text (soak seed 107, 39 000 frames into a long cave-lair fight).
A running shake now keeps its slot and starts over.  This differs from the original in the overlapped case only (one restarted shake
instead of two ticks racing the same counter, i.e. a faster shake); nothing else changes.  Classic fights can hit it too.

**Tests.**  `tests/test_party_ai.py`
 (ring, delegation, leader rotation, shop queue, roster flag, target choice, every echo rule),
`tests/test_coop.py` (the changed ring), the Amiga Debug boot script `tests/boot/play_coop_ai.txt` (knight 2 = Computer, lair fight, loot; `play_coop4.txt` steps over the new ring stop), PC runs `tests/pc_play/pc_play.sh coopai*` and the soak `tests/pc_play/coop_soak.py` (4 knights,
1 human + 3 computers, random keys).  **Open**: the Amiga run is the boot script above (Debug leg, 2026-10-26: both knights fight, the lair is cleared); the valley guardian, the
dragon and black-knight duels with computer knights were only soaked, not played to a win; the brain is the black knights' (it blocks and
strikes by distance and odds, it does not dodge creature special attacks); netplay has no computer knights (a member's pad is
`(node, pad)` state that the lobby would have to carry).
