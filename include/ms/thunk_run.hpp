// ms/thunk_run.hpp - register marshalling core of the asm -> C++ thunks (ROADMAP 3.2b).
//
// Header-only and host-compilable (clang++ in tests/test_thunks.py, m68k g++ in src/rt/thunks.cpp).
// The generated thunk (tools/thunks.py) saves the 68k register file into a ThunkFrame on the
// stack and calls ms_thunk_run(frame, lab_<LABEL>, outMask, spDelta) -> thunkRun().  thunkRun
// seeds a Regs from the frame (ALL registers + CCR are inputs), runs the lifted body and copies
// back only the contract outputs; every other register keeps its entry value and the thunk
// reloads the whole file from the frame afterwards.
//
// outMask: bit i (0..7 = D0..D7, 8..14 = A0..A6) = register i is an output; THUNK_OUT_CCR = the CCR
// is an output (otherwise the entry CCR is restored).  spDelta: bytes the routine pops beyond its
// own return address (contract `sp_delta`); a lifted body must leave a[7] = vsp + spDelta.
#pragma once
#include "ms/regs.hpp"

namespace ms {

enum : uint32_t { THUNK_OUT_CCR = 1u << 15 };

// Layout shared with the generated asm (tools/thunks.py: F_VSP/F_CCR/F_RET/F_SIZE).
struct ThunkFrame {
    uint32_t reg[15];   // d0-d7, a0-a6 at entry; contract outputs overwritten by thunkRun
    uint32_t vsp;       // a[7] as the lifted code sees it: SP after the return address pop
    uint16_t ccr;       // CCR at entry (low 5 bits); output CCR if the contract says so
    uint16_t pad;
    uint32_t ret;       // copy of the return address (the lifted code may scribble on its slot)
};
static_assert(sizeof(ThunkFrame) == 72, "thunk frame layout is baked into tools/thunks.py");
static_assert(__builtin_offsetof(ThunkFrame, vsp) == 60 && __builtin_offsetof(ThunkFrame, ccr) == 64 &&
              __builtin_offsetof(ThunkFrame, ret) == 68, "thunk frame layout is baked into tools/thunks.py");

typedef void (*LiftedFn)(Regs&, Mem&);

// Returns false when the lifted body left the stack unbalanced against the contract.
inline bool thunkRun(ThunkFrame& F, LiftedFn fn, uint32_t outMask, uint32_t spDelta, Mem& mem) {
    Regs R;
    for (int i = 0; i < 8; i++) R.d[i] = F.reg[i];
    for (int i = 0; i < 7; i++) R.a[i] = F.reg[8 + i];
    R.a[7] = F.vsp;
    R.sr = (uint16_t)(F.ccr & CCR_MASK);
    fn(R, mem);
    for (int i = 0; i < 8; i++) if (outMask & (1u << i)) F.reg[i] = R.d[i];
    for (int i = 0; i < 7; i++) if (outMask & (1u << (8 + i))) F.reg[8 + i] = R.a[i];
    if (outMask & THUNK_OUT_CCR) F.ccr = (uint16_t)(R.sr & CCR_MASK);
    return R.a[7] == F.vsp + spDelta;
}

} // namespace ms
