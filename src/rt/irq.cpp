// rt/irq - the original game's interrupt work under ACE. See docs/IRQ.md for the analysis and the contract.
//
// ACE owns the hardware vectors (systemUnuse). Level 3 (VBL/BLIT/COPER, ROADMAP 7.1d) is C++ now: the trampoline
// below switches to a private stack and calls irqLevel3(), which does what the original handler (program LAB_0331,
// mog LAB_0B55) did: blit-done flag, frame counter, mouse pointer, POTGO, the VBL hook list, acks. Level 4 (audio) is
// the synth's own asm handler (mog LAB_0F69, 7.1g) reached through a faked 68020 format-0 exception frame (its RTE
// returns into the trampoline); without one it is the original's no-op default (the callback cells were never written).
// Level 2 (keyboard) is rt/input; levels 1/5/6 were trackdisk only and are not installed.

#include "rt/irq.hpp"
#include "rt/guards.hpp"

#include <ace/managers/log.h>
#ifdef MS_IRQ_TRACE
#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#endif
#include <ace/managers/system.h>
#include <ace/utils/custom.h>
#include <hardware/intbits.h>

#include "rt/abs.h"
#include "rt/autoplay.hpp"
#include "rt/input.hpp"

extern "C" {

// Game handler cell read by the level-4 trampoline (asm); null = the C++ default. Global + externally_visible: -fwhole-program.
__attribute__((used, externally_visible)) void *volatile rt_irq_h4;  // INT4: audio (the synth's handler)
// Set by the ISRs when a debug trace event is queued (-DMS_IRQ_TRACE); polled by rt_irq_disable.
__attribute__((used, externally_visible)) volatile UBYTE rt_irq_logpending;
__attribute__((used, externally_visible)) void rtIrqFlushC(void) {
#ifdef MS_IRQ_TRACE
	rt::traceFlush();
#endif
}

void rt_irq_tramp3(void);
void rt_irq_tramp4(void);

// Installs run from the patched LAB_0325 / LAB_0B49 (patches program.irq2.json / mog.irq2.json), see the asm at the end.
__attribute__((used, externally_visible)) void rtPrgIrqInitC(void) {
	rt::irqInstall(rt::inputProgramCells());
}
__attribute__((used, externally_visible)) void rtMogIrqInitC(void) {
	rt::irqInstall(rt::inputMogCells());
}

// The trampolines' C bodies (private stack, all registers saved by the trampoline).
__attribute__((used, externally_visible)) void rtIrqLevel3C(void) {
	rt::irqLevel3();
}
__attribute__((used, externally_visible)) void rtIrqLevel4C(void) {
	rt::irqLevel4Default();
}

// Calls one VBL hook routine with every callee-saved register protected (asm at the end).
void rtIrqCallHook(ULONG ulFn, volatile ULONG *pNext);

}

