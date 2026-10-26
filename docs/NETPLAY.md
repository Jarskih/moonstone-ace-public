# Network play over bsdsocket.library (ROADMAP M12)

Status: design, 2026-10-10, owner decisions in section 6. Nothing here is implemented. Tags as in `docs/MOONSTONE2.md`: **[fact]** read in the code,
**[inferred]** follows from code but was not run, **[unknown]** needs a measurement or the owner.

Goal: two to four Amigas (later the PC build of M11) play one game together over TCP/IP, in both modes:

* **Normal mode** (classic): the original 1-4 knights taking turns on the map, human-vs-human duels, Practice. Each human knight
  sits at its own machine; when it is another knight's turn the other machines watch the same screen.
* **Co-op mode** (M8): the party of 2-4 knights; each member is steered from the machine its player sits at. Mixed set-ups
  (two knights on one Amiga, two on another) are allowed.

Classic single-machine play stays bit-identical: every net hook is inert unless a network session is running.

---

## 1. Model: deterministic lockstep on the input

Every machine runs the **whole game**; only the **input** travels over the network. The game state is large (knight
records, world, fight jobs, all of RAM, no save game: `docs/MOONSTONE2.md` 1.1) but the rules are deterministic given the
input stream and the RNG seed, so this is the cheapest model by far: a few bytes per machine per input poll.

* **Tick = a main-thread frame boundary** (revised after the 12.3 audit, 1.1c: polls per frame vary from 0 to several, and
  some loops read keys without polling). `netTick()` sits in the game's frame wait (`displayWaitBeam`); it blocks until the
  merged input of that tick is known and latches it; every joystick poll, key read and cursor read inside the tick returns
  that latched frame. Identical machines run identical code, so they reach the same sequence of frame boundaries.
* **Input frame** (per machine per tick): the two joystick words, the keyboard sets' keys, raw key events (Esc, Space, E,
  Return, name typing) and the mouse counter delta. The merge is a **pure function of the game state** (identical on every
  machine): it decides which machine owns the "player stick" (port 1 word), port 0, the keyboard and the mouse for this tick
  (section 3).
* **Input delay** of D ticks, chosen per scene from a delay in **milliseconds** (ticks run at 8-50 Hz depending on the scene,
  1.1c), tuned in 12.13: a machine sends its input for tick k + D while it runs tick k, so a
  LAN round trip never stalls the game.
* **Desync check**: every 50 ticks each machine sends a hash of the game state; a mismatch stops the session with a dump on
  both sides (12.11).
* **Pace**: lockstep runs at the slowest machine's speed. A co-op fight that runs 5 fps on a stock A1200 and 9 fps on the
  owner's (PERF.md "Co-op fights") runs at 5 fps on both [inferred].

Rejected: server-authoritative state streaming (the state is the whole RAM image, no serializer, far too large for a
PCMCIA/serial link) and rollback (needs cheap save/restore of the full state every frame).

### 1.1 What breaks determinism today (audit 12.3, 2026-10-10)

Read-only audit of `src/` and `include/` (`src/lifted` ignored). **Headline results:**

* **No game logic advances by elapsed ticks, with ONE exception: the joystick cursor of the cursor screens** (loot, market,
  temple, smith, stat sheets, donation, wizard; table 1.1b). Everything else runs once per loop iteration; `mogFrame` /
  `prgFrame` only pace (`mogFrameWait`), and the one logic reader of the counter is the dormant `$88` random loop count [fact].
* The **tick = poll** definition of section 1 does not fit the code: polls per frame are 0 (AI map turn), 1, or several
  (one per human fighter), and three loops poll at CPU speed with no frame wait (1.2). The tick must be a frame boundary.
* The RNG is one stream, shared with **sound picks**; it is seeded at every `mog` entry but **not** at "Start game" (R1, R2).
* Only two values read the beam into state: the RNG seed and the flyer's lane wobble (B1, B2).

#### 1.1a Sources (B beam, T tick counters, V VBL hooks and ISRs, K keys and mouse, R RNG, A audio, M memory, O other)

