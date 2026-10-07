// ms/regs.hpp - 68000 register/CCR model for lifted Moonstone routines.
//
// Header-only, no STL, no exceptions/RTTI, no global constructors. Builds with
// clang++ -std=c++17 (host replay) and m68k-amiga-elf-g++ (-fno-exceptions
// -fno-rtti). Only <stdint.h> is used (local typedefs on bare m68k).
//
// Conventions
//  * A lifted routine is `extern "C" void lab_<LABEL>(ms::Regs&, ms::Mem&)`.
//    On entry regs.a[7] is the SP *after* the return address was popped
//    (i.e. the caller's SP before its JSR); a balanced routine leaves it
//    unchanged. The routine "executes RTS" by returning.
//  * regs.sr holds only the CCR (bits 0..4: C V Z N X). System byte unused.
//  * Flag helpers take operands of the operation size (upper bits ignored)
//    and return the masked result; they update the CCR exactly as the 68000
//    does (cmp leaves X alone, logic ops leave X alone, etc).
//    Operand order follows "op src,dst": sub<L>(R, dst, src) = dst - src,
//    so CMP.L D0,D2 is cmp<L>(R, R.d[2], R.d[0]).
//  * Register writes: setB/setW preserve upper bits, setL replaces. setAW
//    sign-extends (MOVEA.W-style address register word writes).
#pragma once
#if defined(__m68k__)
// The bare m68k-amiga-elf toolchain ships no libc headers; the Amiga build gets
// stdint via ACE. Redeclaring the same underlying types is harmless either way.
typedef __UINT8_TYPE__  uint8_t;   typedef __INT8_TYPE__  int8_t;
typedef __UINT16_TYPE__ uint16_t;  typedef __INT16_TYPE__ int16_t;
typedef __UINT32_TYPE__ uint32_t;  typedef __INT32_TYPE__ int32_t;
typedef __UINT64_TYPE__ uint64_t;
#else
#include <stdint.h>
#endif

namespace ms {

// ---- CCR bits ------------------------------------------------------------
enum : uint16_t { CCR_C = 1, CCR_V = 2, CCR_Z = 4, CCR_N = 8, CCR_X = 16, CCR_MASK = 31 };

// ---- size tags -----------------------------------------------------------
struct B { static constexpr unsigned bits = 8;  static constexpr uint32_t mask = 0xFFu; };
struct W { static constexpr unsigned bits = 16; static constexpr uint32_t mask = 0xFFFFu; };
struct L { static constexpr unsigned bits = 32; static constexpr uint32_t mask = 0xFFFFFFFFu; };
template <class S> constexpr uint32_t msb() { return 1u << (S::bits - 1); }

// ---- register file -------------------------------------------------------
struct Regs {
    uint32_t d[8];
    uint32_t a[8];   // a[7] = SP
    uint16_t sr;     // CCR only (see above)

    bool flag(uint16_t f) const { return (sr & f) != 0; }
    bool X() const { return flag(CCR_X); }
    bool N() const { return flag(CCR_N); }
    bool Z() const { return flag(CCR_Z); }
    bool V() const { return flag(CCR_V); }
    bool C() const { return flag(CCR_C); }
    void setFlag(uint16_t f, bool on) { sr = (uint16_t)(on ? (sr | f) : (sr & ~f)); }
    void setCCR(uint16_t ccr) { sr = (uint16_t)(ccr & CCR_MASK); }
    // Set N,Z,V,C at once (X untouched).
    void setNZVC(bool n, bool z, bool v, bool c) {
        sr = (uint16_t)((sr & CCR_X) | (n ? CCR_N : 0) | (z ? CCR_Z : 0) |
                        (v ? CCR_V : 0) | (c ? CCR_C : 0));
    }
};

// ---- sized register writes (upper bits preserved like the 68k) -------------
inline void setB(uint32_t& r, uint32_t v) { r = (r & 0xFFFFFF00u) | (v & 0xFFu); }
inline void setW(uint32_t& r, uint32_t v) { r = (r & 0xFFFF0000u) | (v & 0xFFFFu); }
inline void setL(uint32_t& r, uint32_t v) { r = v; }
inline void setAW(uint32_t& a, uint32_t v) { a = (uint32_t)(int32_t)(int16_t)(uint16_t)v; }
template <class S> inline void setSz(uint32_t& r, uint32_t v) {
    if (S::bits == 8) setB(r, v); else if (S::bits == 16) setW(r, v); else r = v;
}
inline uint32_t sext8(uint32_t v)  { return (uint32_t)(int32_t)(int8_t)(uint8_t)v; }
inline uint32_t sext16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)(uint16_t)v; }

