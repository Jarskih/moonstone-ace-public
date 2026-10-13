// rt/autoplay - see autoplay.hpp.
#include "rt/autoplay.hpp"

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY

#include <ace/managers/key.h>
#include <ace/managers/log.h>
#include <ace/managers/system.h>
#include <ace/utils/custom.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "engine/autoplay.hpp"
#include "engine/netnode.hpp"
#include "engine/tickpoke.hpp"
#include "rt/input.hpp"
#include "rt/net_tick.hpp"
#include "rt/guards.hpp"
#include "rt/perf.hpp"
#include "rt/amiga/serlog.hpp"

// The parser carries its own raw key table (it must build on the host): keep it equal to ACE's.
static_assert(KEY_SPACE == 0x40 && KEY_RETURN == 0x44 && KEY_ESCAPE == 0x45 && KEY_TAB == 0x42, "key codes");
static_assert(KEY_UP == 0x4C && KEY_DOWN == 0x4D && KEY_RIGHT == 0x4E && KEY_LEFT == 0x4F && KEY_A == 0x20 && KEY_Z == 0x31, "key codes");
static_assert(KEY_1 == 0x01 && KEY_0 == 0x0A && KEY_F1 == 0x50 && KEY_F10 == 0x59 && KEY_Q == 0x10, "key codes");

