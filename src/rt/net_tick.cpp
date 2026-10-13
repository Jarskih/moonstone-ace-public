// rt/net_tick - see net_tick.hpp (ROADMAP 12.4).
#include "rt/net_tick.hpp"

#include "rt/autoplay.hpp"
#include "rt/input.hpp"
#include "rt/logwrite.hpp"

extern "C" {
extern uint32_t mogRandomSeed;   // LAB_0973
// src/rt/combat_ui.cpp: one tick of the joystick cursor (ulVbls VBLs); does nothing while the cursor is off.
void rtCuiCursorStep(ULONG ulVbls);
}

namespace rt {

namespace {

volatile bool s_isOn;
ms::TickLatch s_latch;
ms::KeyQueue s_keys;
NetFrameHook s_pfnFrame;
ms::PadFrame s_overridePads;
bool s_isPadsOverridden;
volatile ULONG s_ulPace;       // real VBLs, counted by the interrupt while the mode is on

NetSeedHook s_pfnSeed;
volatile bool s_isSeedOnce;
ULONG s_ulSeedOnce;
volatile bool s_isSeedAt;
ULONG s_ulSeedAtTick, s_ulSeedAtValue;

}  // namespace

bool netDeterministicFlag() {
	return s_isOn;
}

bool netDeterministic() {
	return s_isOn && inputCells() == &inputMogCells();
}

void netDeterministicSet(bool isOn) {
	if(isOn == s_isOn) {
		return;
	}
	if(isOn) {
		ms::keyQueueClear(s_keys);
		ms::latchReset(s_latch, *inputMogCells().pulFrame);
		s_ulPace = s_latch.ulClock;
	}
	s_isOn = isOn;   // last: the main thread sees a complete latch
}

void netDeterministicBegin(ULONG ulClock, ULONG ulTick) {
	netDeterministicSet(false);
	*inputMogCells().pulFrame = ulClock;
	netDeterministicSet(true);
	s_latch.ulTick = ulTick;   // a rejoiner starts in the middle of a game, on the host's tick
}


#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
namespace {
struct TickNote {
	ULONG ulTick, ulClock, ulCaller;
	UWORD uwVbls;
};
constexpr UWORD RING_SIZE = 256;
TickNote s_aRing[RING_SIZE];
ULONG s_ulRingCount;
}  // namespace

void netTickRingDump() {
	const ULONG ulShow = s_ulRingCount < 200 ? s_ulRingCount : 200;
	for(ULONG i = s_ulRingCount - ulShow; i < s_ulRingCount; ++i) {
		const TickNote &t = s_aRing[i % RING_SIZE];
		logWrite("net: ring tick=%lu clock=%lu vbls=%u who=%08lX\n", static_cast<unsigned long>(t.ulTick), static_cast<unsigned long>(t.ulClock),
			static_cast<unsigned>(t.uwVbls), static_cast<unsigned long>(t.ulCaller));
	}
}
#else
void netTickRingDump() {}
#endif

void netTick(ULONG ulVbls, ULONG ulCaller) {
	if(!netDeterministic()) {
		return;
	}
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
	{
		TickNote &t = s_aRing[s_ulRingCount % RING_SIZE];
		t.ulTick = s_latch.ulTick;
		t.ulClock = s_latch.ulClock;
		t.ulCaller = ulCaller;
		t.uwVbls = static_cast<UWORD>(ulVbls);
		++s_ulRingCount;
	}
#else
	(void)ulCaller;
#endif
	autoplayTickPokes(s_latch.ulTick);               // MS_AUTOPLAY: the script's `tick N poke` lines, at the start of tick N on every node (docs/AUTOPLAY.md)
	if(s_isSeedAt && s_latch.ulTick == s_ulSeedAtTick) {
		mogRandomSeed = s_ulSeedAtValue;
		s_isSeedAt = false;
	}
	ms::LocalFrame sFrame;
	inputCaptureLocal(sFrame);                       // hardware + autoplay override + queued key events
	if(s_pfnFrame) {
		s_pfnFrame(s_latch.ulTick, sFrame);          // the network layer: the merged frame of this tick
	}
	inputApplyLocal(sFrame);                         // key events into the game's tables, the mouse
	ms::latchCommit(s_latch, sFrame, inputMogCells().pubKeyDown, ulVbls);
	if(s_isPadsOverridden) {                         // the network layer's merged controllers (it decided who steers whom)
		s_latch.sPads = s_overridePads;
		s_isPadsOverridden = false;
	}
	*inputMogCells().pulFrame = s_latch.ulClock;     // what every reader of LAB_0B9D sees
	rtCuiCursorStep(ulVbls);                         // the cursor screens' joystick cursor, now a function of the tick
}

const ms::TickLatch &netLatch() {
	return s_latch;
}

ULONG netClock() {
	return s_latch.ulClock;
}

UWORD netBeam() {
	return static_cast<UWORD>(s_latch.ulClock);
}

void netSetFrameHook(NetFrameHook pfnHook) {
	s_pfnFrame = pfnHook;
}

void netOverridePads(const ms::PadFrame &sPads) {
	s_overridePads = sPads;
	s_isPadsOverridden = true;
}

void netCountPaceFrame() {
	++s_ulPace;
}

ULONG netPaceFrame() {
	return s_ulPace;
}

void netSetSeedHook(NetSeedHook pfnHook) {
	s_pfnSeed = pfnHook;
}

void netSeedProvide(ULONG ulSeed) {
	s_ulSeedOnce = ulSeed;
	s_isSeedOnce = true;
}

bool netSeedTake(ULONG &ulSeed) {
	if(s_isSeedOnce) {
		ulSeed = s_ulSeedOnce;
		s_isSeedOnce = false;
		return true;
	}
	return s_pfnSeed && s_pfnSeed(ulSeed);
}

void netSeedAtTick(ULONG ulTick, ULONG ulSeed) {
	s_ulSeedAtTick = ulTick;
	s_ulSeedAtValue = ulSeed;
	s_isSeedAt = true;
}

ms::KeyQueue &netKeyQueue() {
	return s_keys;
}

}  // namespace rt
