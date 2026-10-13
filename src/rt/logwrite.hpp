// rt/logwrite - `logWrite(fmt, ...)` for the platform-neutral rt files (ROADMAP 11.1d).
//
// Amiga: ACE's own macro from <ace/managers/log.h> (compiled away without ACE_DEBUG; with MS_AUTOPLAY the force-included
// serlog_logwrite.hpp sends it to the serial wire).  Anywhere ACE is not on the include path (the PC build, the host ratchet
// test of tests/test_host_rt.py) it is rt::platform::logFormat (include/rt/platform/log.hpp), the stderr + log-file sink.
#pragma once
#if defined(__has_include) && __has_include(<ace/managers/log.h>)
#include <ace/managers/log.h>
#else
#include "rt/platform/log.hpp"
#define logWrite(...) ::rt::platform::logFormat(__VA_ARGS__)
#endif
