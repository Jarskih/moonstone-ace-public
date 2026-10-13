// engine/tickpoke - the queue behind the autoplay `tick ... poke` lines (MS_AUTOPLAY, docs/AUTOPLAY.md "Tick-aligned pokes").
// A network game runs in lockstep ticks (rt/net_tick); a plain `poke` fires from the VBL interrupt at whatever tick each window happens to be on,
// so two windows that poke the same variable diverge for a few ticks (and the state hash differs).  A tick-poke is queued by the script and applied
// by the tick itself, at the start of logical tick N, before that tick's game logic: the same point on every node.
//
// Pure: no ACE, no hardware (tests/test_autoplay.py).  One producer (the autoplay interrupt pushes) and one consumer (the main thread takes at the
// start of a tick); every slot is written completely before its flag is raised, so no lock is needed.
#pragma once
#include <stdint.h>

#include "engine/autoplay.hpp"

namespace ms {

constexpr unsigned kTickPokeSlots = 16;
constexpr uint32_t kTickPokeNone = 0xFFFFFFFFu;   // "no tick" (no hash line consumed yet)

// How the tick of a `tick <spec> poke` line is given (ApEvent::ubCode of AP_TICK_POKE).
enum TickPokeMode : uint8_t {
	TPM_ABSOLUTE = 0,   // tick N
	TPM_RELATIVE = 1,   // tick +K : K ticks after the tick the game is on when the line is reached
	TPM_HASH = 2        // tick hash+K : K ticks after the state-hash tick the last barrier consumed (the same tick on both windows)
};

struct TickPoke {
	uint32_t ulTick;
	uint32_t ulSeq;     // push order: pokes of the same tick apply in script order
	uint32_t ulValue;
	char szName[kApTextLen];
};

struct TickPokeQueue {
	TickPoke aPoke[kTickPokeSlots];
	volatile uint8_t aubUsed[kTickPokeSlots];
	uint32_t ulSeq;
};

void tickPokeClear(TickPokeQueue &q);

// The tick a line resolves to.  ulArg = N or K; ulNow = the tick about to run; ulHashTick = the tick of the hash line the last barrier consumed
// (kTickPokeNone: none).  False when it cannot be resolved (relative K = 0, hash base missing, overflow).
bool tickPokeResolve(uint8_t ubMode, uint32_t ulArg, uint32_t ulNow, uint32_t ulHashTick, uint32_t &ulTick);

enum TickPokePush : uint8_t { TPP_OK = 0, TPP_LATE = 1, TPP_FULL = 2 };
// Queue a poke for tick ulTick.  TPP_LATE: ulTick < ulNow (the tick has passed, nothing queued).  TPP_FULL: no free slot.
TickPokePush tickPokePush(TickPokeQueue &q, uint32_t ulTick, uint32_t ulNow, const char *szName, uint32_t ulValue);

enum TickPokeTake : uint8_t { TPT_NONE = 0, TPT_DUE = 1, TPT_LATE = 2 };
// At the start of tick ulNow: the next poke whose tick is <= ulNow, in (tick, push order).  TPT_LATE: its tick is < ulNow (it was pushed too
// late for the tick it named; it is removed and must be reported, not applied).  TPT_NONE: nothing is due.
TickPokeTake tickPokeTake(TickPokeQueue &q, uint32_t ulNow, TickPoke &sOut);

// The first state-hash tick strictly after ulVarTick (ulInterval = the hash period): the line a `sync` after a `wait var` must consume.  ulVarTick is the logical
// tick at whose start the variable first held, the same on every node, so every window picks the same hash line however late its VBL noticed the variable.
uint32_t tickPokeHashFloor(uint32_t ulVarTick, uint32_t ulInterval);

// A serial-log line "net: hash tick N crc XXXXXXXX": N.  False for any other line.
bool tickPokeHashLine(const char *szLine, uint32_t &ulTick);

// ---- the hits the barriers consume (`wait log` / `wait file` / `sync`, docs/AUTOPLAY.md "Barriers") --------------------------------------------------
// rt/autoplay offers every serial-log line that matches some wait to this log; the barriers take from it.  Two windows of a network game run the same script
// and must consume the SAME state-hash line at every barrier (the base of every `tick hash+K poke`), so the rules live here, pure and tested
// (tests/test_autoplay.py replays both windows of the classic run that went wrong: a hash base that outlived its barrier, a floor that took whatever line
// the ring still held).
//
//   wait log hash_tick        takes the oldest hash line nobody consumed; the line's tick becomes the base (ApHitLog::ulBarHash)
//   sync                      forgets every hit so far: the next hash wait needs a line printed after it
//   wait var X; sync          the variable first held at a logical tick the same on every node: the sync keeps the hits and sets the FLOOR = the first hash tick
//                             after it.  The very next hash wait is released by the first line at or after the floor and counts as EXACTLY the floor line,
//                             whatever the ring still holds (an overwritten line, a window that is late) - one shot: a timeout, a `wait var` or the next `sync` drops it
//   timeout / wait var|input  nothing was consumed: the base is gone (a `tick hash+K` then says "no hash barrier", it never uses the line of a barrier long past)
constexpr unsigned kApHitRing = 64;

struct ApHit {
	uint32_t ulSeq;
	uint32_t ulHashTick;   // the tick of a "net: hash tick N" line (kTickPokeNone: another line)
	uint8_t ubPat;
};

struct ApHitLog {
	ApHit aHit[kApHitRing];
	volatile uint32_t ulSeq;          // sequence number of the newest hit
	uint32_t ulPtr;                   // hits up to here are consumed or forgotten
	volatile uint32_t ulNewestHash;   // the tick of the newest hash line noted, kept outside the ring (kTickPokeNone: none yet)
	uint32_t ulFloor;                 // see above (0: no floor)
	uint32_t ulVarHeld;               // the tick the `wait var` right before held at, until the `sync` behind it has used it (kTickPokeNone: none)
	uint32_t ulBarHash;               // the hash tick the last barrier consumed (kTickPokeNone: none)
	uint8_t aubHashPat[32];           // bit per pattern id: this pattern has matched a hash line
};

void apHitsClear(ApHitLog &l);
// A log line matched pattern ubPat (ulHashTick = the tick of a hash line, else kTickPokeNone).  The producer: the slot is complete before the sequence moves.
void apHitsNote(ApHitLog &l, uint8_t ubPat, uint32_t ulHashTick);
// A `wait log|file` of pattern ubPat is checked.  True: released (a hit was consumed, or the floor line has been printed).
bool apHitsTake(ApHitLog &l, uint8_t ubPat);
// A wait gave up (`max` frames): nothing was consumed, the hash base and the floor are gone.
void apHitsTimedOut(ApHitLog &l);
// A `wait var` / `wait input` passed (ulVarTick: the logical tick the variable first held at, kTickPokeNone for an input wait or when no tick was seen).
void apHitsVarPassed(ApHitLog &l, uint32_t ulVarTick);
// A barrier that is neither a `sync` nor a `wait var` is reached: a later `sync` is not "behind a wait var" any more.
void apHitsOtherBarrier(ApHitLog &l);
// A `sync`: forget everything, or - right behind a `wait var` that saw its tick - keep the hits and set the floor (ulInterval = the hash period).
void apHitsSync(ApHitLog &l, uint32_t ulInterval);
inline uint32_t apHitsBase(const ApHitLog &l) { return l.ulBarHash; }

}  // namespace ms
