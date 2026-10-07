// rt/system - ACE manager bring-up/tear-down.
#pragma once

#include <ace/types.h>

namespace rt {

// Creates ACE managers (system, log, memory, timer, blitter, copper).
void systemCreate();
void systemDestroy();

// Boot diagnostics (ROADMAP 2.11). bootLog: one line (ending in a newline) to the ACE log (serial with MS_AUTOPLAY) and to
// PROGDIR:boot.log (rewritten each run). bootLogMemory: free chip / fast and the largest free blocks after `szWhen`.
void bootLog(const char *szLine);
void bootLogMemory(const char *szWhen);

// Fail loudly: bootLog the message, print it to the shell's output, or show a recoverable Intuition alert when there is no
// console (icon start). Call before leaving the game for good; the OS is handed back for the duration.
void fatal(const char *szMsg);

}  // namespace rt
