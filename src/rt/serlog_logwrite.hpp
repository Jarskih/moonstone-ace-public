// Force-included (-include <abs path>/src/rt/serlog_logwrite.hpp, in CMAKE_CXX_FLAGS) in an MS_AUTOPLAY build that has
// ACE_DEBUG off: the game's logWrite() calls then go to the serial port (rt/serlog) instead of compiling away. ACE_DEBUG
// itself is no use here: it makes ACE check the stack against its own bounds, and the game runs on its own static stack
// (src/main.cpp). ACE-internal logWrite calls (inside libace) stay silent. Does nothing when ACE_DEBUG is on or MS_AUTOPLAY
// is off, and tolerates CMake's compiler probes (no include paths there).
#pragma once
#if defined(__has_include)
#if __has_include(<ace/managers/log.h>)
#include <ace/managers/log.h>
#if defined(MS_AUTOPLAY) && MS_AUTOPLAY && !defined(ACE_DEBUG)
#include "rt/serlog.hpp"
#undef logWrite
#define logWrite(...) ::rt::serLogf(__VA_ARGS__)
#endif
#endif
#endif