// ---- flag-computing ALU helpers ------------------------------------------
// ADD: X=C=carry, V=signed overflow.
template <class S> inline uint32_t add(Regs& r, uint32_t dst, uint32_t src) {
    dst &= S::mask; src &= S::mask;
    uint64_t t = (uint64_t)dst + src;
    uint32_t res = (uint32_t)t & S::mask;
    bool c = ((t >> S::bits) & 1) != 0;
    bool v = ((~(dst ^ src) & (dst ^ res)) & msb<S>()) != 0;
    r.setNZVC((res & msb<S>()) != 0, res == 0, v, c);
    r.setFlag(CCR_X, c);
    return res;
}
// ADDX: ADD plus X in; Z is cleared if result != 0, otherwise unchanged.
template <class S> inline uint32_t addx(Regs& r, uint32_t dst, uint32_t src) {
    dst &= S::mask; src &= S::mask;
    uint64_t t = (uint64_t)dst + src + (r.X() ? 1 : 0);
    uint32_t res = (uint32_t)t & S::mask;
    bool c = ((t >> S::bits) & 1) != 0;
    bool v = ((~(dst ^ src) & (dst ^ res)) & msb<S>()) != 0;
    bool z = (res == 0) && r.Z();
    r.setNZVC((res & msb<S>()) != 0, z, v, c);
    r.setFlag(CCR_X, c);
    return res;
}
// SUB: dst - src. X=C=borrow.
template <class S> inline uint32_t sub(Regs& r, uint32_t dst, uint32_t src) {
    dst &= S::mask; src &= S::mask;
    uint32_t res = (dst - src) & S::mask;
    bool c = src > dst;
    bool v = (((dst ^ src) & (dst ^ res)) & msb<S>()) != 0;
    r.setNZVC((res & msb<S>()) != 0, res == 0, v, c);
    r.setFlag(CCR_X, c);
    return res;
}
template <class S> inline uint32_t subx(Regs& r, uint32_t dst, uint32_t src) {
    dst &= S::mask; src &= S::mask;
    uint32_t xin = r.X() ? 1 : 0;
    uint32_t res = (dst - src - xin) & S::mask;
    bool c = ((uint64_t)src + xin) > dst;
    bool v = (((dst ^ src) & (dst ^ res)) & msb<S>()) != 0;
    bool z = (res == 0) && r.Z();
    r.setNZVC((res & msb<S>()) != 0, z, v, c);
    r.setFlag(CCR_X, c);
    return res;
}
// CMP: flags of dst - src, X unchanged, result discarded.
template <class S> inline void cmp(Regs& r, uint32_t dst, uint32_t src) {
    dst &= S::mask; src &= S::mask;
    uint32_t res = (dst - src) & S::mask;
    bool v = (((dst ^ src) & (dst ^ res)) & msb<S>()) != 0;
    r.setNZVC((res & msb<S>()) != 0, res == 0, v, src > dst);
}
// TST / MOVE-style: N,Z from value, V=C=0, X unchanged.
template <class S> inline uint32_t tst(Regs& r, uint32_t v) {
    v &= S::mask;
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, false);
    return v;
}
template <class S> inline uint32_t move(Regs& r, uint32_t v) { return tst<S>(r, v); }
template <class S> inline uint32_t and_(Regs& r, uint32_t a, uint32_t b) { return tst<S>(r, a & b); }
template <class S> inline uint32_t or_ (Regs& r, uint32_t a, uint32_t b) { return tst<S>(r, a | b); }
template <class S> inline uint32_t eor (Regs& r, uint32_t a, uint32_t b) { return tst<S>(r, a ^ b); }
template <class S> inline uint32_t not_(Regs& r, uint32_t a)             { return tst<S>(r, ~a); }
// NEG: 0 - v. X=C = (v != 0), V when v == min.
template <class S> inline uint32_t neg (Regs& r, uint32_t v) { return sub<S>(r, 0, v); }
template <class S> inline uint32_t negx(Regs& r, uint32_t v) { return subx<S>(r, 0, v); }

