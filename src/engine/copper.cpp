// engine/copper - see copper.hpp.  Reference: Amiga Hardware Reference Manual, "Coprocessor hardware" (MOVE / WAIT / SKIP encoding, the
// masked beam compare) and the AGA colour registers (BPLCON3 bank and LOCT).
#include "engine/copper.hpp"
#include "engine/endian.hpp"

namespace ms {
namespace copper {

namespace {

constexpr uint32_t kFrameClocks = static_cast<uint32_t>(kLines) * kLineClocks;
constexpr uint32_t kNever = 0xFFFFFFFFu;

// Register offsets of MOVE destinations.
constexpr uint16_t kCop1lch = 0x080, kCop1lcl = 0x082, kCop2lch = 0x084, kCop2lcl = 0x086, kCopjmp1 = 0x088, kCopjmp2 = 0x08A;
constexpr uint16_t kDiwstrt = 0x08E, kDiwstop = 0x090, kDdfstrt = 0x092, kDdfstop = 0x094, kDmacon = 0x096;
constexpr uint16_t kBplpt1 = 0x0E0;       // BPL1PTH; planes 1..6 are 4 apart up to 0xF6, planes 7 and 8 follow at 0x118 / 0x11C
constexpr uint16_t kBplpt7 = 0x118;
constexpr uint16_t kBplcon0 = 0x100, kBplcon1 = 0x102, kBplcon2 = 0x104, kBplcon3 = 0x106, kBpl1mod = 0x108, kBpl2mod = 0x10A,
                   kBplcon4 = 0x10C;
constexpr uint16_t kSprpt0 = 0x120;       // SPR0PTH; sprite n is 4 apart up to 0x13E
constexpr uint16_t kColor0 = 0x180, kColor31 = 0x1BE;
constexpr uint16_t kFmode = 0x1FC, kNoop = 0x1FE;
constexpr uint16_t kBplcon3Loct = 0x0200;

bool fetch16(const softblit::ChipView &sMem, uint32_t ulAddr, uint16_t &uwOut) {
	const uint32_t ulOff = ulAddr - sMem.origin;
	if(sMem.size != 0 && (sMem.size < 2u || ulOff > sMem.size - 2u)) {
		return false;
	}
	uwOut = be::ld16(sMem.bytes + ulOff);
	return true;
}

void fail(Player &sPlayer, Status eStatus, uint16_t uwW1, uint16_t uwW2) {
	if(sPlayer.status == COPPER_OK) {
		sPlayer.status = eStatus;
		sPlayer.errAddr = sPlayer.pc;
		sPlayer.errWord1 = uwW1;
		sPlayer.errWord2 = uwW2;
	}
	sPlayer.isIdle = true;
}

// First position number >= ulFrom at which the WAIT condition holds, or kNever within this frame.
uint32_t firstSatisfied(uint32_t ulFrom, uint16_t uwIr1, uint16_t uwIr2) {
	if(ulFrom >= kFrameClocks) {
		return kNever;
	}
	const uint32_t ulLine0 = ulFrom / kLineClocks;
	const uint32_t ulClock0 = ulFrom % kLineClocks;
	const uint16_t uwVMask = static_cast<uint16_t>(0x80u | ((uwIr2 >> 8) & 0x7Fu));
	const uint16_t uwVWait = static_cast<uint16_t>((uwIr1 >> 8) & uwVMask);
	for(uint32_t ulLine = ulLine0; ulLine < kLines; ++ulLine) {
		const uint32_t ulStart = (ulLine == ulLine0) ? ulClock0 : 0;
		const uint16_t uwV = static_cast<uint16_t>((ulLine & 0xFFu) & uwVMask);
		if(uwV > uwVWait) {
			return ulLine * kLineClocks + ulStart;
		}
		if(uwV < uwVWait) {
			continue;
		}
		for(uint32_t ulClock = ulStart; ulClock < kLineClocks; ++ulClock) {
			if(beamCompare(static_cast<uint16_t>(ulLine), static_cast<uint16_t>(ulClock), uwIr1, uwIr2)) {
				return ulLine * kLineClocks + ulClock;
			}
		}
	}
	return kNever;
}

void setHigh(uint32_t &ulReg, uint16_t uwValue) {
	ulReg = (ulReg & 0x0000FFFFu) | (static_cast<uint32_t>(uwValue) << 16);
}

void setLow(uint32_t &ulReg, uint16_t uwValue) {
	ulReg = (ulReg & 0xFFFF0000u) | uwValue;
}

// One MOVE.  Returns false for a register that is refused (the caller stops the copper).
bool applyMove(Player &sPlayer, Regs &sRegs, uint16_t uwReg, uint16_t uwValue) {
	if(uwReg >= kBplpt1 && uwReg < kBplpt1 + 6 * 4) {
		const uint8_t ubPlane = static_cast<uint8_t>((uwReg - kBplpt1) >> 2);
		((uwReg & 2) ? setLow : setHigh)(sRegs.bplpt[ubPlane], uwValue);
		sRegs.bplptWritten = static_cast<uint8_t>(sRegs.bplptWritten | (1u << ubPlane));
		return true;
	}
	if(uwReg >= kBplpt7 && uwReg < kBplpt7 + 2 * 4) {
		const uint8_t ubPlane = static_cast<uint8_t>(6 + ((uwReg - kBplpt7) >> 2));
		((uwReg & 2) ? setLow : setHigh)(sRegs.bplpt[ubPlane], uwValue);
		sRegs.bplptWritten = static_cast<uint8_t>(sRegs.bplptWritten | (1u << ubPlane));
		return true;
	}
	if(uwReg >= kSprpt0 && uwReg < kSprpt0 + 8 * 4) {
		const uint8_t ubSprite = static_cast<uint8_t>((uwReg - kSprpt0) >> 2);
		((uwReg & 2) ? setLow : setHigh)(sRegs.sprpt[ubSprite], uwValue);
		sRegs.sprptWritten = static_cast<uint8_t>(sRegs.sprptWritten | (1u << ubSprite));
		return true;
	}
	if(uwReg >= kColor0 && uwReg <= kColor31) {
		const uint32_t ulBank = (sRegs.bplcon3 >> 13) & 7u;
		writeColour(sRegs.palette, ulBank * 32u + ((uwReg - kColor0) >> 1), uwValue, (sRegs.bplcon3 & kBplcon3Loct) != 0);
		return true;
	}
	switch(uwReg) {
		case kCop1lch: setHigh(sPlayer.cop1lc, uwValue); return true;
		case kCop1lcl: setLow(sPlayer.cop1lc, uwValue); return true;
		case kCop2lch: setHigh(sPlayer.cop2lc, uwValue); return true;
		case kCop2lcl: setLow(sPlayer.cop2lc, uwValue); return true;
		case kCopjmp1: sPlayer.pc = sPlayer.cop1lc - 4u; return true;   // the caller adds 4 after every instruction
		case kCopjmp2: sPlayer.pc = sPlayer.cop2lc - 4u; return true;
		case kDiwstrt: sRegs.diwstrt = uwValue; return true;
		case kDiwstop: sRegs.diwstop = uwValue; return true;
		case kDdfstrt: sRegs.ddfstrt = uwValue; return true;
		case kDdfstop: sRegs.ddfstop = uwValue; return true;
		case kDmacon: {
			const uint16_t uwBits = static_cast<uint16_t>(uwValue & kDmaMask);
			if(uwValue & kDmaSet) {
				sRegs.dmacon = static_cast<uint16_t>(sRegs.dmacon | uwBits);
			}
			else {
				sRegs.dmacon = static_cast<uint16_t>(sRegs.dmacon & ~uwBits);
				if(uwBits & (kDmaEn | kDmaCop)) {
					sPlayer.isIdle = true;   // the copper switched its own DMA off: it never wakes again this frame
				}
			}
			return true;
		}
		case kBplcon0: sRegs.bplcon0 = uwValue; return true;
		case kBplcon1: sRegs.bplcon1 = uwValue; return true;
		case kBplcon2: sRegs.bplcon2 = uwValue; return true;
		case kBplcon3: sRegs.bplcon3 = uwValue; return true;
		case kBplcon4: sRegs.bplcon4 = uwValue; return true;
		case kBpl1mod: sRegs.bpl1mod = static_cast<int16_t>(uwValue); return true;
		case kBpl2mod: sRegs.bpl2mod = static_cast<int16_t>(uwValue); return true;
		case kFmode: sRegs.fmode = uwValue; return true;
		case kNoop: return true;
		default: return false;
	}
}

}  // namespace

bool beamCompare(uint16_t uwLine, uint16_t uwClock, uint16_t uwIr1, uint16_t uwIr2) {
	const uint16_t uwBeam = static_cast<uint16_t>(((uwLine & 0xFFu) << 8) | (uwClock & 0xFEu));
	const uint16_t uwPos = static_cast<uint16_t>(uwIr1 & 0xFFFEu);
	const uint16_t uwMask = static_cast<uint16_t>(0x8000u | (uwIr2 & 0x7FFEu));
	return (uwBeam & uwMask) >= (uwPos & uwMask);
}

void writeColour(planar::Palette &sPalette, uint32_t ulIndex, uint16_t uwValue, bool isLoct) {
	if(ulIndex >= planar::kPaletteSize) {
		return;
	}
	const uint32_t ulOld = sPalette.rgb[ulIndex];
	const uint32_t ulR = (uwValue >> 8) & 0xFu;
	const uint32_t ulG = (uwValue >> 4) & 0xFu;
	const uint32_t ulB = uwValue & 0xFu;
	if(isLoct) {
		sPalette.rgb[ulIndex] = (ulOld & 0x00F0F0F0u) | (ulR << 16) | (ulG << 8) | ulB;
	}
	else {
		sPalette.rgb[ulIndex] = planar::rgbAga(uwValue, uwValue);
	}
}

void begin(Player &sPlayer, const softblit::ChipView &sMem, uint32_t ulCop1lc) {
	sPlayer.mem = sMem;
	sPlayer.cop1lc = ulCop1lc;
	sPlayer.pc = ulCop1lc;
	sPlayer.now = 0;
	sPlayer.instructions = 0;
	sPlayer.isIdle = (ulCop1lc == 0);
	sPlayer.status = COPPER_OK;
	sPlayer.errAddr = 0;
	sPlayer.errWord1 = sPlayer.errWord2 = 0;
	// cop2lc keeps its value across frames, like the register
}

Status runTo(Player &sPlayer, Regs &sRegs, uint16_t uwLine, uint16_t uwClock) {
	const uint32_t ulLimit = static_cast<uint32_t>(uwLine) * kLineClocks + uwClock;
	while(!sPlayer.isIdle && sPlayer.now < ulLimit) {
		if(sPlayer.now >= kFrameClocks) {
			sPlayer.isIdle = true;
			break;
		}
		uint16_t uwIr1 = 0;
		uint16_t uwIr2 = 0;
		if(!fetch16(sPlayer.mem, sPlayer.pc, uwIr1) || !fetch16(sPlayer.mem, sPlayer.pc + 2u, uwIr2)) {
			fail(sPlayer, COPPER_BAD_ADDRESS, 0, 0);
			break;
		}
		++sPlayer.instructions;
		if((uwIr1 & 1u) == 0) {   // MOVE
			const uint16_t uwReg = static_cast<uint16_t>(uwIr1 & 0x1FEu);
			if(uwReg < 0x80 || !applyMove(sPlayer, sRegs, uwReg, uwIr2)) {
				fail(sPlayer, COPPER_UNSUPPORTED, uwReg, uwIr2);
				break;
			}
			sPlayer.pc += 4u;
			sPlayer.now += kInstructionClocks;
		}
		else if((uwIr2 & 1u) == 0) {   // WAIT
			const uint32_t ulAt = firstSatisfied(sPlayer.now + kInstructionClocks, uwIr1, uwIr2);
			sPlayer.pc += 4u;
			if(ulAt == kNever) {
				sPlayer.isIdle = true;
			}
			else {
				sPlayer.now = ulAt;
			}
		}
		else {   // SKIP
			sPlayer.now += kInstructionClocks;
			const bool isHeld = beamCompare(static_cast<uint16_t>(sPlayer.now / kLineClocks), static_cast<uint16_t>(sPlayer.now % kLineClocks),
			                                uwIr1, uwIr2);
			sPlayer.pc += isHeld ? 8u : 4u;
		}
	}
	return sPlayer.status;
}

Status runFrame(Player &sPlayer, Regs &sRegs) {
	return runTo(sPlayer, sRegs, kLines, 0);
}

const char *statusText(Status eStatus) {
	switch(eStatus) {
		case COPPER_OK: return "ok";
		case COPPER_BAD_ADDRESS: return "copper list outside chip RAM";
		case COPPER_UNSUPPORTED: return "unsupported copper register or instruction";
	}
	return "?";
}

void regsReset(Regs &sRegs) {
	planar::paletteClear(sRegs.palette);
	for(uint8_t i = 0; i < planar::kMaxPlanes; ++i) {
		sRegs.bplpt[i] = 0;
	}
	for(uint8_t i = 0; i < 8; ++i) {
		sRegs.sprpt[i] = 0;
	}
	sRegs.bpl1mod = sRegs.bpl2mod = 0;
	sRegs.bplcon0 = sRegs.bplcon1 = sRegs.bplcon2 = sRegs.bplcon3 = sRegs.bplcon4 = sRegs.fmode = 0;
	sRegs.diwstrt = 0x2C81;
	sRegs.diwstop = 0xF4C1;
	sRegs.ddfstrt = 0x0038;
	sRegs.ddfstop = 0x00D0;
	sRegs.dmacon = kDmaMask;
	sRegs.bplptWritten = 0;
	sRegs.sprptWritten = 0;
}

}  // namespace copper
}  // namespace ms
