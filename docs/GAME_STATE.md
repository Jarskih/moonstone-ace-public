# Game state layouts (ROADMAP 5.1)

Typed views of mog.asm's BSS/DATA: `include/game/{knight,world,state}.hpp` (structs, `static_assert` on every offset
and size) and `include/game/state_bind.hpp` (typed `extern "C"` declarations of the `mog_LAB_xxxx` symbols).
`tests/test_game_state.py` compiles the headers with clang++ and checks each binding against the DS/DC sizes it parses
from `mog.asm`. Authority is mog.asm; DOC_TECHNIQUE 10.18 and moonshard's `mechanics.{h,c}` / `ORIGINAL_FOUNDATION.md`
were used as cross-checks only.

Conventions: big-endian 68k, `#pragma pack(2)`, pointers as `uint32_t`, `Unk<offset>` for fields that are accessed but
whose role is not established. 2.10 savestates were not available for this task, so no field was checked against live RAM
yet (open item).

## Bound labels

| Label (`mog_` prefix) | C++ type | Size | Count | Evidence |
|---|---|---|---|---|
| `LAB_0613`..`LAB_0617` | `Knight` | 132 ($84) | 5 | DS.L 33 each; stride $84 in LAB_0011/LAB_01BF/LAB_0DBD. 0..3 knights, 4 = dragon (`+54 == 5`, LAB_01AE; LAB_0029 loops 5) |
| `LAB_0618`, `LAB_0619` | `Inventory` | 24 | 5 | DS.L 24 + DS.L 6. LAB_01C4 hands LAB_0618+24*i to `Knight +96`; LAB_0619 is the dragon's (LAB_01AE) |
| `LAB_05E4` | `ActiveKnights` | 22 | 1 | DS.L 5 + DS.W 1. Fight pair, round state, lunar frame/sub-counter (LAB_0036, LAB_0029, LAB_01AE) |
| `LAB_0649` | `Job` | 50 ($32) | 10 | DS.L 125. Sprite/animation job table, LAB_0310 `ADDA.L #$32`, D7=9 |
| `LAB_069F` | `MapNode` | 6 | 10 | S_2 DATA, DC.L x15 = 9 nodes + `$FFFF` terminator; LAB_0069 reads 3 words per entry; ids $15..$18 = home villages (LAB_006C..006E) |
| `LAB_08C6` | `uint32_t` (Lair*) | 4 | 1 | pointer to the current 20-byte `Lair` record (LAB_01B5, LAB_0DA3, LAB_0E04) |
| `LAB_05B9` | `uint32_t[25]` | 100 | 1 | heap pointer table; +68 = Lair records, +72 = lair loot Inventories (LAB_01B3) |
| `LAB_05C3` | `uint32_t` | 4 | 1 | base of 20 heap creature records (Knight layout, LAB_000A/LAB_003C) |
| `LAB_0633`, `LAB_05F2`, `LAB_068B`, `LAB_068D` | `uint32_t` (Knight*) | 4 | 1 | current knight / first fighter / UI snapshots |
| `LAB_068C`, `LAB_068E` | `uint32_t` (Inventory*) | 4 / 8 | 1 | `Knight +96` of the two above (LAB_058A); LAB_068E is DS.L 2 |
| `LAB_068F` | `uint32_t` | 4 | 1 | scene id (DOC_TECHNIQUE 10.19) |
| `LAB_05C5` | `WordSlot` | 4 | 1 | player count; DS.L 1 but only `.W` accesses |
| `LAB_05DB`, `LAB_0654`, `LAB_0655`, `LAB_0663`, `LAB_0659`, `LAB_065A`, `LAB_06C0`, `LAB_06C1` | `uint16_t` | 2 | 1 | scheduler/clock scalars, all accessed `.W` (see comments in `state_bind.hpp`) |
| `LAB_0665` | `WordSlot` | 4 | 1 | turn budget = `Knight::ubDerivedEnd << 4` (LAB_0DBE); DS.L 1, `.W` accesses |
| `LAB_05DC`, `LAB_05DF` | `ByteSlot` | 2 | 1 | defeat bits / screen-mode byte; DS.W 1, `.B` accesses at the label (high byte) |
| `LAB_06C2` | `uint8_t[8]` | 8 | 1 | S_4 DATA `"-/.010./"` = moon frame per index (45,47,46,48,49,48,46,47) |

Not bound as a struct (no single BSS label): `Lair` (20 B x 24, heap via `LAB_05B9[17]`, current = `LAB_08C6`) and the lair loot
`Inventory` x 24 (heap via `LAB_05B9[18]`). Their layouts are in `world.hpp` / `knight.hpp`.

## Field counts

| Struct | Size | Named | Unknown (`Unk`) |
|---|---|---|---|
| `Knight` | 132 | 47 | 10 (`uwHeight`, `ubInputPort`, `ulSpare58`, `ubSpare87`, `ubCooldownLo`, `ubSpare112[4]`, `uwReachX`, `uwTooCloseX`, `uwDepthReach`, `ubSpare131`) |
| `Inventory` | 24 | 12 slots (7 with a role: +4, +6, +8, +18, +20, +22; counts at +0, +2, +10, +12, +14, +16 seen only in the transfer table) | 12 odd pad bytes |
| `ActiveKnights` | 22 | 7 | 3 (`ubUnk9`, `ulStatusLink`, `ubUnk17`) |
| `Lair` | 20 | 7 | 1 (`uwHeight`) |
| `MapNode` | 6 | 3 | 0 |
| `Job` | 50 | 13 | 4 (`uwUnk8`, `ubUnk12[10]`, `ubUnk23`, `ubUnk33[3]`) |

## Where the asm disagrees with DOC_TECHNIQUE 10.18

The doc's table was written from partial reading; the asm contradicts it in these places (asm wins):

| Offset | DOC 10.18 | mog.asm |
|---|---|---|
| +2..+18 | item counts inside the knight record | there are none; items live in the separate 24-byte `Inventory` reached through `+96` (LAB_01C4, LAB_0013 `MOVEA.L 96(A0),A1`) |
| +73 | `skill_level` (temple raises it) | lives: `SUBI.B #1,73` on a lost fight (LAB_000E, LAB_0030), init 5 (LAB_01C6) |
| +83 | `enemy_flag` (0 players, $FF AI) | wizard-visit recency, decays by 10 per day to 0 (LAB_002B); $FF is the AI sentinel inside that scheme |
| +96 | `linked_knight` | pointer to the knight's `Inventory` |
| +126/+128 | spawn x/y | live overworld position, +1 per step (LAB_0DA6), compared with the node table (LAB_0069) |
| +54 | 0..3 colours, 4/5 black | 0..3 knights, 4 = AI-controlled knight, 5 = dragon |
| Inventory +4/+8 | sharp sword at 8, protection at 6 | +4 forces sword $19 (LAB_0013), +6 = +20 max HP per count, +8 = damage shift (LAB_0204) |

## Open questions

* The 2.10 savestates are not in this tree; confirm `ubType`, `ubInputPort`, `ubBehaviourFlags/105` and the Inventory slots at
  +0/+2/+10/+12/+14/+16 against live RAM.
* `Knight +14` / `+18` overlap with how LAB_0DCB initialises the dragon (`MOVE.W #$B6,14(A0)`, `MOVE.L #5,16(A0)`): the dragon
  appears to reuse them for flight state before the collision code owns them. Left as the collision pointers.
* Knight pointer fields are `uint32_t`; no typed accessor (`Knight* fromAddr(uint32_t)`) exists yet because the target
  address-to-pointer mapping belongs to the runtime (src/rt), not the headers.