// ---- shifts / rotates -------------------------------------------------------
// `cnt` is the effective count (immediate 1..8, or register value, taken mod 64).
// cnt==0: C cleared, X unchanged, V cleared. Implemented as a loop (cnt <= 63)
// so the over-width edge cases match the CPU.
template <class S> inline uint32_t lsl(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool c = false;
    if (cnt == 0) { r.setNZVC((v & msb<S>()) != 0, v == 0, false, false); return v; }
    for (uint32_t i = 0; i < cnt; i++) { c = (v & msb<S>()) != 0; v = (v << 1) & S::mask; }
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, c); r.setFlag(CCR_X, c); return v;
}
template <class S> inline uint32_t lsr(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool c = false;
    if (cnt == 0) { r.setNZVC((v & msb<S>()) != 0, v == 0, false, false); return v; }
    for (uint32_t i = 0; i < cnt; i++) { c = (v & 1) != 0; v >>= 1; }
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, c); r.setFlag(CCR_X, c); return v;
}
template <class S> inline uint32_t asl(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool c = false, ov = false;
    if (cnt == 0) { r.setNZVC((v & msb<S>()) != 0, v == 0, false, false); return v; }
    for (uint32_t i = 0; i < cnt; i++) {
        c = (v & msb<S>()) != 0; uint32_t n = (v << 1) & S::mask;
        if ((n ^ v) & msb<S>()) ov = true;
        v = n;
    }
    r.setNZVC((v & msb<S>()) != 0, v == 0, ov, c); r.setFlag(CCR_X, c); return v;
}
template <class S> inline uint32_t asr(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool c = false;
    if (cnt == 0) { r.setNZVC((v & msb<S>()) != 0, v == 0, false, false); return v; }
    for (uint32_t i = 0; i < cnt; i++) { c = (v & 1) != 0; v = (v >> 1) | (v & msb<S>()); }
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, c); r.setFlag(CCR_X, c); return v;
}
template <class S> inline uint32_t rol(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool c = false;
    for (uint32_t i = 0; i < cnt; i++) { c = (v & msb<S>()) != 0; v = ((v << 1) | (c ? 1u : 0u)) & S::mask; }
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, c);   // X unchanged
    return v;
}
template <class S> inline uint32_t ror(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool c = false;
    for (uint32_t i = 0; i < cnt; i++) { c = (v & 1) != 0; v = (v >> 1) | (c ? msb<S>() : 0u); }
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, c);
    return v;
}
template <class S> inline uint32_t roxl(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool x = r.X();
    for (uint32_t i = 0; i < cnt; i++) { bool o = (v & msb<S>()) != 0; v = ((v << 1) | (x ? 1u : 0u)) & S::mask; x = o; }
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, x); r.setFlag(CCR_X, x); return v;
}
template <class S> inline uint32_t roxr(Regs& r, uint32_t v, uint32_t cnt) {
    v &= S::mask; cnt &= 63; bool x = r.X();
    for (uint32_t i = 0; i < cnt; i++) { bool o = (v & 1) != 0; v = (v >> 1) | (x ? msb<S>() : 0u); x = o; }
    r.setNZVC((v & msb<S>()) != 0, v == 0, false, x); r.setFlag(CCR_X, x); return v;
}

