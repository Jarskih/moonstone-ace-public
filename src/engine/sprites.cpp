// engine/sprites - see sprites.hpp.  Reference: Amiga Hardware Reference Manual, "Sprite hardware" (control words, vertical reuse,
// attached sprites, priority via BPLCON2) and the AGA addendum (FMODE sprite widths, BPLCON4 colour banks).
#include "engine/sprites.hpp"
#include "engine/endian.hpp"

namespace ms {
namespace sprites {

namespace {

bool inView(const softblit::ChipView &sMem, uint32_t ulAddr, uint32_t ulLen) {
	const uint32_t ulOff = ulAddr - sMem.origin;
	return sMem.size == 0 || (ulLen <= sMem.size && ulOff <= sMem.size - ulLen);
}

// Bytes ulAddr .. +ulLen-1 (zeros and false when outside the view).
bool readBytes(const softblit::ChipView &sMem, uint32_t ulAddr, uint8_t *pubOut, uint32_t ulLen) {
	if(!inView(sMem, ulAddr, ulLen)) {
		for(uint32_t i = 0; i < ulLen; ++i) {
			pubOut[i] = 0;
		}
		return false;
	}
	const uint8_t *pubSrc = sMem.bytes + (ulAddr - sMem.origin);
	for(uint32_t i = 0; i < ulLen; ++i) {
		pubOut[i] = pubSrc[i];
	}
	return true;
}

// The value of one sprite at buffer position lX, 0 if it has no pixel there.
uint8_t pixelAt(const Channel &sCh, int32_t lX, int32_t lX0) {
	if(!sCh.isVisible) {
		return 0;
	}
	const int32_t lK = lX + lX0 - static_cast<int32_t>(sCh.hstart);
	if(lK < 0 || lK >= static_cast<int32_t>(sCh.width)) {
		return 0;
	}
	return sCh.pixels[lK];
}

}  // namespace

uint8_t widthOf(uint16_t uwFmode) {
	switch((uwFmode >> 2) & 3u) {
		case 0: return 16;
		case 3: return 64;
		default: return 32;
	}
}

void begin(Set &sSet, const uint32_t *pulPtr) {
	for(uint8_t n = 0; n < kChannels; ++n) {
		Channel &sCh = sSet.ch[n];
		sCh.ptr = pulPtr[n];
		sCh.state = SPR_NEED_CTL;
		sCh.isVisible = false;
		sCh.isAttach = false;
		sCh.vstart = sCh.vstop = sCh.hstart = 0;
		sCh.width = 16;
		for(uint8_t i = 0; i < 64; ++i) {
			sCh.pixels[i] = 0;
		}
	}
}

void setPointer(Set &sSet, uint8_t ubSprite, uint32_t ulPtr) {
	if(ubSprite < kChannels) {
		sSet.ch[ubSprite].ptr = ulPtr;
	}
}

Status step(Set &sSet, const softblit::ChipView &sMem, uint16_t uwLine, bool isDmaOn, uint16_t uwFmode) {
	Status eStatus = SPRITES_OK;
	const uint8_t ubWidth = widthOf(uwFmode);
	const uint32_t ulHalf = ubWidth / 8u;     // bytes of one plane of a line
	const uint32_t ulLine = ulHalf * 2u;      // bytes of a control or data line
	for(uint8_t n = 0; n < kChannels; ++n) {
		Channel &sCh = sSet.ch[n];
		sCh.isVisible = false;
		if(!isDmaOn || uwLine < kFirstDmaLine || (sCh.ptr == 0 && sCh.state == SPR_NEED_CTL)) {
			continue;
		}
		// the state left by earlier lines: a block starts or ends on this line
		if(sCh.state == SPR_ACTIVE && uwLine >= sCh.vstop) {
			sCh.state = SPR_NEED_CTL;
		}
		if(sCh.state == SPR_WAIT_START && uwLine == sCh.vstart) {
			sCh.state = (uwLine >= sCh.vstop) ? SPR_NEED_CTL : SPR_ACTIVE;   // an empty block ends where it starts
		}
		if(sCh.state == SPR_ACTIVE) {
			uint8_t aubLine[16];
			if(!readBytes(sMem, sCh.ptr, aubLine, ulLine)) {
				eStatus = SPRITES_BAD_ADDRESS;
			}
			sCh.ptr += ulLine;
			sCh.width = ubWidth;
			for(uint8_t k = 0; k < ubWidth; ++k) {
				const uint8_t ubMask = static_cast<uint8_t>(0x80u >> (k & 7u));
				const uint8_t ubA = (aubLine[k >> 3] & ubMask) ? 1 : 0;
				const uint8_t ubB = (aubLine[ulHalf + (k >> 3)] & ubMask) ? 2 : 0;
				sCh.pixels[k] = static_cast<uint8_t>(ubA | ubB);
			}
			sCh.isVisible = true;
		}
		else if(sCh.state == SPR_NEED_CTL) {
			uint8_t aubCtl[16];
			if(!readBytes(sMem, sCh.ptr, aubCtl, ulLine)) {
				eStatus = SPRITES_BAD_ADDRESS;
			}
			sCh.ptr += ulLine;
			const uint16_t uwPos = be::ld16(aubCtl);
			const uint16_t uwCtl = be::ld16(aubCtl + 2);
			sCh.vstart = static_cast<uint16_t>((uwPos >> 8) | (((uwCtl >> 2) & 1u) << 8));
			sCh.vstop = static_cast<uint16_t>((uwCtl >> 8) | (((uwCtl >> 1) & 1u) << 8));
			sCh.hstart = static_cast<uint16_t>(((uwPos & 0xFFu) << 1) | (uwCtl & 1u));
			sCh.isAttach = (uwCtl & 0x80u) != 0;
			sCh.state = SPR_WAIT_START;
		}
	}
	return eStatus;
}

void drawLine(const Set &sSet, int32_t lX0, uint16_t uwCount, uint16_t uwBplcon4, LineBuf &sOut) {
	if(uwCount > kMaxLine) {
		uwCount = kMaxLine;
	}
	for(uint16_t x = 0; x < uwCount; ++x) {
		sOut.idx[x] = 0;
		sOut.group[x] = 0;
	}
	const uint8_t ubBankEven = static_cast<uint8_t>((uwBplcon4 >> 4) & 0xFu);
	const uint8_t ubBankOdd = static_cast<uint8_t>(uwBplcon4 & 0xFu);
	for(int8_t bPair = 3; bPair >= 0; --bPair) {
		const Channel &sEven = sSet.ch[2 * bPair];
		const Channel &sOdd = sSet.ch[2 * bPair + 1];
		const bool isAttached = sOdd.isAttach && (sOdd.isVisible || sEven.isVisible);
		for(uint16_t x = 0; x < uwCount; ++x) {
			const uint8_t ubE = pixelAt(sEven, x, lX0);
			const uint8_t ubO = pixelAt(sOdd, x, lX0);
			uint8_t ubIdx = 0;
			if(isAttached) {
				const uint8_t ubV = static_cast<uint8_t>((ubO << 2) | ubE);
				if(ubV != 0) {
					ubIdx = static_cast<uint8_t>((ubBankOdd << 4) + ubV);
				}
			}
			else if(ubE != 0) {   // the even sprite is in front of the odd one of its pair
				ubIdx = static_cast<uint8_t>((ubBankEven << 4) + 4 * bPair + ubE);
			}
			else if(ubO != 0) {
				ubIdx = static_cast<uint8_t>((ubBankOdd << 4) + 4 * bPair + ubO);
			}
			if(ubIdx != 0) {   // pairs are visited from 3 down to 0: the lower pair overwrites
				sOut.idx[x] = ubIdx;
				sOut.group[x] = static_cast<uint8_t>(bPair);
			}
		}
	}
}

void composite(const LineBuf &sSprites, uint16_t uwCount, const Compose &sCompose, uint8_t *pubIdx, uint8_t *pubIsSprite) {
	if(uwCount > kMaxLine) {
		uwCount = kMaxLine;
	}
	uint8_t ubPf1p = static_cast<uint8_t>(sCompose.bplcon2 & 7u);
	if(ubPf1p > 4) {
		ubPf1p = 4;
	}
	for(uint16_t x = 0; x < uwCount; ++x) {
		const bool isInWindow = static_cast<int32_t>(x) >= sCompose.lWinLo && static_cast<int32_t>(x) < sCompose.lWinHi;
		const uint8_t ubSpr = sSprites.idx[x];
		const uint8_t ubPf = pubIdx[x];
		pubIsSprite[x] = 0;
		if(ubSpr != 0 && (isInWindow || sCompose.isBorderSprites) && (sSprites.group[x] < ubPf1p || ubPf == 0)) {
			pubIdx[x] = ubSpr;
			pubIsSprite[x] = 1;
		}
		else if(isInWindow) {
			pubIdx[x] = static_cast<uint8_t>(ubPf ^ sCompose.ubPlayfieldXor);
		}
	}
}

}  // namespace sprites
}  // namespace ms
