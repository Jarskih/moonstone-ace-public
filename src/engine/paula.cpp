// engine/paula - see include/engine/paula.hpp for the model.  Integer arithmetic only.
//
// Time bookkeeping: one output frame is kClockPal "units" long, one Paula clock tick is ulRate units (so a byte of period P lasts
// P * ulRate units and a frame lasts kClockPal units; both are exact integers for any rate).  A channel integrates
// (byte value * duration) over the frame; the frame's sample is that integral over the frame length, scaled by the volume.
#include "engine/paula.hpp"

#include "engine/endian.hpp"

namespace ms {
namespace paula {

namespace {

constexpr uint16_t kDmaMask = kDmaEn | 0x000F;
constexpr uint16_t kRegChannelsEnd = kRegAud + 0x10 * kChannels;

uint16_t setClr(uint16_t uwOld, uint16_t uwValue) {
	const uint16_t uwBits = static_cast<uint16_t>(uwValue & 0x7FFF);
	return (uwValue & kSetClr) ? static_cast<uint16_t>(uwOld | uwBits) : static_cast<uint16_t>(uwOld & ~uwBits);
}

int64_t floorDiv(int64_t llA, int64_t llB) {   // llB > 0
	int64_t llQ = llA / llB;
	if((llA % llB) != 0 && llA < 0) {
		--llQ;
	}
	return llQ;
}

int16_t clamp16(int64_t llV) {
	if(llV > 32767) {
		return 32767;
	}
	if(llV < -32768) {
		return -32768;
	}
	return static_cast<int16_t>(llV);
}

// The word at chip address ulAddr (big-endian bytes); 0 outside the view.
uint16_t readWord(const softblit::ChipView &mem, uint32_t ulAddr) {
	const uint32_t ulOff = ulAddr - mem.origin;
	if(mem.size != 0 && (ulAddr < mem.origin || ulOff > mem.size - 2u)) {
		return 0;
	}
	return be::ld16(mem.bytes + ulOff);
}

bool isEnabledByDma(const Paula &p, uint8_t ubCh) {
	return (p.uwDmacon & kDmaEn) != 0 && (p.uwDmacon & (1u << ubCh)) != 0;
}

// Make the channel states follow DMACON: a newly enabled channel waits for the next time advance, a disabled one stops (holding its output).
void followDmacon(Paula &p) {
	for(uint8_t c = 0; c < kChannels; ++c) {
		Channel &ch = p.ch[c];
		if(isEnabledByDma(p, c)) {
			if(ch.ubState == CH_IDLE) {
				ch.ubState = CH_STARTING;
			}
		}
		else {
			ch.ubState = CH_IDLE;
		}
	}
}

// Deliver the enabled AUDx requests: to the hook, or by acknowledging them.
void deliver(Paula &p, const Hooks &hooks) {
	const uint16_t uwPending = static_cast<uint16_t>(p.uwIntreq & p.uwIntena & kIntAudAll);
	if(uwPending == 0 || (p.uwIntena & kIntEn) == 0) {
		return;
	}
	if(hooks.pfnMasterOn != nullptr && !hooks.pfnMasterOn(hooks.pCtx)) {
		return;
	}
	if(hooks.pfnInterrupt != nullptr) {
		hooks.pfnInterrupt(hooks.pCtx);
	}
	else {
		p.uwIntreq = static_cast<uint16_t>(p.uwIntreq & ~uwPending);
	}
}

uint64_t periodUnits(const Paula &p, const Channel &ch) {
	uint32_t ulPer = ch.uwPer;
	if(ulPer == 0) {
		ulPer = 65536;
	}
	else if(ulPer < kPeriodMin) {
		ulPer = kPeriodMin;
	}
	return static_cast<uint64_t>(ulPer) * p.cfg.ulRate;
}

// One word fetch.  A counter at 0 reloads from LC / LEN and raises the channel's request first (the DMA start is such a reload too).
void fetchWord(Paula &p, uint8_t ubCh) {
	Channel &ch = p.ch[ubCh];
	if(ch.ulCnt == 0) {
		ch.ulPtr = ch.ulLc & ~1u;
		ch.ulCnt = ch.uwLen != 0 ? ch.uwLen : 65536u;
		p.uwIntreq = static_cast<uint16_t>(p.uwIntreq | intAudBit(ubCh));
	}
	ch.uwWord = readWord(p.mem, ch.ulPtr);
	ch.ulPtr += 2;
	--ch.ulCnt;
}

// STARTING -> PLAYING: the first fetch (a reload), the interrupt it raises, then the first byte.
void beginChannel(Paula &p, uint8_t ubCh, const Hooks &hooks) {
	Channel &ch = p.ch[ubCh];
	ch.ulCnt = 0;
	fetchWord(p, ubCh);
	deliver(p, hooks);
	if(ch.ubState != CH_STARTING) {   // the handler switched the DMA off again
		return;
	}
	ch.ubState = CH_PLAYING;
	ch.ubHalf = 0;
	ch.bOut = static_cast<int8_t>(ch.uwWord >> 8);
	ch.ullRem = periodUnits(p, ch);
}

// The byte in play ended.
void advanceChannel(Paula &p, uint8_t ubCh, const Hooks &hooks) {
	Channel &ch = p.ch[ubCh];
	if(ch.ubHalf == 0) {
		ch.ubHalf = 1;
		ch.bOut = static_cast<int8_t>(ch.uwWord & 0xFF);
	}
	else {
		fetchWord(p, ubCh);
		deliver(p, hooks);
		if(ch.ubState != CH_PLAYING) {   // the handler switched the DMA off: the old byte stays on the output
			return;
		}
		ch.ubHalf = 0;
		ch.bOut = static_cast<int8_t>(ch.uwWord >> 8);
	}
	ch.ullRem = periodUnits(p, ch);
}

// Integral of the channel's output over one frame, times its volume.
int64_t integrateFrame(Paula &p, uint8_t ubCh, const Hooks &hooks) {
	Channel &ch = p.ch[ubCh];
	const int64_t llVol = ch.ubVol;
	int64_t llAcc = 0;
	uint64_t ullLeft = kClockPal;
	while(ullLeft != 0) {
		if(ch.ubState != CH_PLAYING) {
			llAcc += static_cast<int64_t>(ch.bOut) * static_cast<int64_t>(ullLeft);
			break;
		}
		const uint64_t ullTake = ch.ullRem < ullLeft ? ch.ullRem : ullLeft;
		llAcc += static_cast<int64_t>(ch.bOut) * static_cast<int64_t>(ullTake);
		ch.ullRem -= ullTake;
		ullLeft -= ullTake;
		if(ch.ullRem == 0) {
			advanceChannel(p, ubCh, hooks);
		}
	}
	return llAcc * llVol;
}

}  // namespace

void reset(Paula &p, const Config &cfg, const softblit::ChipView &mem) {
	uint8_t *pBytes = reinterpret_cast<uint8_t *>(&p);
	for(uint32_t i = 0; i < sizeof(Paula); ++i) {
		pBytes[i] = 0;
	}
	p.cfg = cfg;
	if(p.cfg.ulRate == 0) {
		p.cfg.ulRate = 48000;
	}
	if(p.cfg.ubSeparation > 100) {
		p.cfg.ubSeparation = 100;
	}
	p.mem = mem;
	p.uwDmacon = kDmaEn;
	p.uwIntena = kIntEn;
}

void write16(Paula &p, uint16_t uwReg, uint16_t uwValue) {
	if(uwReg >= kRegAud && uwReg < kRegChannelsEnd) {
		const uint8_t ubCh = static_cast<uint8_t>((uwReg - kRegAud) >> 4);
		Channel &ch = p.ch[ubCh];
		switch((uwReg - kRegAud) & 0x0F) {
			case 0:
				ch.ulLc = (ch.ulLc & 0x0000FFFFu) | (static_cast<uint32_t>(uwValue) << 16);
				break;
			case 2:
				ch.ulLc = (ch.ulLc & 0xFFFF0000u) | uwValue;
				break;
			case 4:
				ch.uwLen = uwValue;
				break;
			case 6:
				ch.uwPer = uwValue;
				break;
			case 8: {
				const uint16_t uwVol = static_cast<uint16_t>(uwValue & 0x7F);
				ch.ubVol = static_cast<uint8_t>(uwVol > 64 ? 64 : uwVol);
				break;
			}
			default:   // AUDxDAT: manual playback is not modelled
				break;
		}
		return;
	}
	switch(uwReg) {
		case kRegDmacon:
			p.uwDmacon = static_cast<uint16_t>(setClr(p.uwDmacon, uwValue) & kDmaMask);
			followDmacon(p);
			break;
		case kRegIntena:
			p.uwIntena = setClr(p.uwIntena, uwValue);
			break;
		case kRegIntreq:
			p.uwIntreq = setClr(p.uwIntreq, uwValue);
			break;
		case kRegAdkcon:
			p.uwAdkcon = setClr(p.uwAdkcon, uwValue);
			break;
		default:
			break;
	}
}

void write32(Paula &p, uint16_t uwReg, uint32_t ulValue) {
	write16(p, uwReg, static_cast<uint16_t>(ulValue >> 16));
	write16(p, static_cast<uint16_t>(uwReg + 2), static_cast<uint16_t>(ulValue));
}

uint16_t read16(const Paula &p, uint16_t uwReg) {
	switch(uwReg) {
		case kRegDmaconr:
			return p.uwDmacon;
		case kRegAdkconr:
			return p.uwAdkcon;
		case kRegIntenar:
			return p.uwIntena;
		case kRegIntreqr:
			return p.uwIntreq;
		default:
			return 0;
	}
}

bool adkModulationRequested(const Paula &p) {
	return (p.uwAdkcon & 0x00FF) != 0;
}

void render(Paula &p, int16_t *pOut, uint32_t ulFrames, const Hooks &hooks) {
	const int64_t llSep = p.cfg.ubSeparation;
	for(uint32_t f = 0; f < ulFrames; ++f) {
		for(uint8_t c = 0; c < kChannels; ++c) {
			if(p.ch[c].ubState == CH_STARTING) {
				beginChannel(p, c, hooks);
			}
		}
		deliver(p, hooks);
		int64_t allI[kChannels];
		for(uint8_t c = 0; c < kChannels; ++c) {
			allI[c] = integrateFrame(p, c, hooks);
		}
		const int64_t llL = allI[0] + allI[3];   // the Amiga: 0 and 3 left, 1 and 2 right
		const int64_t llR = allI[1] + allI[2];
		const int64_t llDen = static_cast<int64_t>(kClockPal) * 100;
		int32_t lL = static_cast<int32_t>(clamp16(floorDiv(llL * (100 + llSep) + llR * (100 - llSep), llDen)));
		int32_t lR = static_cast<int32_t>(clamp16(floorDiv(llR * (100 + llSep) + llL * (100 - llSep), llDen)));
		if(p.cfg.ulLowPass != 0) {
			p.lLpL += static_cast<int32_t>((static_cast<int64_t>(lL - p.lLpL) * p.cfg.ulLowPass) >> 16);
			p.lLpR += static_cast<int32_t>((static_cast<int64_t>(lR - p.lLpR) * p.cfg.ulLowPass) >> 16);
			lL = p.lLpL;
			lR = p.lLpR;
		}
		pOut[2 * f] = static_cast<int16_t>(lL);
		pOut[2 * f + 1] = static_cast<int16_t>(lR);
	}
}

}  // namespace paula
}  // namespace ms
