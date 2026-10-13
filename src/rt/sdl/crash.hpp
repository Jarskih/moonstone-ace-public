// rt/sdl/crash - the PC crash reporter and the clean-exit hooks behind rt::platform::crashReporterInstall (ROADMAP 11.9, docs/PC_PORT.md 5.7).
//
// Install (crashReporterInstall):
//   * an unhandled structured exception (Windows) or a fatal signal (SIGSEGV, SIGFPE, SIGILL, SIGBUS elsewhere; SIGABRT everywhere) writes a
//     text report - what happened, the fault address as module+offset, the registers, the return addresses found on the stack - to
//     crash.log next to the executable (rt/sdl/log logDirectory), to moonstone.log and to stderr, shows a message box naming the file unless
//     the run is quiet (uiSetQuiet), and ends the process.  There is no minidump and no symbol lookup: the offsets are resolved against the
//     map of the build that crashed (clang -Wl,/MAP or llvm-symbolizer with the PDB).
//   * Ctrl+C, Ctrl+Break, closing the console window, logoff and shutdown leave the game cleanly: the handler posts SDL_QUIT, the frame
//     loop returns and the guards of main run.  A second request while the first is still pending ends the process at once (a stuck game
//     must stay killable).
#pragma once
#include "engine/guard.hpp"

namespace rt {
namespace sdl {

// crashReporterInstall() ... crashReporterRemove().
class CrashReporterScope {
public:
	CrashReporterScope();
	~CrashReporterScope();
	CrashReporterScope(const CrashReporterScope &) = delete;
	CrashReporterScope &operator=(const CrashReporterScope &) = delete;
	CrashReporterScope(CrashReporterScope &&) = delete;
	CrashReporterScope &operator=(CrashReporterScope &&) = delete;
};
MS_GUARD_PINNED(CrashReporterScope);

// The text of a report for a fatal condition: szWhat on the first line, then the process facts.  Written to the three sinks above.
// Exposed for tests (--crash-test) and for callers that detect a fatal condition themselves.
void crashWriteReport(const char *szReport);

// --crash-test KIND: make the condition happen on purpose.  access = write through a null pointer, abort = abort(), fatal = rt::fatal(),
// quit = post SDL_QUIT.  Returns for "quit" and "fatal".  False for an unknown KIND.
bool crashRaiseTest(const char *szKind);

}  // namespace sdl
}  // namespace rt
