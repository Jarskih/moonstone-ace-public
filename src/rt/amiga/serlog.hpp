// rt/serlog - log lines on the Paula serial port, for headless runs. WinUAE can write the serial port to its log
// (command line -serlog, see tools/uaeshot.ps1 and docs/AUTOPLAY.md); no DOS call is needed, so it works from interrupt
// and game context. Built only with -DMS_AUTOPLAY; ACE's own logWrite reaches the same wire with -DACE_DEBUG=ON
// -DACE_DEBUG_SERIAL=ON (ACE claims the serial resource and sets SERPER itself; serLogOpen then just shares it).
#pragma once
#include <ace/types.h>

namespace rt {

// Claim the serial hardware (misc.resource) and set the baud rate. Call with the OS available (rt::systemCreate).
void serLogOpen();

// Write one zero-terminated string; no formatting, no newline added. Safe in any context, gives up after a timeout.
void serLogWrite(const char *szText);

// printf-style (192-byte stack buffer); lines from interrupt and main context can interleave on the wire.
void serLogf(const char *szFormat, ...) __attribute__((format(printf, 1, 2)));

}  // namespace rt