namespace rt {

namespace {

constexpr ULONG SCRIPT_MAX = 16384;
constexpr ULONG HEARTBEAT_FRAMES = 250;
// A `shot` freezes the machine for this many VBLs (8 s) inside the autoplay tick, so the picture the host captures is the one the
// script asked for even when the host is slow to notice the log line (the capture used to race the game: a shot taken ~20 s late
// showed the next screens). The host checks for the "shot-end" line after capturing and reports SHOT-LATE if it is already there.
constexpr ULONG SHOT_FREEZE_FRAMES = 400;

ms::ApEvent s_events[ms::kApMaxEvents];
char s_script[SCRIPT_MAX];
UWORD s_uwCount;
UWORD s_uwNext;
bool s_isArmed;
bool s_isQuit;
volatile ULONG s_ulFrame;
UBYTE s_ubJoy[2];
bool s_isJoyOverride[2];
constexpr ULONG PULSE_FRAMES = 3;   // default length of a `pulse`: the map polls only every 2nd-3rd frame
constexpr UBYTE PULSE_HOLD_MAX = 50;   // an unread pulse stays up this many more ticks at most (a menu that polls once per redraw, a shot freeze), then MISSED
UBYTE s_ubPulseLeft[2];       // `joyN <dirs> pulse [N]`: ticks the state stays up (0 = no pulse running)
ULONG s_ulPulsePolls[2];      // reads of the port by the game while the pulse was up (0 = the press was missed)
UBYTE s_ubPulseHeld[2];       // ticks a pulse was held past its length because the game had not read it yet (PULSE_HOLD_MAX at most)
volatile bool s_isPolled;     // the game read the joystick since the last tick
ULONG s_ulPollRun;            // ticks with a poll in the current run (silences of at most s_ulGap ticks do not end a run)
ULONG s_ulGap;                // G of the barrier being waited for (read by the tick)
ULONG s_ulSilent = 100;       // ticks since the last poll (a file open sets it high: the load is a break)
bool s_isGapSeen;             // a break (a long silence before a poll, or a load) was seen since the current `wait input` was reached

// ---- barriers (`wait file|log`, `sync`) ----
// Every serial-log line is offered to autoplayNoteLine(): a line that matches the pattern of some `wait` in the script is remembered
// as a "hit" (sequence number + pattern id) in a small ring, so a wait reached after its event already happened still finds it.
// A wait consumes the first unconsumed hit of its pattern.  The rules (which hash line a barrier takes, the base of `tick hash+K poke` lines) are engine/tickpoke's
// ApHitLog, pure and tested; this file only feeds it and asks it.
ms::ApHitLog s_hits;
// `wait var` and the hash barrier behind it (ROADMAP M12): a window notices the variable at its own VBL, i.e. at a tick that differs by a few ticks between the windows,
// and a `sync` + `wait log hash_tick` right behind could then take hash line H on one window and H + 50 on the other, for the rest of the script (every later
// `tick hash+K poke` lands on a different tick).  The tick at whose START the variable first held is the same on every node (autoplayTickPokes checks it), so
// the `sync` behind the wait keeps the hits and the next hash wait takes the first hash line after that tick on every window.
ULONG s_ulVarTick = ms::kTickPokeNone;      // the logical tick at whose start the pending `wait var` first held (none: not yet)
ms::TickPokeQueue s_sTickPokes;              // `tick ... poke` lines waiting for their tick (applied by autoplayTickPokes, called by rt::netTick)
UWORD s_uwPatEvent[ms::kApMaxEvents];   // pattern id -> index of the first wait event with it
UBYTE s_ubPatOfEvent[ms::kApMaxEvents]; // event index -> pattern id (waits only)
UWORD s_uwPats;
bool s_isWaiting;             // the barrier at s_uwNext has been reached
ULONG s_ulBase;               // frame at which the current segment started (events are relative to it)
ULONG s_ulWaitStart;          // frame at which the current wait was reached
bool s_isKeyDown[128];
UBYTE s_ubMashPeriod[3];      // mash: frames per pattern (0 = off)
ULONG s_ulMashNext[3];        // frame of the next pattern
ULONG s_ulMashSeed = 12345;   // deterministic pattern chooser
volatile bool s_isReleaseKeys;

char lowc(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

bool startsWith(const char *sz, const char *szPre) {
	while(*szPre) {
		if(*sz++ != *szPre++) {
			return false;
		}
	}
	return true;
}

// Case-insensitive: does szHay contain szPat? '_' in the pattern also matches a space.
bool containsCi(const char *szHay, const char *szPat) {
	for(; *szHay; ++szHay) {
		const char *h = szHay;
		const char *p = szPat;
		while(*p && *h && (lowc(*h) == lowc(*p) || (*p == '_' && *h == ' '))) {
			++h;
			++p;
		}
		if(!*p) {
			return true;
		}
	}
	return false;
}

// "files: open PROGDIR:data/Re.a 55588" vs the name "re.a": the path's last component, case-insensitive.
bool fileLineMatches(const char *szLine, const char *szName) {
	const char *p = szLine + 12;
	const char *pBase = p;
	for(; *p && *p != ' '; ++p) {
		if(*p == '/' || *p == ':') {
			pBase = p + 1;
		}
	}
	const char *n = szName;
	for(const char *q = pBase; q < p; ++q, ++n) {
		if(!*n || lowc(*q) != lowc(*n)) {
			return false;
		}
	}
	return *n == 0;
}

void endPulse(UBYTE ubPort);

// The fight autopilot's patterns: the eight directions and neutral, each with and without fire (mostly with: the attacks).
const UBYTE s_aubMash[] = {
	ms::kJoyFire, ms::kJoyLeft | ms::kJoyFire, ms::kJoyRight | ms::kJoyFire, ms::kJoyUp | ms::kJoyFire, ms::kJoyDown | ms::kJoyFire,
	ms::kJoyLeft | ms::kJoyUp | ms::kJoyFire, ms::kJoyRight | ms::kJoyUp | ms::kJoyFire, ms::kJoyLeft | ms::kJoyDown | ms::kJoyFire,
	ms::kJoyRight | ms::kJoyDown | ms::kJoyFire, ms::kJoyLeft, ms::kJoyRight, ms::kJoyUp, ms::kJoyDown, 0,
};

// `mash wasd`: the joystick bits of a pattern as key presses of the keyboard set B (raw W $11, A $20, S $21, D $22, left Alt $64 = fire).
void mashKeys(UBYTE ubBits) {
	static const UBYTE aubRaw[5] = {0x11, 0x21, 0x20, 0x22, 0x64};
	static const UBYTE aubBit[5] = {ms::kJoyUp, ms::kJoyDown, ms::kJoyLeft, ms::kJoyRight, ms::kJoyFire};
	for(UBYTE i = 0; i < 5; ++i) {
		const bool isDown = (ubBits & aubBit[i]) != 0;
		if(s_isKeyDown[aubRaw[i]] != isDown) {
			s_isKeyDown[aubRaw[i]] = isDown;
			inputInjectKey(aubRaw[i], isDown);
		}
	}
}

void releaseHeld() {   // joysticks now, keys at the next tick
	bool isAny = false;
	for(UBYTE i = 0; i < 2; ++i) {
		if(s_isJoyOverride[i] && s_ubJoy[i]) {
			s_ubJoy[i] = 0;
			isAny = true;
		}
	}
	for(UWORD i = 0; i < 128 && !isAny; ++i) {
		isAny = s_isKeyDown[i];
	}
	for(UBYTE i = 0; i < 2; ++i) {
		if(s_ubPulseLeft[i]) {
			endPulse(i);
			isAny = true;
		}
	}
	if(isAny) {
		s_isReleaseKeys = true;
	}
}

// A pulse ends: the port reads as released again.
void endPulse(UBYTE ubPort) {
	s_ubPulseLeft[ubPort] = 0;
	s_ubJoy[ubPort] = 0;
	serLogf("AUTOPLAY pulse joy%u %s, %lu reads\n", static_cast<unsigned>(ubPort),
		s_ulPulsePolls[ubPort] ? "done" : "MISSED (the game did not read the port)", static_cast<unsigned long>(s_ulPulsePolls[ubPort]));
}

}  // namespace

void autoplayInit() {
	serLogOpen();
	serLogWrite("AUTOPLAY boot\n");
	LONG lGot = -1;
	{
		SystemAccess sOs;
		DosHandle sScript("PROGDIR:autoplay.txt", MODE_OLDFILE);
		if(sScript) {
			lGot = Read(sScript.get(), s_script, SCRIPT_MAX);
		}
	}
	if(lGot <= 0) {
		serLogWrite("AUTOPLAY no PROGDIR:autoplay.txt, inactive\n");
		return;
	}
	const ms::ApParse sRes = ms::apParse(s_script, static_cast<ULONG>(lGot), s_events, ms::kApMaxEvents);
	s_uwCount = sRes.uwCount;
	s_uwPats = 0;
	for(UWORD i = 0; i < s_uwCount; ++i) {   // one pattern id per distinct (kind, text)
		const UBYTE ubKind = s_events[i].ubKind;
		if(ubKind != ms::AP_WAIT_FILE && ubKind != ms::AP_WAIT_LOG) {
			continue;
		}
		UWORD uwPat = 0;
		for(; uwPat < s_uwPats; ++uwPat) {
			const ms::ApEvent &o = s_events[s_uwPatEvent[uwPat]];
			if(o.ubKind == ubKind && containsCi(o.szText, s_events[i].szText) && containsCi(s_events[i].szText, o.szText)) {
				break;
			}
		}
		if(uwPat == s_uwPats) {
			s_uwPatEvent[s_uwPats++] = i;
		}
		s_ubPatOfEvent[i] = static_cast<UBYTE>(uwPat);
	}
	serLogf("AUTOPLAY script: %u events, %u bad lines (first %u)%s\n", (unsigned)sRes.uwCount, (unsigned)sRes.uwErrors,
		(unsigned)sRes.uwFirstErrLine, sRes.isOverflow ? ", OVERFLOW" : "");
	ms::tickPokeClear(s_sTickPokes);
	ms::apHitsClear(s_hits);
	s_ulFrame = 0;
	s_uwNext = 0;
	s_isArmed = true;
}

void autoplayNoteLine(const char *szLine) {
	if(!s_isArmed || startsWith(szLine, "AUTOPLAY")) {
		return;
	}
	const bool isOpen = startsWith(szLine, "files: open ");
	if(isOpen) {
		// A file open starts a load; the frame counter stands still during it, so a press that triggered it would otherwise stay
		// "held" for the next screen (the name entry took a leftover fire as OK). Let go of everything now.
		releaseHeld();
		// ... and no tick runs during the load, so the "no poll" gap `wait input` looks for would never be seen: a load is a gap.
		s_ulSilent = 100;
	}
	if(!s_uwPats) {
		return;
	}
	const bool isFail = isOpen && startsWith(szLine, "files: open FAIL");
	ULONG ulHashTick = ms::kTickPokeNone;
	if(!ms::tickPokeHashLine(szLine, ulHashTick)) {
		ulHashTick = ms::kTickPokeNone;
	}
	for(UWORD uwPat = 0; uwPat < s_uwPats; ++uwPat) {
		const ms::ApEvent &e = s_events[s_uwPatEvent[uwPat]];
		const bool isHit = (e.ubKind == ms::AP_WAIT_FILE) ? (isOpen && !isFail && fileLineMatches(szLine, e.szText)) : containsCi(szLine, e.szText);
		if(isHit) {
			ms::apHitsNote(s_hits, static_cast<UBYTE>(uwPat), ulHashTick);
		}
	}
}

namespace {

// Stand still for n vertical blanks (inside the level-3 handler: the main code and the lower interrupts wait, the display keeps going).
// Counts VERTB requests by polling INTREQR, with a spin cap so a dead video beam can never hang the machine.
void freeze(ULONG ulFrames) {
	ULONG ulSpin = 0;
	for(ULONG n = 0; n < ulFrames;) {
		if(g_pCustom->intreqr & INTF_VERTB) {
			g_pCustom->intreq = INTF_VERTB;
			++n;
			ulSpin = 0;
		}
		else if(++ulSpin > 4000000UL) {
			break;
		}
	}
}

}  // namespace

namespace {

// A `tick ... poke` line has been reached: work out its tick and queue it.  A poke that cannot be queued in time is an ERROR line (tools/netpair.py
// counts "AUTOPLAY ERROR" as BAD): a tick-poke that did not happen would let the two windows play different games.
void queueTickPoke(const ms::ApEvent &e) {
	const ULONG ulNow = netLatch().ulTick;   // the tick about to run
	ULONG ulTick = 0;
	if(!ms::tickPokeResolve(e.ubCode, e.ulTick, ulNow, ms::apHitsBase(s_hits), ulTick)) {
		serLogf("AUTOPLAY ERROR tick-poke %s %lu: no tick (no hash barrier consumed yet, or +0)\n", e.szText, static_cast<unsigned long>(e.ulValue));
		return;
	}
	const ms::TickPokePush ePush = ms::tickPokePush(s_sTickPokes, ulTick, ulNow, e.szText, e.ulValue);
	if(ePush != ms::TPP_OK) {
		serLogf("AUTOPLAY ERROR tick-poke %s %lu for tick %lu not queued: %s (now tick %lu)\n", e.szText, static_cast<unsigned long>(e.ulValue),
			static_cast<unsigned long>(ulTick), ePush == ms::TPP_LATE ? "late" : "queue full", static_cast<unsigned long>(ulNow));
	}
}

}  // namespace

// The start of logical tick ulTick (rt::netTick, main thread, before the tick's input is latched): every tick-poke due is applied, in script order.
void autoplayTickPokes(ULONG ulTick) {
	if(!s_isArmed) {
		return;
	}
	if(s_isWaiting && s_uwNext < s_uwCount && s_events[s_uwNext].ubKind == ms::AP_WAIT_VAR && s_ulVarTick == ms::kTickPokeNone) {
		ULONG ulNow = 0;
		if(autoplayPeek(s_events[s_uwNext].szText, ulNow) && ulNow == s_events[s_uwNext].ulValue) {
			s_ulVarTick = ulTick;
		}
	}
	ms::TickPoke sPoke;
	for(;;) {
		const ms::TickPokeTake eTake = ms::tickPokeTake(s_sTickPokes, ulTick, sPoke);
		if(eTake == ms::TPT_NONE) {
			return;
		}
		if(eTake == ms::TPT_LATE) {   // pushed after its tick had begun
			serLogf("AUTOPLAY ERROR tick-poke %s %lu late: wanted tick %lu, now tick %lu\n", sPoke.szName, static_cast<unsigned long>(sPoke.ulValue),
				static_cast<unsigned long>(sPoke.ulTick), static_cast<unsigned long>(ulTick));
			continue;
		}
		const bool isOk = autoplayPoke(sPoke.szName, sPoke.ulValue);
		serLogf("AUTOPLAY tick-poke %s %lu at tick %lu%s\n", sPoke.szName, static_cast<unsigned long>(sPoke.ulValue), static_cast<unsigned long>(ulTick),
			isOk ? "" : " FAILED");
	}
}

void autoplayTick() {
	if(!s_isArmed) {
		return;
	}
	const ULONG ulFrame = ++s_ulFrame;
	if(ulFrame % HEARTBEAT_FRAMES == 0) {
		serLogf("AUTOPLAY frame %lu\n", static_cast<unsigned long>(ulFrame));
		perfHeartbeat(HEARTBEAT_FRAMES);  // rt/perf: frame-loop statistics of the last 250 VBLs
	}
	perfPoll();  // rt/perf: wrap count of the beam clock (once per VBL)
	if(s_isPolled) {
		s_isPolled = false;
		if(s_ulSilent > s_ulGap) {   // polling starts again after a break: a new input loop
			s_ulPollRun = 0;
			s_isGapSeen = true;
		}
		++s_ulPollRun;
		s_ulSilent = 0;
	}
	else {
		++s_ulSilent;
	}
	for(UBYTE i = 0; i < 2; ++i) {   // running pulses (armed by an event of an earlier tick)
		if(s_ubPulseLeft[i] && --s_ubPulseLeft[i] == 0) {
			if(s_ulPulsePolls[i] == 0 && s_ubPulseHeld[i] < PULSE_HOLD_MAX) {   // not read yet: hold it like a player would, one tick at a time
				++s_ubPulseHeld[i];
				s_ubPulseLeft[i] = 1;
			} else {
				endPulse(i);
			}
		}
	}
	for(UBYTE i = 0; i < 2; ++i) {   // the fight autopilot
		if(s_ubMashPeriod[i] && ulFrame >= s_ulMashNext[i]) {
			s_ulMashSeed = s_ulMashSeed * 1103515245UL + 12345UL;
			s_ubJoy[i] = s_aubMash[(s_ulMashSeed >> 16) % (sizeof(s_aubMash) / sizeof(s_aubMash[0]))];
			s_isJoyOverride[i] = true;
			s_ulMashNext[i] = ulFrame + s_ubMashPeriod[i];
		}
	}
	if(s_ubMashPeriod[2] && ulFrame >= s_ulMashNext[2]) {   // the same patterns on the keyboard set B
		s_ulMashSeed = s_ulMashSeed * 1103515245UL + 12345UL;
		mashKeys(s_aubMash[(s_ulMashSeed >> 16) % (sizeof(s_aubMash) / sizeof(s_aubMash[0]))]);
		s_ulMashNext[2] = ulFrame + s_ubMashPeriod[2];
	}
	if(s_isReleaseKeys) {
		s_isReleaseKeys = false;
		bool isAny = false;
		for(UWORD i = 0; i < 128; ++i) {
			if(s_isKeyDown[i]) {
				s_isKeyDown[i] = false;
				inputInjectKey(static_cast<UBYTE>(i), false);
				isAny = true;
			}
		}
		serLogf("AUTOPLAY release-on-load frame %lu%s\n", static_cast<unsigned long>(ulFrame), isAny ? " keys" : "");
	}
	while(s_uwNext < s_uwCount) {
		const ms::ApEvent &e = s_events[s_uwNext];
		if(ms::apIsBarrier(e.ubKind)) {
			if(!s_isWaiting) {
				s_isWaiting = true;
				s_ulWaitStart = ulFrame;
			}
			if(s_ulBase + e.ulFrame > ulFrame) {
				break;   // minimum delay not over
			}
			bool isGo = true;
			bool isTimedOut = false;   // the wait gave up: nothing was consumed, no hash base is left (ms::apHitsTimedOut)
			if(e.ubKind != ms::AP_SYNC && e.ubKind != ms::AP_WAIT_VAR) {
				ms::apHitsOtherBarrier(s_hits);
			}
			if(e.ubKind == ms::AP_SYNC) {
				ms::apHitsSync(s_hits, ms::NN_HASH_INTERVAL);
			}
			else if(e.ubKind == ms::AP_WAIT_VAR) {
				ULONG ulNow = 0;
				const bool isKnown = autoplayPeek(e.szText, ulNow);
				if(!(isKnown && ulNow == e.ulValue)) {
					if(e.uwMax && ulFrame - s_ulWaitStart >= e.uwMax) {
						isTimedOut = true;
						serLogf("AUTOPLAY wait-timeout var %s=%s frame %lu\n", e.szText, isKnown ? "other" : "UNKNOWN", static_cast<unsigned long>(ulFrame));
					}
					else {
						isGo = false;
					}
				}
			}
			else if(e.ubKind == ms::AP_WAIT_INPUT) {
				s_ulGap = e.ubBits;
				if(ulFrame == s_ulWaitStart) {
					s_isGapSeen = false;   // only a gap after this barrier counts
				}
				if(!(s_isGapSeen && s_ulPollRun >= e.ulValue)) {
					if(e.uwMax && ulFrame - s_ulWaitStart >= e.uwMax) {
						isTimedOut = true;
						serLogf("AUTOPLAY wait-timeout input frame %lu\n", static_cast<unsigned long>(ulFrame));
					}
					else {
						isGo = false;
					}
				}
			}
			else if(!ms::apHitsTake(s_hits, s_ubPatOfEvent[s_uwNext])) {
				if(e.uwMax && ulFrame - s_ulWaitStart >= e.uwMax) {
					isTimedOut = true;
					serLogf("AUTOPLAY wait-timeout %s frame %lu\n", e.szText, static_cast<unsigned long>(ulFrame));
				}
				else {
					isGo = false;
				}
			}
			if(!isGo) {
				break;
			}
			serLogf("AUTOPLAY waited %s frame %lu\n", e.ubKind == ms::AP_SYNC ? "sync" : e.ubKind == ms::AP_WAIT_INPUT ? "input" : e.szText, static_cast<unsigned long>(ulFrame));
			if(e.ubKind == ms::AP_WAIT_VAR && s_ulVarTick != ms::kTickPokeNone) {
				serLogf("AUTOPLAY wait var %s held at tick %lu\n", e.szText, static_cast<unsigned long>(s_ulVarTick));
			}
			if(isTimedOut) {
				ms::apHitsTimedOut(s_hits);   // the base of `tick hash+K` is the line a barrier CONSUMED: a barrier that gave up consumed none (the stale base made the host queue tick 1360 at tick 9202)
			}
			else if(e.ubKind == ms::AP_WAIT_VAR) {
				ms::apHitsVarPassed(s_hits, s_ulVarTick);   // the `sync` right behind it uses the tick the variable first held at
			}
			else if(e.ubKind == ms::AP_WAIT_INPUT) {
				ms::apHitsVarPassed(s_hits, ms::kTickPokeNone);
			}
			if(e.ubKind == ms::AP_WAIT_VAR) {
				s_ulVarTick = ms::kTickPokeNone;
			}
			s_isWaiting = false;
			s_ulBase = ulFrame;
			++s_uwNext;
			continue;
		}
		if(s_ulBase + e.ulFrame > ulFrame) {
			break;
		}
		++s_uwNext;
		switch(e.ubKind) {
			case ms::AP_KEY:
				s_isKeyDown[e.ubCode & 127] = e.isDown != 0;
				inputInjectKey(e.ubCode, e.isDown != 0);
				break;
			case ms::AP_JOY:
				s_ubJoy[e.ubCode & 1] = e.ubBits;
				s_isJoyOverride[e.ubCode & 1] = true;
				s_ubPulseLeft[e.ubCode & 1] = (e.isDown != 0 && e.ubBits != 0) ? static_cast<UBYTE>(e.uwMax ? e.uwMax : PULSE_FRAMES) : 0;
				s_ulPulsePolls[e.ubCode & 1] = 0;
				s_ubPulseHeld[e.ubCode & 1] = 0;
				break;
			case ms::AP_SHOT:
				serLogf("AUTOPLAY shot %s frame %lu\n", e.szText, static_cast<unsigned long>(ulFrame));
				freeze(SHOT_FREEZE_FRAMES);
				serLogf("AUTOPLAY shot-end %s\n", e.szText);
				break;
			case ms::AP_LOG:
				serLogf("AUTOPLAY log %s frame %lu\n", e.szText, static_cast<unsigned long>(ulFrame));
				break;
			case ms::AP_MASH:
				if(e.ubCode == 2) {   // `mash wasd`: the keyboard set B (W A S D + Alt)
					s_ubMashPeriod[2] = e.ubBits;
					s_ulMashNext[2] = ulFrame;
					if(!e.ubBits) {
						mashKeys(0);
					}
					break;
				}
				s_ubMashPeriod[e.ubCode & 1] = e.ubBits;
				s_ulMashNext[e.ubCode & 1] = ulFrame;
				if(!e.ubBits) {
					s_ubJoy[e.ubCode & 1] = 0;
				}
				break;
			case ms::AP_POKE:
				serLogf("AUTOPLAY poke %s %lu %s frame %lu\n", e.szText, static_cast<unsigned long>(e.ulValue),
					autoplayPoke(e.szText, e.ulValue) ? "ok" : "FAILED", static_cast<unsigned long>(ulFrame));
				break;
			case ms::AP_TICK_POKE:
				queueTickPoke(e);
				break;
			case ms::AP_QUIT:
				s_isQuit = true;
				serLogf("AUTOPLAY quit frame %lu\n", static_cast<unsigned long>(ulFrame));
				break;
		}
	}
}

void autoplayJoy(ms::JoyBits &sBits) {
	autoplayNotePoll();
	autoplayJoyOverride(sBits);
}

void autoplayNotePoll() {
	s_isPolled = true;
	for(UBYTE i = 0; i < 2; ++i) {
		if(s_isJoyOverride[i] && s_ubPulseLeft[i]) {
			++s_ulPulsePolls[i];
		}
	}
}

void autoplayJoyOverride(ms::JoyBits &sBits) {
	for(UBYTE i = 0; i < 2; ++i) {
		if(s_isJoyOverride[i]) {
			(i ? sBits.uwPort1 : sBits.uwPort0) = s_ubJoy[i];
		}
	}
}

bool autoplayFireHeld() {
	const bool isFire = ((s_isJoyOverride[0] && (s_ubJoy[0] & ms::kJoyFire)) || (s_isJoyOverride[1] && (s_ubJoy[1] & ms::kJoyFire)));
	if(isFire) {   // the intro never reads the joystick through joyPoll: this counts as a read of a pulse
		for(UBYTE i = 0; i < 2; ++i) {
			if(s_ubPulseLeft[i]) {
				++s_ulPulsePolls[i];
			}
		}
	}
	return isFire;
}

bool autoplayQuit() {
	return s_isQuit;
}

}  // namespace rt

#endif
