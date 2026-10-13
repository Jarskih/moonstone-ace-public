// rt/platform/log - diagnostics: log lines, the boot log, fatal errors, the crash reporter (ROADMAP 11.1, 11.9; docs/PC_PORT.md section 4).
//
// Today three paths exist side by side: ACE's logWrite macro (37 call sites in src/rt, compiled away without ACE_DEBUG, serial with
// MS_AUTOPLAY), rt/serlog (the Paula serial port, MS_AUTOPLAY), and the PROGDIR: log files (boot.log, files.log, mods.log, crash.log).
// The names are logLine/logFormat on purpose: logWrite is a preprocessor macro in ACE and would rewrite a declaration of the same name.
//   Amiga  : src/rt/amiga/ (serlog.cpp for the serial wire, system.cpp for the boot log and the Intuition alert, crash.cpp).
//   PC     : stderr plus a log file next to the executable; the crash reporter is a structured-exception / signal handler.
#pragma once
#include <stdint.h>

namespace rt {
namespace platform {

// One zero-terminated string, no formatting, no newline added. Safe in any context (interrupt included); gives up rather than block.
void logLine(const char *szText);
// printf-style (192-byte buffer); lines from interrupt and main context may interleave.
void logFormat(const char *szFormat, ...) __attribute__((format(printf, 1, 2)));

// Boot diagnostics (rt::bootLog): one line ending in a newline, to the log wire and to boot.log (rewritten each run).
void bootLog(const char *szLine);
// Free memory by kind after szWhen (rt::bootLogMemory), as one boot-log line.
void bootLogMemory(const char *szWhen);

// Fail loudly: log the message and show it where the user can see it (console output, or an alert when started from an icon).
// Called before leaving the game for good.
void fatal(const char *szMessage);

// The crash reporter for the duration of the run (rt::crashInstall / crashRemove): a hardware exception (Amiga) or a fault signal (PC)
// writes crash.log with whatever the platform can say about the faulting place and parks / exits.
void crashReporterInstall();
void crashReporterRemove();

}  // namespace platform
}  // namespace rt
