// rt/sdl/log - the PC log sink behind rt::platform::log (ROADMAP 11.4): every line goes to stderr and to moonstone.log next to the
// executable.  LogSession opens the file for the duration of the run (a guard: the file is closed on every exit path).
#pragma once
#include "engine/guard.hpp"

namespace rt {
namespace sdl {

// Open ("moonstone.log" in the directory of the executable, rewritten each run) / close the log file.  Lines logged while no file
// is open still reach stderr.
void logOpen();
void logClose();

// logOpen() ... logClose().
class LogSession {
public:
	LogSession() { logOpen(); }
	~LogSession() { logClose(); }
	LogSession(const LogSession &) = delete;
	LogSession &operator=(const LogSession &) = delete;
	LogSession(LogSession &&) = delete;
	LogSession &operator=(LogSession &&) = delete;
};
MS_GUARD_PINNED(LogSession);

// The directory the log files live in (with trailing '/'; empty = the working directory).  Valid after logOpen, also when the log file
// itself could not be created.  crash.log is written here too.
const char *logDirectory();

// Quiet = no message boxes (headless runs, tests: a dialog nobody can click would hang the run).  Errors still go to stderr and the log.
void uiSetQuiet(bool isQuiet);
bool uiIsQuiet();

// A platform function that is not implemented yet was called: logs "STUB <name> (ROADMAP <task>)" the first time per call site.
// Used by the stubs of src/rt/sdl/*.cpp, each of which carries a `TODO(11.x)` naming the task that replaces it.
void stubHit(const char *szName, const char *szTask, bool &isReported);

}  // namespace sdl
}  // namespace rt

// One line in the body of a stub: MS_STUB("11.6") reports the enclosing function once per run.
#define MS_STUB(task) do { static bool s_isReported = false; ::rt::sdl::stubHit(__func__, task, s_isReported); } while(0)