| # | Source | Where | Effect / finding | Net-mode fix (12.4) |
|---|---|---|---|---|
| B1 | `VHPOSR & 3` picks 1 of 4 RNG seeds (`mogSeedTable`) | `src/rt/mainloop.cpp:219-221`, `src/game/mainloop.cpp:39-42` | seed of the whole game [fact]; see R1 | host-sent `SEED`; clients never read the beam |
| B2 | Flyer ("Be") lane wobble `(VHPOSR & 7) << 2` added to the target's Y when it turns and its toggle is 0 | `src/rt/fighters.cpp:107,179`, `src/game/fighters.cpp:495-496`, `src/game/rules/ai_fight_flyer.cpp:55-57` | creature position in a fight: a real divergence between two machine speeds [fact] | `(logicalTick & 7)`; must not call `rngNext` (keeps the classic RNG order) |
| B3 | Frame-end wait `while(VHPOSR.hi != $F5)` (every flip, `wait`, palette write) | `src/rt/display_ops.cpp:98-118` | wall time only, state untouched [fact]; the natural place for the logical tick (1.1c) | `netTick()` called from `displayWaitBeam` |
| B4 | Beam reads for profiling (`VPOSR`/`VHPOSR`) | `src/rt/perf.cpp:39,55`, `src/rt/irq.cpp:92-97` (self test) | `MS_AUTOPLAY` / `MS_IRQ_SELFTEST` builds only, no game state [fact: stubs in `rt/perf.hpp`] | none |
| T1 | VBL counters `prgFrame` (`LAB_0379`) / `mogFrame` (`LAB_0B9D`): `++` in the level-3 ISR while `FrameOn` (`LAB_0363`/`0B87`, initial `$FFFF`, never cleared: the original's key `$FF` toggle is not ported) | `src/rt/irq.cpp:158-162` | free-running, wall-time driven, stands still during file loads [fact] | keep as the pacing counter; add a separate `logicalTick`, advanced at the poll, for logic readers |
| T2 | Readers, pacing only: `mogFrameStart/Wait` waits `budget - (tick - stamp)` beams (budget 2 on the map, 6 in fights) | `src/game/mogjobs.cpp:79-90`, `src/rt/mainloop.cpp:185-189,241-248`, `src/game/combat.cpp:166` (stamp written, never read), `src/engine/scenes.cpp:63-71` (program `LAB_0017/18`) | only decides how many beams to wait; the frame loop runs once per iteration whatever the result [fact] | stays wall time; in net mode lockstep makes it the floor of the tick rate |
| T3 | Reader, logic: opcode `$88` with count 0 = random loop count `1..31` from `tick & $1F` | `src/engine/anim.cpp:94-98` (program-overlay jobs, `prgFrame` via `src/rt/engine_anim.cpp:59`), `src/game/combat_script.cpp:371-374` (`mogFrame` via `src/rt/combat_script.cpp:108`) | `py tools/fightscript.py dis --creature X` over all 9 creature kits: **no** `loop random`, only fixed counts [fact]; knight, effect, map and program-overlay scripts not scanned [unknown] | `logicalTick & $1F` in net mode; 12.4 adds a test that scans script data for `$88 00` |
| T4 | Autoplay scheduler counts VBLs (`wait`, `pulse`, `mash`) and injects keys and sticks from the ISR | `src/rt/autoplay.cpp:46-75,331-377`, `src/rt/irq.cpp:168` | `MS_AUTOPLAY` only; the 12.5 gate needs scripts keyed by `wait var` / `wait input`, not by frame distance [inferred] | 12.5 scripts use `wait var`; no bare frame counts across a scene change |
| V1 | VBL hook list (`prgHooks` / `mogHooks`, 9 slots, run in the level-3 ISR after the counter) | `src/rt/irq.cpp:76-86,165` | hooks in use: (a) joystick cursor `cursorTick`, (b) palette fade `rt_palette_tick_*` (+ `rt_synth_fade`), (c) DIW shake `rt_mog_diw_shake_tick`; (a) is game state, (b)(c) display only [fact] | (a) moves to the logical tick (V2); (b)(c) stay in the ISR and out of the state hash |
| V2 | **The joystick cursor hook writes game state in the ISR**: `mogCursorX/Y` += 2 per VBL, `mogCursorLock`; it calls `joyPoll`, so the ISR also writes `mogJoyPort0/1`, reads `currentMember()` and runs `autoplayJoy` | `src/rt/combat_ui.cpp:1029-1065`, installed by `src/rt/input.cpp:260-275` | the cursor position at click time depends on the main loop's speed (1.1b); ISR and main thread both write `mogJoyPort*` [fact] | in net mode `cursorTick` is no hook: it runs once per logical tick from the main thread with the merged frame |
| V3 | Palette ramp slots and job addresses handed back to the game (`mogSelectJob`, `rt_palette_ramp_add_mog`) | `src/rt/scene_menu.cpp:152`, `src/rt/engine_palette.cpp:95-100` | ramp advance is per VBL (display only); the job pointer is an address [inferred: no logic waits on ramp completion] | ramps out of the hash; verified by the 12.5 gate |
| K1 | Keyboard ISR writes `mogKeyDown[]` and the last-key cell (`mogKeyAny`, low byte `LAB_0B86`) at any time; the main thread reads, then clears it | `src/rt/input.cpp:115-120,169-190` (`feedGame`, `deliverKey`, `inputKeyIsr`) | a press between read and clear is lost; the key a frame sees depends on ISR timing; fights clear the whole table every frame (`opKeyReset`, `src/rt/combat.cpp:114-120`) [fact] | `deliverKey` only queues; the poll applies the merged key events of the tick, the game clears as today |
| K2 | Mouse counters `JOY0DAT` sampled per VBL into `mogMouseX/Y`, button cells, idle word and long | `src/rt/input.cpp:145-167`, `src/engine/input.cpp:72-98`, called from `src/rt/irq.cpp:163` | **no reader of the pointer cells in `src/`** [fact: grep `pwMouse*`, `MouseX/Y`]; side effect: any movement writes the long `$10` over the first key-table bytes (`engine/input.hpp:11-13`) | net mode: skip `inputPointerTick` (inert) |
| K3 | Port 0 word is raw `JOY0DAT`: a mouse in port 0 delivers counter bits as "directions"; fire = CIA-A PRA bit 6 (left mouse button) | `src/rt/input.cpp:97-108`, `src/engine/input.cpp:25-48` | random direction bits on port 0 reach the second fighter, duels, Practice [fact] | the port 0 word is part of the input frame; a node with a mouse sends 0 unless bound to port 0 |
| K4 | Intro skip reads CIA-A PRA and a key-press counter directly, outside `joyPoll` | `src/rt/introskip.cpp:42-70` | program overlay only (the ending is not skippable); a local decision that sends this node to `mog` early | intro and ending are outside the session; the session starts in the title menu |
| R1 | **Seeding events**: `rtMogRngSeed` is the tail of `rt_audio_quiesce_mog`, the first instruction of every `mog` entry: (1) boot intro to `mog` (or intro skip), (2) after each ending (`program` plays it, then `rt_run_mog`). **Not** at "Start game", "Practice" or a game over without ending | `src/rt/audio.cpp:154-181`, `src/rt/game.cpp:143-145,213-229`, `src/game/scenes/ending.cpp:1-4`, `src/rt/introskip.cpp:92` | the stream simply continues from the title menu into a new campaign: a session started from the menu (12.8a) begins after the only seeding event [fact] | `SEED` is also sent at session start (host draws; all nodes write `mogRandomSeed` at the same logical tick); NETPLAY 4.1 "every seeding event" is not enough |
| R2 | One RNG stream `mogRandomSeed` (`rngNext` / `rngPercent`), also consumed for **sound variants**; no draw in an ISR [fact: grep] | sounds: `src/game/fighters.cpp:1609-1650`, `src/game/fight_ops.cpp:139-141,170-239`, `src/rt/fighters.cpp:223-227`; logic: `src/rt/arena.cpp:1171,1279`, `src/game/overworld.cpp:99-101,392-395`, `src/game/rules/ai_*.cpp`, `src/rt/scene_places.cpp:105-126`, `src/rt/party_fight.cpp:35` | sound picks advance the seed on the logic path: a node with sound off, muted or failed must still draw [fact] | rule: sound code never gates `rngNext`; host test counts draws with sound stubbed out |
| A1 | Audio / synth state feeding logic | `src/rt/sfx.cpp`, `src/engine/sfx.cpp:8-37` | the channel busy mask is software state changed only by game calls (`sfxRequest/StartFixed/Release/StopAll`), not by the Paula ISR `rt_synth_int4` [inferred from grep] | 12.5: audio on / off gives equal hashes |
| M1 | Uninitialised memory | `src/rt/game.cpp:171-198`, `:75-108` (`imageEnter`) | arenas are `MEMF_CLEAR`, overlay data is reset from a backup at each entry, the mod `Block` is cleared; no `memAlloc` without clear on a logic path [fact: grep]; garbage in C++ stack locals and struct padding that a hash would cover [unknown] | host tests of the logic units under clang `-fsanitize=memory`; hash fields, not raw RAM |
| M2 | Address-dependent logic: records hold 68k pointers (`Knight::ulInventory/ulName/ulOpponent/ulScript`, `DaggerSlot::ulOwner`) | `include/game/creatures.hpp:79-95`, `include/game/knight.hpp` | only equality and in-array index / offset arithmetic found: `rs.index(addr)` (`src/game/rules/ai_map.cpp:112`), `currentMember()` (`src/rt/input.cpp:74-81`); `sortNeighboursByDistance` is a bubble sort on distance with no address tie-break (`ai_map.cpp:82-108`); no pointer hash or order across arrays [fact] | the state hash normalises every pointer field (field map in `engine/statehash`); stock vs fast RAM only moves bases |
| O1 | File loads (`systemUse` windows) | `src/rt/files.cpp`, `src/rt/game.cpp:213` | the counter stands still; pacing excludes the load; state does not depend on it [fact]; whether key events during the OS window are lost [unknown] | no net poll inside a load window; keys come from the input frame |
| O2 | Run-time variants decided per machine: enhanced-art fallback (arenas too big: runs the original art), gore option (`ifnogore` script op, `LAB_06DA` in `flyerFinishesTarget`), mod ini (`g_gameData`), `s_bMenuCoop` | `src/rt/game.cpp:169-199`, `src/rt/modload.cpp`, `src/rt/scene_menu.cpp:167,183` | a stock A1200 can fall back from `MS_ENHANCED` at run time where the 8 MB machine does not (cel sizes feed hit boxes [inferred]); gore, mods and menu options change the rules | all in `CONFIG` and the handshake: enhanced flag as run, gore, `g_gameData` checksum; a fallback means refuse to join |

#### 1.1b Does any logic depend on elapsed ticks? Per scene

| Scene | Answer | Evidence |
|---|---|---|
| Title, knight select, name entry | **No.** One stick poll (names: also one key read) per loop iteration; the loops are not paced and spin between redraws, only the redraws wait (`wait(6)`, `wait(4)`) | `src/game/scene_menu.cpp:138-146,264-290,327-352` |
| Map, human and AI turn | **No.** One `mapFrame` per iteration: poll, key read (`$20`, `E`, `Q`), move, flip, `FrameWait` (budget 2). AI turns do not poll the stick | `src/game/overworld.cpp:678-730` |
| Map node menu (digit keys) | **No** ticks, but a busy loop on the key cell with no poll and no wait | `src/game/overworld.cpp:236-243` |
| Places, towns, shops, temple, loot, stat sheets, donation, wizard (cursor screens) | **YES.** The cursor moves 2 px per VBL in the ISR hook; the main loop (one `hitTest` per flip, or a free spin) sees it at wall-time instants; a click uses the cursor position and `mogCursorLock` of that instant (`mogCursorX >= $A0` also picks buy or sell) | `src/rt/combat_ui.cpp:1029-1065`, `src/game/combat.cpp:746-757`, `src/rt/screens.cpp:213-215`, `src/rt/scene_town.cpp:70` |
| Dice, ritual, wizard speech | **No.** Frame loops (`jobPass`, `tickPass`, `flip`) with the fire read inside the job; `wait(mogDiceMode)` is pacing; `untilFire` polls once per frame | `src/rt/screens.cpp:419-447`, `src/rt/combat_ui.cpp:1069-1080` |
| Fights (duel, lair, dragon, Practice) | **No.** `fightRun` is one pass per frame: stamp, jobs (one poll per human fighter, more in grab / slam / carry states), script tick, flip, end test, `FrameWait` (budget 6). Randomness comes from `mogRandomSeed` except B2. The pause key `$20` blocks in a poll-free spin | `src/game/combat.cpp:130-136,160-182`, `src/game/fighters.cpp:158-161,891,963`, `src/game/fight_creatures.cpp:103` |
| Co-op banners, pad choice | **No.** `ulFrame` counts loop iterations (`displayWaitFrames(1)` + one poll), not ticks | `src/rt/party_turn.cpp:132-153` |
| Intro, ending (program overlay) | paced by tick deltas (`LAB_0017/18`, `SopTicks`) but **no game state** is touched; the skip is a local CIA read | `src/engine/scenes.cpp:63-71,308-313`, `src/rt/introskip.cpp` |

Waits (`wait(n)`, `displayWaitFrames`, fades, jingles) are whole-frame beam waits: they delay the main thread only. What
changes during them is ISR state (V1-V3, K1, T1) and nothing else, so with V2 and K1 fixed a wait is deterministic.

#### 1.1c What this means for the lockstep plan (harder than section 1 assumes)

1. **Tick = poll does not hold.** Polls per frame vary (AI map turn 0; fights 1 per human fighter plus state-dependent extras),
   and `rtMogWaitFireC` (`src/rt/input.cpp:246-256`) polls in a tight loop with no beam wait (thousands per second). Make the
   tick a **frame boundary** of the main thread: `netTick()` at `displayWaitBeam` (every flip and `wait`); polls inside a
   tick return the same latched merged frame; the poll-free or spinning loops of 1.2 get an explicit `displayWaitBeam` + tick
   in net mode.
2. **The tick rate differs per scene and sets the latency budget.** Map budget 2 VBL (25 Hz), fights 6 VBL (8.3 Hz), cursor
   screens about 1 VBL per iteration (50 Hz). Input delay D = 2 hides 80 ms on the map and 240 ms in fights, but only 40 ms
   on cursor screens: set D in milliseconds per scene, not in ticks [inferred].
3. **The cursor screens need the hook rewritten**, not a flag: V2 moves 2 px per VBL independent of the main thread. In net
   mode the move happens per logical tick (the cursor speed then follows the tick rate; scale the step or accept a slower
   cursor: owner decision).
4. **Mid-session seed** (R1): a session started from the title menu is after the only seeding event, so NETPLAY 4.1 needs
   "SEED at game start" in addition to "SEED at each `mog` entry".
5. **The state hash cannot be a raw RAM CRC**: pointers are everywhere (M2), including script pointers relocated per arena.
   `engine/statehash` needs a field map (knights, jobs, creature work blocks, RNG, day counters); display and ISR state
   (palette ramps, cursor sprite, DIW shake) stays out.
6. **Run-time variants** (enhanced fallback, gore, mods, menu options) must be negotiated and verified, not only the build hash.
7. **Keyboard pad sets** reach the game through `padsFill` inside `joyPoll`, so they are covered by the tick only if the key-down
   table is frozen per tick (K1).
8. **Today the ISR writes game state** (cursor, `mogJoyPort*`, key cells). Fixing V2 and K1 removes it from the list of state
   writers; only the counters (pacing) and display state remain.

### 1.2 Input read sites

Main = the game's main thread (the lockstep thread); ISR = level-3 / CIA interrupt. "Spin" = a loop with no beam wait.

| Site | File:line | Thread | Scene | Note |
|---|---|---|---|---|
| `joyPoll` (both ports, fire from CIA-A PRA, `autoplayJoy`, co-op `coopStick`, writes `mogJoyPort0/1`) | `src/rt/input.cpp:97-108` | both | all | entry `rtMogJoyReadC` (`:241`) = `LAB_00EE`; the only joystick reader |
| Title and knight-select `ReadJoy` | `src/rt/scene_menu.cpp:129-133`, `src/game/scene_menu.cpp:139,220,265,306,331`; `src/rt/hunk9.cpp:31` | main | title, knights, names, co-op pad choice | 1 poll per iteration, unpaced spin |
| Map `mlJoystick` | `src/rt/overworld.cpp:339-341` | main | map | 1 per frame on human turns, none on AI turns |
| Fighter input `inputFor`, `casterGrabbed/Slam`, snatcher hold | `src/rt/fighters.cpp:93-99,250-257`, `src/game/fighters.cpp:158-161,891,963`, `src/game/fight_creatures.cpp:103` | main | fights, duels | several per frame, each with `mogCurKnight` = that fighter (the merge is a function of state) |
| Dice `joyFire`, wizard `untilFire`, banner `inputSeen` | `src/rt/screens.cpp:119,465`, `src/rt/combat_ui.cpp:1069-1080`, `src/rt/party_turn.cpp:132-137` | main | dice, wizard, co-op banners | 1 per frame |
| **Cursor hook** `cursorTick` | `src/rt/combat_ui.cpp:1029-1065` | **ISR** | cursor screens | polls the stick at 50 Hz, independent of the main thread (V2) |
| `rtMogWaitFireC` (wait for fire press, then release; co-op: any pad) | `src/rt/input.cpp:246-256` | main | text screens, avoid-battle, new day, intro texts | **Spin with polls, no beam wait**; callers `src/rt/combat.cpp:178`, `screens.cpp:118`, `overworld.cpp:405,882`, `mainloop.cpp:98` |
| Pad layer: `padsFill` (keyboard sets arrows / WASD from `mogKeyDown`, adapter not read yet), `partyStick` | `src/engine/pad.cpp:43-60`, `src/game/party.cpp:60-70`, `src/rt/input.cpp:87-95` | main (and ISR through V2) | co-op | the place where `(node, pad)` binding (section 3.2) plugs in |
| Key cell reads: map `mlReadKey`, `opReadKey` | `src/rt/overworld.cpp:342,539` | main | map | `$20`, `E`, `Q` once per frame after the poll |
| **Map node menu** digit keys | `src/game/overworld.cpp:236-243` | main | arrival menu | **Spin on the key cell, no poll, no wait** |
| Fight pause key `$20` | `src/game/combat.cpp:130-136` | main | fights | one read per frame; the pause itself **spins on the key cell with no poll** |
| `rtMogKeyWaitC` (fight "extra pass") | `src/rt/input.cpp:233-239`, `src/rt/combat.cpp:151` | main | fights when `pExtraPassFlag` | **Spin on the key cell, no poll** (debug-style single step) |
| Name entry typing (`nameEdit`: Return `$1C`, Backspace `$0E`, `rt_mog_key_xlat`) | `src/game/scene_menu.cpp:264-290`, `src/rt/scene_menu.cpp:134-135` | main | knight select | polls the stick first, then reads the key word: covered by the tick |
| Key clears `rt_*_key_reset` (clears the down table and `mogKeyAny`) | `src/rt/input.cpp:220-231` | main | all | writes input state from the main thread; must clear the latched copy, not the queue |
| Keyboard ISR `deliverKey` (+ `inputInjectKey` for autoplay) | `src/rt/input.cpp:169-195` | **ISR** | all | writes `mogKeyDown`, `mogKeyAny`/`LAB_0B86`, skip-press counter (K1) |
| Mouse pointer `inputPointerTick` (JOY0DAT counters, left button) | `src/rt/input.cpp:145-167` | **ISR** | all | outputs unused (K2) |
| Intro skip (CIA-A PRA, press counter) | `src/rt/introskip.cpp:42-70` | main (`rt_prg_wait_beam`) | intro | local; outside the session (K4) |

Loops that read keys or the cursor **without** a joystick poll (no lockstep tick as the code stands): map node menu
(`overworld.cpp:236`), fight pause (`combat.cpp:134`), `rtMogKeyWaitC`, the donation screen (`screens.cpp:213`), `screenRun`
(`game/combat.cpp:749`, flips but does not poll: its input is the ISR cursor), and the AI map turn. Loops that poll but
**never wait a beam**: `rtMogWaitFireC` and the title / knight-select loops. All of them need `netTick()` added (1.1c item 1).

The gate before any network code (12.5): **the same autoplay script on a cycle-exact stock A1200 and on the owner's
config (68020 + 8 MB fast) gives the same state-hash log**, in classic and co-op play scripts.

### 1.3 Deterministic mode as built (ROADMAP 12.4, 2026-10-10)

`rt::netDeterministic()` (`src/rt/net_tick.{hpp,cpp}`; pure half `include/engine/nettick.hpp`, `src/engine/nettick.cpp`). One flag, set
by the session later and by the autoplay hook `poke netdet 1` today; **only the mog overlay** runs in it (intro and ending of the program
overlay stay classic, as section 1.1c item 4 wants). Flag off: every hook is one branch on the flag and nothing else changes.

* **The tick.** `netTick(ulVbls)` runs at the frame boundaries of the main thread: after the beam wait in `rt::displayWaitBeam`
  (`ulVbls = 1`; `src/rt/display_ops.cpp`: every flip, `wait(n)`, fade and palette write passes there) and at the end of the loop frame
  wait `rtMogFrameWait` (`ulVbls` = the logical remainder of the budget; map and fights). It builds the **local frame**
  (`ms::LocalFrame`: both joystick words after the autoplay override, raw JOY0DAT + left button for the mouse, up to 16 key events
  the keyboard interrupt queued in `ms::KeyQueue`), hands it to the **network seam** `rt::netSetFrameHook(fn(tick, frame&))` (may block,
  may replace the frame; none = local), then applies it: the mouse step, the key events into `mogKeyDown` / the last-key cell
  (`feedGame`, only here), the `ms::TickLatch` (frame, `PadFrame`, tick count, logical clock). `joyPoll` returns the latched joystick
  words, `coopStick` the latched pad frame; key reads see the tables as the tick left them.
* **Poll-free spins** (1.2) call `ms::tickIdle()` (a hook installed by `rtMogMain`: one beam wait + tick when the mode is on): the
  arrival menu's key loop (`game/overworld.cpp`), the fight pause (`game/combat.cpp`), the donation screen (`rt/screens.cpp`),
  `rtMogKeyWaitC`, `rtMogWaitFireC` and every menu / knight-select / name-entry poll (`rt/scene_menu.cpp opReadJoy`). `screenRun` and the AI
  map turn flip every iteration, so they tick anyway.
* **Logical clock and pacing.** The value of `mogFrame` (`LAB_0B9D`) is the latch clock: `netTick` adds the VBLs it stands for, the VBL
  interrupt no longer touches it and counts a private pacing counter (`rt::netPaceFrame`). The loop frame wait is computed from the
  logical clock (`mogFrameWait`, so the number of ticks per loop iteration is a function of the game's own beam waits only: map 1 tick
  from the flip + 1 tick of the remaining VBL, fights 1 + 5), while the **real** wait is what the original computes from real time
  (`budget - (pace now - pace at frame start)` raw beam waits that do not tick). A fast machine runs at the original speed, a slow one
  never waits longer than the original would, and both see the same clock at the same tick.
* **Cursor.** `cursorTick` is split: the arithmetic is `ms::cursorStep(state, joystickBits, ulVbls)` (host-tested against the old
  per-VBL code); in the mode the interrupt hook returns at once and `netTick` steps the cursor from the main thread by
  `ms::kCursorPixelsPerVbl` (2, one named constant) times the VBLs of the tick. The lock cell is written once per tick.
* **Seed.** `rtMogRngSeed` first asks `netSeedTake` (a one-shot `netSeedProvide(v)` or the hook `netSetSeedHook`, which a client blocks in
  until the host's `SEED` arrives); no value = the beam as before. `netSeedAtTick(tick, v)` writes `mogRandomSeed` at the start of that
  tick (session start: the same tick on every node).
* **Flyer wobble** (`cbBeam`, B2) returns the low bits of the logical clock in the mode; no RNG draw.
* **`$88 00`.** `py tools/fightscript.py loops` scans every decodable script of the mog script hunk (knight actions, effects, map objects,
  the nine creature kits) and a heuristic walk of the program overlay: **no `loop random` anywhere** (fixed counts only). The reader
  (`env.pRandom = &mogFrame`) sees the logical clock in the mode anyway. `tests/test_fightscript.py LoopRandomScan` keeps it that way.
* **Not deterministic yet / differences from flag off:** keyboard pads in fights see the pad frame of the tick, not the table after the
  loop's own `opKeyReset` (the merge replaces them anyway); `wait input` barriers of the autoplay count ticks, not polls; the mouse
  pointer cells now move at the tick; the intro and ending are not covered; a press during a file load (`systemUse` window) is queued by
  the keyboard interrupt and applied at the next tick (the audit's open question O1 is answered for a normal run: nothing is lost unless
  64 events pile up). No state hash yet (12.5) and no merge (12.6).

---

## 2. Transport: bsdsocket.library

* **API**: `bsdsocket.library` v4 (Roadshow, AmiTCP 4.x, Miami, Genesis; WinUAE emulates it with "bsdsocket.library
  emulation" on the host's sockets, which is how development and the two-instance tests run). The library base is
  **per task** (each task opens its own), `WaitSelect()` replaces `select()`, `IoctlSocket(FIONBIO)` makes a socket
  non-blocking, errors come from `Errno()`.
* **Topology**: a **star**. The host accepts up to three clients; every client sends its input frames to the host, the host
  merges nothing (merging is game-state logic, done on every machine) but **orders and relays**: it broadcasts the tick's
  complete set of input frames. Only the host needs an open port; n-1 connections instead of n(n-1)/2.
* **TCP with `TCP_NODELAY`** first: reliable and ordered, which is exactly what lockstep needs; the payload is tiny so
  head-of-line blocking costs little on a LAN. The per-tick traffic moves to UDP with redundant history once a probe worked (2.2, built as 12.21); TCP stays for the rest and as the fallback.
* **Port**: one fixed default (configurable in `net.ini`).

### 2.1 The OS problem (the main risk, spike 12.2)

During play the game runs with AmigaOS **switched off**: ACE's `systemUnuse` calls `systemOsDisable` (Forbid and all OS
interrupts off, `ace/src/ace/managers/system.c:1072-1095`) [fact]. The TCP stack and the network card's SANA-II driver are
OS tasks and interrupt servers, so no socket call can work while the game owns the machine. Two ways out:

* **A. Net window per tick**: a `NetWindow` guard (RAII, `docs/RAII.md`) does `systemUse()` (as file reads already do, with
  the copper/DMA state saved), runs the non-blocking `send`/`recv` and `WaitSelect` with a short timeout, then `systemUnuse()`.
  Simple and keeps the game's ownership rule. Costs: the systemUse/Unuse round trip per tick (to be measured), and packets
  are processed only inside windows, so the card's receive buffer must cover a whole tick (fine at TCP's flow control,
  [unknown] for small PCMCIA cards).
* **B. OS alive during a net session**: keep multitasking and the card's interrupt running for the whole session, the game
  using ACE with OS interrupts left on; a net task with its own socket base exchanges frames with the game through a
  ring buffer. Better latency, but it changes the M2 ownership rule for the whole game (VBL/keyboard ownership,
  blitter/copper sharing with the OS) and costs CPU on a stock A1200.

Recommendation: **A first**, measured on WinUAE and on the owner's machine; B only if A's window costs too much frame time.

---

### 2.2 Input over UDP with history (owner 2026-10-10)

TCP stays for everything that must arrive exactly once and in order but is rare: HELLO, CONFIG, READY, SEED, HASH, DROP, BYE,
PING. The per-tick traffic moves to **UDP with redundant history**, so a lost packet costs nothing instead of a TCP resend
hitch:

* **Client -> host:** each packet carries the client's input frames for the last K ticks it has not seen acknowledged (K up to
  8, newest first) plus `ack` = the newest tick whose merged TICK it holds completely. One lost packet is covered by the next.
* **Host -> client:** each packet carries the merged frames (all nodes) of the last ticks that client has not acknowledged, plus
  the host's `ack` of that client's inputs. Both sides drop history the other side has acknowledged.
* **Order and gaps:** frames are keyed by tick, so duplicates and reordering are harmless (the tick ring already rejects DUP and
  flags CONFLICT). If the gap grows beyond what the history covers (a long outage), the receiver asks for a resend over TCP;
  that is the rare path, the waiting box of 4.2 covers it on screen.
