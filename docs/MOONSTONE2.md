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

* New menu item "Mode: Classic / Co-op" next to Players/Gore (`include/game/scene_menu.hpp:53` `MenuItem`, `src/game/scene_menu.cpp`). In co-op mode the player count means
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

* **Black-knight pool.** Add up to four BK records (`mogKnights[5+i]`, inventories in a new array; do **not** widen or renumber `mogKnights[0..4]`: the dragon is index 4 in
  13 places, `src/rt/overworld.cpp:636,662,783`, `src/rt/arena.cpp:264`, `src/game/overworld.cpp:570,630`...). `ulKind = 4`, `ubType` `$10`, own gold/progress/shopping via the
  existing `aiShopWish/aiBuyStat/roamPick` code paths, which are record-indexed and 4-slot-bound (`overworld.cpp:229-262`, `RecordSet` is 5 records).
* **How many fight.** On contact the fight is party size N vs. N black knights. Two readings of decision 4 [Q3]: (A) the map shows **one BK group token**
  that spawns N fighters; (B) N individual BK tokens roam and the fight takes the ones adjacent. Recommended: **A** for the first version (one `AR_DUEL` row,
  a single AI turn slot, no per-BK pathing; N BK records are brought into the fight, the others idle). It also cuts the AI turn work on the map.
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

---

## 7. Open questions for the owner (short)

1. **Black knights**: do you want **one BK group token** that spawns N fighters (recommended), or N individual BK tokens on the map?
2. **BK count with 1 player**: N = party size means 1 knight fights 1 BK (the original's duel). OK?
3. **Who moves the token**: the leader's pad only, leader rotating each round (recommended), or any pad?
4. **Party move budget**: leader's endurance or the party minimum (recommended)?
5. **Death**: a knight who is down when a fight ends loses a life even if the party won (matches the original), or only when the whole party loses?
6. **Friendly fire**: off (recommended; allies pass through each other) or on (more chaos, needs team handling for blocks and daggers)?
7. **Home villages**: does any party member use any village, or only a knight's own village (the original rule)?
8. **Ending**: only the stone-owner/leader's ending, or all four endings in sequence?
9. **Colours**: accept two knight colour triplets plus HUD tags in the 5-plane mode, or require the 6-plane enhanced art mode for four distinct knights?
10. **Gold**: copied loot means 4x gold in the party. Keep (decision 3), or scale shop prices by party size?
11. **Adapter**: which parallel-port 4-player adapter do you own (pinout/documentation), and is a 4-player test setup available for the P1 diagnostics?
12. **Hardware**: stock 68020 at 14 MHz (the measured configuration) or an accelerator? The CPU question decides how many fighters 8.4 can afford (4.6).