// ---- MULU/MULS (flags per 68000 manual) -------------------------------------
inline uint32_t mulu(Regs& r, uint32_t d, uint32_t s) {
    uint32_t res = (d & 0xFFFFu) * (s & 0xFFFFu);
    r.setNZVC((res & 0x80000000u) != 0, res == 0, false, false); return res;
}
inline uint32_t muls(Regs& r, uint32_t d, uint32_t s) {
    uint32_t res = (uint32_t)((int32_t)(int16_t)d * (int32_t)(int16_t)s);
    r.setNZVC((res & 0x80000000u) != 0, res == 0, false, false); return res;
}

// ---- condition codes (Bcc/Scc/DBcc) -------------------------------------------
enum Cond { T, F, HI, LS, CC, CS, NE, EQ, VC, VS, PL, MI, GE, LT, GT, LE };
inline bool cond(const Regs& r, Cond c) {
    bool cf = r.C(), v = r.V(), z = r.Z(), n = r.N();
    switch (c) {
    case T:  return true;        case F:  return false;
    case HI: return !cf && !z;   case LS: return cf || z;
    case CC: return !cf;         case CS: return cf;
    case NE: return !z;          case EQ: return z;
    case VC: return !v;          case VS: return v;
    case PL: return !n;          case MI: return n;
    case GE: return n == v;      case LT: return n != v;
    case GT: return !z && n == v; case LE: return z || n != v;
    }
    return false;
}

// ---- big-endian memory -----------------------------------------------------
// Addresses are 68k addresses (uint32_t). Host implementations are
// buffer-backed (tests/diff/replay_main.cpp); on the Amiga use DirectMem.
struct Mem {
    virtual uint8_t  r8 (uint32_t a) = 0;
    virtual void     w8 (uint32_t a, uint8_t v) = 0;
    virtual uint16_t r16(uint32_t a) { return (uint16_t)((r8(a) << 8) | r8(a + 1)); }
    virtual uint32_t r32(uint32_t a) { return ((uint32_t)r16(a) << 16) | r16(a + 2); }
    virtual void     w16(uint32_t a, uint16_t v) { w8(a, (uint8_t)(v >> 8)); w8(a + 1, (uint8_t)v); }
    virtual void     w32(uint32_t a, uint32_t v) { w16(a, (uint16_t)(v >> 16)); w16(a + 2, (uint16_t)v); }
    // Stack helpers (a[7] pre-decrement / post-increment).
    void push32(Regs& r, uint32_t v) { r.a[7] -= 4; w32(r.a[7], v); }
    void push16(Regs& r, uint16_t v) { r.a[7] -= 2; w16(r.a[7], v); }
    uint32_t pop32(Regs& r) { uint32_t v = r32(r.a[7]); r.a[7] += 4; return v; }
    uint16_t pop16(Regs& r) { uint16_t v = r16(r.a[7]); r.a[7] += 2; return v; }
protected:
    ~Mem() {}
};

#if defined(__m68k__)
// Amiga build: 68k addresses are real pointers and memory is already big-endian.
struct DirectMem : Mem {
    uint8_t  r8 (uint32_t a) override { return *(volatile uint8_t*)a; }
    void     w8 (uint32_t a, uint8_t v) override { *(volatile uint8_t*)a = v; }
    uint16_t r16(uint32_t a) override { return *(volatile uint16_t*)a; }
    uint32_t r32(uint32_t a) override { return *(volatile uint32_t*)a; }
    void     w16(uint32_t a, uint16_t v) override { *(volatile uint16_t*)a = v; }
    void     w32(uint32_t a, uint32_t v) override { *(volatile uint32_t*)a = v; }
};
#endif

} // namespace ms
