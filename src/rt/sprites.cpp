// rt/sprites - mog S_37 (LAB_0E75..0E85, SECSTRT_37) in C++ (ROADMAP 7.1e). See sprites.hpp.
//
// Register contracts of the asm entries (all registers are preserved except the documented results; flags are not):
//   (rt_mog_sprite_dma_on LAB_0E75 and rt_mog_sprite_set LAB_0E85 went with their dead patches, 7.1 cleanup; tests/sprites_emu_support.cpp keeps them for the unicorn comparison.)
//   rt_mog_sprite_place     (LAB_0E78)    D0 = sprite, D1.w = x, D2.w = y (screen coordinates, the original adds $80 / $2C)
//   rt_mog_sprite_build     (SECSTRT_37)  A0 = sprite record, D0.w = frame, A1 = output buffer; out: A1 = end of the data,
//                                         A0 = start of the partner sprite for an attached frame (else unchanged)
// LAB_0E76 and LAB_0E77 stay asm (nothing calls them any more): they only call / jump to LAB_0E85.
#include "rt/sprites.hpp"


#include <ace/utils/custom.h>

#include "engine/display_fx.hpp"
#include "rt/copper_stub.hpp"

extern "C" {
extern ULONG mogSpriteStart;            // start of the last sprite built (read by mog LAB_0574) (LAB_0E8D)
extern ULONG mogSpritePartner;            // start of its partner (attached frames) (LAB_0E8E)
extern const UBYTE mogNoSprite[];  // the "no sprite" data: height 5, no pixels (SECSTRT_38)
}

namespace {

constexpr UBYTE SPRITES = 8;
ULONG s_aulData[SPRITES];  // LAB_0E8C: the data last installed per sprite

inline UBYTE partnerOf(UBYTE ubSprite) {
	return (ubSprite & 1) ? ubSprite - 1 : ubSprite + 1;
}

inline WORD heightWord(ULONG ulData) {
	return *reinterpret_cast<const WORD *>(ulData);
}

// one pass of LAB_0E88..0E8A: remember the data, point SPRxPT at it (past the height word)
void install(UBYTE ubSprite, ULONG ulData) {
	if(ubSprite >= SPRITES) {
		return;
	}
	s_aulData[ubSprite] = ulData;
	rt::copperStubSetSprite(ubSprite, ulData + 2);
}

// LAB_0E7C..0E80: the control words of one sprite's data (the height word first)
void place(ULONG ulData, WORD wX, WORD wY) {
	ms::spriteControl(reinterpret_cast<UBYTE *>(ulData + 2), heightWord(ulData), wX, wY);
}

}  // namespace

