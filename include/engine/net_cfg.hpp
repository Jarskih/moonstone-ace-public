// engine/net_cfg - MS_NET: network play compiled in (1) or not (0).  The Amiga build defines it from the CMake option MS_NET (OFF by default:
// the stock A1200 cannot hold the net code, docs/MEMORY.md "MS_NET"); the PC build (cmake/sdl.cmake) always defines 1.  Host test drivers,
// which compile single files without the build's definitions, see the full game: the default is 1.
#pragma once

#if !defined(MS_NET)
#define MS_NET 1
#endif
