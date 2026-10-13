// RT_FN(rt_x): byte pointer to an asm-callable rt_* entry (include/rt/abs.h) - what `mog_LAB_xxxx` / `prg_LAB_xxxx` was while the
// label was a one-instruction `JMP rt_x` stub (ROADMAP 7.1o, tools/stub_cutover.py).  JSR RT_FN(rt_x) == JSR of the old label.
#pragma once
#include <stdint.h>

#include "rt/abs.h"

#ifdef __cplusplus
#define RT_FN(f) (reinterpret_cast<uint8_t *>(&(f)))
#endif

// RT_HOOK(rt_x, rtXC): the address a VBL hook list / handler table holds for the routine `rt_x` whose C body is `rtXC`.  The Amiga build keeps the
// asm entry (its register shim); the PC build has no register shims, so the list holds the C body itself (the hook dispatcher calls it with the C
// convention either way, rt/irq.cpp runHooks; ROADMAP 11.10).
#if defined(MS_PLATFORM_SDL)
#define RT_HOOK(entry, cbody) (reinterpret_cast<uint8_t *>(&(cbody)))
#else
#define RT_HOOK(entry, cbody) RT_FN(entry)
#endif
