// rt/game - enters the original game (asm/program.s + asm/mog.s) from C++.
// (see docs/BOOT_CHAIN.md)
#pragma once

// Runs the overlay chain the original bootstrap ran: program -> mog (-> program for the
// ending/diag pass -> mog ...). Allocates the two free-memory arenas the loaders handed to
// each overlay, then calls prg_SECSTRT_0 / mog_SECSTRT_0 with the registers the original
// loader set. Returns only if an overlay's entry routine returns (the originals never do).
// M1 state: interrupts, display and file access are still logging stubs (M2), so the game
// is not expected to run correctly yet.
void rtGameRun();