* **Size:** an input frame is 11 bytes; 8 ticks x 4 nodes x 11 bytes + headers stays under 400 bytes, far below any MTU.
* **Same port numbers:** UDP on the same port as the TCP game port (6502 direct, 6510 at the relay). The host forwards both
  TCP and UDP 6502 for direct internet play.
* **Fallback:** the session starts on TCP; the nodes then exchange a UDP probe both ways. If no UDP arrives within a few seconds
  (a firewall that only passes TCP, an old stack), the session simply stays on TCP. The player sees nothing either way.
* **Relay:** the lobby server gets a UDP socket on 6510 that forwards UDP between session members, identified by the relay
  session id in each packet (no IP addresses handed out, as for TCP; 2.2b adds the optional direct path, for which the relay does introduce the two ends of a link).
* **Amiga side:** `bsdsocket.library` has UDP (`socket(AF_INET, SOCK_DGRAM)`, `sendto`, `recvfrom`) in every stack; the
  packets go in the same short lean windows as today, so the window cost does not change.
* **Wire format:** new messages INPUT_UDP / TICK_UDP (a small header with session id, sender node, ack, first tick, count, then
  the frames), encoded and checked like the others in `engine/netproto`, mirrored in `tools/netserver/netproto.py`.

#### 2.2a As built (ROADMAP 12.21, protocol 6, 2026-10-11)

Code: `engine/netudp` (pure: the planner, the probe handshake, the acks), `engine/netnode` (sends and reads the datagrams through two optional `NetIo` calls),
`engine/netproto` (INPUT_UDP / TICK_UDP), `rt/net` (the datagram socket, `sendto` / `recvfrom`), `rt/net_session` (opens it, logs), `tools/netserver/server.py`
(the UDP relay).  Tests: `tests/test_netudp.py` (+ `netudp_driver.cpp`), `tests/test_netsession.py` section `nudp`, `test_netproto`, `test_netserver`.

* **Wire (protocol 6, `NET_PROTO_VERSION` 6; the public server must be redeployed).**  One datagram = one message with the usual 6 byte header (type 21 / 22), never on a
  TCP stream.  `INPUT_UDP` (client -> host): session id, token, node, flags, `ack`, `first`, `count`, `count` input frames (19 + 11c bytes).  `TICK_UDP` (host -> client):
  session id, token, node, flags, `ack`, drop sequence, nodes, `first`, `count`, `count` x `nodes` frames (21 + 11nc bytes, 373 at most).  The window is
  **ascending** (`first` = the oldest tick in it; "newest first" in the first sketch above was only about which ticks are kept), at most `NET_UDP_HISTORY_MAX` = 8 ticks,
  `count` 0 is a probe or an ack alone.  `session` = the relay session id (0 = a direct game), `token` = the receiving client's rejoin token (NETPLAY.md 4.5: the
  secret the host made and told only that client in its CONFIG), so a stray datagram is ignored; the flag `NET_UDP_HEARS` says "I have received a valid datagram of yours".
* **Acks.**  `ack` = how many ticks the sender holds, contiguous from tick 0 of the ring: the client's ack of the TICKs counts the complete ticks ahead of the game's tick
  (not while a node is dropped: a rejoin may still change them), the host's ack of a client's input counts that node's frames.  The peer's ack only moves forward and is
  clamped to what was produced.  A client treats the ticks its game already consumed as acknowledged (the host cannot have run them without the client's input).
  An ack that no datagram carried is sent alone after 80 ms (`NU_ACK_DELAY_MS`); normally the next tick's datagram carries it.
* **The window.**  Every datagram carries all ticks the peer has not acknowledged, newest 8 at most.  A lost datagram costs nothing as long as one of the next 7 gets
  through; a datagram that arrives twice or late is harmless (frames are keyed by tick into the tick ring: a duplicate is `NET_PUT_DUP`, a different frame for the same tick
  is `NET_PUT_CONFLICT` = desync, as on TCP).  The host keeps the relayed TICKs of the last 16 ticks (`NetNode.aHist`) so that it can repeat them after it has run on.
  A stalled sender repeats the window every 60 ms (`NU_RESEND_MS`), a quiet link sends an ack-only datagram every 4 s (NAT mapping, relay membership).
* **Beyond the history (the TCP escape).**  Ticks older than the window go over TCP once (sender-driven, in the existing INPUT / TICK messages, so the receiver needs no new
  code); and when the peer's ack has not moved for 250 ms (`NU_ESCALATE_MS`) **every** unacked tick also goes over TCP, once.  So an outage costs at most a quarter second
  of waiting, not the outage; the waiting box of 4.2 covers the rest.  There is no "ask for a resend" message: the sender knows from the missing ack.
* **Probe and fallback.**  The session starts on TCP (HELLO, CONFIG, READY, SEED, HASH, DROP, BYE, PING, the rejoin and the lobby messages stay there for ever).  When the
  lockstep is configured each link probes: a datagram without frames, every 250 ms.  A client knows where the host is (the connect address, or the lobby server for a relayed
  game); a direct host learns the client's address from its first datagram (token + node must fit), a relayed host's link has the lobby server as its peer.  The handshake is
  two-way (three datagrams): a link goes ACTIVE only after a datagram that carries `HEARS`, so a path that works in one direction never starts.  While ACTIVE the ticks
  are not sent over TCP.  No handshake within 3 s of **pumping** time (`NU_PROBE_GIVEUP_MS`; a scene load counts at most 250 ms per gap) -> the link stays on TCP,
  silently (`net: udp: fallback to tcp (no answer)`).  An ACTIVE link whose acks stop for 5 s with ticks outstanding (`NU_LOSS_MS`) reverts to TCP for the rest of the
  session (`... (loss streak)`); it never comes back to UDP.  **The 5 s are PUMP time on a living path** (first emulator runs 2026-10-12: a fight / lair load stops the pump for
  10 s on both machines, and the wall-clock rule called that a loss streak): `NetUdpLink.ulStuckMs` adds at most `NU_PUMP_GAP_MAX_MS` (250) per plan, whatever the gap since the
  last plan, and nothing while the node flags the peer silent (`isPeerSilent`: nothing at all from it for `NU_ALIVE_MS` = 2.5 s, no datagram and no TCP, though its PINGs come every
  second: it is loading or frozen, and TCP would wait just as long).  Only a peer that is alive over TCP while its acks do not come is a black hole.  The 250 ms escalation runs on
  the same clock.  A rejoiner's link is always TCP (its replay of the ticks past the turn is a TCP matter, 4.5): the rejoiner's node has no UDP at all, the host's link record is
  fresh (`netNodeAdoptRejoiner`: TCP, reason rejoin), `hostOnInputUdp` refuses datagrams for such a link (a late datagram of the player's old incarnation: same node, token and
  session), and `hostDropNode` clears the dropped player's datagram state (window, acks, peer address) even when the broken link was already marked unused; the survivors
  keep their UDP.  (The 2026-10-12 logs show `udp: ok (node 1)` and a second fallback line after the rejoin in the JOIN window's section: it is the joiner's first session, the logs
  are per window.)  Forced TCP: command line `TCP` or `net.ini` `[net] udp = no` (`... (disabled)`); no datagram socket or no peer address -> `... (no udp socket)`.
* **Drops stay in order.**  DROP (TCP) must reach a client before any TICK of the drop tick or later, which a datagram could overtake.  Every TICK_UDP carries the number of
  DROPs the host has sent; a client that has received fewer ignores the datagram (the window repeats it) until the DROP is in.
* **Ports and sockets.**  The host binds its datagram socket on the game port (the same number as TCP, 6502; 6590 in the test runs; forward UDP as well for direct internet
  play), a client on an ephemeral port.  All calls are in the same lean windows as the TCP ones: a `recvfrom` per pump (at most 16 datagrams) and one `sendto` per tick and link.
  The socket is a stack guard (`Socket`, closed with the transport).
* **Relay.**  `tools/netserver` opens a UDP socket on the TCP port's number.  Members are identified by the relay session id and the token in each datagram; the server learns
  the tokens from the CONFIG messages it forwards over the TCP pipes, registers the source address of the datagrams it sees (a client's INPUT_UDP registers the client and
  is forwarded to the host's address; a TICK_UDP from the registered host IP registers the host and is forwarded to the client of its token), forgets silent addresses after
  60 s, drops what is oversized, undecodable, of another kind, of an unknown session or token, from a non-host IP as TICK_UDP, or above 400 datagrams/s per source IP
  (burst 800), allows 3 clients per session, logs and hands out no address (except the PUNCH_INFO of 2.2b to the two ends of a link).  Without UDP on the server (or a firewall) the games stay on TCP.
* **Log lines** (docs/AUTOPLAY.md): `net: udp: probe sent (node N)`, `net: udp: ok (node N)`, `net: udp: fallback to tcp (reason) node N`, and at the end of a session
  `net: udp: session: sent N received M covered C escalated E bad B probes P, links active A tcp T` ("covered" = frames that arrived new in a datagram that was not the
  newest one: the history paid for a lost or late datagram; "escalated" = ticks that also went over TCP).  The PERF heartbeat of an autoplay build adds
  `PERFU udp tx= rx= covered= esc= bad= active= tcp=`.  `tools/netpair.py` expects `udp: ok` on both windows and no fallback (`--tcp` runs the A/B with TCP forced).
* **Not done.**  A rejoiner on UDP; NAT hole punching is 2.2b (client to host only, the star stays); a measured comparison of the window cost on real hardware (WinUAE only so far); resend requests.

#### 2.2b Relay-assisted direct connection (hole punching, owner request 2026-10-26)

A game joined through FIND runs through the lobby server's relay, so every tick makes the detour client -> relay -> host (about 100 ms of ping for a player with 30-45 ms to the
relay on both sides).  The relay now also introduces the two ends of a link to each other; they try a **direct UDP path** and move the per-tick traffic there when it works.  Whatever
does not work stays exactly as in 2.2a: the relayed UDP path, and below it TCP through the relay.  TCP control (HELLO, CONFIG, PING, HASH, DROP, BYE, the rejoin) always stays on the relay.

* **Per link, never per session.**  The topology is the star of section 2: clients only ever talk to the host.  So the direct path is a property of **one client-host link**.
  A 4-player game has three links; each one goes direct (a punched NAT, or the LAN) or stays on the relay, and each one falls back on its own.  A mixed session (client 1 direct,
  client 2 via the relay, client 3 direct) is normal: the host's tick broadcast is one TICK per client link anyway (`udpFlushLink` per link), and every client acknowledges what it
  holds whatever path it came by.  There is no client-to-client path.
* **Every sender picks its own path.**  A receiver accepts a valid datagram (session, token, node fit) from any source and only looks at the ack and the frames in it.  So a path that
  works in one direction only costs nothing: the sender whose acks do not come moves to the relay, the other direction keeps going direct.  Frames are keyed by tick, so a tick that
  arrives by both paths is a duplicate (`NET_PUT_DUP`), never a conflict, and the 8-tick history window of 2.2a repeats everything the peer has not acknowledged on whatever path is
  current: a path change cannot lose a tick or desync.
* **Messages** (UDP only, between a node and the relay; same protocol version 6, no version bump):
  `PUNCH_REG` (type 23, 16 bytes): session, the **client's token** (it names the link), role (host / client), capability bit `DIRECT`, the node's own address and port of its
  datagram socket (0 = unknown).  `PUNCH_INFO` (type 24, 21 bytes): session, token, flags (`LOCAL`), the peer's public address and port as the relay saw its datagrams, and the peer's
  own local address and port (valid with `LOCAL`).  The two directly exchanged probes are ordinary `INPUT_UDP` / `TICK_UDP` datagrams without frames, with two new flag bits:
  `NET_UDP_DIRECT` (2: "this datagram travelled the direct path") and `NET_UDP_DHEARS` (4: "I have received a DIRECT datagram of yours").
* **Compatibility is by silence.**  A node sends `PUNCH_REG` only when it is built with this feature and `net.ini direct` is not `no`.  The relay answers only to a registered pair.
  An **old client** never registers, so the relay never sends anything about it, and its host (new) simply gives up on that link after 8 s (`NU_PUNCH_INFO_GIVEUP_MS`; log
  `direct failed, via relay (no answer from the relay)`) and goes on with the relayed UDP as before.  An **old relay** drops `PUNCH_REG` as an unknown type (it fails the decode
  of `tools/netserver/netproto.py`), so new nodes give up the same way after 8 s.  An old node never receives a type or a flag it cannot decode.  The types 23 / 24 are not game
  messages (`GAME_TYPES` ends at 20): they cannot pass the TCP pipes.
* **The relay's part** (`tools/netserver/server.py`, `_punch_reg`): it checks session and token exactly as for `INPUT_UDP` (the token must have passed a forwarded CONFIG), takes the
  node's public address from the **source of the PUNCH_REG datagram** (the NAT mapping of the socket that talks to the relay, i.e. the one the peer has to hit), and when both
  ends of the link have registered it sends `PUNCH_INFO` to **both** (and again to whoever repeats its `PUNCH_REG`).  The peer's local address is handed over only when both ends
  come from the **same public IP** (they are on one LAN) **and** the address is private / link-local / loopback (a node cannot make its peer probe an arbitrary internet address).
  The host's registration only counts from the IP that registered the game.  Nothing is logged; the addresses live in RAM and expire with the UDP registration (60 s).
  **Privacy change:** until now no node ever learned another player's address through the relay.  A client now learns the host's public IP and the host the clients' (that is what a
  direct connection is).  `net.ini direct = no` keeps a player out of it; `--no-punch` makes a server ignore `PUNCH_REG`.
* **The state machine per link** (`engine/netudp` `NetDirect`, pure and host-tested): `REG` (the link is relayed and probing; `PUNCH_REG` every 500 ms, at most 8 s) -> `PUNCH`
  (`PUNCH_INFO` arrived; **both ends send probes at once** to the candidate addresses every 100 ms, from the same socket that talks to the relay so the NAT mapping matches; at most 5 s)
  -> `DIRECT`; any failure -> `RELAY` for the rest of the session (never back, like TCP).  Candidates: the peer's public address, and its local address when `PUNCH_INFO` has one; a
  datagram from the local address replaces a public source ("on one LAN the direct local path is preferred") and a public source never replaces the local one; a source address
  different from the one the relay saw (a NAT that picked another port) is answered as well.  The handshake is two-way like the relay one: a link is `DIRECT` when a datagram that
  carries `DHEARS` arrives (the peer hears us and we hear the peer), three datagrams, and the one that completes it is answered at once so the peer completes too.
* **Using the path.**  The link's own state (probe handshake of 2.2a, `ACTIVE`) is untouched; once it is `ACTIVE` and the direct path is `DIRECT`, this side's datagrams (ticks, acks, the
  4 s keep-alive) go to the direct address with the `DIRECT` flag.  While direct, an extra ack-only datagram goes **via the relay** every 4 s, so the relay path (the relay's registration of
  the member, the NAT mapping towards the relay) is still alive when it is needed.  The direct path's own NAT mapping is kept by the normal 4 s keep-alive of the link.
* **When the direct path dies** (the mapping expired, a route changed, a firewall closed): the sender's acks stop.  After `NU_DIRECT_STALL_MS` = 400 ms of pump time with ticks
  unacknowledged (the same stall clock as the loss rule; it does not count while the peer is silent in a scene load) the sender moves to the relay path, logs
  `direct failed, via relay (path lost)`, and gives the relay path its own full loss budget (250 ms TCP escalation, 5 s to TCP).  On the direct path the TCP escalation is suspended
  (it would move the acks and hide the stall).  The other end notices on its own.  Nobody waits for the other: no tick is lost, the lockstep waits at most those 400 ms.
* **Log lines** (docs/AUTOPLAY.md): `net: udp: direct ok (node N, a.b.c.d:port)` when a link goes direct (the address is the peer's direct address; node = the client's node, on both
  ends), `net: udp: direct failed, via relay (node N, no answer from the relay | no answer from the peer | path lost)`, and `direct D` appended to the session summary line.  Not done:
  the on-screen ping line does not show DIRECT / RELAY yet (the lobby text has its own work in progress).
