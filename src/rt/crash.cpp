// rt/crash - see crash.hpp. 68020 only (exception frame formats 0/2/9/A/B).
#include "rt/crash.hpp"
#include "rt/guards.hpp"

#include <ace/managers/system.h>
#include <ace/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

extern "C" {
// Filled by the asm entry (rt_crash_entry) before the reporter runs.
__attribute__((used, externally_visible)) volatile ULONG rt_crash_regs[16];  // d0-d7, a0-a6, ssp
__attribute__((used, externally_visible)) volatile ULONG rt_crash_usp;
__attribute__((used, externally_visible)) volatile ULONG rt_crash_vec;
__attribute__((used, externally_visible)) volatile ULONG rt_crash_frame[8];   // first 32 bytes of the frame
__attribute__((used, externally_visible)) volatile ULONG rt_crash_busy;
__attribute__((used, externally_visible)) UBYTE rt_crash_stack[4096];
__attribute__((used, externally_visible)) volatile ULONG rt_trace_ring[1024];
__attribute__((used, externally_visible)) volatile ULONG rt_trace_idx;
void rt_trace_sv(void);
void rt_crash_vec2(void);
void rt_crash_report(void);
}

namespace {

// Vector 8 (privilege violation) is exec's own Supervisor() mechanism (systemUse/Unuse use it): leave it.
constexpr unsigned FIRST_VEC = 2, LAST_VEC = 11;
constexpr bool isOwned(unsigned v) { return v != 8; }  // vector 9 = trace: rt_trace_entry (below)
ULONG s_saved[LAST_VEC + 1];
bool s_isInstalled;
BPTR s_fh;
char s_line[160];
unsigned s_n;

void put(const char *p) { while(*p && s_n < sizeof(s_line) - 2) s_line[s_n++] = *p++; }
void hex(ULONG v, unsigned digits) {
	for(int i = (int)digits - 1; i >= 0; --i) {
		s_line[s_n++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
	}
}
void flush() {
	s_line[s_n++] = 10;
	if(s_fh) Write(s_fh, s_line, s_n);
	s_n = 0;
}

}  // namespace

// ---- memory diff (find who scribbles over OS memory) ----------------------------------------
namespace {
constexpr ULONG MEM_END = 0x200000, BLK = 256;
constexpr unsigned NBLK = MEM_END / BLK;
ULONG s_blkHash[NBLK];
bool s_hasSnap;
ULONG s_diffLo[64], s_diffHi[64];
unsigned s_diffCount;
ULONG s_arena[2][2];

ULONG hashBlk(unsigned i) {
	const ULONG *p = (const ULONG *)(i * BLK);
	ULONG h = 0;
	for(unsigned n = 0; n < BLK / 4; ++n) h = ((h << 1) | (h >> 31)) ^ p[n];
	return h;
}
}  // namespace

namespace rt {
void crashSetArena(unsigned i, void *p, ULONG ulSize) { s_arena[i][0] = (ULONG)p; s_arena[i][1] = ulSize; }
// Bytes of an arena up to its last non-zero longword (arenas start zeroed): the high-water mark of
// everything the game wrote there, scratch tail included. Debug aid for sizing SCRATCH_* in game.cpp.
ULONG crashArenaHighWater(unsigned i) {
	const ULONG *p = (const ULONG *)s_arena[i][0];
	ULONG n = s_arena[i][1] / 4;
	while(n && !p[n - 1]) --n;
	return n * 4;
}
void crashMemSnap() {
	for(unsigned i = 0; i < NBLK; ++i) s_blkHash[i] = hashBlk(i);
	s_hasSnap = true;
}
void crashMemDiff() {
	if(!s_hasSnap) return;
	s_diffCount = 0;
	for(unsigned i = 0; i < NBLK; ++i) {
		if(hashBlk(i) == s_blkHash[i]) continue;
		const ULONG lo = i * BLK;
		if(s_diffCount && s_diffHi[s_diffCount - 1] == lo) s_diffHi[s_diffCount - 1] = lo + BLK;
		else if(s_diffCount < 64) { s_diffLo[s_diffCount] = lo; s_diffHi[s_diffCount] = lo + BLK; ++s_diffCount; }
	}
	s_hasSnap = false;
}
}  // namespace rt

extern "C" __attribute__((used, externally_visible)) void rt_crash_c(void) {
	systemUse();
	s_fh = Open((CONST_STRPTR)"PROGDIR:crash.log", MODE_NEWFILE);
	const ULONG ulFmt = (rt_crash_frame[1] & 0xFFFF) >> 12;  // format nibble of the word at +6
	put("EXC vec="); hex(rt_crash_vec, 2);
	put(" SR="); hex(rt_crash_frame[0] >> 16, 4);
	put(" PC="); hex(((rt_crash_frame[0] & 0xFFFF) << 16) | (rt_crash_frame[1] >> 16), 8);
	put(" fmt="); hex(ulFmt, 1);
	put(" SSW="); hex(rt_crash_frame[2] & 0xFFFF, 4);
	put(" faultaddr="); hex(rt_crash_frame[4], 8);
	flush();
	for(unsigned i = 0; i < 8; ++i) {
		put("frame+"); hex(i * 4, 2); put(" "); hex(rt_crash_frame[i], 8); flush();
	}
	for(unsigned i = 0; i < 15; ++i) {
		put(i < 8 ? "D" : "A"); hex(i & 7, 1); put("="); hex(rt_crash_regs[i], 8);
		if(i & 1) flush(); else put("  ");
	}
	flush();
	put("USP="); hex(rt_crash_usp, 8); put(" SSP="); hex(rt_crash_regs[15], 8); flush();
	const ULONG ulPc = ((rt_crash_frame[0] & 0xFFFF) << 16) | (rt_crash_frame[1] >> 16);
	put("code@PC:");
	for(unsigned i = 0; i < 24; ++i) { put(" "); hex(*(const volatile UBYTE *)(ulPc + i), 2); }
	flush();
	// Stack words (USP, then SSP if the crash was in supervisor mode): return-address candidates.
	for(unsigned s = 0; s < 2; ++s) {
		const ULONG *p = (const ULONG *)(s ? rt_crash_regs[15] : rt_crash_usp - 64);
		put(s ? "SSP stack:" : "USP-64 stack:"); flush();
		for(unsigned i = 0; i < 48; ++i) {
			if(!(i & 7)) { if(i) flush(); put(" "); }
			hex(p[i], 8); put(" ");
		}
		flush();
	}
	put("ARENAS chip="); hex(s_arena[0][0], 8); put("+"); hex(s_arena[0][1], 8);
	put(" fast="); hex(s_arena[1][0], 8); put("+"); hex(s_arena[1][1], 8); flush();
	put("MEMDIFF (256-byte blocks changed between crashMemSnap and crashMemDiff):"); flush();
	for(unsigned i = 0; i < s_diffCount; ++i) { put(" "); hex(s_diffLo[i], 8); put("-"); hex(s_diffHi[i], 8); flush(); }
	if(rt_trace_idx) {
		put("TRACE (oldest first, last 1024 PCs):"); flush();
		const ULONG ulIdx = rt_trace_idx;
		const unsigned uStart = ulIdx >= 1024 ? (ulIdx & 1023) : 0;
		const unsigned uCount = ulIdx >= 1024 ? 1024 : ulIdx;
		for(unsigned i = 0; i < uCount; ++i) {
			if(!(i & 7)) { if(i) flush(); }
			hex(rt_trace_ring[(uStart + i) & 1023], 8); put(" ");
		}
		flush();
	}
	// Hunk map of this exe (runtime start, size) in segment-list order = elf2hunk section order.
	{
		struct CommandLineInterface *pCli = Cli();
		put("SEGLIST:"); flush();
		if(pCli && pCli->cli_Module) {
			const ULONG *pSeg = (const ULONG *)((ULONG)pCli->cli_Module << 2);
			for(unsigned i = 0; pSeg && i < 200; ++i) {
				put(" "); hex(i, 2); put(" start="); hex((ULONG)(pSeg + 1), 8); put(" size="); hex(pSeg[-1] - 8, 8); flush();
				pSeg = (const ULONG *)(pSeg[0] << 2);
			}
		}
	}
	if(s_fh) Close(s_fh);
	for(;;) {
		Delay(50);
	}
}

// Vector entry points: one tiny thunk per vector stores the number, then the common entry (supervisor
// mode, raw CPU frame on the SSP). It pops the frame (size from the format nibble), pushes a format-0
// frame (SR=0 = user mode, PC = rt_crash_report) and RTEs: the reporter then runs in user mode and
// switches to its private stack before any C code.
asm(R"(
	.text
	.globl rt_crash_report
	.globl rt_crash_vec2
	.balign 2
rt_crash_vec2:
	move.w #2,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec3:
	move.w #3,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec4:
	move.w #4,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec5:
	move.w #5,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec6:
	move.w #6,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec7:
	move.w #7,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec8:
	move.w #8,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec9:
	move.l %a0,-(%sp)
	move.l %d0,-(%sp)
	move.l rt_trace_idx,%d0
	and.l #1023,%d0
	lea rt_trace_ring,%a0
	move.l 10(%sp),(%a0,%d0.l*4)       /* PC of the next instruction (frame: SR.w, PC.l above our 2 pushes) */
	addq.l #1,rt_trace_idx
	move.l (%sp)+,%d0
	move.l (%sp)+,%a0
	rte
	.balign 2
rt_crash_vec10:
	move.w #10,-(%sp)
	bra.s rt_crash_common
	.balign 2
rt_crash_vec11:
	move.w #11,-(%sp)
rt_crash_common:
	move.w (%sp)+,rt_crash_vec+2
	tst.l rt_crash_busy
	bne.s rt_crash_dead
	move.l #1,rt_crash_busy
	movem.l %d0-%d7/%a0-%a7,rt_crash_regs
	move.l %usp,%a0
	move.l %a0,rt_crash_usp
	lea rt_crash_frame,%a1
	move.l %sp,%a0
	moveq #7,%d0
1:	move.l (%a0)+,(%a1)+
	dbra %d0,1b
	move.w 6(%sp),%d1
	lsr.w #8,%d1
	lsr.w #4,%d1               /* format nibble */
	moveq #8,%d0
	cmp.w #2,%d1
	bne.s 2f
	moveq #12,%d0
2:	cmp.w #9,%d1
	bne.s 3f
	moveq #20,%d0
3:	cmp.w #10,%d1
	bne.s 4f
	moveq #32,%d0
4:	cmp.w #11,%d1
	bne.s 5f
	moveq #92,%d0
5:	lea -8(%sp,%d0.w),%a0       /* top of the old frame - 8: room for a format-0 frame */
	clr.w (%a0)
	move.l #rt_crash_report,2(%a0)
	clr.w 6(%a0)
	move.l %a0,%sp
	rte
	.globl rt_trace_sv
rt_trace_sv:
	ori.w #0x8000,(%sp)       /* T1 into the SR the Supervisor() frame will RTE back to */
	rte
rt_crash_dead:
	stop #0x2700
	bra.s rt_crash_dead
rt_crash_report:
	lea rt_crash_stack+4096,%sp
	jsr rt_crash_c
6:	bra.s 6b
	.balign 4
rt_crash_table:
	.long rt_crash_vec2,rt_crash_vec3,rt_crash_vec4,rt_crash_vec5,rt_crash_vec6
	.long rt_crash_vec7,rt_crash_vec8,rt_crash_vec9,rt_crash_vec10,rt_crash_vec11
	.globl rt_crash_table
)");

extern "C" const ULONG rt_crash_table[10];

namespace rt {

void crashInstall() {
	if(s_isInstalled) {
		return;
	}
	{
		IrqOff sOff;
		for(unsigned v = FIRST_VEC; v <= LAST_VEC; ++v) {
			if(!isOwned(v)) continue;
			volatile ULONG *pVec = (volatile ULONG *)(v * 4);
			s_saved[v] = *pVec;
			*pVec = rt_crash_table[v - FIRST_VEC];
		}
	}
	rt_crash_busy = 0;
	s_isInstalled = true;
}

void crashTraceStart() {
	rt_trace_idx = 1;  // 0 = "never started" for the reporter; the first slot stays a dummy
	Supervisor((ULONG (*)())rt_trace_sv);
}

void crashRemove() {
	if(!s_isInstalled) {
		return;
	}
	{
		IrqOff sOff;
		for(unsigned v = FIRST_VEC; v <= LAST_VEC; ++v) {
			if(isOwned(v)) *(volatile ULONG *)(v * 4) = s_saved[v];
		}
	}
	s_isInstalled = false;
}

}  // namespace rt
