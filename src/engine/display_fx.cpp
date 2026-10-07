// engine/display_fx - see include/engine/display_fx.hpp. Literal transcriptions of the asm (program.asm 10475-11262, mog.asm 8510-8960
// and 26214-26366), including their word arithmetic; the rt layer in src/rt/ feeds them the game's cells.
#include "engine/display_fx.hpp"

namespace ms {

namespace {

// no libc here: a plain loop GCC must not turn into memcpy (the barrier keeps the idiom recogniser away)
inline void putWords(uint16_t *puwDst, const uint16_t *puwSrc, uint8_t ubCount) {
	for(uint8_t i = 0; i < ubCount; ++i) {
		puwDst[i] = puwSrc[i];
#if defined(__GNUC__) && !defined(__clang__)
		asm volatile("" : : : "memory");
#endif
	}
}

}  // namespace

// ---- hardware sprite -------------------------------------------------------------------------------------------------------

void spriteControl(uint8_t *pCtl, int16_t wHeight, int16_t wX, int16_t wY) {
	const uint16_t uwX = static_cast<uint16_t>(wX + 0x80);
	const uint16_t uwY = static_cast<uint16_t>(wY + 0x2C);
	const uint16_t uwHeight = static_cast<uint16_t>(wHeight < 0 ? -wHeight : wHeight);
	const uint16_t uwStop = static_cast<uint16_t>(uwY + uwHeight);
	pCtl[0] = static_cast<uint8_t>(uwY);
	pCtl[1] = static_cast<uint8_t>(uwX >> 1);
	pCtl[2] = static_cast<uint8_t>(uwStop);
	uint8_t ubBits = pCtl[3] & ~7;
	if(uwY & 0x100) {
		ubBits |= 4;
	}
	if(uwStop & 0x100) {
		ubBits |= 2;
	}
	if(uwX & 1) {
		ubBits |= 1;
	}
	pCtl[3] = ubBits;
}

// ---- DIW shake -------------------------------------------------------------------------------------------------------------

const uint16_t kDiwShake[kDiwShakeLength] = {0x0800, 0x0000, 0xF800, 0x0000, 0x0200, 0x0000, 0xFE00, 0x0000, 0x0100, 0x0000, 0xFF00, 0xFFFF};

void diwShakeStart(DiwShake &sShake) {
	sShake.index = 0;
	sShake.count = 3;
	sShake.reload = 3;
}

DiwShakeStep diwShakeTick(DiwShake &sShake) {
	DiwShakeStep sStep = {kDiwWait, 0, 0};
	--sShake.count;                      // SUBI.W #1 / BNE: the word wraps
	if(sShake.count != 0) {
		return sStep;
	}
	const uint16_t uwOffset = kDiwShake[sShake.index];
	if(uwOffset == 0xFFFF) {             // LAB_0429: the window back to normal (the caller unhooks the tick)
		sStep.act = kDiwRestore;
		sStep.diwstrt = kDiwStrtNormal;
		sStep.diwstop = kDiwStopNormal;
		return sStep;
	}
	sStep.act = kDiwMove;
	sStep.diwstrt = static_cast<uint16_t>(kDiwStrtNormal + uwOffset);
	sStep.diwstop = static_cast<uint16_t>(kDiwStopNormal + uwOffset);
	++sShake.index;
	sShake.count = sShake.reload;
	return sStep;
}

// ---- wipe ---------------------------------------------------------------------------------------------------------------------

uint16_t wipeOffset(uint16_t uwX, uint16_t uwY) {
	const uint16_t uwLine = static_cast<uint16_t>(uwY * 20);               // LSL.W #4 + LSL.W #2, ADD.W
	return static_cast<uint16_t>(static_cast<uint16_t>((uwX >> 4) + uwLine) << 1);
}

WipeTile wipeTile(int16_t wX, int16_t wY, uint16_t uwTile, uint16_t uwBlockHeight, uint32_t ulPreviousSkip) {
	WipeTile sTile;
	sTile.draw = false;
	sTile.height = uwBlockHeight;
	sTile.srcSkip = ulPreviousSkip;
	sTile.srcOffset = 0;
	sTile.dstOffset = 0;
	if(wY >= 200) {                                                         // CMP.W #$c8,D1 / BGE.W
		return sTile;
	}
	uint16_t uwY = static_cast<uint16_t>(wY);
	uint16_t uwX = static_cast<uint16_t>(wX);
	// LAB_05C8
	const int16_t wEnd = static_cast<int16_t>(uwBlockHeight + wY);
	if(wEnd > 200) {
		sTile.height = static_cast<uint16_t>(200 - wY);
	}
	else {
		sTile.srcSkip = 0;
		if(wY < 0) {
			const uint16_t uwUp = static_cast<uint16_t>(~wY);               // lines above the screen, minus one
			sTile.height = static_cast<uint16_t>(uwBlockHeight - uwUp);
			sTile.srcSkip = static_cast<uint16_t>(static_cast<uint16_t>(uwUp << 5) + static_cast<uint16_t>(uwUp << 3));
			uwY = 0;
		}
	}
	if(wX < 0) {
		uwX = 0;
	}
	// LAB_05CD: the tile number's low byte -> position in its picture (10 blocks a row, 32 x 25 pixels)
	const uint16_t uwByte = uwTile & 0xFF;
	const uint16_t uwRow = uwByte / 10;
	const uint16_t uwColumn = static_cast<uint16_t>(uwByte - uwRow * 10);
	sTile.srcOffset = wipeOffset(static_cast<uint16_t>(uwColumn * 32), static_cast<uint16_t>(uwRow * 25)) + sTile.srcSkip;
	sTile.dstOffset = wipeOffset(uwX, uwY);
	sTile.draw = true;
	return sTile;
}

WipeRows wipeRows(uint16_t uwProgress, uint16_t uwFirstRow) {
	WipeRows sRows;
	const uint16_t uwQuotient = static_cast<uint16_t>(uwProgress / 25);
	sRows.rest = static_cast<uint16_t>(uwProgress % 25);
	sRows.mapOffset = static_cast<uint16_t>(uwQuotient * 20 + 20UL * uwFirstRow);
	sRows.startY = static_cast<int16_t>(25UL * uwFirstRow - sRows.rest);
	return sRows;
}

WipeCode wipeCode(int16_t wCode) {
	const uint32_t ulCode = static_cast<uint32_t>(static_cast<int32_t>(wCode));   // EXT.L
	WipeCode sCode;
	sCode.picture = ulCode / 80;                                                    // DIVU #80
	// (the original reads the base table past its three entries for a code of 240 or more: no such code exists)
	const uint16_t uwBase = sCode.picture < 3 ? kWipePictureBase[sCode.picture] : 0;
	sCode.tile = static_cast<uint16_t>(static_cast<uint16_t>(ulCode) - uwBase);
	return sCode;
}

// ---- fight scene palettes -----------------------------------------------------------------------------------------------------

void fighterColors(uint16_t *puwDst, uint32_t ulClass) {
	static const uint16_t s_aaw[5][3] = {
		{0x000A, 0x0007, 0x0004}, {0x0F80, 0x0C50, 0x0A30}, {0x08C6, 0x0593, 0x0251}, {0x0F22, 0x0B22, 0x0700}, {0x0206, 0x0103, 0x0001},
	};
	putWords(puwDst, ulClass <= 3 ? s_aaw[ulClass] : s_aaw[4], 3);   // 0, 1, 3, 2 are tested in turn, everything else is the last set
}

namespace {

void caveColors(uint16_t *puwDst, uint32_t ulRegion) {
	static const uint16_t s_aawSet[3][7] = {
		{0x0025, 0x0004, 0x0001, 0x0830, 0x0400, 0x0F80, 0x0C00},   // region $C
		{0x0104, 0x0102, 0x0000, 0x0600, 0x0300, 0x0693, 0x0C00},   // region 0
		{0x0500, 0x0200, 0x0000, 0x0B40, 0x0610, 0x0895, 0x0C00},   // the others
	};
	putWords(puwDst, s_aawSet[ulRegion == 0xC ? 0 : (ulRegion == 0 ? 1 : 2)], 7);
}

}  // namespace

void sceneColors(uint16_t *puwPal, const SceneTables &sTables, uint32_t ulScene, uint32_t ulRegion, uint32_t ulClassSecond) {
	putWords(puwPal, sTables.base, 32);
	uint16_t *puwDst = &puwPal[9];   // LAB_08D9 + $12
	switch(ulScene) {
	case 0x00: {
		static const uint16_t s_auw[7] = {0x0776, 0x0443, 0x0731, 0x0520, 0x0300, 0x009A, 0x0C00};
		putWords(puwDst, s_auw, 7);
		break;
	}
	case 0x0C:
		fighterColors(puwDst, ulClassSecond);
		break;
	case 0x18:
	case 0x20:
		caveColors(puwDst, ulRegion);
		break;
	case 0x24: {
		static const uint16_t s_auw[7] = {0x0653, 0x0942, 0x0720, 0x0500, 0x0A96, 0x0875, 0x0C00};
		putWords(puwDst, s_auw, 7);
		break;
	}
	case 0x14: {
		static const uint16_t s_auw[7] = {0x0C00, 0x0976, 0x0700, 0x0500, 0x0754, 0x0C30, 0x0A00};
		static const uint16_t s_auwTail[3] = {0x0FC0, 0x0F80, 0x0C50};
		putWords(puwDst, s_auw, 7);
		putWords(&puwPal[29], s_auwTail, 3);   // LAB_08D9 + $3A
		break;
	}
	case 0x30: {
		static const uint16_t s_auw[7] = {0x0F96, 0x0C63, 0x0930, 0x0842, 0x0521, 0x0F63, 0x0C00};
		putWords(puwDst, s_auw, 7);
		break;
	}
	case 0x40: {
		static const uint16_t s_auw[6] = {0x055A, 0x0347, 0x0123, 0x0001, 0x0F00, 0x0800};
		putWords(puwDst, s_auw, 6);
		break;
	}
	case 0x08:
		putWords(puwDst, sTables.cave, 23);
		break;
	case 0x04: {
		static const uint16_t s_auw[7] = {0x0332, 0x0CCB, 0x0B81, 0x0851, 0x0630, 0x0F52, 0x0900};
		putWords(puwDst, s_auw, 7);
		break;
	}
	default:
		break;
	}
}

void sceneTail(uint16_t *puwPal, const SceneTables &sTables, uint32_t ulScene, uint32_t ulRegion, uint32_t ulClassFirst) {
	fighterColors(&puwPal[6], ulClassFirst);
	// LAB_0409: the region's tweak
	if(ulScene != 8) {
		const uint16_t *puwTable = nullptr;
		switch(ulRegion) {
		case 0:
			puwTable = sTables.region0;
			break;
		case 4:
			puwTable = sTables.region4;
			break;
		case 8:
		case 0xC: {
			static const uint16_t s_auw[4] = {0x0FFD, 0x0998, 0x0776, 0x0443};
			putWords(&puwPal[1], s_auw, 4);
			puwTable = (ulRegion == 8) ? sTables.region8 : sTables.region12;
			break;
		}
		default:
			break;
		}
		if(puwTable) {
			putWords(&puwPal[16], puwTable, 13);   // LAB_040F: LAB_08D9 + $20 bytes
		}
	}
	puwPal[0] = 0;
	if(ulScene != 0x14) {
		puwPal[15] = 0x0C00;
	}
}

}  // namespace ms
