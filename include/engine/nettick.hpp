// engine/nettick - the pure half of the deterministic mode for network play (ROADMAP 12.4, docs/NETPLAY.md 1.1c, 1.2).
//
// In deterministic mode the game runs in TICKS. A tick is one frame boundary of the main thread (rt/net_tick.cpp netTick, called
// from the beam wait every main loop passes). Everything the game reads as "input" or as "time" is decided at the boundary:
//
//   LocalFrame   what one machine feeds into a tick: both joystick words, the raw JOY0DAT (mouse counters), the mouse button and
//                the key events the keyboard interrupt queued since the last tick. The network layer replaces it with the merged
//                frame (the seam, rt/net_tick.hpp NetFrameHook).
//   KeyQueue     interrupt -> main thread hand-over of key events (single producer, single consumer, lock free).
//   TickLatch    the frame the current tick sees (joysticks, pad frame) and the logical clock: the value the game's frame
//                counters (mogFrame / prgFrame, the VBL counters of the original) show to every reader.
//   cursorStep   the joystick cursor of the cursor screens (loot, shops, temple...), stepped per tick instead of per VBL.
//
// Pure: no ACE, no hardware, no globals.
#pragma once
#include <stdint.h>

#include "engine/input.hpp"
#include "engine/pad.hpp"

namespace ms {

// ---- key events ------------------------------------------------------------------------------------------------------

constexpr uint8_t kNetKeyQueueSize = 64;   // power of two; events beyond it are dropped and counted (a person types far slower)
constexpr uint8_t kNetMaxKeys = 16;        // key events one tick applies; the rest wait in the queue for the next tick

// A raw event as the keyboard interrupt sees it: (amiga key << 1) | released, the byte rt/input feeds to ms::keyDecode.
struct KeyQueue {
	volatile uint8_t aubCode[kNetKeyQueueSize];
	volatile uint8_t ubHead;         // written by the producer (the interrupt)
	volatile uint8_t ubTail;         // written by the consumer (the main thread)
	volatile uint16_t uwDropped;     // events lost because the queue was full
};

// Interrupt side. False (and uwDropped counted) when the queue is full.
bool keyQueuePush(KeyQueue &q, uint8_t ubCode);
// Main thread side. False when empty.
bool keyQueuePop(KeyQueue &q, uint8_t &ubCode);
// Forget everything (mode switch).
void keyQueueClear(KeyQueue &q);

// ---- the input frame -------------------------------------------------------------------------------------------------

struct LocalFrame {
	JoyBits joy;                 // the joystick words as the game reads them (after the autoplay override)
	uint16_t uwMouseDat;         // raw JOY0DAT: the mouse counters (ms::mouseStep)
	uint8_t ubMouseButton;       // 1 = left button down (CIA-A PRA bit 6 low)
	uint8_t ubKeys;              // key events in aubKey
	uint8_t aubKey[kNetMaxKeys]; // in arrival order
};

void localFrameClear(LocalFrame &f);
// Move up to kNetMaxKeys queued events into the frame (ubKeys is set).
void localFrameTakeKeys(LocalFrame &f, KeyQueue &q);

// ---- the tick latch --------------------------------------------------------------------------------------------------

struct TickLatch {
	LocalFrame sFrame;     // the frame all polls of the current tick return
	PadFrame sPads;        // the controllers of that frame (co-op): joysticks and the keyboard sets after this tick's key events
	uint32_t ulTick;       // ticks completed
	uint32_t ulClock;      // the logical frame counter (VBLs): the value mogFrame / prgFrame show
};

// Start (or restart) at a counter value: no frame, tick 0.
void latchReset(TickLatch &t, uint32_t ulClock);
// Close a tick: the frame becomes the latched one, the pads are derived from it and the key-down table (already updated with the
// frame's key events), the clock advances by ulVbls (the VBLs this tick stands for) and the tick count by one.
void latchCommit(TickLatch &t, const LocalFrame &f, const volatile uint8_t *pKeyDown, uint32_t ulVbls);

// ---- the joystick cursor ---------------------------------------------------------------------------------------------

// The original moves the cursor 2 pixels per VBL. One named constant: the owner may want another speed in net play (12.4).
constexpr uint16_t kCursorPixelsPerVbl = 2;
constexpr int16_t kCursorMaxX = 0x13A;
constexpr int16_t kCursorMaxY = 0xC3;

struct CursorState {
	uint16_t uwX, uwY;    // LAB_097F / LAB_0980
	uint16_t uwLock;      // LAB_0981: 1 = locked, 0 while fire is down
};

// One VBL of mog LAB_057D (the cell arithmetic and its signed clamps), uwBits = the cursor's joystick word.
void cursorStepVbl(CursorState &s, uint16_t uwBits);
// A tick of ulVbls VBLs: the same step ulVbls times (the clamp applies in between, as in the original).
void cursorStep(CursorState &s, uint16_t uwBits, uint32_t ulVbls);

// ---- the idle seam of the poll-free spin loops ------------------------------------------------------------------------

// Loops that wait for a key / fire / click without passing a beam wait (NETPLAY 1.2: the arrival menu, the fight pause) call
// tickIdle() every iteration. It does nothing unless rt installed a hook (deterministic mode: one real frame wait + a tick).
inline void (*g_pfnTickIdle)() = nullptr;   // constant-initialised (no global constructor); header-only so host drivers need no extra source
inline void tickIdle() {
	if(g_pfnTickIdle) {
		g_pfnTickIdle();
	}
}

}  // namespace ms
