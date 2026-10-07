// RT_FN(rt_x): byte pointer to an asm-callable rt_* entry (include/rt/abs.h) - what `mog_LAB_xxxx` / `prg_LAB_xxxx` was while the
// label was a one-instruction `JMP rt_x` stub (ROADMAP 7.1o, tools/stub_cutover.py).  JSR RT_FN(rt_x) == JSR of the old label.
#pragma once
#include <stdint.h>

#include "rt/abs.h"

#ifdef __cplusplus
#define RT_FN(f) (reinterpret_cast<uint8_t *>(&(f)))
#endif