namespace rt {

namespace {

constexpr UWORD INTS_GAME_MASKED = INTF_COPER | INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;

ULONG s_ulVbl;
UWORD s_uwInstalls;

// The original SECSTRT_15 / SECSTRT_20 (the VBL service routine). Its head counted down the trackdisk motor-off timeout
// (cells LAB_038C/LAB_038D); nothing positive is ever stored there any more (the file layer is rt/files), so that part is
// gone. What is left is the hook list: up to nine routine addresses, called in order until the 0 terminator. A hook may
// edit the list while it runs (LAB_057B clears its own slot), so the slot is re-read after every call.
void runHooks(volatile ULONG *pList) {
	while(*pList != 0) {
		const ULONG ulFn = *pList++;
		rtIrqCallHook(ulFn, pList);
	}
}

tAceIntHandler tramp(void (*pfn)(void)) {
	return reinterpret_cast<tAceIntHandler>(pfn);
}

}  // namespace

#ifdef MS_IRQ_SELFTEST
// Current raster line; vposr and vhposr are read as one longword and the read is repeated until it is stable
// (a torn read at line 255/256 would otherwise look like a frame wrap).
UWORD beamLine() {
	volatile ULONG *pPos = reinterpret_cast<volatile ULONG *>(&g_pCustom->vposr);
	ULONG ulA, ulB;
	do {
		ulA = *pPos;
		ulB = *pPos;
	} while(((ulA ^ ulB) >> 8) & 0x1FF);
	return static_cast<UWORD>((ulA >> 8) & 0x1FF);
}

// Debug aid (-DMS_IRQ_SELFTEST, first install only): the game's main flow may not get far yet, so measure
// here. Enable interrupts, poll the beam for 500 frames (10 s), and compare the beam's frame count with ACE's
// VBL count and with the game's own frame counter (incremented by the original VBL handler). Keys pressed
// during the window show up in the key events and the variables. Results go to PROGDIR:irq.log.
void selfTest(const IrqCells *pVars) {
	g_pCustom->intena = INTF_INTEN;  // master off (the trace flush's systemUnuse switched it on)
	// Phase A (INTEN off): validate the beam-wrap counter against INTREQ's VERTB bit, polled by hand.
	{
		ULONG ulWraps = 0, ulReq = 0;
		UWORD uwPrevA = 0;
		g_pCustom->intreq = INTF_VERTB;
		while(ulWraps < 100) {
			const UWORD uwV = beamLine();
			if(uwV < uwPrevA) {
				++ulWraps;
			}
			uwPrevA = uwV;
			if(g_pCustom->intreqr & INTF_VERTB) {
				g_pCustom->intreq = INTF_VERTB;
				++ulReq;
			}
		}
		traceEvent(5, ulWraps, ulReq, 0);
	}
	g_pCustom->intena = INTF_SETCLR | INTF_INTEN;
	const ULONG ulVbl0 = s_ulVbl;
	const ULONG ulGame0 = pVars ? *pVars->pulFrame : 0;
	ULONG ulFrames = 0;
	UWORD uwPrev = 0;
	while(ulFrames < 500) {
		const UWORD uwV = beamLine();  // vposr:vhposr in one read
		if(uwV < uwPrev) {
			++ulFrames;
		}
		uwPrev = uwV;
	}
	g_pCustom->intena = INTF_INTEN;
	traceEvent(3, ulFrames, s_ulVbl - ulVbl0, (pVars ? *pVars->pulFrame : 0) - ulGame0);
	ULONG ulDown = 0;
	for(UBYTE i = 0; pVars && i < 128; ++i) {
		ulDown += pVars->pubKeyDown[i];
	}
	traceEvent(4, inputEventCount(), pVars ? *pVars->pubKeyLast : 0, ulDown);
	traceFlush();
}
#endif

void irqLevel3() {
	const IrqCells *pCells = inputCells();
	if(!pCells) {
		return;
	}
	const UWORD uwReq = g_pCustom->intreqr;                          // MOVE.W INTREQR,D0, once, like the original
	if(uwReq & INTF_BLIT) {
		*pCells->puwBlitDone = 0;                                    // LAB_0367 := 0
		g_pCustom->intreq = INTF_BLIT;
	}
	if(uwReq & INTF_VERTB) {
		if(*pCells->puwFrameOn != 0) {                               // LAB_0363: frame counting enabled
			++*pCells->pulFrame;                                     // LAB_0379
		}
		inputPointerTick(*pCells);                                   // JSR LAB_034D
		g_pCustom->potgo = 1;                                        // MOVE.W #1,POTGO
		runHooks(pCells->pulHooks);                                  // JSR SECSTRT_15
		g_pCustom->intreq = INTF_VERTB;
		++s_ulVbl;
		autoplayTick();                                              // MS_AUTOPLAY: scripted input, a no-op otherwise
#ifdef MS_IRQ_TRACE
		if(s_ulVbl % 50 == 0) {
			traceEvent(0, s_ulVbl, *pCells->pulFrame, inputEventCount());
		}
#endif
	}
	if(uwReq & INTF_COPER) {
		g_pCustom->intreq = INTF_COPER;
	}
}

void irqLevel4Default() {
	// program LAB_0337 / mog LAB_0B5B: for AUD3..AUD0 in that order ack the bit and call the callback cell
	// (LAB_0371..036E / LAB_0B95..0B92), which holds the RTS stub and is never written by any code.
	const UWORD uwReq = g_pCustom->intreqr;
	static const UWORD s_auwAud[4] = {INTF_AUD3, INTF_AUD2, INTF_AUD1, INTF_AUD0};
	for(UBYTE i = 0; i < 4; ++i) {
		if(uwReq & s_auwAud[i]) {
			g_pCustom->intreq = s_auwAud[i];
		}
	}
}

void irqInstall(const IrqCells &sCells) {
	inputBind(&sCells);
	// Original LAB_0325 / LAB_0B49: wait for the beam, take the first mouse counter reading, master interrupt off,
	// vector pokes + CIA setup + INTENA, master on. The beam wait stays in asm (rt_prg_irq_init); under ACE the vectors and
	// the CIA masks are ACE's, so what is left here is the registration.
	g_pCustom->intena = INTF_INTEN;
	inputPointerInit(sCells);
	rt_irq_h4 = nullptr;                                             // the default audio handler until the synth swaps it in
	++s_uwInstalls;
#ifdef MS_IRQ_TRACE
	traceEvent(2, s_uwInstalls, 0, 0);
	traceFlush();
#endif
	logWrite("rt irq: install #%u (%s)\n", (unsigned)s_uwInstalls, sCells.szName);

	// systemSetInt force-enables the INTENA bit. The original keeps COPER and the audio ints masked (INTENA
	// $1f95 at init; the sound player enables its channels itself), so put those bits back as they were.
	const UWORD uwKeep = g_pCustom->intenar & INTS_GAME_MASKED;
	systemSetInt(INTB_VERTB, tramp(rt_irq_tramp3), nullptr);
	systemSetInt(INTB_BLIT, tramp(rt_irq_tramp3), nullptr);
	systemSetInt(INTB_COPER, tramp(rt_irq_tramp3), nullptr);
	systemSetInt(INTB_AUD0, tramp(rt_irq_tramp4), nullptr);
	systemSetInt(INTB_AUD1, tramp(rt_irq_tramp4), nullptr);
	systemSetInt(INTB_AUD2, tramp(rt_irq_tramp4), nullptr);
	systemSetInt(INTB_AUD3, tramp(rt_irq_tramp4), nullptr);
	g_pCustom->intena = INTS_GAME_MASKED;
	g_pCustom->intena = INTF_SETCLR | uwKeep;

	inputInstall();
	g_pCustom->intena = INTF_SETCLR | INTF_INTEN;                    // LAB_0557: master on
#ifdef MS_IRQ_SELFTEST
	if(s_uwInstalls == 1) {
		selfTest(&sCells);
	}
#endif
}

unsigned long irqVblCount() {
	return s_ulVbl;
}

void irqSetInt4(void *pHandler) {
	rt_irq_h4 = pHandler;
}

void irqRemove() {
	inputRemove();
	systemSetInt(INTB_VERTB, nullptr, nullptr);
	systemSetInt(INTB_BLIT, nullptr, nullptr);
	systemSetInt(INTB_COPER, nullptr, nullptr);
	systemSetInt(INTB_AUD0, nullptr, nullptr);
	systemSetInt(INTB_AUD1, nullptr, nullptr);
	systemSetInt(INTB_AUD2, nullptr, nullptr);
	systemSetInt(INTB_AUD3, nullptr, nullptr);
	rt_irq_h4 = nullptr;
}

#ifdef MS_IRQ_TRACE
// Debug trace (build with -DMS_IRQ_TRACE): the ISRs queue events, rt_irq_disable (main context, the game calls
// it constantly) flushes them to PROGDIR:irq.log through dos.library, inside the same systemUse() guard
// rt/files uses. ACE's logWrite has no sink on stock WinUAE.
namespace {
struct TraceEv { ULONG ulKind, ulA, ulB, ulC; };
TraceEv s_pTrace[32];
volatile UBYTE s_ubHead, s_ubTail;
BPTR s_bpLog;
}

int formatEv(char *szLine, unsigned uwSize, const TraceEv &e) {
	return e.ulKind == 2
		? snprintf(szLine, uwSize, "INSTALL #%lu h3=%lx h4=%lx\n", e.ulA, e.ulB, e.ulC)
		: e.ulKind == 3
		? snprintf(szLine, uwSize, "SELFTEST beam-frames=%lu ace-vbl=%lu game-frames=%lu\n", e.ulA, e.ulB, e.ulC)
		: e.ulKind == 5
		? snprintf(szLine, uwSize, "SELFTEST phase A (interrupts off): beam-wraps=%lu vertb-polls=%lu\n", e.ulA, e.ulB)
		: e.ulKind == 4
		? snprintf(szLine, uwSize, "SELFTEST keys=%lu last=%02lx down-flags=%lu\n", e.ulA, e.ulB, e.ulC)
		: e.ulKind == 0
		? snprintf(szLine, uwSize, "IRQ vbl=%lu game=%lu keys=%lu\n", e.ulA, e.ulB, e.ulC)
		: snprintf(szLine, uwSize, "KEY raw=%02lx %s xlat=%02lx last=%02lx\n", e.ulA >> 1, (e.ulA & 1) ? "up" : "down", e.ulB, e.ulC);
}

void traceEvent(ULONG ulKind, ULONG ulA, ULONG ulB, ULONG ulC) {
	const TraceEv sEv = {ulKind, ulA, ulB, ulC};
	const UBYTE ubNext = (s_ubHead + 1) & 31;
	if(ubNext == s_ubTail) {
		return;
	}
	s_pTrace[s_ubHead] = sEv;
	s_ubHead = ubNext;
	rt_irq_logpending = 1;
}

void traceFlush() {
	if(s_ubHead == s_ubTail) {
		rt_irq_logpending = 0;
		return;
	}
	{
		OsAccess sOs;
		if(!s_bpLog) {
			s_bpLog = Open((CONST_STRPTR)"PROGDIR:irq.log", MODE_NEWFILE);   // kept open for the whole run (trace build only)
		}
		while(s_ubHead != s_ubTail && s_bpLog) {
			char szLine[96];
			const int n = formatEv(szLine, sizeof(szLine), s_pTrace[s_ubTail]);
			Write(s_bpLog, szLine, n);
			s_ubTail = (s_ubTail + 1) & 31;
		}
	}
	rt_irq_logpending = (s_ubHead != s_ubTail);
}
#endif

}  // namespace rt

