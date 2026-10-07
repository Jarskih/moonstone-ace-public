// game/creatures - hit / contact detection, dagger flight and the creature job plumbing of mog.asm in C++
// (ROADMAP 6.6).  Transcribed from mog.asm; labels are cited per function.  tests/test_creatures.py runs every
// function against the lifted asm (tools/lift.py output, a literal 68k transliteration) over random arenas and
// against a Python model written from the listing.
//
// Pure: no ACE, no OS, no globals.  The game's own cells are passed in (src/rt/creatures.cpp wires them to the asm
// symbols, one trampoline per patched routine, asm/patches/mog.creatures.json).  Big-endian game bytes (cel tables,
// hit records, draw lists, the hit-set table) are read bytewise; the structs (Knight, CombatJob, DaggerSlot, ...)
// are native, which is the same thing on the 68k and swapped by the host test.
//
// What is here
//   Contact (the LAB_03BE family)
//     contactOverlap   LAB_03CA   closed-interval overlap of two intervals (the coarse gate; D5 += 1 in the asm)
//     depthClose       LAB_03B3   |depth A - depth B| <= 10 (the z gate of the pair scan and of the movement probe)
//     contactTest      LAB_03DB   one attack frame against one hurt frame: coarse boxes, then the contact points
//                                 of the attacker's hit record against the defender's bitplane mask
//     contactScan      LAB_03BE   the per-frame scan: every attack-list entry of every job against every hurt-list
//                                 entry of every other job in reach, first hit wins, links + hit point written
//     blockedMask      LAB_03A9   movement probe of one fighter against the others (+ LAB_03AC / LAB_03B3)
//   Damage
//     contactDamage   (rules/damage.cpp, LAB_021B)   damage of the attacker's current action (base table + strength + sword, doubled)
//     protectedDamage (rules/damage.cpp, LAB_0204)   damage shifted down by the defender's protection count
//   Dagger flight (the six-slot trajectory table LAB_0301)
//     daggerAim        LAB_02D3   aims the dagger block LAB_062E at the target (range, step count)
//     daggerStart      LAB_02F6   registers a trajectory for the block in a slot
//     daggerStep       LAB_02FD   one step of the trajectory of an owner
//     daggerRelease    LAB_02CA   throw: one dagger less, a creature record of type $34 running the dagger script
//   Creature plumbing
//     jobCreate        LAB_0310   first free job slot of the job table takes a script for an owner record
//     recordAlloc      LAB_0171   first free record of the 20-record creature heap
//     creatureSpawn    LAB_02D0   record + job for a new creature / projectile
//     creatureDispatch LAB_0322   per-frame pass over the job table calling each owner's AI handler (asm, per type)
//     clearHitLinks    LAB_0161   forgets all hit / attacker links
//   Predicates the AI handlers use
//     depthNear LAB_02BC, xNear LAB_02BF, xDelta LAB_02C2, faceTarget LAB_02C4
//
// What stays asm: the sprite blitter, the hit.dat loader LAB_03CE/LAB_03DA (file I/O), the AI handlers themselves
// (the LAB_0621/LAB_0622/LAB_08C7 tables: combat FSM, ROADMAP 6.4), the lifted leaf routines LAB_0315/LAB_0319/
// LAB_031B/LAB_030D (swap list pure.txt) and LAB_03CA's other callers (the overworld code calls the swapped leaf).
//
// Quirks kept (each commented where it happens): every compare / add of the asm is a 16-bit word operation, the
// branches are the exact conditions the 68k evaluates (BMI looks at N only, BGE / BLT at N xor V), the contact point
// reported is built from the RAW record point bytes (no mirror), the hit-record skip loop and the 21-record link
// clear overrun the 20-record heap by one record, LAB_02D3 writes 128(A0) of whatever record A0 holds.
// Deliberate differences (also commented): a plane mask above 15 or of zero planes reports no contact (the asm walks
// off its popcount table / loops 65536 times), a hit set that is not registered reports no contact (the asm scans
// without end), a divide by zero in the dagger setup reports failure (the asm takes the divide exception).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "game/combat_script.hpp"
#include "game/knight.hpp"

