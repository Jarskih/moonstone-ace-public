// engine/dispframe - see dispframe.hpp.
#include "engine/dispframe.hpp"

#include "engine/planar.hpp"
#include "engine/sprites.hpp"

namespace ms {
namespace dispframe {

namespace {

constexpr uint16_t kBplcon2KillEhb = 0x0200;
constexpr uint16_t kBplcon3Brdsprt = 0x0002;
constexpr uint16_t kBplcon0Unsupported = 0x8000 | 0x0800 | 0x0400 | 0x0004 | 0x0040;   // hires, HAM, dual playfield, interlace, super-hires
constexpr uint16_t kBplcon3SpresMask = 0x00C0;

uint32_t colourOf(const copper::Regs &sRegs, uint8_t ubIdx, bool isSprite) {
	const bool isEhb = planesOf(sRegs.bplcon0) == 6 && (sRegs.bplcon2 & kBplcon2KillEhb) == 0;
	if(isEhb && !isSprite && ubIdx >= 32) {
		return (sRegs.palette.rgb[ubIdx - 32] >> 1) & 0x7F7F7Fu;
	}
	return sRegs.palette.rgb[ubIdx];
}

}  // namespace

uint8_t planesOf(uint16_t uwBplcon0) {
	const uint8_t ubBpu = static_cast<uint8_t>((uwBplcon0 >> 12) & 7u);
	if(ubBpu == 0 && (uwBplcon0 & 0x0010u) != 0) {
		return 8;
	}
	return ubBpu;
}

void render(const softblit::ChipView &sMem, const copper::Regs &sInitial, uint32_t ulCop1lc, const Output &sOut, Result &sResult) {
	sResult.problems = 0;
	sResult.copperStatus = copper::COPPER_OK;
	sResult.copperErrAddr = 0;
	sResult.copperErrWord1 = sResult.copperErrWord2 = 0;
	sResult.badBitplaneAddr = 0;
	if(sOut.width == 0 || sOut.width > sprites::kMaxLine || sOut.height == 0 || sOut.pixels == nullptr) {
		return;
	}
	copper::Regs sRegs = sInitial;
	sRegs.bplptWritten = 0;
	sRegs.sprptWritten = 0;
	copper::Player sPlayer;
	copper::begin(sPlayer, sMem, ulCop1lc);
	sprites::Set sSprites;
	sprites::begin(sSprites, sRegs.sprpt);
	uint32_t aulRun[planar::kMaxPlanes];   // the running bitplane pointers
	for(uint8_t i = 0; i < planar::kMaxPlanes; ++i) {
		aulRun[i] = sRegs.bplpt[i];
	}
	static sprites::LineBuf sLineSprites;
	uint8_t aubRow[sprites::kMaxLine];
	uint8_t aubLine[sprites::kMaxLine];
	uint8_t aubIsSprite[sprites::kMaxLine];

	for(uint16_t uwLine = 0; uwLine < copper::kLines; ++uwLine) {
		// 1. the copper instructions that start before this line's data fetch
		if(!sPlayer.isIdle) {
			copper::runTo(sPlayer, sRegs, uwLine, sRegs.ddfstrt);
			for(uint8_t i = 0; i < planar::kMaxPlanes; ++i) {
				if(sRegs.bplptWritten & (1u << i)) {
					aulRun[i] = sRegs.bplpt[i];
				}
			}
			for(uint8_t i = 0; i < sprites::kChannels; ++i) {
				if(sRegs.sprptWritten & (1u << i)) {
					sprites::setPointer(sSprites, i, sRegs.sprpt[i]);
				}
			}
			sRegs.bplptWritten = 0;
			sRegs.sprptWritten = 0;
		}
		// 2. the sprite DMA of the line
		const bool isSpriteDma = (sRegs.dmacon & copper::kDmaEn) != 0 && (sRegs.dmacon & copper::kDmaSpr) != 0;
		if(sprites::step(sSprites, sMem, uwLine, isSpriteDma, sRegs.fmode) == sprites::SPRITES_BAD_ADDRESS) {
			sResult.problems |= PROBLEM_SPRITE_ADDRESS;
		}
		// 3. the playfield fetch of the line (the pointers run on even for lines that are not stored)
		const uint16_t uwV0 = static_cast<uint16_t>(sRegs.diwstrt >> 8);
		const uint16_t uwV1 = static_cast<uint16_t>(((sRegs.diwstop >> 8) & 0xFFu) | ((sRegs.diwstop & 0x8000u) ? 0u : 0x100u));
		const int32_t lH0 = sRegs.diwstrt & 0xFF;
		const int32_t lH1 = static_cast<int32_t>(sRegs.diwstop & 0xFFu) | 0x100;
		const bool isInV = uwLine >= uwV0 && uwLine < uwV1;
		const uint8_t ubPlanes = planesOf(sRegs.bplcon0);
		const bool isFetch = isInV && ubPlanes >= 1 && (sRegs.dmacon & copper::kDmaEn) != 0 && (sRegs.dmacon & copper::kDmaBpl) != 0;
		uint32_t ulWords = 0;
		if(sRegs.ddfstop >= sRegs.ddfstrt) {
			ulWords = (static_cast<uint32_t>(sRegs.ddfstop - sRegs.ddfstrt) >> 3) + 1u;
			if(ulWords > sprites::kMaxLine / 16u) {
				ulWords = sprites::kMaxLine / 16u;
			}
		}
		const bool isOutputLine = uwLine >= sOut.originY && uwLine < static_cast<uint32_t>(sOut.originY) + sOut.height;
		const int32_t lFetchX0 = 2 * static_cast<int32_t>(sRegs.ddfstrt) + 17;   // low-res position of the first fetched pixel
		bool isRowValid = false;
		if(isFetch && ulWords != 0) {
			if(isOutputLine) {
				planar::Mode sMode;
				sMode.planes = ubPlanes;
				sMode.width = static_cast<uint16_t>(ulWords * 16u);
				sMode.height = 1;
				for(uint8_t i = 0; i < planar::kMaxPlanes; ++i) {
					sMode.ptr[i] = aulRun[i];
				}
				sMode.mod1 = sRegs.bpl1mod;
				sMode.mod2 = sRegs.bpl2mod;
				const planar::Status eStatus = planar::rowIndices(sMem, sMode, 0, aubRow);
				isRowValid = eStatus != planar::PLANAR_BAD_MODE;
				if(eStatus == planar::PLANAR_BAD_ADDRESS) {
					if((sResult.problems & PROBLEM_BAD_ADDRESS) == 0) {
						sResult.badBitplaneAddr = aulRun[0];
					}
					sResult.problems |= PROBLEM_BAD_ADDRESS;
				}
			}
			for(uint8_t i = 0; i < ubPlanes; ++i) {
				const int32_t lMod = (i & 1) ? sRegs.bpl2mod : sRegs.bpl1mod;
				aulRun[i] += static_cast<uint32_t>(static_cast<int32_t>(ulWords * 2u) + lMod);
			}
		}
		// 4. the pixels of the output rectangle
		if(!isOutputLine) {
			continue;
		}
		if((sRegs.bplcon0 & kBplcon0Unsupported) != 0) {
			sResult.problems |= PROBLEM_MODE;
		}
		if(sRegs.bplcon1 != 0) {
			sResult.problems |= PROBLEM_SCROLL;
		}
		if((sRegs.bplcon3 & kBplcon3SpresMask) > 0x0040) {
			sResult.problems |= PROBLEM_SPRITE_RES;
		}
		if((sRegs.fmode & 3u) != 0) {
			sResult.problems |= PROBLEM_FMODE;
		}
		for(uint16_t x = 0; x < sOut.width; ++x) {
			const int32_t lHpos = sOut.originX + x;
			const bool isWin = isInV && lHpos >= lH0 && lHpos < lH1;
			const int32_t lK = lHpos - lFetchX0;
			const bool isFetched = isWin && isRowValid && lK >= 0 && lK < static_cast<int32_t>(ulWords * 16u);
			aubLine[x] = isFetched ? aubRow[lK] : 0;
		}
		sprites::drawLine(sSprites, sOut.originX, sOut.width, sRegs.bplcon4, sLineSprites);
		sprites::Compose sCompose;
		sCompose.bplcon2 = sRegs.bplcon2;
		sCompose.isBorderSprites = (sRegs.bplcon3 & kBplcon3Brdsprt) != 0;
		sCompose.lWinLo = isInV ? lH0 - sOut.originX : 0;
		sCompose.lWinHi = isInV ? lH1 - sOut.originX : 0;
		sCompose.ubPlayfieldXor = static_cast<uint8_t>(sRegs.bplcon4 >> 8);
		sprites::composite(sLineSprites, sOut.width, sCompose, aubLine, aubIsSprite);
		uint32_t *pulDst = sOut.pixels + static_cast<uint32_t>(uwLine - sOut.originY) * sOut.width;
		for(uint16_t x = 0; x < sOut.width; ++x) {
			pulDst[x] = colourOf(sRegs, aubLine[x], aubIsSprite[x] != 0);
		}
	}
	sResult.copperStatus = sPlayer.status;
	sResult.copperErrAddr = sPlayer.errAddr;
	sResult.copperErrWord1 = sPlayer.errWord1;
	sResult.copperErrWord2 = sPlayer.errWord2;
	if(sPlayer.status != copper::COPPER_OK) {
		sResult.problems |= PROBLEM_COPPER;
	}
}

}  // namespace dispframe
}  // namespace ms