* **Test options** (`net.ini [net]`, tests only): `direct = no` (never register), `direct_test = block` (drop every received direct datagram: the punch cannot succeed) and
  `direct_test = kill` with `direct_kill_ms = N` (drop them N ms after the link went direct: the path dies).  Because the options act on the receiver they work on one node of a
  session only, which is how a mixed session is made on localhost.
* **Code:** `engine/netudp` (state machine, `netUdpPlan` path choice, flags), `engine/netnode` (PUNCH_REG / PUNCH_INFO, probes, receive), `engine/netproto` (messages),
  `rt/net_session` (local address, log lines, options), `rt/net` / `rt/sdl/net_winsock` (`udpLocal`: the node's own address and port, from `getsockname`; the Amiga call is the SFD's
  -102, not yet run on hardware), `tools/netserver/server.py` (`_punch_reg`).  Tests: `tests/netudp_driver.cpp` (`direct`: machine, plan, pair), `tests/netsession_driver.cpp`
  (`testDirect`: real nodes over a fake relay: 2 and 4 nodes, mixed links, an old client, an old relay, loss, a dying path), `tests/test_netproto.py`, `tools/netserver/selftest.py`
  (leg 5), `tests/test_netserver.py`, and the end-to-end run `tests/pc_net/pc_net_relay.py` (two to four PC instances through a local `server.py`).
* **Not done / limits.**  Symmetric NATs (a different mapping per destination) and strict firewalls cannot be punched: those links stay on the relay, by design.  IPv4 only.  The
  direct path is not retried after it failed.  A rejoiner's link is TCP-only as before.  Nothing was measured on a real NAT or in a real 100 ms case yet: the localhost runs prove the
  logic, the lockstep and the fallbacks, not the NAT behaviour.

## 3. Who controls what (the merge)

Each machine is a **node** (0 = host). Each human knight (classic) or party member (co-op) is bound to `(node, PadSource)`;
the binding is made in the lobby and the knight screen and sent in the session config, so every machine has the same table.

### 3.1 Normal mode

| Situation (game state) | Port 1 word (the "player stick") | Port 0 word | Keyboard / mouse |
|---|---|---|---|
| Map turn of human knight k (`setupTurn` writes `+11 = 2`, `src/game/rules.cpp:190`) | k's node, its joystick | 0 | k's node |
| Turn of an AI (black) knight | 0 (AI moves from the RNG, identical everywhere) | 0 | nobody (Esc ignored) |
| Town / place / loot screens of knight k | k's node | 0 | k's node |
| Duel of two humans (`fightMeet`: first human port 1, second port 0, `src/game/combat.cpp:256-265`) | first knight's node | second knight's node | first knight's node |
| Practice (knight 0 port 1 vs knight 1 port 0, `src/game/mainloop.cpp:49,52`) | node of knight 0 | node of knight 1 | host |
| "Next Day" (`NET_SCREEN_NEXTDAY`, `rtOwNewDayWait`; the current knight is a black knight's here, so no turn knight's pad exists) | every human knight's pad OR-ed | none | host |
| Title menu, knight select | each node picks its own knights (12.9); the menu itself is the host's | | host |

A player always uses the joystick in **their own** port 1 (and may pick another local controller): the merge maps it onto the
port word the original reads, so the rules code does not change.

Merge details decided in 12.6 (`src/engine/netplay.cpp`, comment above `netMerge`): the title menu is the host's (raw ports,
keyboard, mouse); in a duel with an AI first fighter keyboard / mouse go to the second fighter's node; a classic knight's word
comes from its binding `(node, pad)`, default joystick 1 of its own node, so hot-seat on one machine works unchanged.

### 3.2 Co-op mode

Owner 2026-10-10: online players can use the keyboard as well as a joystick. Each player picks Joystick 1, Joystick 2, Keys
arrows or Keys WASD on the knight screen's Control line **on their own machine** (built 2026-10-10, task "net pads").

**The binding is shared game state.** `PartyConfig` (`include/game/party.hpp`) gets `isNet` and `aubNode[member]` next to
`aubPad[member]`. Member m sits at node `aubNode[m]` (the CONFIG's member-to-node table, `netDefaultBindings`: m mod nodes; the
rt capture step `netSnapshotCapture` binds the party to it on the first tick after the title menu started the co-op party, the same
tick on every node) and `aubPad[m]` is the controller its player picked **on that node** (`PAD_NONE` until picked). The pair is in
the state hash (`net_hash`, group `party`: `pad`, `node`), so a node that disagrees about a pick is a desync, not a silent split.

**Routing.** The game still reads member m from the merged pad frame (`partyPadBits`, `ubFocus` TURN / ANY / member unchanged), at
slot `partySlot(m)`: the member index in a net game, `aubPad[m]` otherwise. `game/net_state` hands the merge `{node aubNode[m], pad
aubPad[m], slot m}`; `netMerge` puts node N's `PadFrame` slot P word into merged slot m, so two players who both picked "Joystick 1"
on their own machines land in distinct slots and never collide. Fights already give each fighter its own pad (8.4b), so a 4-knight
fight with four machines needs nothing beyond this. Focus ANY (title menu, the "press fire" waits): every slot is that pad OR-ed over
all nodes, the host owns keyboard and mouse. Local (non-network) co-op (`isNet` false, also the zero BSS state) is unchanged:
`aubPad[m]` is the slot.

**Uniqueness is per node.** Two members on the same machine need different controllers; members on different machines may pick the
same one (everybody has a Joystick 1). `partyPadTakenFor / partyNextPad / partyFirstFreePad` look only at earlier members of the same
node in a net game; the net default offer is Joystick 1, then the next free one on that node.

