// rt/net_tick - the deterministic mode for network play (ROADMAP 12.4, docs/NETPLAY.md 1.1c, 1.2, 4.1; pure half: engine/nettick).
//
// ONE flag, netDeterministic(). OFF (the default, and always in the program overlay: intro and ending are outside a session) the game
// is bit-identical to classic play: every hook below is a branch on the flag and nothing else. ON, the game runs in ticks:
//
//   * netTick(n) is called at every frame boundary of the main thread: rt::displayWaitBeam (n = 1 VBL) and the frame wait of the
//     fight / map loop (n = the VBLs it stands for, rtMogFrameWait). It latches ALL input for the tick: both joystick words (and
//     the autoplay override), the pad frame, the key events the keyboard interrupt queued (applied to mogKeyDown / the last-key
//     cell only here), the mouse. Polls, key reads and cursor reads between two ticks see that state only.
//   * the logical clock (the value of mogFrame, LAB_0B9D) advances at the tick, not in the VBL interrupt. The interrupt keeps a
//     private pacing counter (netPaceFrame) so the frame wait still waits real VBLs and the game runs at its normal speed.
//   * the joystick cursor (cursor screens) steps from the tick: 2 px per VBL the tick stands for (engine/nettick kCursorPixelsPerVbl).
//   * the random seed can be provided (netSeedProvide / netSetSeedHook) instead of VHPOSR, and set at an arbitrary tick.
//
// The seam for the network layer (12.6..12.8): netSetFrameHook(). The tick builds the LOCAL frame, hands it to the hook, and applies
// whatever the hook leaves in it (the hook may block until the merged frame of this tick is known and replaces the frame). No hook:
// the local frame is used as it is.
#pragma once
#include <ace/types.h>

#include "engine/nettick.hpp"

namespace rt {

// ---- the flag ----------------------------------------------------------------------------------------------------------

// The raw flag (set by the session, or by the autoplay `poke netdet 1` test hook).
bool netDeterministicFlag();
// Switch the mode. Safe from the main thread and from the VBL interrupt (the autoplay hook): everything is prepared before the flag
// goes up; switching off hands the (logical) counter back to the interrupt.
void netDeterministicSet(bool isOn);
// Switch the mode on with the logical clock at ulClock and tick 0 (a network session starts both nodes at the same clock; ROADMAP 12.8b).
// ulTick: the tick the game runs next (0 for a session start; a rejoiner starts on the host's tick, NETPLAY.md 4.5).
void netDeterministicBegin(ULONG ulClock, ULONG ulTick = 0);

// The flag AND the mog overlay is the active one (the intro and the ending of the program overlay run classic).
bool netDeterministic();

// ---- the tick -----------------------------------------------------------------------------------------------------------

// A frame boundary standing for ulVbls VBLs. No-op unless netDeterministic().
void netTick(ULONG ulVbls, ULONG ulCaller = 0);
// MS_AUTOPLAY diagnostics (no-ops otherwise): a ring of the last ticks (tick, clock before, VBLs, caller) logged when a session ends in a desync.
void netTickRingDump();
// The latch (the frame all polls of the current tick return).
const ms::TickLatch &netLatch();
// The logical frame counter (what mogFrame shows).
ULONG netClock();
// Beam-position substitute for the flyer wobble (src/rt/fighters.cpp cbBeam): the low bits of the logical clock.
UWORD netBeam();

// Network seam: called once per tick with the local frame, may block and may replace it. ulTick = the tick about to run.
typedef void (*NetFrameHook)(ULONG ulTick, ms::LocalFrame &sFrame);
void netSetFrameHook(NetFrameHook pfnHook);
// Called by the frame hook: the controllers of this tick (co-op: the merged pad frame) replace the pads netTick derives from the frame.
void netOverridePads(const ms::PadFrame &sPads);

// ---- pacing (real time) -------------------------------------------------------------------------------------------------

// The interrupt's private VBL counter while the mode is on (mogFrame no longer moves in the interrupt).
void netCountPaceFrame();
ULONG netPaceFrame();

// ---- the random seed (NETPLAY 4.1) ----------------------------------------------------------------------------------------

// Source asked by every seeding event (rtMogRngSeed) before it reads the beam: true = the value to use. A client's hook blocks
// until the host's SEED arrives. One-shot values (netSeedProvide) win over the hook.
typedef bool (*NetSeedHook)(ULONG &ulSeed);
void netSetSeedHook(NetSeedHook pfnHook);
void netSeedProvide(ULONG ulSeed);
bool netSeedTake(ULONG &ulSeed);   // rtMogRngSeed: false = read the beam as the original
// Write the seed into mogRandomSeed when the tick counter reaches ulTick (session start: the same tick on every node).
void netSeedAtTick(ULONG ulTick, ULONG ulSeed);

// The keyboard interrupt's queue (rt/input pushes, netTick pops).
ms::KeyQueue &netKeyQueue();

}  // namespace rt
