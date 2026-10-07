// rt/crash - exception reporter for the original game (ROADMAP 2.4e). Replaces the CPU exception
// vectors 2..11 (bus/address error, illegal, divide, ..., line A/F) for the duration of rtGameRun.
// On any exception it pops the CPU frame, drops to user mode on a private stack, and writes
// PROGDIR:crash.log (vector, SR, PC, fault address, registers, stack words, code bytes at PC)
// through dos.library, then parks. See docs/DISPLAY.md section "2.4e".
#pragma once

namespace rt {
void crashInstall();    // patch the vectors (idempotent)
void crashTraceStart();  // set T1: record every PC into a 1024-entry ring dumped by the reporter (slow!)
void crashSetArena(unsigned i, void *p, unsigned long ulSize);  // 0 = chip arena, 1 = fast arena (reported)
unsigned long crashArenaHighWater(unsigned i);  // bytes up to the last non-zero longword of an arena
void crashMemSnap();    // hash every 256 bytes of the first 2 MB
void crashMemDiff();    // record the ranges whose hash changed since crashMemSnap (reported on a crash)
void crashRemove();     // restore the vectors saved by crashInstall
}
