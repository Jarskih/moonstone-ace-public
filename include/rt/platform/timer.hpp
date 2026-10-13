// rt/platform/timer - the 50 Hz heartbeat, the beam and the interrupt master (ROADMAP 11.1, 11.5; docs/PC_PORT.md section 4, docs/IRQ.md).
//
// The original's interrupt work is two events: the vertical blank (frame counter, mouse pointer sample, POTGO, the VBL hook list,
// the scripted-input tick) and the blitter-done flag. rt/irq.cpp holds that work (irqLevel3: neutral C++ over the game's cells);
// what the platform provides is the event, the beam position the game polls, and the master switch.
//   Amiga  : src/rt/amiga/ (ACE systemSetInt, private-stack trampolines, VHPOSR/VPOSR, INTENA master bit).
//   PC     : an SDL timer / frame pacer calls the handler 50 times a second; the beam is derived from the time since the last tick.
#pragma once
#include <stdint.h>

namespace rt {
namespace platform {

typedef void (*TickFn)();

// Register the vertical-blank handler (VERTB, level 3): called from interrupt context at 50 Hz (60 Hz NTSC), on its own stack on the
// Amiga. nullptr unregisters. (irqInstall / irqRemove.)
void timerSetVblankHandler(TickFn pfnHandler);
// Register the blitter-done handler (BLIT, level 3). nullptr unregisters.
void timerSetBlitDoneHandler(TickFn pfnHandler);

// VBLs since the handler was installed (irqVblCount).
uint32_t timerVblankCount();

// The raster position the game reads: VPOSR bit 0 and VHPOSR high byte = line 0..312 (perf, irq selftest); the raw VHPOSR word is the
// "random" seed of mog's fighters (fighters.cpp beamPosition; ROADMAP 12.3 makes it a session value in netplay).
uint16_t timerBeamLine();
uint16_t timerBeamVhposr();

// Wait until the beam is on line ubLine (8-bit, the high byte of VHPOSR) and has left it again: the once-per-frame sync of
// displayWaitBeam (line $F5). pfnWhileWaiting (may be null) is called in the wait loop before the line is reached (the program
// overlay's intro-skip poll).
void timerWaitBeam(uint8_t ubLine, TickFn pfnWhileWaiting);

// One idle step of a loop that waits for something an interrupt delivers (a key press, the frame counter) without waiting for the beam
// (ms::tickIdle).  The Amiga's interrupts run by themselves, so there is nothing to do; the PC delivers the 50 Hz boundary and the SDL events from
// here (and yields the CPU while it waits for the next one).
void timerIdle();

// The interrupt master (INTENA bit 14): off for the short critical sections of rt_irq_disable / rt_irq_enable.
void timerMasterIrq(bool isEnabled);

// Scope guard for timerMasterIrq(false) ... timerMasterIrq(true) (docs/RAII.md). The original's brackets are flat, so it does not nest.
class MasterIrqOff {
public:
	MasterIrqOff() { timerMasterIrq(false); }
	~MasterIrqOff() { timerMasterIrq(true); }
	MasterIrqOff(const MasterIrqOff &) = delete;
	MasterIrqOff &operator=(const MasterIrqOff &) = delete;
	MasterIrqOff(MasterIrqOff &&) = delete;
	MasterIrqOff &operator=(MasterIrqOff &&) = delete;
};

}  // namespace platform
}  // namespace rt