namespace ms { namespace game {

#pragma pack(push, 2)

// The contact point of a hit: LAB_0A52 / LAB_0A53 (attack point = raw record point + attacker position).
struct ContactPoint {
	uint16_t uwX;
	uint16_t uwY;
};

// LAB_062E: the dagger block the AI fills before LAB_02D3 / LAB_02F6 (5 longs).  Offsets are the asm's.
struct DaggerBlock {
	uint32_t ulOwner;          // +0   the thrower's record (the slot key)
	uint16_t uwX;              // +4   start x
	uint16_t uwDepth;          // +6   start depth (record +8)
	uint16_t uwHeight;         // +8   start height (record +6)
	uint16_t uwTargetX;        // +10  end x (target +4 shifted by the speed)
	uint16_t uwTargetDepth;    // +12
	uint16_t uwTargetHeight;   // +14
	uint16_t uwSteps;          // +16  flight time in steps (LAB_0628)
	uint16_t uwExtra;          // +18  arc parameter (LAB_0629; 0 / 2 / $78 ... set by the callers)
};
static_assert(sizeof(DaggerBlock) == 20, "LAB_062E DS.L 5");

// LAB_0301 + 20 * n: one trajectory, six slots (LAB_02F2 clears all 120 bytes).  Positions are fixed point.
struct DaggerSlot {
	uint32_t ulOwner;          // +0   0 = free
	uint16_t uwSteps;          // +4   steps left; the slot is freed when it counts down to 0
	int16_t  swVz;             // +6   vertical speed (height, 8.8)
	int16_t  swGravity;        // +8   subtracted from the vertical speed every step
	int16_t  swDx;             // +10  x per step (10.6)
	int16_t  swDy;             // +12  depth per step (10.6)
	int16_t  swX;              // +14  x (10.6)
	int16_t  swY;              // +16  depth (10.6)
	int16_t  swZ;              // +18  height (8.8)
};
static_assert(sizeof(DaggerSlot) == 20, "LAB_0301: 6 x 20 bytes");

#pragma pack(pop)

constexpr uint32_t DAGGER_SLOTS = 6;
constexpr uint32_t CREATURE_RECORDS = 20;       // LAB_05C3: LAB_0171 scans 20 records of $84 bytes
constexpr uint32_t LINK_CLEAR_RECORDS = 21;     // LAB_0161 clears one record more (D7 = $14 with DBF)
constexpr uint32_t HIT_LIST_BYTES = 800;        // LAB_064F / LAB_0650: 10 jobs x 8 entries x 10 bytes
constexpr uint32_t HIT_ENTRY_BYTES = 10;
constexpr uint32_t HIT_SLOT_BYTES = 80;         // per job (LAB_0308: A2 += $50)

// ---- contact ----------------------------------------------------------------------------------------------

// LAB_03CA: the coarse gate.  Intervals [a0, a1] and [b0, b1] with the original's decision tree on 32-bit signed
// compares (CMP.L D0,D2 / BMI ...): touching ends count.  Returns true where the asm does D5 += 1.  Callers in the
// scan pass zero-extended words (then it is an unsigned 16-bit interval test); the overworld callers do too.
bool contactOverlap(int32_t a0, int32_t a1, int32_t b0, int32_t b1);

// LAB_03B3: the two depths are within 10 of each other.  Word arithmetic exactly as the asm: SUB.W, BPL on N only,
// NEG.W (-32768 stays), CMP.W #10 / BGT signed.  (moonshard's ms_contact_depth_ok special-cases 0x8000; the 68k
// does not: CMP resets the overflow flag NEG set, so 0x8000 counts as close.)
bool depthClose(uint16_t uwDepthA, uint16_t uwDepthB);

// LAB_03DD..LAB_03DE: the hit set registered for an attacker cel table.  pPairs = LAB_0A51: ulCount pairs of
// big-endian longs {cel table address, hit set address}.  Returns the hit set (a game address) or 0.  The asm scans
// without a bound (a missing table runs off the pairs); here an unknown key finds nothing.
uint32_t hitSetFind(const uint8_t *pPairs, uint32_t ulCount, uint32_t ulKey);

// LAB_03DF..LAB_03E0: the hit record of frame uwIndex.  A record is {count, ?, maxDx, maxDy, count * (x, y)} = 4 +
// 2 * count bytes, and a frame without contact points is the single byte 0.  Walks uwIndex records from pSet.
const uint8_t *hitRecordAt(const uint8_t *pSet, uint16_t uwIndex);

// LAB_03DB.  Does the attacker (cel table pAttTab, hit record of frame uwAttFrame in pHitSet, at uwAttX / uwAttY)
// touch the defender (cel table pDefTab, frame uwDefFrame, at uwDefX / uwDefY)?
//   Cel table (big-endian): +2 long = base of the plane data, entries from +10, 10 bytes each {long offset into the
//   planes, word width, word height, word (flags << 8) | plane mask}.  The attacker mirrors when bit 0 of the flags
//   byte (entry +8) is CLEAR: x is then mirrored in its own width (LAB_0A54).
//   Test: attacker box (x .. x + maxDx, y .. y + maxDy, mirrored boxes shifted) against the defender cel rectangle,
//   then each record point inside the rectangle: the bit of the defender's planes (word at row * rowBytes + x / 16
//   * 2 of each of popcount(mask) planes, one plane every row * rowBytes bytes) decides.  The bit position and the
//   point reported come from the RAW point (mirror ignored, a quirk of the original); the window gate uses the
//   mirrored one.  Returns true and the contact point on a hit.
// pDefTab / pAttTab / pHitSet are the tables as bytes (jobPtr of the game addresses); the plane base inside the
// defender's table is a game address, resolved with jobPtr.
bool contactTest(const uint8_t *pDefTab, uint16_t uwDefFrame, uint16_t uwDefX, uint16_t uwDefY,
                 const uint8_t *pAttTab, const uint8_t *pHitSet, uint16_t uwAttFrame, uint16_t uwAttX,
                 uint16_t uwAttY, ContactPoint *pPoint);

// Everything LAB_03BE touches.  The attack / hurt lists are the 800-byte buffers LAB_064F / LAB_0650 that the draw
// events of combat_script fill (draw flag bits 1 / 0), one 80-byte slot per job, entries {long cel table, word
// frame, word x, word y} big-endian, the end of a slot is a zero long.
struct ContactEnv {
	CombatJob *pJobs;              // LAB_0649, COMBAT_JOB_COUNT slots
	const uint8_t *pHitPairs;      // LAB_0A51
	uint32_t ulHitCount;           // pairs registered: (LAB_0A4E - LAB_0A51) / 8
	Knight *pDragon;               // LAB_0617
	Knight *pKnights;              // LAB_0613 (4 records)
	Knight *pCreatures;            // LAB_05C3's value: the creature heap (20 records + the one the link clear touches)
	uint8_t *pAttackLists;         // LAB_064F
	uint8_t *pHurtLists;           // LAB_0650
};

// LAB_0161: every record's hit target (+14) and attacker (+18) links are cleared: the dragon, 21 creature records
// (one more than the heap holds, see LINK_CLEAR_RECORDS), the four knights.
void clearHitLinks(const ContactEnv &env);

// LAB_03BE (called from the frame loop LAB_0037): clearHitLinks, then for every active job and each of its attack
// entries, every OTHER active job whose depth is within 10 (depthClose on the job's +10) and each of its hurt
// entries: contactTest.  The first hit stores the hurt record in the attacker's +14, the attacker record in the
// hurt record's +18 and the contact point in the hurt record's +122 / +124, then the scan moves on to the next
// job (the remaining attack entries of that job are not tested).  Finally both lists are zeroed (LAB_03C7).
void contactScan(const ContactEnv &env);

// LAB_03A9: the movement probe.  pSelf = the moving fighter (A0), uwStep = its x step (D0), uwFacing = D2 (only the
// low two bits matter: 1 = right).  Every other active job's owner with a non-zero box (+58) and HP (+80) > 0 is
// tested: facing right / left blocks bit 0 / 1 when it is on that side and its x box and y box overlap with the
// step added to the x box; bits 2 / 3 block up / down when within 20 in depth and the boxes overlap.  Returns the
// permitted-direction mask (bits 0..3 cleared when blocked, bit 4 always set, the asm's $1F start value).
uint16_t blockedMask(const CombatJob *pJobs, const Knight *pSelf, uint16_t uwStep, uint16_t uwFacing);

// ---- damage -----------------------------------------------------------------------------------------------

// LAB_021B contactDamage and LAB_0204 protectedDamage are rules now (ROADMAP 9.6g): src/game/rules/damage.cpp, declared in
// game/rules/damage.hpp.  They are declared here too so the engine-level callers keep their include.
uint32_t contactDamage(const Knight &attacker, const uint8_t *pDamageTable, const Inventory &inv, uint16_t uwMoonFrame);
uint16_t protectedDamage(uint16_t uwDamage, const Inventory &inv);

// ---- dagger flight ----------------------------------------------------------------------------------------

// LAB_02D3 result: the cells the callers read afterwards.
struct DaggerAim {
	uint16_t uwDist;           // LAB_02DA: the larger of |dx| and |depth difference|
	uint16_t uwSteps;          // LAB_0628: dist / 8, at least 4
	uint16_t uwArc;            // LAB_0629: dist / 2 (at least 3), 10 when steps were raised to 4
};

// LAB_02D3: fills the block from the thrower and the target and sizes the flight.  swSpeed (D7) is negated when the
// target is on the right (SUB.W / BMI: N only); the target x becomes target + speed, also stored in the thrower's
// +126 (map x).  QUIRK: the target depth is stored in +128 of pStray (A0 on entry, normally the thrower itself, not
// the A1 the rest of the routine uses).  Also stores uwSteps in the thrower's +106 as a word (timer + the byte
// after it) and in the block's +16 / +18.
DaggerAim daggerAim(DaggerBlock &block, Knight &self, const Knight &target, Knight *pStray, int16_t swSpeed);

// LAB_02F6: registers the block's flight in the slot of its owner (the first slot holding the key, else the first
// free one).  Returns 0, -1 when all six slots hold other owners, or -2 where the asm would divide by zero (the
// original takes the divide exception; the slot is left as the asm leaves it up to that point).  When a slot is
// found the thrower's +126 / +128 take the two junk words the asm copies from low memory (uwJunkA / uwJunkC =
// rt_abs_a / rt_abs_c; pSelf = LAB_0633's record).  Two set-ups by the height difference: more than 5 apart the
// vertical speed is 2 * (dh * 256 / steps), the gravity speed / (steps - 1) (DIVS, quotient overflow leaves the
// dividend: the asm's ADD.W then works on the dividend); otherwise an arc from the extra parameter (DIVU).
// pSlot (optional) receives where the asm leaves A0: the index of the slot the scan stopped at (DAGGER_SLOTS when none
// matched); the asm then writes the slot through (A0)+, so after a successful set-up A0 = slot + 20 bytes.
int32_t daggerStart(DaggerSlot *aSlots, const DaggerBlock &block, Knight *pSelf, uint16_t uwJunkA, uint16_t uwJunkC,
                    uint32_t *pSlot = 0);

// LAB_02FD: one step of the flight of ulOwner.  Returns -1 when the owner has no slot (the out words are not
// touched), 0 while flying, 1 when the last step was made (the slot is freed).  out[0] = x, out[1] = depth,
// out[2] = height, the values after the step (asr 6 / 6 / 8).
// pSlot (optional): the slot index the scan stopped at (DAGGER_SLOTS when none), where the asm leaves A0.  The AI handler
// LAB_0280 reads 64(A0) after the call as if A0 still were the target record: that is the slot table, kept.
int32_t daggerStep(DaggerSlot *aSlots, uint32_t ulOwner, uint16_t aOut[3], uint32_t *pSlot = 0);

// ---- creature plumbing ------------------------------------------------------------------------------------

// LAB_0310: the first free job slot (byte +0 == 0) takes the script, owner, frame list, position, facing and type;
// its work block (36 bytes) is cleared and +0 / +1 are set.  Returns 0, or 1 when all ten are in use (the asm then
// prints a debug string through the stub LAB_0BB3 = RTS).
uint32_t jobCreate(CombatJob *pJobs, uint32_t ulScript, uint32_t ulOwner, uint32_t ulFrames, uint16_t uwX,
                   uint16_t uwY, uint16_t uwZ, uint8_t ubFacing, uint8_t ubType);

// LAB_0171: first free record (+0 == 0) of the creature heap, marked in use (+0 = 1).  When all 20 are taken the
// asm returns the address after the last one without marking it: the result then lies one record past the heap
// (and LAB_02D0 writes there).  Returns that record.
Knight *recordAlloc(Knight *pCreatures);

// What LAB_02D0 leaves in A1 / D0.
struct SpawnResult {
	Knight *pRecord;
	uint32_t ulStatus;         // jobCreate's result
};

// LAB_02D0: allocates a record and starts a job for it: +4 / +6 / +8 = x / y / z, +10 = facing, +38 = frames,
// links (+14 / +18) cleared, +0 = 1, +77 = type, then jobCreate with the record as owner.
SpawnResult creatureSpawn(CombatJob *pJobs, Knight *pCreatures, uint32_t ulScript, uint32_t ulFrames, uint16_t uwX,
                          uint16_t uwY, uint16_t uwZ, uint8_t ubFacing, uint8_t ubType);

// LAB_02CA, the dagger throw (script op $B0 target): the thrower loses a dagger (+76, byte), a record of type $34 is
// spawned with the dagger script, then it gets the dagger damage table (+42) and the action $0C (+64).  Returns what
// LAB_02D0 left in A1 / D0.
SpawnResult daggerRelease(CombatJob *pJobs, Knight *pCreatures, Knight &thrower, uint32_t ulDaggerScript,
                      uint32_t ulDamageTable, uint32_t ulFrames, uint16_t uwX, uint16_t uwY, uint16_t uwZ,
                      uint8_t ubFacing);

// What an AI handler returns (LAB_0322's contract): A0 = next script, 0 (kill the creature) or -1 (keep running),
// and the position / facing the job takes.
struct HandlerResult {
	uint32_t ulScript;         // A0
	uint16_t uwX, uwY, uwZ;    // D0..D2
	uint8_t  ubFacing;         // D3
};

struct DispatchEnv {
	CombatJob *pJobs;
	const uint8_t *pHandlerTable;      // LAB_08C7: longs, indexed by the job's type BYTE (not scaled: $0C, $10, ...)
	// The handler is asm: A0 = the owner record, returns HandlerResult.  pOwner is the job's owner (a game address).
	void (*callHandler)(uint32_t ulHandler, uint32_t ulOwner, HandlerResult *pOut);
};

// LAB_0322: per-frame pass.  For every active job whose owner has a hit link (+14 or +18) set, or whose script is
// not running (+1 == 0): call the handler of its type, then -1 leaves the job alone, 0 stops it (+0 / +1 = 0 and the
// owner's first long cleared: the record is free), anything else is the new script with the position and facing.
void creatureDispatch(const DispatchEnv &env);

// ---- predicates of the AI handlers ------------------------------------------------------------------------

// LAB_02BC: |other.depth - self.depth| <= self +120 (word compares, BPL on N only).
bool depthNear(const Knight &self, const Knight &other);

// LAB_02BF: |target.x - self.x| <= swLimit (signed word compare).
bool xNear(const Knight &self, const Knight &target, int16_t swLimit);

// LAB_02C2: |self.x - target.x|, and bSelfLeft = the difference was negative (D1 = 1).
uint16_t xDelta(const Knight &self, const Knight &target, bool &bSelfLeft);

// LAB_02C4: turns self towards the target: facing 3 when self.x >= target.x (signed), else 1.
void faceTarget(Knight &self, const Knight &target);

}}  // namespace ms::game
