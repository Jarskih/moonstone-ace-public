// rt/sprites - mog S_37 (LAB_0E75..0E85, SECSTRT_37) in C++ (ROADMAP 7.1e). See sprites.hpp.  Platform-neutral since ROADMAP 11.10: the sprite
// pointers and the sprite DMA switch go through rt/platform/display.hpp (the game's copper stub on the Amiga, the sprite pointers of the display
// model on the PC), the sprite data is big-endian words in chip RAM (rt/chipmem.hpp), the 68k register shims are src/rt/amiga/sprites_entries.cpp.
//
// Register contracts of the asm entries (all registers are preserved except the documented results; flags are not):
//   (rt_mog_sprite_dma_on LAB_0E75 and rt_mog_sprite_set LAB_0E85 went with their dead patches, 7.1 cleanup; tests/sprites_emu_support.cpp keeps them for the unicorn comparison.)
//   rt_mog_sprite_place     (LAB_0E78)    D0 = sprite, D1.w = x, D2.w = y (screen coordinates, the original adds $80 / $2C)
//   rt_mog_sprite_build     (SECSTRT_37)  A0 = sprite record, D0.w = frame, A1 = output buffer; out: A1 = end of the data,
//                                         A0 = start of the partner sprite for an attached frame (else unchanged)
// LAB_0E76 and LAB_0E77 stay asm (nothing calls them any more): they only call / jump to LAB_0E85.
#include "rt/sprites.hpp"

#include "engine/display_fx.hpp"
#include "rt/chipmem.hpp"
#include "rt/platform/display.hpp"

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
	return static_cast<WORD>(rt::chipLd16(reinterpret_cast<const void *>(ulData)));
}

// one pass of LAB_0E88..0E8A: remember the data, point SPRxPT at it (past the height word)
void install(UBYTE ubSprite, ULONG ulData) {
	if(ubSprite >= SPRITES) {
		return;
	}
	s_aulData[ubSprite] = ulData;
	rt::platform::displaySetSprite(ubSprite, ulData + 2);
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
	platform::displaySpritesEnable(true);   // DMACON SET | SPREN
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
	const UBYTE *pPlane0 = reinterpret_cast<const UBYTE *>(rt::chipLd32(pRec + 2) + rt::chipLd32(pFrame + 10));
	const UWORD uwHeight = rt::chipLd16(pFrame + 16);
	const UBYTE *pPlane1 = pPlane0 + uwHeight * 2;  // one plane = height words
	UBYTE *pOut = reinterpret_cast<UBYTE *>(ulOut);
	if(!(pFrame[19] & 8)) {  // 4 colours: one sprite, two planes
		chipSt16(pOut, uwHeight);
		chipSt16(pOut + 2, 0);
		chipSt16(pOut + 4, 0);
		pOut += 6;
		for(UWORD i = 0; i < uwHeight; ++i) {
			chipSt16(pOut, chipLd16(pPlane0));
			chipSt16(pOut + 2, chipLd16(pPlane1));
			pOut += 4;
			pPlane0 += 2;
			pPlane1 += 2;
		}
		chipSt16(pOut, 0);
		chipSt16(pOut + 2, 0);
		return reinterpret_cast<ULONG>(pOut + 4);
	}
	// 16 colours: an attached pair. The second sprite follows the first one's end marker and carries the attach bit ($80 in
	// its second control word); the first sprite gets planes 0/1, the second planes 2/3.
	const UBYTE *pPlane2 = pPlane1 + uwHeight * 2;
	const UBYTE *pPlane3 = pPlane2 + uwHeight * 2;
	const UWORD uwNegHeight = static_cast<UWORD>(-static_cast<WORD>(uwHeight));
	chipSt16(pOut, uwNegHeight);
	chipSt16(pOut + 2, 0);
	chipSt16(pOut + 4, 0);
	pOut += 6;
	UBYTE *pTwo = pOut + 4 + static_cast<UWORD>(uwHeight << 2);
	UBYTE *const pSecond = pTwo;
	chipSt16(pTwo, uwNegHeight);
	chipSt16(pTwo + 2, 0);
	chipSt16(pTwo + 4, 0x80);
	pTwo += 6;
	for(UWORD i = 0; i < uwHeight; ++i) {
		chipSt16(pOut, chipLd16(pPlane0));
		chipSt16(pOut + 2, chipLd16(pPlane1));
		chipSt16(pTwo, chipLd16(pPlane2));
		chipSt16(pTwo + 2, chipLd16(pPlane3));
		pOut += 4;
		pTwo += 4;
		pPlane0 += 2;
		pPlane1 += 2;
		pPlane2 += 2;
		pPlane3 += 2;
	}
	chipSt16(pOut, 0);
	chipSt16(pOut + 2, 0);
	chipSt16(pTwo, 0);
	chipSt16(pTwo + 2, 0);
	mogSpritePartner = reinterpret_cast<ULONG>(pSecond);
	*pulSecond = reinterpret_cast<ULONG>(pSecond);
	return reinterpret_cast<ULONG>(pTwo + 4);
}

}  // namespace rt

// ---- C bodies of the asm shims (src/rt/amiga/sprites_entries.cpp) -------------------------------------------------------------

#define RT_USED extern "C" __attribute__((used, externally_visible))

RT_USED void rtMogSpritePlaceC(ULONG ulSprite, ULONG ulX, ULONG ulY) {
	rt::spritePlace(static_cast<UBYTE>(ulSprite), static_cast<WORD>(ulX), static_cast<WORD>(ulY));
}

// Returns the end of the data (A1); *pulSecond (0 when not attached) goes to A0 in the wrapper.
RT_USED ULONG rtMogSpriteBuildC(const void *pRecord, ULONG ulFrame, ULONG ulOut, ULONG *pulSecond) {
	return rt::spriteBuild(pRecord, static_cast<UWORD>(ulFrame), ulOut, pulSecond);
}