**The knight screen in lockstep (superseded by 4.2b, 2026-10-10).** A network game no longer runs the knight screen in lockstep: the lobby is the
knight screen, outside the lockstep, and the party is bound to its nodes and controllers by the CONFIG (`game/net_knights`). What follows still
describes `NET_SCREEN_PADS` and the Control line's focus, which only a knight screen opened *inside* a running session would use. The screen runs on every node and must stay identical, so only the owner's input may move a
member's Control line. While member m's line is on offer the screen sets `g_party.ubFocus = m`; `netStateFill` turns "co-op menu
with a member as focus" into the new screen class `NET_SCREEN_PADS`, and `netMerge` then merges only node `aubNode[m]`: the "player
stick" is every pad of that node OR-ed (the owner may hold any controller, the offer is only what he is about to choose), keyboard
and mouse belong to that node, every other node (and every other member's pad) is ignored. Left / right cycle the free controllers
of that node, fire takes the one on offer and returns the focus to ANY (portrait and name entry stay open to everybody, as 12.8b).
The line reads "Player N  <pad>" so each player sees whose line it is.

**End of session.** `partyLocalize` (called by `endLockstep`) clears `isNet`, so the members read their picked controllers from the
local pad frame again. Not done here (drops, 12.11): `netStateApplyDrop` rebinds a dropped node's members to the host on a `NetGameState`
copy; the live source of truth is now `g_party`, so the drop code must write the new `(node, pad)` into `g_party` (and `partyPadTakenFor`
keeps the host's per-node rule). A member whose node is gone has no owner: the knight screen waits (merge gives nobody), the map gives
no input.

Host tests: `tests/netplay_driver.cpp testMergePicks` (every `(node, pad)` pick of four members on 2..4 nodes, same pad on different
nodes, the `NET_SCREEN_PADS` merge), `tests/test_coop.py` (per-node uniqueness, slots, localize), `tests/netsession_driver.cpp
npads::` (a real `SceneKnights` per node on a lockstep session over fake pipes: node 1 plays Keys WASD and node 0 Joystick 1, the
other node's presses on a line do nothing, both nodes merge identical input at every tick and end with the same party).

---

## 4. Session

* **Starting a session** (player request 2026-10-10: one Amiga hosts, the other joins by typing the host's IP address, on
  the command line through `ReadArgs()` or in the main menu). Two Amigas are the 2-node case of the star, i.e. plain peer to
  peer: the host listens, the client connects to it, and from then on both exchange input frames. Sources, the first one
  found wins:
  1. **CLI arguments** through `dos.library ReadArgs()`, template
     `HOST/S,JOIN/K,PORT/K/N,NAME/K,DELAY/K/N`: `moonstone HOST` waits for players, `moonstone JOIN 192.168.1.20` (a name
     works too, resolved with `gethostbyname`) connects. Parsed in `main()` (`src/main.cpp:126`) **before** `rt::systemCreate()`,
     while the OS is still fully there, and kept in a plain `NetArgs` struct for the game; the `RDArgs` are released by a guard
     (`FreeArgs`) right after parsing (RAII, `docs/RAII.md`).
  2. **Workbench icon tool types** with the same keywords (`HOST`, `JOIN=192.168.1.20`, `PORT=...`), since `ReadArgs()` has no
     arguments when the game is started from its icon; read through `icon.library` in the same place.
  3. **`PROGDIR:net.ini`** (read with `engine/inifile.cpp`): defaults such as `port`, `name`, `delay`, the last address.
  4. **Title menu** (12.8a): "Network: Off / Host / Join" and an address editor; the address typed there is written back to
     `net.ini` for the next start. The bold font has no `:` glyph, so the editor needs its own glyphs (or digits and `.` only,
     with the port on its own line).
  Without any of these the game starts as today (no session, classic and co-op play on one machine unchanged).
* **Handshake**: protocol version, exe build hash and data checksum (all nodes must run the same build on the same disks:
  a different build is a different game), player name, local controllers. The host answers with its first random seed (4.1), mode (classic /
  co-op), player count and the knight-to-node table.
* **Lobby** (4.2b): the knight screen, entered straight from HOST GAME / a finished CONNECT; START when another player has joined and everybody
  has picked; the game starts on the map when every node reported ready.
* **Messages** (pure encode/decode in `src/engine/netproto.cpp`, versioned, big-endian on the wire):
  `HELLO`, `LOBBY_PICK`, `LOBBY_STATE`, `CONFIG`, `READY`, `SEED(tick, seed)`, `INPUT(tick, node, frame)`, `TICK(tick, frames of all nodes)`
  (host broadcast), `HASH(tick, crc32)`, `PING/PONG`, `BYE(reason)`, `DROP(tick, node)`.
  **Implemented (12.6/12.7, `include/engine/netproto.hpp`):** header 6 bytes (u16 magic `$4D53` "MS", u8 protocol version 6, u8 type,
  u16 payload length), payload at most 512 bytes, names 16 bytes printable ASCII NUL-padded (one encoding per message).

  | type | message | payload |
  |---|---|---|
  | 1 | HELLO | u32 build hash, u32 data checksum, name[16], u8 controller mask, u16 game version, u8 rule options (12.8b: gore 1, enhanced art 2, mods 4) |
  | 2 | CONFIG | u8 mode, u8 nodes 1..4, u8 your node, u8 delay 0..8, u32 seed, u8 b 0..4, b x {u8 node, u8 pad, u8 slot}, nodes x name[16], nodes x u8 portrait (0..3, all different) |
  | 3 | READY | u8 node |
  | 4 | SEED | u32 tick, u32 seed |
  | 5 | INPUT | u32 tick, u8 node, frame (11) |
  | 6 | TICK | u32 tick, u8 count 1..4, count x frame |
  | 7 | HASH | u32 tick, u32 crc32 |
  | 8 / 9 | PING / PONG | u32 token |
  | 10 | BYE | u8 reason (normal, version, build, full, desync, timeout, dropped, refused), u16 the sender's game version |
  | 11 | DROP | u32 tick, u8 node (the host's tick; sent before any TICK at or after it) |
  | 12 | LOBBY_PICK | u8 stage (0 browse, 1 claim, 2 done), u8 portrait 0..3, u8 pad (PadSource or $FF), name[16]; browse and claim carry pad $FF and an empty name, done a name and a pad (19) |
  | 13 | LOBBY_STATE | u8 mode, u8 your seat 1..3, 4 x {u8 state (0 free, 1 browsing, 2 claimed, 3 done), u8 portrait, u8 pad, name[16]}; a free seat is all zero (pad $FF), claimed and done portraits are all different (78) |
  | 21 | INPUT_UDP (datagram, protocol 6, 2.2a) | u32 session, u32 token, u8 node 1..3, u8 flags (1 = HEARS), u32 ack, u32 first, u8 count 0..8, count x frame (19 + 11c); client -> host |
  | 22 | TICK_UDP (datagram, protocol 6, 2.2a) | u32 session, u32 token, u8 node 1..3, u8 flags, u32 ack, u8 drop sequence, u8 nodes 2..4, u32 first, u8 count 0..8, count x nodes x frame (21 + 11nc); host -> client |
  | 32 | ANNOUNCE (LAN UDP) | listing (27) |
  | 33 / 34 / 35 | REGISTER / REFRESH (u8 players, u8 state: 0 waiting, 1 playing) / UNREGISTER | listing (27) / 2 / empty |
  | 36 / 37 | LIST_REQ (u32 build hash) / LIST_REPLY | 4 / u8 count 0..8, count x {u32 game id, listing} (1 + 31c) |
  | 38 / 39 | JOIN_REQ / JOIN_REPLY | u32 game id, u32 my build hash, u16 my game version (10) / u8 result (direct, relay, not found, full, build mismatch), u32 IPv4, u16 port, u32 relay session, u16 the host's game version (13) |
  | 40 | RELAY_HELLO | u32 relay session id, u8 role (0 host, 1 client): the first message of a relay connection |
  | 41 | REGISTER_REPLY | u8 result (listed direct, listed with relay, bad name, already listed, server full), u32 game id, u32 relay session id: the lobby's answer to REGISTER |

  A listing is name[16], u8 mode, u8 players, u8 max players, u32 build hash, u16 TCP port, u16 game version (27 bytes). The **input frame**
  (`NetFrame`, 11 bytes): u32 with the six `PadSource` words at 5 bits each, u8 flags (key count 0..4, mouse button), i8 mouse
  dx / dy, four raw key events (bit 7 = down). `NetFramer` cuts messages out of a TCP byte stream.

  **Game version (12.17, protocol 2).** `include/engine/version.hpp` holds the one constant `GAME_VERSION` (u16, major << 8 | minor), a header
  so the game, the host tests and the protocol all see it with no build-system plumbing; bump it there. It travels in HELLO, the
  listing, BYE (the sender's version) and JOIN_REQ / JOIN_REPLY, purely so the screens can say "host runs 1.3, you have 1.2": the
  **build hash stays the only compatibility check**. Where the client learns the host's version: from the list (listing), from
  JOIN_REPLY (also on a build mismatch, where nothing else is handed out), and from the host's BYE(version / build) after HELLO.
  The lobby server itself has no game version and sends 0. The protocol version byte is a different thing (wire layout); it is 4 (protocol 4 = the lobby: LOBBY_PICK / LOBBY_STATE, the portraits in CONFIG;
  the lobby server accepts exactly the version of `tools/netserver/netproto.py` `PROTO_VERSION`, which this change bumped to 4 - **a deployed server of protocol 3
  must be updated before a protocol-4 game can list itself**, see tools/netserver/README.md "Updating").

  **Playing games (12.18, protocol 3).** REFRESH carries a state byte: 0 waiting, 1 playing (one exact encoding, validated like every field).
  The host sends 1 from the first REFRESH after START and keeps refreshing every 30 s during the game. The server keeps such a listing (dashboard
  status "Playing", API `status`), leaves it out of LIST_REPLY and answers JOIN_REQ for it with "full"; it is sticky (a later 0 does not bring it
  back); UNREGISTER, a closed connection or 75 s of silence end it. The old behaviour (UNREGISTER at START) is gone: UNREGISTER now means "the
  session ended".

  **Lobby / relay handshake (12.17, 12.13a).** The host sends REGISTER; the lobby connects back to the registering address and answers
  REGISTER_REPLY with `direct` (clients get the host's address from JOIN_REPLY) or `relay` (plus a session id). A relayed host keeps
  one idle connection to the lobby (RELAY_HELLO host); a client connects and sends RELAY_HELLO client; the lobby pairs them and then
  forwards whole game messages (types 1..13) between the two connections unchanged, one pipe per client (the host opens a fresh idle
  connection as soon as data arrives on the old one). JOIN_REPLY `relay` carries address 0 and port 0, meaning "the lobby you
  are talking to". `tools/netserver/` implements it (README there).

### 4.1 Random seed: the host sends it

The game has one RNG stream, the 32-bit shift-register state `LAB_0973` (`mogRandomSeed`, stepped by `ms::rngNext` /
`rngPercent`, `include/engine/util.hpp:12-16`) [fact]. It is seeded at exactly one place, `rtMogRngSeed`
(`src/rt/mainloop.cpp:219-221`, the original `LAB_04A5` at the start of `SECSTRT_0`): the low two bits of `VHPOSR` pick one of
the four longs of `LAB_0974` [fact]. In a session:

* **Only the host draws.** On the host `rtMogRngSeed` runs as today (its own beam position, the original's four seeds) and
  the host sends the result to every client as `SEED(tick, value)` before it lets the tick run.
* **Clients never read `VHPOSR` for the seed.** Their `rtMogRngSeed` blocks at that tick until the host's `SEED` arrives and
  writes that value into `mogRandomSeed`. A missing seed is a stall like a missing input frame (4.2).
* **Session start is a seeding event too** (12.3 audit): the only original seeding runs at `mog` entry, before the title
  menu, so a session started from the menu (12.8a) begins after it. At session start the host draws a fresh seed and every
  node writes it into `mogRandomSeed` at the same logical tick (the first tick of the session).
* **Every seeding event, not only the first.** `SECSTRT_0` runs at each entry into `mog` (overlay switch,
  `docs/BOOT_CHAIN.md`), so a new game after a game over is seeded again; each event is its own `SEED` message, matched by tick.
  The first one also travels in `CONFIG` so a client can check it before the game starts.
* **Nothing else needs a seed.** The other random-looking sources (the `$88` loop count from the tick counter, the flyer's
  `cbBeam`) become functions of the logical tick in deterministic mode (12.4), identical on every node by construction.
* **Checked.** `mogRandomSeed` is part of the state hash, so a client that missed or misapplied a seed is caught at the next
  hash exchange as a desync, with the seed values in both dumps.
* Owner option (Q7): the host may send **any 32-bit value** (for example from its timer) instead of one of the original
  four, for more varied games; the default keeps the original four so classic sessions match single-player behaviour.

### 4.2 Drops and desync

* **Disconnect** (owner 2026-10-10: wait for the other player, and let the player stop waiting): when a tick stalls for more than
  about a second, the game shows a **WAITING FOR <name>** box over the frozen picture and keeps waiting, with no short automatic
  drop, so a slow link or a player who briefly lost the connection does not lose their knight. The waiting player can end it:
  **fire or Esc: "CONTINUE WITHOUT <name>"** (a second press confirms, so a stray fire does not drop anyone).
  * The **host** stopping the wait drops that client: DROP(tick, node) to every remaining node, applied at the same tick.
  * A **client** stopping the wait for the host leaves the session and plays on alone from the same state (the other players'
    knights are treated as dropped on that machine, by the mode rules below); the remaining clients see the host as dropped.
  * A closed TCP connection skips the waiting box and drops at once (the peer is gone, nothing to wait for).
  * **The host stops or leaves the game** (owner 2026-10-10): every client **continues on its own machine** from the same state,
    with a short **HOST LEFT, PLAYING ON ALONE** message. There is no host migration, so with several clients each one goes on
    alone; the other players' knights are treated as dropped there (mode rules below). The host's BYE(normal) makes this
    immediate; a host that simply vanishes reaches the same result through the waiting box or the safety timeout.
  * A very long safety timeout (5 minutes, `TIMEOUT` overrides) drops without a press, for unattended machines.
  After a drop the dropped knight is handled by mode: Classic: the dropped knight becomes an AI knight (`ulKind = 4`, the
  original's black-knight state, `src/rt/arena.cpp:1037-1038`) (owner 2026-10-10). Co-op: its member's controller passes to
  the host (owner 2026-10-10), or to the player who plays on alone.
* **Desync**: hash mismatch stops the session on every node with `net: desync at tick N` and a state dump in the serial/boot
  log, so the two dumps can be diffed.  A desync is not a drop: nobody becomes an AI knight, the game goes on locally as before.

#### 4.2a Drops and the waiting box as built (ROADMAP 12.11, 2026-10-10)

Code: `engine/netwait` (the box state machine and its text, pure), `engine/netnode` (drop events, DROP, keepalive; pure), `engine/netplay`
(`netBindingsDrop`, `NetDropEvent`), `game/net_drop` (what a drop does to the game; pure), `rt/net_box` (draws the box), `rt/net_session`
(the glue), `rt/input` (`inputConfirmDown`).  Tests: `tests/netsession_driver.cpp` (`testWaitBox`, `testNetDropGame`, `testDropGame`: full
sessions of 2 to 3 nodes with the game side, classic and co-op), `tests/netplay_driver.cpp` (binding table), both with mutation proofs.

* **The waiting box.** A tick that has waited about a second (`NW_BOX_DELAY_MS`) draws a box in the middle of both screen buffers (colour 0
  with a frame in the text's ink, the small menu font, no palette change; the picture under it is saved and put back when the wait is over,
  `rt::NetBox` is the RAII guard): line 1 **WAITING FOR <NAME>**, line 2 FIRE OR ESC TO STOP.  The wait goes on - nothing drops by itself
  before the safety timeout.  The player's press is read from the hardware directly (`inputConfirmDown`: either joystick fire, the left
  mouse button, Esc; the lockstep input is frozen while waiting).  A press that starts while the button is still held from before does
  not count (an edge is needed).  **First press: CONTINUE WITHOUT <NAME>** / FIRE OR ESC AGAIN; a **second press within 4 s**
  (`NW_CONFIRM_MS`) confirms; after 4 s without it the question lapses back to WAITING FOR (a stray press drops nobody).  The host's box
  names the client the tick still waits for ("SEVERAL PLAYERS" when it is more than one); a client's box always names the host (the host
  relays every tick).  Names are upper-cased and cut to 12 characters.  No ':' , ',' or '?' is used (the game fonts lack glyphs): the host-left
  message is two lines, HOST LEFT / PLAYING ON ALONE.
* **Host confirms** (`netNodeDropMissing`): every client the tick waits for is dropped.  The drop tick is the first tick the host has **not yet
  relayed** (`ulNextBroadcast`, not its own tick: a client can be up to the input delay ahead of the host), so no client has run it and none has
  seen a TICK for it; `DROP(tick, node)` is queued to the other clients before any TICK at or after it, the dropped client gets BYE(dropped)
  and its link closes, TICKs carry an empty frame for the dropped node from its drop tick on.  The session goes on for the rest; when the last
  client is gone the host's node ends with `NE_ALONE` and the game plays on alone.
* **Client confirms while waiting for the host** (`netNodeClose`): BYE(normal) to the host (which drops that client at once and goes on), the
  client's node ends (`NE_CLOSED`) and the game plays on alone from the same state.
* **Closed connections and deliberate quits.**  A client's closed link, broken send or BYE (anything but desync) at the running host drops
  it at once, no box.  The host's BYE(normal) (quit: `netSessionShutdown` from `gameMain`) or closed link ends every client's node
  (`NE_PEER_BYE` / `NE_LINK_LOST`): each client plays on alone with the **HOST LEFT / PLAYING ON ALONE** message (2.5 s, same box).
* **Safety timeout.**  `NN_DEFAULT_TIMEOUT_MS` is now 5 minutes (was 10 s), `TIMEOUT` overrides.  A wait as a whole that exceeds it drops the
  missing nodes on the host (as a confirmed drop); a client that waited that long leaves (BYE timeout) and plays on alone.  A client's
  whole-wait limit is `NN_CLIENT_GRACE_MS` (15 s) longer than the host's, so the host's own drop of a third node reaches it first.  The link
  silence timeout (peer silent while this node waits) uses the same value.  **Keepalive**: a waiting node sends PING every second
  (`NN_KEEPALIVE_MS`), the peers answer PONG, so a host that waits for one client does not look dead to the others; only a peer that
  has really gone silent runs into the silence timeout.  There is no shorter automatic drop any more.
* **What a drop does to the game** (`game/net_drop`, applied by `applyDueDrops` at the drop tick, after the state-hash capture of that tick
  and before the merge, in (tick, node) order on every machine):
  * Classic: each of the dropped node's human knight records gets `ulKind = KIND_AI` (4); outside a fight also the black knight's map type
    (`ActorType::KnightMap`) and AI port, as `resetGame` (LAB_01AE) gives them; in a fight both stay as the fight set them.  Everything that tells a
    human from an AI knight reads `ulKind` (turns, ai_map, combat, overworld), so the turn slots stay and the knight plays its turns from the RNG,
    identically on every machine.  The **game-over rule** compared the skipped turns with the number of human players (LAB_05C5): `loadTurn` now
    passes `humanKnightCount` (the human knights among the first LAB_05C5 records; the original value when there are none), so a game with
    dropped knights ends when every *remaining* human is out.  The binding becomes `NET_NODE_DROPPED` (steers nothing, like NONE) with one
    difference: the **place / loot screen of a dropped knight that was open at the drop** is closed with the host's joystick 1 and keyboard
    (an AI knight never has one, so the merge would otherwise wait forever for input nobody gives).
  * Co-op: the members stay members; each member of the node passes to the taker (the host, node 0) on the first selectable controller none
    of the taker's other members uses (`netBindingsDrop` on the CONFIG table).  Since the net pads merge (3.2) the live binding is
    `g_party.aubNode[m]` / `aubPad[m]` (what the merge and the state hash read), so the same drop runs `partyNetDrop` on it, at the same tick on
    every survivor, in (tick, node) and member order: the member's `aubNode` becomes the taker and its `aubPad` the taker's first selectable
    controller that no other member AT THAT NODE holds (per-node uniqueness; the chosen Keys WASD of a dropped player is not kept).  A member
    that has not picked a controller yet (knight screen) only changes node and stays `PAD_NONE`: the taker's player makes the choice, so the
    screen never waits for a node that is gone; an open Control line whose offered pad the new node already uses is re-offered with the next
    free one (`SceneKnights::padChoice`).  The slot of the merged pad frame (member index) stays.
  * **Playing on alone** (`netPlayAlone`): queued drops are applied with this node as the taker, then every other node of the session (same
    rebinding, so every member ends on this node with a distinct controller); the session's hook stops merging and `partyLocalize` makes the
    party local again: members without a pick get their default / the first free controller, the knight screen's member focus returns to ANY.
    A party that was never bound to the nodes (CONFIG table only) takes the table's pads (`netLeavePads`).  A desync / protocol error does
    none of this.
* **Known limits.** A fight that is running when its fighter's node drops keeps the fighter's controller word at 0 (it stands still) until the
  fight is over; the fight starts and ends are the only places the original re-reads who is human.  The 12.8b `net_pair` scripts need no change
  (the waiting box may show in a window's screenshot taken during a peer's frozen shot).
* **Not in v1**: late join and resync (both need a full-state snapshot serializer; shares work with a save-game feature).

---

### 4.2b The knight screen is the lobby (owner 2026-10-10, replaces the shared title menu and the separate lobby page; built 2026-10-10)

* **Flow: HOST -> lobby -> START** (owner 2026-10-10). HOST GAME on the Multiplayer screen (where PUBLIC already is) listens and goes straight into
  the lobby; there is no host page and no START on the Multiplayer screen. A joining player's CONNECT lands in the same lobby as soon as the host has
  seated it (the host's first LOBBY_STATE; until then the join page says CONNECTED). Esc on the portraits leaves the lobby (the link closes, the seat is
  free) and returns to the Multiplayer screen.
* **The lobby is the original Select a Knight screen** (`game/scene_lobby`, `SceneLobby`): the four portraits (64 x 76 at y 80), the title, a status line
  **PLAYERS n OF 4** (the font has no `/`, so "n/4" reads "n OF 4") and, on every machine, the mode line **MODE  COOP** (default) / **MODE  CLASSIC**
  (the font has no `-`). Only the host moves up to the mode line (left / right / fire switch it); the old title-menu Mode / Players choice is not used.
  The player count of the game is the number of players: knights = players (extra local knights on the host's machine may come later).
* **Selectors and picks** (owner 2026-10-11: no colours). A player sees only his OWN knight marked: the knight screen's highlight frame (sprite 1)
  follows his selector while he browses and stays on his knight after the pick (name entry, controller line, waiting for START). Other players'
  selectors are invisible and browsing selectors may overlap. A knight someone has **picked** (fire; claimed or done) shows its player's name in the
  top-left corner of the portrait (small font on a dark box in the screen's black, 2 pixels inside the portrait, cut so that box and text fit the
  64 pixel width: the width is measured with the small font at run time, `rt/lobby_draw` `lobbySmallWidth`). Picks are exclusive: the selector jumps
  over the knights others picked, a pick of a picked knight is refused, and when someone picks the knight you browse your selector moves on to the next
  unpicked one (done in the room, so every machine agrees). A joiner starts on the lowest knight nobody stands on (host 0, then 1, 2, 3; a joiner after
  someone left fills the freed one; when all four are browsed, the lowest unpicked). Then the **name entry** (pre-filled with the player name of the
  Multiplayer screen, 13 characters at most like the original editor, keyboard of that machine only, Return or fire finishes), then in co-op the
  **controller line** "Control  Joystick 1 / Joystick 2 / Keys arrows / Keys WASD" (left / right, fire takes it; any player may take any controller,
  every machine has its own). Esc during the name or the controller, or after the pick, lets go of the knight.
  The loop redraws after every change (~20 frames with the repeat wait) and reads the stick between redraws: a script (autoplay) must hold a press for
  30 frames and space the steps by 60 or more.
* **Before START the screen is not in lockstep**: picks travel as lobby messages. `LOBBY_PICK` (client -> host: browse = the selector stands here / let
  go, claim = fire on a portrait, done = name + controller) and `LOBBY_STATE` (host -> every client after every change, answering every pick, including a
  claim that lost: the authoritative room, with each client's own seat). The host settles races for a portrait in the order it reads the links (its own
  press is applied at once): the first claim holds the portrait, the other player is put back to browsing on the next free portrait and sees it in the
  next LOBBY_STATE (the client only calls its claim lost when the room shows someone else holding the portrait, so a stale state is no rejection).
  A player who leaves (BYE, closed or broken link) frees the seat and the portrait; the host's lobby goes on (it used to end the host's session).
  Pure code: `engine/netroom` (seats, picks, races, START rule, the wire form), `engine/netnode` (the room inside the node: seating at HELLO, picks,
  broadcasts, leaving), `game/scene_lobby` (the screen). Host tests: `tests/test_lobby_screen.py` (room + screen with mutations),
  `tests/netsession_driver.cpp` namespace `nlobby` (2 to 4 real nodes over byte pipes: join, mirror, races both ways, a player leaving by BYE and by a dead
  link, START rules, the mode line, CONFIG -> identical knight records on every node).
* **START button** (host only): below the portraits (y 172, the same line as the name entry and the controller line, which replace it while the host
  is choosing): the host moves down to it (the menu arrow) and presses fire. It is drawn greyed out (recoloured to palette entry 3) until at least one
  other player has joined and everyone has picked a knight (`roomStartEnabled`, checked again by the transport). Until a player (the host included) has picked a knight, the line reads **SELECT YOUR KNIGHT** (the host sees START once it moves down to the START row); joined players who have picked see **WAITING FOR HOST**
  in its place. Fire on START sends **CONFIG** (mode, portraits, names, controllers, seed): the nodes are the joined seats in seat order (a hole where a
  player left is closed), record i is played at node i. `LOBBY_*` pokes: `poke netaction 3` presses START (autoplay).
* **Every machine builds the same knight records from CONFIG** (`game/net_knights`, `netKnightsApply`; called by `rtSceneKnightsRun` in place of the screen):
  `Knight::ulKind` = the portrait, `ubInputPort` = Joy1, the name buffer of the portrait = the picked name (NUL, then spaces, so the 22 bytes agree), the
  player count (`LAB_05C5`), the damage divisor and the published count as after the title menu; co-op: `partyStart` + `partyNetBind` (member i at node i)
  and `aubPad[i]` = the controller its player picked, focus on the current knight's turn; classic: no party, every knight on Joystick 1 of its own machine.
  The RNG is written with the CONFIG's seed at `netSessionBegin` (the game starts at once: `resetGame` draws the lair rolls before any tick).
  The main flow goes `Title -> Campaign`; the title menu's exit and the knight screen are skipped, nothing waits for a title menu in lockstep.
  **Stats, hp and the rest of a record are not the knight screen's**: class stats (`str 1, con 1, end 1`, hp 14 ...) come from `resetGame` / `startKnights` /
  `rtKnightDefaults` (LAB_01AE / LAB_01BE) on every machine, as in the single-player game; the screen only sets `+54` kind, `+11` port and the name.
  `netKnightsApply` writes exactly those (and the party), so it cannot depend on what a machine held before; host test `nlobby::testConfigIgnoresPriorState`
  (zeroed / resetGame / leftover priors give the same kinds, ports, 22 name bytes and party, nothing else of a record is written, and a classic result equals the
  real `SceneKnights` driven by the same picks).  A `net: dump` that shows `str=9 con=1E` on one node and `str=1` on the other at the same tick is the
  net_pair script's `poke strength ...` landing at different ticks, not the knight build (run 94/95 on 2026-10-11: the host's fallback START poke 200 frames
  behind the fire made the host's first `sync` come after tick 100, so its barriers ran one hash ahead of the join window's).
* **Removed / dead after this change:** `NetSeam::pfnStart` and `netSessionStart` (START on the Multiplayer screen), the Multiplayer screen's lobby page,
  `NETROW_START`, `NET_START_HOST / CLIENT`, `netNodeStart(mode, seed)` (START now reads the room), the CONFIG with default bindings only, the first-tick
  `partyNetBind` of `netSnapshotCapture` and the seed-at-tick-0 of the session (see above). Kept, not reachable in a normal network game: the title menu runs in
  lockstep only after a game over inside a session (`NetMenuScope`), and `SceneKnights`' network branches / `NET_SCREEN_PADS` only for a knight screen opened inside a
  running session; they are left alone because the pad routing of fights and keyboard input is being changed in parallel (`party.cpp`).
* Gore and mods are the host's settings (a mismatch is already refused at HELLO). No title menu and no knight screen run in a network game.
* **What to check on screen** (shots: `netpair-h-lobby`, `-name`, `-control`, `-picked`): the highlight frame on the own knight only, the picked
  knights' names in the top-left corner on a dark box, the greyed START (recolour of the rectangle x 100, y 172, 120 x 16), the PLAYERS / MODE lines
  (y 24 / 42), the listing state in the small font at the bottom left (y 190), the arrow on the MODE / START row.

### 4.3 First light as built (ROADMAP 12.8b, 2026-10-10)

Two WinUAE windows play one co-op game.  Code: `engine/netnode` (session state machine, pure), `engine/netglue` (local frame <-> network
frame <-> raw key codes), `game/net_state` (the merge's view of the game), `game/net_hash` (state walk), `rt/net` (sockets and windows),
`rt/net_session` (the glue), `rt/net_state` (reads the live cells); `tools/netpair.py` runs the two windows.

* **Start.** The command line (`HOST`, `JOIN <addr>`, `PORT`, `NAME`, `DELAY`, `TIMEOUT`) only pre-fills the Multiplayer screen; the session is
  started from that screen (Host game / Join game).  Host: listen and into the lobby; client: connect and send HELLO (build fingerprint = size + sparse
  CRC of `PROGDIR:moonstone`, data checksum = CRC of `g_gameData`, version, rule options).  A mismatch is refused with BYE (build / refused) and the
  host stays in the lobby.  An accepted player is seated (LOBBY_STATE) and picks its knight in the lobby (4.2b).  START (host, lobby) sends every accepted
  player its CONFIG (mode, nodes, your node, delay, first seed, bindings: record i on node i with the controller its player chose in co-op / joystick 1
  in classic, names, portraits) and closes the listen socket; the client answers READY and its lobby leaves; both call `netSessionBegin`.
* **Begin.** All nodes clear the key tables, install the frame hook and the seed hook, write the CONFIG's seed into the RNG and switch the
  deterministic mode on with the logical clock 0 (`netDeterministicBegin`), right after the lobby.  The first lockstep screen is the map set-up: the
  knights come from the CONFIG (4.2b), there is no title menu and no knight screen.  The mode (classic / co-op) is in the CONFIG and chosen by the host in
  the lobby.
* **Tick.** `netTick` -> frame hook, ONE lean OS window per tick: local frame -> `NetFrame` (pads incl. the keyboard sets from a shadow key table,
  up to 4 key events, mouse delta) -> INPUT to the host (the host files its own, relays a complete tick as TICK) -> wait in the window
  (WaitSelect 2 ms slices; a peer silent for TIMEOUT s ends the session) -> `netMerge` with `NetGameState` from the real game
  (`game/net_state`: menu / map / fight, current record, fight ports, bindings, focus) -> the merged joystick words, key events (turned back
  into raw codes) and merged pad frame replace the local ones.  `netFrame()` in `displayWaitBeamRaw` is gone: no window outside a tick.
* **Seeds.** Later seeding events (`rtMogRngSeed`): the host draws as the original does and sends SEED(tick, value); the client waits for it.
* **Hash.** Every 50 ticks (before the tick runs) both nodes hash the same fields (`game/net_hash`: five knight records and the active
  creature records with pointers normalised to array tag + offset, inventories, names, 24 lairs and their loot, turn / day / moon / fight
  scalars, party, RNG state, tick, clock), send HASH, compare; a mismatch ends the session with BYE(desync) and `net: desync at tick N` plus the
  field dump (`net: dump <group>: name=hex ...`) of that tick on each node.  Log lines: `net: hash tick N crc XXXXXXXX`.
* **Ends.** BYE, a closed link, a silent peer, a desync: `net: session ended: ...`, sockets closed, the hook stops merging, the game goes on with
  the local input (deterministic mode stays on).  Since 12.11 (section 4.2a) a client's loss no longer ends the host's session, and a node that
  plays on alone has the other knights dropped (AI knight, host takes the member).
* **Co-op binding.** Member i sits at node i (CONFIG; knights = players); its owner picks the controller in the lobby (4.2b), which the CONFIG carries
  and `netKnightsApply` writes into `g_party.aubNode` / `aubPad` (merged slot = member index).  The live binding is `g_party.aubNode[m]` / `aubPad[m]`; a
  co-op drop rewrites it (section 4.2a).
* **Four windows and the classic pair (2026-10-11).** `py tools/netpair.py --quad` (= `--players 4`) runs the host (instance 94) and three joiners (95 / 96 / 97, names
  GUEST1..3, each its own `hd-<instance>`) as one job under the emulator lock; each next joiner starts when the host log shows the previous one seated (`net: peers K`),
  so the seats and the portraits 0..3 come in window order.  Scripts `net_quad_host.txt` + `net_quad_join1..3.txt` (generated by `mknetpair.py`): everybody keeps
  Joystick 1 of its own machine; four map rounds with the leader rotating 0..3 (`wait var leader`); every window pushes its stick right in every round, only the
  leader's machine moves the token and ends the turn (E); shots `netpair-q<k>-*`.  PASS = every window quit and every tick hashed by all four windows has one crc.
  `py tools/netpair.py --classic`: the host switches the lobby's MODE row to CLASSIC (up from the portraits, fire; log `net: lobby mode classic`), both pick a knight
  (no controller line in classic), round 1 knight 0 then knight 1 walk and end their turns (each only with its own machine; the other stick is pushed too and ignored),
  "Next Day"; round 2 the host walks, the guest's knight is put in a lair and fights it with its own stick (`net_classic_*.txt`, shots `netpair-ch-*` / `netpair-cj-*`).
  Found while writing it: in a classic game every human knight starts on Joystick 1 and a map turn only rewrites the port of the knight whose turn it is, so
  `netStateFill` took the LOWEST record on port 1 as the fight's first stick: the second knight's lair fight was steered by the host's machine.  It now takes the
  fighters named by the fight loop's own cells (`ActiveKnights::ulCurrent / ulOpponent`), then the knight of the turn, then the old scan (`NetSnapshot::ulFightFirst /
  ulFightSecond`, host test `netsession_driver` `testState`).

### 4.4 The relay and the game browser as built (ROADMAP 12.13a game side + 12.18, 2026-10-11)

The server side of both (lobby, REGISTER_REPLY direct / relay, RELAY_HELLO, the pipes) existed since 12.17; this is the game's side.  **No wire change, no
protocol bump** (`NET_PROTO_VERSION` stays 4): a deployed server of the current `tools/netserver/` needs no redeploy for it.

* **Host listed via relay** (`engine/netrelay`, pure, host-tested; `rt/net_relay` gives it the transport).  When the lobby answers REGISTER with "relay"
  (`NLS_LISTED_RELAY`, the session id is `NetLobby::ulSession`, `rt/net_lobby lobbyRelaySession()`), the host keeps **one idle connection** to the lobby
  server: connect (the address `lobbyBegin` resolved), `RELAY_HELLO(session, host)`, then wait; a PING every 60 s keeps it open (the server drops a quiet idle
  connection after 600 s) and its PONGs are swallowed.  The server tells the host nothing when it pairs a client: the client's first message (HELLO) simply
  arrives on that connection.  So the moment anything but a PONG shows up (a BYE of version 0 is the server refusing the session: retried after 30 s), the
  machine **hands the connection over**: `NetTransport::relayHandOver` moves the socket into the first free game link slot (`m_aConn`) and puts the bytes
  already read in front of that link's stream (`NetPushback`, served first by `NetTransport::recv`); `step()` then does `netNodeLinkUp` for that link and the
  node cannot tell the pipe from an accepted TCP connection (same HELLO, lobby, lockstep messages).  A fresh idle connection opens 200 ms later for the next
  client.  Up to three clients, any mix of direct (the listen socket stays open) and relayed: the three links are shared; a fourth finds no free link and its
  pipe is closed.  A lost connection is retried after 3 s, a new relay session (the listing was re-registered: a MODE change is UNREGISTER + REGISTER with a
  new session) moves the idle connection to the new session, a direct listing (`session 0`) never starts the machine.  START closes the idle connection
  (`relayHostEnd`; the pipes of the players already in stay).  Every call happens inside the OS windows the session opens anyway (`step()`); the machine
  is due at most every 100 ms and costs a clock read otherwise.  Log lines: `net: relay state N session ... pairs ... fails ...` (state 4 = idle connection up),
  `net: relay client arrived on link N`.
* **Lobby keepalive** (`engine/netnode`, `NN_LOBBY_KEEPALIVE_MS` = 30 s): in the lobby every node PINGs its peers when nothing else was sent, because the
  server closes a relay pipe that carried nothing for 120 s (a NAT forgets a quiet direct connection as well).
* **FIND GAME** (`game/net_find`, pure, `tests/test_net_find.py`; `engine/netbrowse`, the lobby client; `rt/net_find` the transport).  A row of the
  Multiplayer screen between JOIN GAME and NAME (`NETROW_FIND`, `poke netaction 4`) opens a page on the title picture: LIST_REQ to the lobby (net.ini
  `lobby` / `lobby_port`, else the built-in server), one row per open game of this build in the small font - name (capitals), COOP / CLASSIC, "n OF 4",
  OPEN / FULL (the server lists only open games, so FULL and the dimmed rows are for a server that lists more) - then REFRESH and BACK.  Up / down move the
  cursor (it starts on the first game once the list is in), fire on a game joins it, fire on REFRESH asks again, BACK / Esc go back to the Multiplayer
  screen with the cursor on FIND GAME.  A game of another build or game version is dimmed (`pfnRecolour`, palette entry 3: not checked on screen) and
  answers OTHER VERSION with "HOST RUNS 1.2 YOU HAVE 1.3" without contacting anything.  Status lines (small font, centred, two lines): CONTACTING LOBBY, NO TCP
  IP STACK, SERVER NOT FOUND, SERVER NOT ANSWERING, NO OPEN GAMES FOUND, GAME FULL, GAME NOT FOUND, OTHER VERSION, CONNECTING, WAITING FOR HOST, NO ANSWER.
  One request = one fresh connection (`NetBrowse`: connect, send, read one reply, close; the list is kept while a refresh is under way, a failed request
  clears it).  **JOIN_REPLY**: *direct* - `netSessionJoinStart(a.b.c.d, port, name)`, the Multiplayer screen's own join path; *relay* -
  `netSessionJoinRelay(session, name)`: connect to the LOBBY server, send `RELAY_HELLO(session, client)` and then the usual HELLO (the server forwards from
  there); *not found / full / build mismatch* - the status line (the mismatch carries the host's version).  The connect then reports through the same
  `netSessionPoll` as a typed address and the page leaves for the lobby when the host has seated the player (`NET_LOBBY_CLIENT`).  A BYE from the server
  (version 0: the host's idle connection was missing) shows NO ANSWER, a BYE FULL from the host or the relay GAME FULL.  Log lines: `net: find screen open`,
  `net: find status N games M` (status 2 = list in), `net: join via relay ...`.
* **Tests.**  `tests/test_netrelay.py`: the relay machine, the pushback, `NetBrowse` and a host `NetNode` that seats relayed and direct players, over fake
  connections with mutations; and end to end against the real `tools/netserver` server (the connect-back always fails): a host listed via relay, two
  clients that FIND it, JOIN it through the relay, pick, START, 60 lockstep ticks with equal frames on every node; a direct listing keeps the relay out; a
  re-listing moves the idle connection; ten minutes of a quiet relayed lobby; a client refused when the idle connection is missing.
  `tests/test_net_find.py` (the list page, JOIN_REPLY handling, texts, the scene's drawing, mutations).  Two windows: `py tools/netpair.py --relay` starts a LOCAL
  lobby server (`py -m tools.netserver --no-connect-back`, new flag for tests) and runs `tests/boot/net_relay_host.txt` / `net_relay_join.txt`
  (generated by `tests/boot/src/mknetpair.py` like the direct pair): same session as the direct run, but the guest uses FIND GAME.

### 4.5 Rejoin as built (ROADMAP M12 rejoin = 12.15a, protocol 5, 2026-10-11)

A dropped player can come back at any time - also after the host chose CONTINUE WITHOUT and plays on alone - and **enters at the next map turn,
never in the middle of a fight**.  The host freezes the lockstep at the turn, sends the world, both sides compare a hash of it after the load and then the
lockstep goes on with the player back in its seat.  Rejoin is the mirror image of a drop: the drop makes the knight a computer knight from tick Tb on
(4.2a); the rejoin gives it back to its player from tick Tu = Tb + D on, with empty input frames for the node in between (`netUndropNode`).

* **Tokens.**  At START every node gets a random 32 bit token in CONFIG (`NetConfig.ulToken`, new field; from the host's entropy - the lobby socket
  clock and the host's pad / RNG seed source, `netNodeStart(.., ulEntropy)` - never from the game RNG, so it cannot be predicted from the game).  The client
  remembers `(how it got there: address + port or relay session, its node, its token, the host's name)` in `rt/net_rejoin` `RejoinMemory`
  when the lockstep begins; the memory outlives the session that ended with the drop.  A reconnect must show node + token: the right pair reclaims
  exactly that seat.  The host keeps `aulToken[]` and `rt/net_rejoin` the bindings / portraits of the START (`cfgStart`).
* **Messages (protocol 5).**  `REJOIN` (14: build, data hash, version, rules, node, token) answered by `REJOIN_OK` (15: node) or a `BYE` with reason 8 no
  seat / 9 bad token / 10 rejoin failed (+ the existing build / version reasons); `STATE_BEGIN` (16: tick, clock, state hash, CRC, byte count, chunk count,
  mask of the live nodes), `STATE_CHUNK` (17: index, length, up to 384 bytes), `STATE_END` (18: chunk count, CRC), `RESUME` (19: tick, my hash of the
  loaded world), `BOUNDARY` (20: tick, mask of the nodes that enter, hash).  CONFIG +u32 token and a binding node `0xFE` (= dropped) is valid; JOIN_REQ
  +u8 flags (bit 0: "I hold the token of this game").  The relay passes types 1..20.  `NET_SNAP_MAX` = 6144 bytes of world, `NET_CHUNK_BYTES` = 384.
  The server (`tools/netserver`) answers a JOIN_REQ for a **Playing** game with the rejoin flag with the direct address or the relay session (without the
  flag still "full"); the host checks the token itself.  **The public server must be redeployed** for protocol 5 (version byte in every header).
* **State machines (pure, `engine/netrejoin`, `engine/netsnap`, host-tested).**  `NetRejoinHost` owns the links of people who ask (state per link: new,
  waiting for the turn, sending, loaded, ready) and is independent of the lockstep `NetNode`; `NetRejoinClient` is the rejoiner's side (REJOIN sent ->
  waiting -> receiving -> loaded -> confirmed -> done / failed with a reason).  The host's `NetNode` has the lockstep part: `netNodeRejoinableMask`
  (dropped seats whose drop tick is already behind us), `netNodeApplyRejoin` / `netNodeAdoptRejoiner` / `netNodeResumeRejoiner`, `netNodeReopenSolo` (a host
  that ended with NE_ALONE restarts the session at the tick of the turn with only itself alive).  The tick ring, TICK relay and DROP stay as they were.
* **The hand-over of the link (what the pumps read, and who reads on).**  Both rejoin pumps read at most 16 chunks of 256 bytes per call (`ulCapped` counts it; the
  rt logs `gave up reading a link after the round cap` once - the 3.8 KB world plus its messages is more than one call reads, so the line is normal on every
  rejoin).  Nothing is lost at the cap: the next call reads on, and when the rejoiner's pump meets the host's RESUME it stops (`RC_DONE`) with the complete
  messages behind it in its framer and the rest of that read in `aubTail`; `netNodeResumeRejoiner` replays framer and tail into the node, and the node's own pump
  reads the socket from there.  The host's side is the same: `netNodeAdoptRejoiner` takes the rejoin framer of the link.  The host test "rejoin burst hand-over"
  (`tests/netsession_driver.cpp`) puts the world, the RESUME, 16 ticks and pings on the wire in one burst, with the rejoiner reading nothing meanwhile.
  **A stall of several seconds right after a rejoin in a two-window run is the other window's `shot`** (8 s freeze, docs/AUTOPLAY.md): its peer shows WAITING FOR and
  `net: waiting tick N ... (waited 7000 ms)` and the logs of the two windows look as if the frozen one ran ahead (its vbl clock stood still).
* **Where the rejoin happens: the map turn.**  The turn scene (`game/scenes/turn_end`) calls `turnBoundary()` (a hook, installed only in a network game)
  after the turn is over and before the map re-enters.  `rt/net_rejoin netTurnBoundary` then does: the host (`hostTurn`) - if a seat is away and a request
  waits, make sure no fight is running (`netWorldFightIdle`, `ubFightActive`; else the rejoin waits another turn, log `a fight is running at the turn`; creature slots left active by earlier fights do NOT block it, they travel with the world), normalise the
  state the way the map start does (jobs reset, colour slots freed without a fade, the dragon inactive - on the image **and** live, so the host and the
  rejoiner start the map from the same state), capture, encode, send `STATE_BEGIN/CHUNK/END` to the asking nodes, wait for their `RESUME` (hash equal)
  - 25 s, then the rejoin is dropped and the host plays on without it (the others got `BYE` rejoin failed) - and send `BOUNDARY` to the survivors.
  The surviving clients (`survivorTurn`) **wait at every map turn while some node is dropped** (that is deterministic on every node: `netNodeDroppedMask`)
  for the host's BOUNDARY (tick, mask, hash) and apply the same effects on the same tick; a wait with nothing to do is a few frames.
* **The world (`game/net_world`, `rt/net_world`).**  The audit of what the deterministic game needs beyond the 50 tick state hash (`game/net_hash`
  `NetStateImage`: knights, inventories, names, lairs, turn / day / moon, RNG, party): **the extras** - market stock (6 longs), wizard gift cycles and
  bad-luck flag, the dragon (active, timer, speeds, last id / x, touched records), the terrain counter and blocked flag, the map modes (forced, ambush,
  saved position, locked), the fight budget / latches, AI goal and walk cells, roam-built, humans-saved, the spawn / flyer / bat / hop counters, the
  wave level, `ActiveKnights::ulCurrent`, the **map AI's turn inputs** (`NW_VERSION` 3, found in the first two-window rejoin run: `net: desync at tick 3000`, a
  black knight one tile off: `mapScreenEnter` runs `MS_MOVE` with LAB_0656 `mogAiMove`, the move word the previous turn's last frame left behind, so the
  next knight takes one step with a stale word - a rejoiner holding 0 ends a pixel off, a tile on a tile edge; carried now with the AI walk cells 1..6,
  the roam lair / hidden flag, the shop wish, the step gate, the town distances and the opponent scratch), the black-knight pool (records 5..8 and their inventories) and the co-op `PartyMap`.  The image
  (`NetWorldImage`) is written by one walk of templates for the encoder, the decoder and the hasher (so they cannot drift apart).  Pointers in records
  become `(tag << 24) | offset` (the arrays, the pool, the name targets, the owned data regions of `g_imageMog`), `0xEE000000` for "other"; pointers into
  owned data (scripts, tables) are not hashed (`dptr`) and an unknown pointer keeps the receiver's own.  **The creature pool is sent** (`NW_VERSION` 2):
  the original frees slots only through the job kill / `fightDespawn`, so leftovers stay active on the map (found in the first two-window run: the
  gate "no creature active" never opened) and the 50 tick hash walks the active ones.  Form: a count, then (slot index, Knight record) for each ACTIVE
  slot, pointers normalised like the knights' (tag + offset into `mogCreatureHeap` etc.); the decoder frees every slot of the receiver first and
  `netWorldApply` copies the pool back, so both machines hold the same creatures at the same slots.  Size: the whole image is 5556 bytes with all 20
  slots active (limit `NET_SNAP_MAX` 6144), less when fewer are.  The hash of the image (`netWorldHash`) is compared once, after the load; the 50 tick hash stays net_hash's.
* **The knight goes back to its player.**  `game/net_rejoin netRejoinApply`: classic - the knight's kind is the portrait again (it was `KIND_AI`) and the
  binding is the original one of the START; co-op - `partyNetRejoin` binds the party member to the node and pad again; the drop queue of that node is
  forgotten (`netDropQueueForget`).  The knight's hp, gold, items and position are the host's (the world).  The rejoiner's local game is thrown away: the
  title menu path builds the knights from the first CONFIG of the host (`netSessionBeginRejoin`), the world is decoded over them
  (`netRejoinApplyStep`, after the campaign set-up) and its hash is compared with the host's before RESUME is sent; a mismatch -> `BYE` (rejoin failed),
  the rejoiner plays on alone and the host continues without it.
* **The host after the last client is gone.**  The host's `NetNode` ends with NE_ALONE as before, but `rejoinHostEnterSolo` keeps the lobby listing (the
  public list shows the game as **Playing** as long as a seat can be rejoined; REFRESH goes on), keeps the listen socket and the relay idle connection,
  and closes only the player links (the listen socket is replaced with `NetTransport::listenAgain`, which touches nothing else: the old `hostListen` reset the lobby
  connection too, the listing was re-registered with a NEW relay session and the dropped player's remembered session found no host, batch6 `--relay`);
  `rejoinHostIdle` (from the frame hook while not in lockstep) serves them at most every 100 ms.  At the next map
  turn `netNodeReopenSolo` restarts the session at that tick with the host alone and the rejoiner enters.  Several rejoins in one game, two players at once,
  and a **second drop of the same player** work (the token stays; the seat is rejoinable again).
* **What the player sees.**  Host: a non-blocking note over the map ("<NAME> REJOINS" / "AT THE NEXT TURN" - no '/', '-' or ':' in the game font;
  `rt/net_box` `netNoteShow`, drawn by the display flip hook, ~3 s), later "<NAME> IS BACK".  Multiplayer screen of the dropped machine: a **REJOIN GAME**
  row under FIND GAME while a game is remembered (`netSessionRejoinAvailable`), then the status lines ASKING THE HOST / WAITING FOR THE NEXT TURN, or the
  refusal reason (NO SEAT TO REJOIN, REJOIN REFUSED - bad token, OTHER VERSION, HOST GONE, REJOIN FAILED).  `poke netaction 5` presses the row.  FIND GAME
  does not show a Playing game (the server lists only games that can be joined): see "limits".
* **Limits and follow-ups.**  (1) If the **host** drops, the clients continue alone as before; there is no rejoin to a client (it would need a host
  migration: the client does not have the other nodes' tokens or the listing).  (2) FIND GAME has no entry for the remembered game yet; the JOIN_REQ
  rejoin flag and the server answer exist (`selftest.py` and `test_netserver.py` pin them), the screen row is the Multiplayer screen's REJOIN GAME.
  (3) The world is taken at the map turn only; a player who asks during a fight waits for the next turn.  (4) `NET_SNAP_MAX` is 6144 bytes with all four
  knights; a world larger than that refuses the rejoin (log `the world does not fit`).  (5) The emulator run of the whole flow is the integrator's
  (`tests/boot/net_*_*.txt` have the rejoin step at the end; the engine side is covered by the host tests below).
* **Tests.**  `tests/test_netsession.py` (driver `tests/netsession_driver.cpp`, section `nrejoin`): real `NetNode`s over byte pipes - drop, host
  CONTINUE WITHOUT, rejoin with equal per-tick hashes for 1..3 clients, classic and co-op; wrong token refused with the reason; a request during a fight waits for
  the next turn; the same player dropped and rejoining twice; two players at once; one after the other; the host alone (twice); the host leaving; the host /
  client machines of `engine/netrejoin` on their own; plus ~20 mutations (a snapshot not taken, token not checked, the boundary not applied, ...) behind the
  mutation gate.  `tests/test_networld.py` (`tests/networld_driver.cpp`): the codec, the hash, the chunk receiver and the rejoin effects, with a mutation test.
  `tests/test_netproto.py` / `test_netserver.py` / `tools/netserver/selftest.py`: protocol 5, the relay range, the join of a Playing game.
  `tests/test_net_menu.py`: the REJOIN row.  `tests/boot/src/mknetpair.py` adds the rejoin step to the two-window runs (`py tools/netpair.py` and `--relay`).

### 4.6 The ending of a network game (ROADMAP 12.19, 2026-10-11)

**Decision.** The quest is won inside the lockstep (Stonehenge, the stone owner among the party, the leader's fire: every node reaches `pvRunProgram`
at the same tick). The ending itself is the program overlay: classic code outside the lockstep (`netDeterministic()` is false there, no `netTick`, no frames
exchanged, no hash), so **both machines play the ending on their own timers** (about two minutes; the pages that wait for fire take each player's own
fire) and the session **ends with the ending**, in two steps:

1. `netSessionQuestComplete` (`rt/net_session.cpp`, called by `pvRunProgram` just before the overlay switch; log `net: the quest is complete`): the host's
   lobby listing is removed at once (`net: lobby: unlisted`; a host that plays on alone as "Playing" for a dropped player closes its listener too), and
   `rejoinHostEnd` - nobody can rejoin a finished game. Nothing is sent to the peer: a node may still be a few ticks behind the other (up to DELAY), and a BYE now
   would make it play the last ticks alone and miss the ending.
2. `netSessionCloseAfterEnding` (`opEndingRun` in `rt/progmain.cpp`, right after the ending's last page, before the program overlay starts mog again; log
   `net: session closed after the ending`): BYE(normal) to the peer, the sockets close, the lockstep flag and the deterministic mode go off, the rejoin
   memory of a client is forgotten (the REJOIN GAME row must not offer a finished game), the party reads its real controllers again.

Before this change the lockstep stayed marked as running through the ending: nobody served the sockets for two minutes (the lobby dropped the listing after
its silence timeout), the game came back in mog with the lockstep "running" and no frames merged, and the main menu was the *replicated post-game* menu
(`rtSceneMenuRun` `bBack = !netSessionRunning()`: no LOCAL / MULTIPLAYER / SETTINGS), with the next tick waiting for a peer that was in its own ending. Now the game
returns to the ordinary main menu on both machines (LOCAL / MULTIPLAYER / SETTINGS) and a new game can be hosted or joined at once. A machine that finishes its
ending first only closes its side: the peer's BYE sits unread until its own close, which then finds a closed socket and goes on. A peer that was dropped earlier
(`isLockstep` false) does nothing at the ending. The BYE(normal) semantics the close relies on (the closer ends NE_CLOSED, the host of a client that quits drops
it and goes on) are pinned by `tests/test_netsession.py`; the two calls themselves need the transport and are the emulator run `py tools/netpair.py --ending` (`tests/boot/net_end_host.txt` /
`net_end_join.txt`, generated by `tests/boot/src/mknetpair.py`): the lobby flow of 4.2b, two map rounds, then **tick-pokes** (docs/AUTOPLAY.md "Tick-aligned
pokes") that set what `play_end` sets (`moonstones 15` on both knights, `warp_node 7` on the leader) at hash tick + 10 / + 12 on both windows, one more hash
bar that covers the poked state (equal hashes), the leader's fire (the map enters Stonehenge; the place loads, so held keys are released, and shows "You have completed the quest"), a second fire of any pad (the "press fire" waits take every controller; without it the run sits on that text), the ending on both windows (shots `netend-h-*` / `netend-j-*`), the closed session and the
main menu. `netpair.py --ending` passes when both windows have the equal hashes up to the quest, equal tick-pokes, no hash after the quest, both close lines, the
main menu (`rtGameRun: enter mog` after the close) and the host's `net: lobby: unlisted`.

**Colours of the other non-fight screens (2026-10-11):** the dice house, the ritual and the wizard draw THIS machine's knight's class ramp too (`netSessionLocalKind`, `game/local_knight.hpp`; palette only, nothing in the state hash or the timing); fights and screens that name a knight keep the real colours (MOONSTONE2.md 2.6 table).

**Colours of the ending (2026-10-11):** each machine's ending wears ITS OWN knight (the stone owner when it sits at this node, else the first record bound to it): `pvRunProgram` rewrites the kind bits of the boot flags with `endingFlagsForKind` (`game/ending_knight.hpp`) before the overlay switch; `netSessionEndingKnight` gives the record. Details in MOONSTONE2.md 2.6.

### 4.7 The link line: ping and packet loss on screen (owner request 2026-10-12)

One short line in the bottom rows of the screen during a network game, in the small menu font: `PING 38MS  LOSS 2 PCT  F10` (UDP carries the ticks) or `PING 38MS  TCP` (a link is not
on UDP: no datagrams, fell back, or still probing).  The unit is spelled `PCT`: the small font has no glyph for `%` (nor `:` `/` `-`; `engine/netstats` formats with upper case letters,
digits and spaces only, and the test checks it).  The first second shows nothing (no round trip yet).

* **Ping** is the round trip of the keepalive PING / PONG.  The PING already carries a u32 token and the PONG echoes it, so there is **no protocol change** (protocol stays 6, no server
  redeploy): `pingPeers` (engine/netnode) records, per link, the clock at which token N went out; the PONG of N gives a sample (`netStatsPong`), smoothed with an EWMA of weight 1/4 (the first
  sample is taken whole).  A PING to a link replaces the one in flight (a late PONG of the old token is ignored).  What changed in the pump: the once-a-second PING that a *stalled* node already
  sent (`NN_KEEPALIVE_MS`) now goes out whenever the session runs (eight bytes a second each way per link); nothing else, in particular no extra lean window (it rides the pump's flush).  The
  sample includes the peer's pump latency (one game frame): it is the delay the lockstep really sees, not an ICMP echo.
* **Loss** is counted per link from the datagrams of 2.2a: every tick that a datagram brings for the first time is FRESH (it was the newest tick of its datagram: nothing before it was missing)
  or COVERED (new, but behind a newer tick of the same datagram: an earlier datagram was lost or late, the history repaired it).  loss = covered / (fresh + covered) over the last five one-second
  buckets (`netStatsTickIn`, `netStatsLossPct`), rounded, 0 while fewer than 25 ticks are in the window.  A tick that never comes by datagram (a stalled link: TCP carries it) is not counted
  there; that shows in the ping and in the WAITING box.  A client counts one frame per tick (not one per node).
* **Which link**: the host shows the worst of its clients (highest ping, highest loss, `TCP` as soon as one link is not on UDP), a client shows its link to the host (`netNodeStatsView`).
* **Drawing** (`rt/net_box`, the same display flip hook as the NOTE, 12.15): the session sets the text twice a second (`publishLinkLine`, from the frame hook, display only: the numbers are what the
  pump already holds, nothing reads them back, so the state hash, the RNG and the timing cannot change).  The hook draws the strip into the screen that is shown next and puts the picture back after
  the swap, exactly as the note; the strip is rendered once per text with the small font's printer and kept as a bitmap, so a flip with an unchanged line copies five longs per plane and row (and the
  two screens' original pixels are saved the same way).  Rows 192..199, x 0..159, colour 0 background: the screen has no border (320x200 exactly), so the line sits over the bottom-left corner of
  whatever the scene shows (the map's art and the fight's floor are plain picture there in the shots of `build/shots`; the town / place screens were not checked pixel by pixel: look at them in the next
  netpair batch and move `kStatY` / `kStatTextX` in rt/net_box.cpp if a screen has text there; the NOTE box and the WAITING box are elsewhere and coexist).  It appears wherever the game flips with the display's flip, i.e. every scene of a session; a text that is not refreshed for three seconds (the session ended, a scene that never
  runs the frame hook) is not drawn.
* **Toggle**: `F10` on the machine (rt/input swallows it while a lockstep session runs, so it never enters the key queue or the lockstep input; without a session it passes through as before), `net.ini`
  `[net] stats = no` (default yes), `poke netstats 1|0` for scripts (docs/AUTOPLAY.md).  The log shows the line as `PERFN <text>` after each PERF line (` (hidden)` when switched off).
* Tests: `tests/test_netstats.py` (+ `netstats_driver.cpp`: tokens, EWMA, window, worst link, TCP case, no session, the formatter and its character set; mutation tests) and the `nudp`/`stats`
  section of `tests/netsession_driver.cpp` (real nodes over the lossy fake datagram network: clean = 0 percent, 30 percent loss is seen on both ends, TCP-only says TCP, nothing before the first PONG).
  The picture itself is emulator work: `py tools/netpair.py` and look at the `netpair-h-map` / `netpair-j-map` shots (bottom-left corner), or `--tcp` for the `TCP` variant.

## 5. Code layout (follows AGENTS.md patterns: pure logic host-tested, hardware in `src/rt`, RAII guards)

| Unit | Contents | Tests |
|---|---|---|
| `include/engine/netplay.hpp`, `src/engine/netplay.cpp` | input frame layout, tick ring buffer, input delay, merge (state -> owner table -> merged `JoyBits` / `PadFrame` / keys) | host: merge tables for every row of section 3, a simulated 2- and 4-node run with random latency and drops |
| `include/engine/netproto.hpp`, `src/engine/netproto.cpp` | message encode/decode, versioning (protocol 4: LOBBY_PICK / LOBBY_STATE, portraits in CONFIG) | host: round trips, truncated / corrupt input, mutations |
| `include/engine/netrelay.hpp`, `src/engine/netrelay.cpp`, `src/rt/net_relay.cpp` | the host's idle relay connection (RELAY_HELLO, PING, hand-over of a paired client with its first bytes), the byte pushback, RELAY_HELLO bytes (4.4) | `tests/test_netrelay.py` (fake connections, mutations, the real server) |
| `include/engine/netbrowse.hpp`, `src/engine/netbrowse.cpp`, `src/rt/net_find.cpp` | the joining player's lobby client: LIST_REQ / JOIN_REQ, one connection per request (4.4) | `tests/test_netrelay.py`, `tests/test_net_find.py` |
| `include/game/net_find.hpp`, `src/game/net_find.cpp` | FIND GAME: the list page, JOIN_REPLY handling (direct / relay / refusals), the scene (4.4) | `tests/test_net_find.py` |
| `include/engine/netroom.hpp`, `src/engine/netroom.cpp` | the lobby room: seats, picks, races, START rule, LOBBY_STATE form (4.2b) | `tests/test_lobby_screen.py`, `tests/netsession_driver.cpp` |
| `include/game/scene_lobby.hpp`, `src/game/scene_lobby.cpp`, `src/rt/lobby_draw.cpp` | the lobby screen (selectors, picks, name, controller, START, mode line) and its bitplane primitives | `tests/test_lobby_screen.py` |
| `include/game/net_knights.hpp`, `src/game/net_knights.cpp` | CONFIG -> knight records, names, party (identical on every node) | `tests/netsession_driver.cpp` |
| `include/engine/statehash.hpp` | CRC over the game state with addresses normalised | host: same state at two base addresses gives one hash |
| `src/rt/net.cpp`, `src/rt/net.hpp` | bsdsocket wrapper: `SocketLib` (OpenLibrary/CloseLibrary), `Socket` (CloseSocket), `NetWindow` (systemUse/Unuse) guards; non-blocking TCP | WinUAE + `tools/netpeer.py` |
| `src/rt/input.cpp` | at the poll: deterministic-mode latch, hand the local frame to netplay, return the merged words | boot scripts |
| `tools/netpeer.py` | a host-side node (Python sockets) that speaks the protocol: replays a recorded input log, or acts as the host for one WinUAE instance | used by 12.12 |

The transport sits behind a small interface (`netOpen/netSend/netRecv/netClose`) so the PC build (M11) can supply Winsock /
BSD sockets later (12.14).

---

## 5a. Game browser (owner request 2026-10-10)

A screen that lists open games so a player can pick one instead of typing an address. Two lists, one screen:

* **LAN games, no server needed.** A host sends a small UDP broadcast (`ANNOUNCE`: game name, mode, players `n/max`, build
  hash, TCP port) once a second to the LAN; a browsing machine listens on that port and lists what it hears, dropping entries
  silent for 5 s. Works with every bsdsocket stack (`SO_BROADCAST`), [unknown] on WinUAE's bsdsocket emulation (test in 12.16).
* **Public internet games: a lobby server.** A small program on an always-on machine (Python first, `tools/netserver/`, section 5b; later
  also inside the PC build), the same one as the relay of 12.13a:
  * a host that ticks "public" **registers** (name, mode, players, build hash, port) over TCP and refreshes every 30 s; a game
    that stops refreshing leaves the list, a full one is hidden from the browser, a started one stays as Playing (not joinable);
  * the browser **asks for the list** (only games with the same build hash, since other builds cannot play together), shows
    name, mode (classic / co-op), players and ping;
  * **the host's address comes from the lobby** (owner 2026-10-10: the server's job is to pass the host's IP on). A host behind a
    router does not know its own public IP; the lobby takes it from the registering connection, together with the game's
    port, and tests it with a connect back. The list itself shows only name, mode, players and ping; the lobby hands the
    address to a client **when it picks that game to join**, and the client then connects directly.
  * **joining through the relay** instead when the connect back failed (the host cannot forward a port): the client gets
    the relay's address and a session id, so no host IP is handed out by JOIN_REPLY.  Once the game runs, the two ends of each link are introduced to each other for the optional
    direct path (2.2b): from then on the client does learn the host's public IP and the host the client's, unless `net.ini direct = no`.
  * Wire format: the same versioned binary messages as the game (`engine/netproto`), plain TCP. No HTTP and no TLS on the
    Amiga side (AmiSSL would be a big dependency); nothing secret travels, the list is public by nature.
  * Abuse limits on the server: one listing per address, name length / character filter (the game's font set only), rate
    limits; a listing never carries anything but the fields above.
* **Screen**: a new title-menu entry "Find game": the joystick moves through the list, fire joins, a line at the bottom
  switches LAN / internet and refreshes.  (As built for the internet list, 12.18: a FIND GAME row of the Multiplayer screen, section 4.4; the LAN list is 12.16.) The host side is a "Public: yes / no" line on the Host page (12.8a),
  **Yes by default** (owner 2026-10-10). A public host REGISTERs with the built-in lobby when it starts waiting, REFRESHes every
  30 s with the player count, and stays listed after START with the status **Playing** (owner 2026-10-10: hosted games must
  show on the dashboard; needs a "playing" status in the server and a refresh every 30 s during play).
  **Host half as built (12.18h, 2026-10-10):**
  * *Main page of the Multiplayer screen:* a row `PUBLIC YES / NO` between NAME and BACK (fire or left / right toggles); saved in
    `net.ini` as `public = yes|no` (default yes; `lobby = <name>` and `lobby_port = <n>` are read, never written).
  * *Pure part* `engine/netlobby` (host-tested, `tests/test_netlobby.py`): connect, REGISTER, REGISTER_REPLY (direct / relay / refusals), REFRESH
    every 30 s and within 1 s of a changed player count or of START, loss of the connection (re-register after 20 s, with the playing state),
    unreachable lobby (retry every 60 s), UNREGISTER at the end. Nothing blocks; times come from the caller's clock.
  * *rt part* `rt/net_lobby` + `NetTransport::lobby*`: one more non-blocking TCP socket (outside the game's links). The state machine is stepped
    from `step()` of `rt/net_session.cpp`, i.e. inside the windows the session opens anyway (menu poll, every tick, the waits): no window of its
    own; a pass costs a clock read and a comparison unless a message is due (a REFRESH every 30 s, a read of the quiet connection every 5 s).
    `gethostbyname` runs once per host session, inside a window, before the game plays (never during play: a reconnect then uses the cached
    address); a dead resolver can stall that one call, the only blocking one.
  * *What the player sees* (on the lobby screen, bottom left, small font, host only; the host page of 12.8a is gone, 4.2b): `LISTED ON THE LOBBY`, `LISTED VIA RELAY`, `CONTACTING LOBBY`, `LOBBY UNREACHABLE`
    (the game still hosts directly), `LOBBY FULL`, `ALREADY LISTED`, `NAME REFUSED BY LOBBY`, or `PRIVATE GAME`.
  * *Listing contents:* game name = the player name, mode = the lobby's current mode (CO-OP by default; the host's MODE line, 4.2b), players now / 4,
    **The mode follows the MODE line (2026-10-11, no protocol change):** REFRESH has no mode field, so `netLobbySetMode` (fed every lobby poll by
    `rt/net_session.cpp` from the room) turns a changed mode into UNREGISTER + REGISTER on the same connection, within 1 s: the dashboard and the
    join list show the new mode after the server's connect-back test (a new game id each time, never two listings). From START on the mode is
    final (a later change is ignored; a game that re-lists after a lost connection registers with the mode it started with).
    Build fingerprint of the running exe, game version, the game's TCP port. `LISTED VIA RELAY` is shown as the server says it; the game side of
    the relay (the host's idle RELAY_HELLO connection, handed over to a game link when a client arrives) is built since 2026-10-11: section 4.4.
* **The lobby address** is built in: `moonstone.shatteredallegiance.com`, TCP 6510 (owner's server, 2026-10-10;
  `include/engine/lobby.hpp`; web dashboard on 8080), a host name resolved with `gethostbyname`, and can be overridden in `net.ini`
  (`lobby = ...`), so players can run their own.

## 5b. The server: when internet play needs one

Internet play itself does **not** need a server: two players who know each other connect directly (`moonstone JOIN <ip>`)
once the host forwards one TCP port at its router. A server is needed for three things:

| Need | Why | Without a server |
|---|---|---|
| Public game browser (12.17, 12.18) | someone has to keep the list of open games and pass the host's public IP to a joining client (the host cannot know it behind a router) | LAN browser only (12.16); friends swap IP addresses |
| Relay (12.13a) | many players cannot forward a port: shared routers, mobile / carrier-grade NAT (no public IPv4 address of their own) | those players can only join, never host |
| Hiding the host's IP from strangers | a public listing would otherwise hand the address to anyone | only play with people you give the address to |

So it is **one small program, `tools/netserver/`** (Python, standard library only): lobby + relay in one process, one TCP port.

* **Relay**: both nodes connect *out* to the server (outgoing connections pass every home router), the server forwards each
  message to the other session members unchanged. It does not run the game and does not need the game data; it never
  sees anything but input frames, hashes and names.
* **Load**: lockstep traffic is a few KB/s per game [inferred, measured in 12.13], so a hundred relayed games fit a very
  small rented server or a free cloud tier; CPU is negligible.
* **Latency**: a relayed game pays the detour via the server; a server near the players (Europe for the Amiga scene) keeps the
  extra round trip small. Direct connections stay the first choice whenever the host's port is open.
* **Why not NAT hole punching** (two clients behind routers meeting directly with the server's help): it works mostly with
  UDP, the transport is TCP (12.7), and it still fails on carrier-grade NAT. A later UDP transport (12.13) could add it to
  save relay traffic; the relay stays as the fallback.
* **Operation** (12.19): runs as a service (systemd unit or a container), logs only counts and errors (no IP log kept
  beyond the abuse limits' short memory), restarts by itself; the address built into the game is a host name, so the server
  can move without a new game build; `net.ini` can point at a self-run server.
* **UDP** (12.21, protocol 6): the server also opens a UDP socket on the same port number and forwards the per-tick datagrams (2.2a "Relay"); open UDP 6510 in the
  firewall next to TCP 6510.  `tools/netserver/selftest.py` has a UDP leg.
* **The PC build** (M11, 12.14) can contain the same server later, so a player with a PC can run one at home.

## 6. Owner decisions (2026-10-10)

1. **Hardware**: the owner has no network card; players asked for network play. So M12 targets the **generic
   `bsdsocket.library` API** and makes no assumption about card or stack; development and the regression run on WinUAE's
   bsdsocket emulation, real-hardware checks (12.13) come from players or a card bought later. What A1200 players commonly have:
   * **PCMCIA Ethernet** (NE2000-compatible cards with the `cnet.device` SANA-II driver) or **PCMCIA WiFi** (Prism2 cards,
     `prism2.device`): the usual A1200 set-up. [unknown] PCMCIA and more than 4 MB of fast RAM can clash on some
     accelerators (the PCMCIA window lies at `$600000-$9FFFFF`); players' configs must be checked, the owner's ACA-1232 + 8 MB
     included, before recommending a card.
   * **Plipbox** (Ethernet on the parallel port): works, but uses the same port as the 4-player adapter (8.2a), so the two
     exclude each other.
   * **Serial PPP/SLIP** to a PC that routes: slow but enough, since lockstep sends only a few bytes per tick (about 2-3 KB/s with
     TCP/IP headers at 25 ticks/s, within 57,600 baud) [inferred]; the serial port is then not free for `serlog`.
   * **Stacks**: Roadshow (current, commercial), AmiTCP / Genesis / Miami (older, free). All provide `bsdsocket.library` v4;
     the game uses only the common subset (socket, connect, bind/listen/accept, send/recv, `WaitSelect`, `IoctlSocket`,
     `setsockopt(TCP_NODELAY)`, `gethostbyname`, `Errno`) so any of them works.
2. **LAN and internet**. Internet means: the host opens one TCP port at its router (NAT), the input delay adapts to the measured
   round trip (12.13), and a small **relay** that both sides connect out to helps players who cannot open a port (12.13a).
3. **2 machines first**, the code N-generic up to 4 nodes.
4. **Drops** as recommended: a dropped classic knight becomes an AI knight (`ulKind = 4`); a dropped co-op member goes to the
   host's controller.
5. **Local and remote players together** from the start (the `(node, pad)` binding).
6. **Cross-play with the PC build** (M11): yes. The protocol is big-endian on the wire and the PC transport sits behind the same
   interface (12.14); a PC can also be the host or the relay.

Still open:

7. **Seed range**: the host sends one of the original four seeds (default, as single-player) or any 32-bit value?
8. **Who runs the public lobby server** (and the relay): **the owner runs it on their own server** (2026-10-10). Open for 12.19:
   the host name to build into the game, and whether the server can run Python 3 as a service (or a container).

**Fight pause key disabled in network games (2026-10-26).** The lockstep delivers the keyboard events of ONE node per tick (the owner of the stick, `netMerge`), so
the original fight pause (`$20` = Space, `endAndPause`, a poll-free spin that waits for the NEXT key) could be started by one player and never ended by the other: both machines
hung (found with `pc_net.sh ... fightj`, where only the joiner presses keys); on a Keys-arrows pad Space is also the fire button.  `opTranslateKey` (`src/rt/combat.cpp`) now
returns 0 for the pause key while `netDeterministic()`, on every node alike; classic and local play keep the original pause.
