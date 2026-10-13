// rt/chipalloc - zero-filled chip memory for the platform-neutral rt files (ROADMAP 11.10).  Amiga: ACE's memAllocChipClear / memFree (as the callers
// had them); PC: the emulated chip RAM (rt/platform/system.hpp allocChip).  Same convention as rt/logwrite.hpp: selected by the ACE header being there.
#pragma once
#include <stdint.h>

#if defined(__has_include) && __has_include(<ace/managers/memory.h>)
#include <ace/managers/memory.h>
namespace rt {
inline void *chipAllocClear(uint32_t ulBytes) { return memAllocChipClear(ulBytes); }
inline void chipFree(void *pBlock, uint32_t ulBytes) { memFree(pBlock, ulBytes); }
}
#else
#include "rt/platform/system.hpp"
namespace rt {
inline void *chipAllocClear(uint32_t ulBytes) { return platform::allocChip(ulBytes); }
inline void chipFree(void *pBlock, uint32_t ulBytes) { platform::freeMem(pBlock, ulBytes); }
}
#endif
