// rt/irq - the original game's interrupt work under ACE (ROADMAP 2.2, 7.1d). Design: docs/IRQ.md.
#pragma once
#include "rt/input.hpp"

namespace rt {

// Bind the overlay's cells, register the level-3 (VBL/BLIT/COPER) and level-4 (audio) trampolines with ACE and the
// keyboard callback with rt/input. Called once per overlay from the patched LAB_0325 (program) / LAB_0B49 (mog).
void irqInstall(const IrqCells &sCells);

// The level-3 handler body (program LAB_0331 / mog LAB_0B55), called by the trampoline on its private stack.
void irqLevel3();

// The level-4 handler when the synth has not installed its own (program LAB_0337 / mog LAB_0B5B): acks the audio bits.
void irqLevel4Default();

// Replace the level-4 (audio) handler; the original swaps in the ST/NT player's handler this way.
void irqSetInt4(void *pHandler);

// Level-3 VERTB services since boot (counts only while the game owns the interrupt; rt/perf, ROADMAP 7.2).
unsigned long irqVblCount();

// Unhook everything from ACE (not needed while the game never returns).
void irqRemove();

#ifdef MS_IRQ_TRACE
// Debug trace queue (kind 0 = VBL status line, 1 = key event); flushed to PROGDIR:irq.log by rt_irq_disable.
void traceEvent(unsigned long ulKind, unsigned long ulA, unsigned long ulB, unsigned long ulC);
void traceFlush();
#endif

}  // namespace rt
