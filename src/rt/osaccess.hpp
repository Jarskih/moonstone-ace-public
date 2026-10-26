// rt/osaccess - the "OS window" bracket of the platform-neutral rt files (ROADMAP 11.9).
//
// Amiga: rt::OsAccess of rt/guards.hpp (systemUse, blitter handed to the OS; every dos.library call brackets itself the same way).
// PC: nothing to give back, so the guard does nothing; it keeps the call sites identical to the Amiga ones.
#pragma once

#if defined(__has_include) && __has_include(<ace/managers/memory.h>)
#include "rt/guards.hpp"
#else
#include "engine/guard.hpp"

namespace rt {

class OsAccess {
public:
	OsAccess() {}
	~OsAccess() {}
	OsAccess(const OsAccess &) = delete;
	OsAccess &operator=(const OsAccess &) = delete;
	OsAccess(OsAccess &&) = delete;
	OsAccess &operator=(OsAccess &&) = delete;
};
MS_GUARD_PINNED(OsAccess);

}  // namespace rt
#endif