namespace rt {

void spritesReset() {
	for(UBYTE ubSprite = 0; ubSprite < SPRITES; ++ubSprite) {
		s_aulData[ubSprite] = 0;
		asm volatile("" : : : "memory");  // keeps GCC from turning the loop into a memset call: there is no libc
	}
}

void spriteDmaOn() {
	g_pCustom->dmacon = 0x8020;  // SET | SPREN
}

void spriteInstall(UBYTE ubSprite, ULONG ulData, ULONG ulData2) {
	install(ubSprite, ulData);
	if(heightWord(ulData) < 0) {  // attached pair
		const UBYTE ubPartner = partnerOf(ubSprite);
		if(ubPartner != 0) {
			install(ubPartner, ulData2);
		}
	}
}

void spriteOff(UBYTE ubSprite) {
	spriteInstall(ubSprite, reinterpret_cast<ULONG>(mogNoSprite), 0);
}

void spritePlace(UBYTE ubSprite, WORD wX, WORD wY) {
	if(ubSprite >= SPRITES || s_aulData[ubSprite] == 0) {
		return;
	}
	const ULONG ulData = s_aulData[ubSprite];
	place(ulData, wX, wY);
	if(heightWord(ulData) < 0) {
		const UBYTE ubPartner = partnerOf(ubSprite);
		if(ubPartner != 0 && s_aulData[ubPartner] != 0) {
			place(s_aulData[ubPartner], wX, wY);
		}
	}
}

ULONG spriteBuild(const void *pRecord, UWORD uwFrame, ULONG ulOut, ULONG *pulSecond) {
	mogSpriteStart = ulOut;
	*pulSecond = 0;
	const UBYTE *pRec = static_cast<const UBYTE *>(pRecord);
	// 10(A0,D0.W) with D0 = frame * 10 (MULU), the index register used as a signed word
	const UBYTE *pFrame = pRec + static_cast<WORD>(static_cast<ULONG>(uwFrame) * 10);
	const UWORD *pPlane0 = reinterpret_cast<const UWORD *>(
		*reinterpret_cast<const ULONG *>(pRec + 2) + *reinterpret_cast<const ULONG *>(pFrame + 10)
	);
	const UWORD uwHeight = *reinterpret_cast<const UWORD *>(pFrame + 16);
	const UWORD *pPlane1 = pPlane0 + uwHeight;  // one plane = height words
	UWORD *pOut = reinterpret_cast<UWORD *>(ulOut);
	if(!(pFrame[19] & 8)) {  // 4 colours: one sprite, two planes
		*pOut++ = uwHeight;
		*pOut++ = 0;
		*pOut++ = 0;
		for(UWORD i = 0; i < uwHeight; ++i) {
			*pOut++ = *pPlane0++;
			*pOut++ = *pPlane1++;
		}
		*pOut++ = 0;
		*pOut++ = 0;
		return reinterpret_cast<ULONG>(pOut);
	}
	// 16 colours: an attached pair. The second sprite follows the first one's end marker and carries the attach bit ($80 in
	// its second control word); the first sprite gets planes 0/1, the second planes 2/3.
	const UWORD *pPlane2 = pPlane1 + uwHeight;
	const UWORD *pPlane3 = pPlane2 + uwHeight;
	const UWORD uwNegHeight = static_cast<UWORD>(-static_cast<WORD>(uwHeight));
	*pOut++ = uwNegHeight;
	*pOut++ = 0;
	*pOut++ = 0;
	UWORD *pTwo = reinterpret_cast<UWORD *>(reinterpret_cast<ULONG>(pOut) + 4 + static_cast<UWORD>(uwHeight << 2));
	UWORD *const pSecond = pTwo;
	*pTwo++ = uwNegHeight;
	*pTwo++ = 0;
	*pTwo++ = 0x80;
	for(UWORD i = 0; i < uwHeight; ++i) {
		*pOut++ = *pPlane0++;
		*pOut++ = *pPlane1++;
		*pTwo++ = *pPlane2++;
		*pTwo++ = *pPlane3++;
	}
	*pOut++ = 0;
	*pOut++ = 0;
	*pTwo++ = 0;
	*pTwo++ = 0;
	mogSpritePartner = reinterpret_cast<ULONG>(pSecond);
	*pulSecond = reinterpret_cast<ULONG>(pSecond);
	return reinterpret_cast<ULONG>(pTwo);
}

}  // namespace rt

// ---- C bodies and asm wrappers ----------------------------------------------------------------------------------------

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED void rtMogSpritePlaceC(ULONG ulSprite, ULONG ulX, ULONG ulY) {
	rt::spritePlace(static_cast<UBYTE>(ulSprite), static_cast<WORD>(ulX), static_cast<WORD>(ulY));
}

// Returns the end of the data (A1); *pulSecond (0 when not attached) goes to A0 in the wrapper.
RT_USED ULONG rtMogSpriteBuildC(const void *pRecord, ULONG ulFrame, ULONG ulOut, ULONG *pulSecond) {
	return rt::spriteBuild(pRecord, static_cast<UWORD>(ulFrame), ulOut, pulSecond);
}

// The C ABI clobbers D0/D1/A0/A1 only; the original routines clobbered more but callers may rely on any of them being kept.
asm(R"(
	.text
	.globl rt_mog_sprite_place
rt_mog_sprite_place:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	move.l %d2,-(%sp)
	move.l %d1,-(%sp)
	move.l %d0,-(%sp)
	jsr rtMogSpritePlaceC
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_sprite_build
rt_mog_sprite_build:
	movem.l %d0-%d1/%a0,-(%sp)
	subq.l #4,%sp
	pea 0(%sp)
	move.l %a1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogSpriteBuildC
	lea 16(%sp),%sp
	movea.l %d0,%a1
	move.l (%sp)+,%d1
	beq.s 1f
	move.l %d1,8(%sp)
1:	movem.l (%sp)+,%d0-%d1/%a0
	rts
)");