// ---- asm: trampolines and register-preserving shims for the generated rt_irq_* call sites -----------

// Level 3: called by ACE as a plain function (args ignored). Saves every register, switches to a private stack and runs
// the C++ handler. Level 4: the same, but when the synth's handler is installed (rt_irq_h4) it is entered through the frame
// an exception would have pushed (vector word, return PC, SR) and its RTE lands on 8:, so the original asm handler runs
// unchanged; with no handler the C++ default runs.
asm(R"(
	.macro RT_IRQ_TRAMP_C name, stk, cfn
	.globl \name
\name:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %sp,%a1
	lea \stk+4096,%sp
	move.l %a1,-(%sp)
	jsr \cfn
	move.l (%sp)+,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
	.endm

	.macro RT_IRQ_TRAMP_H name, cell, stk, vec, cfn
	.globl \name
\name:
	movem.l %d0-%d7/%a0-%a6,-(%sp)
	move.l %sp,%a1
	lea \stk+4096,%sp
	move.l %a1,-(%sp)
	move.l \cell,%a0
	move.l %a0,%d0
	bne.s 6f
	jsr \cfn
	bra.s 9f
6:
	move.w #\vec,-(%sp)
	pea 8f
	move.w %sr,-(%sp)
	jmp (%a0)
8:
9:
	move.l (%sp)+,%sp
	movem.l (%sp)+,%d0-%d7/%a0-%a6
	rts
	.endm

	.section .bss.rt_irq_stk,"aw",@nobits
	.balign 4
rt_irq_stk3:
	.space 4096
rt_irq_stk4:
	.space 4096

	.text
	RT_IRQ_TRAMP_C rt_irq_tramp3, rt_irq_stk3, rtIrqLevel3C
	RT_IRQ_TRAMP_H rt_irq_tramp4, rt_irq_h4, rt_irq_stk4, 0x70, rtIrqLevel4C

| The installs: entered by JMP from the patched LAB_0325 (after its beam wait) / LAB_0B49, and return to their caller.
	.macro RT_IRQ_INIT_BODY cfn
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr \cfn
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts
	.endm
	.globl rt_prg_irq_init
rt_prg_irq_init:
	RT_IRQ_INIT_BODY rtPrgIrqInitC
	.globl rt_mog_irq_init
rt_mog_irq_init:
	RT_IRQ_INIT_BODY rtMogIrqInitC

| One VBL hook: A1 = the routine, A0 = the next slot (as the original's dispatcher left them); every register the hook may
| clobber and C expects preserved is saved.
	.globl rtIrqCallHook
rtIrqCallHook:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a1
	move.l 52(%sp),%a0
	jsr (%a1)
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_irq_disable
rt_irq_disable:
	move.w #0x4000,0xdff09a
	tst.b rt_irq_logpending
	bne.s 1f
	rts
1:	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtIrqFlushC
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_irq_enable
rt_irq_enable:
	move.w #0xc000,0xdff09a
	rts
)");

