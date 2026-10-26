// engine/paula - see include/engine/paula.hpp for the model.  Integer arithmetic only.
//
// Time bookkeeping: one output frame is kClockPal "units" long, one Paula clock tick is ulRate units (so a byte of period P lasts
// P * ulRate units and a frame lasts kClockPal units; both are exact integers for any rate).  A channel integrates
// (byte value * duration) over the frame; the frame's sample is that integral over the frame length, scaled by the volume.
#include "engine/paula.hpp"

#include "engine/endian.hpp"
#include "engine/paulablep.hpp"

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

// The steps of one channel's output in one frame, for RENDER_BLEP: the chip time of each (units from the frame start, 0..kClockPal) and the
// change of the level (byte * volume).
struct Steps {
	uint32_t ulCount;
	uint32_t aulAt[kMaxStepsPerFrame];
	int32_t alDelta[kMaxStepsPerFrame];
};

void addStep(Steps *pSteps, uint32_t ulAt, int32_t lDelta) {
	if(pSteps == nullptr || lDelta == 0) {
		return;
	}
	if(pSteps->ulCount < kMaxStepsPerFrame) {
		pSteps->aulAt[pSteps->ulCount] = ulAt;
		pSteps->alDelta[pSteps->ulCount] = lDelta;
		++pSteps->ulCount;
	}
	else {   // cannot happen (a byte outlasts a frame); the surplus merges into the last step
		pSteps->aulAt[kMaxStepsPerFrame - 1] = ulAt;
		pSteps->alDelta[kMaxStepsPerFrame - 1] += lDelta;
	}
}

// Integral of the channel's output over one frame, times its volume (RENDER_BOX's sample).  With pSteps (RENDER_BLEP) it also lists the
// steps of the output level in the frame: the walk through chip time is the same, so both renderers see the same interrupts.
int64_t integrateFrame(Paula &p, uint8_t ubCh, const Hooks &hooks, Steps *pSteps) {
	Channel &ch = p.ch[ubCh];
	const int64_t llVol = ch.ubVol;
	int64_t llAcc = 0;
	uint64_t ullLeft = kClockPal;
	int32_t lLevel = ch.lLevel;
	if(pSteps != nullptr) {   // a volume change, or the first byte of a started channel, steps at the frame start
		const int32_t lNow = static_cast<int32_t>(ch.bOut) * static_cast<int32_t>(llVol);
		addStep(pSteps, 0, lNow - lLevel);
		lLevel = lNow;
	}
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
			if(pSteps != nullptr) {
				const int32_t lNow = static_cast<int32_t>(ch.bOut) * static_cast<int32_t>(llVol);
				addStep(pSteps, static_cast<uint32_t>(kClockPal - ullLeft), lNow - lLevel);
				lLevel = lNow;
			}
		}
	}
	ch.lLevel = lLevel;
	return llAcc * llVol;
}

// One output frame of chip time: the channels that were started run their first fetch, the pending requests are delivered, every channel is
// walked through the frame.  pInt gets the box integrals, pSteps (if not null, one per channel) the steps.
void stepFrame(Paula &p, const Hooks &hooks, int64_t *pInt, Steps *pSteps) {
	for(uint8_t c = 0; c < kChannels; ++c) {
		if(p.ch[c].ubState == CH_STARTING) {
			beginChannel(p, c, hooks);
		}
	}
	deliver(p, hooks);
	for(uint8_t c = 0; c < kChannels; ++c) {
		pInt[c] = integrateFrame(p, c, hooks, pSteps != nullptr ? &pSteps[c] : nullptr);
	}
}

// The optional one-pole low-pass of the output.
void lowPass(Paula &p, int32_t &lL, int32_t &lR) {
	if(p.cfg.ulLowPass != 0) {
		p.lLpL += static_cast<int32_t>((static_cast<int64_t>(lL - p.lLpL) * p.cfg.ulLowPass) >> 16);
		p.lLpR += static_cast<int32_t>((static_cast<int64_t>(lR - p.lLpR) * p.cfg.ulLowPass) >> 16);
		lL = p.lLpL;
		lR = p.lLpR;
	}
}

constexpr int64_t kBlepUnit = static_cast<int64_t>(1) << kBlepShift;

// Adds a step of dL / dR (level change times the speaker weights) at chip time ulAt of frame ulFrame to the frames' sums: the band-limited
// residual, interpolated between the two nearest table columns.
void addBlepStep(Paula &p, const int32_t *pKernel, uint32_t ulAt, int64_t llDeltaL, int64_t llDeltaR) {
	const uint64_t ullQ = (static_cast<uint64_t>(ulAt) * kBlepPhases << 16) / kClockPal;
	const uint32_t ulPhase = static_cast<uint32_t>(ullQ >> 16);
	const int64_t llFrac = static_cast<int64_t>(ullQ & 0xFFFF);
	for(uint32_t t = 0; t < kBlepTaps; ++t) {
		const int32_t *pRow = pKernel + t * kBlepColumns + ulPhase;
		int64_t llV = pRow[0];
		if(llFrac != 0) {
			llV += (static_cast<int64_t>(pRow[1] - pRow[0]) * llFrac) >> 16;
		}
		const uint32_t ulSlot = (p.ulFrame + t - kBlepDelay) & (kBlepRing - 1);
		p.allRing[0][ulSlot] += llDeltaL * llV;
		p.allRing[1][ulSlot] += llDeltaR * llV;
	}
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
	if(p.cfg.ubRender > RENDER_BLEP) {
		p.cfg.ubRender = RENDER_BOX;
	}
	if(p.cfg.ubRender == RENDER_BLEP) {
		blepKernel();   // build the table now, not in the middle of a frame
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
	const bool isBlep = p.cfg.ubRender == RENDER_BLEP;
	const int32_t *pKernel = isBlep ? blepKernel() : nullptr;
	const int64_t llDen = static_cast<int64_t>(kClockPal) * 100;
	const int64_t llBlepDen = static_cast<int64_t>(100) * kBlepUnit;
	for(uint32_t f = 0; f < ulFrames; ++f) {
		int64_t allI[kChannels];
		int32_t lL;
		int32_t lR;
		if(!isBlep) {
			stepFrame(p, hooks, allI, nullptr);
			const int64_t llL = allI[0] + allI[3];   // the Amiga: 0 and 3 left, 1 and 2 right
			const int64_t llR = allI[1] + allI[2];
			lL = static_cast<int32_t>(clamp16(floorDiv(llL * (100 + llSep) + llR * (100 - llSep), llDen)));
			lR = static_cast<int32_t>(clamp16(floorDiv(llR * (100 + llSep) + llL * (100 - llSep), llDen)));
		}
		else {
			Steps asSteps[kChannels];
			for(uint8_t c = 0; c < kChannels; ++c) {
				asSteps[c].ulCount = 0;
			}
			stepFrame(p, hooks, allI, asSteps);
			for(uint8_t c = 0; c < kChannels; ++c) {
				const bool isLeft = c == 0 || c == 3;
				const int64_t llOwn = 100 + llSep;
				const int64_t llOther = 100 - llSep;
				for(uint32_t s = 0; s < asSteps[c].ulCount; ++s) {
					const int64_t llDelta = asSteps[c].alDelta[s];
					addBlepStep(p, pKernel, asSteps[c].aulAt[s], llDelta * (isLeft ? llOwn : llOther), llDelta * (isLeft ? llOther : llOwn));
				}
			}
			// the held level at the end of the frame (the naive sample), then the frame kBlepDelay back is complete
			const int64_t llLevelL = static_cast<int64_t>(p.ch[0].lLevel) + p.ch[3].lLevel;
			const int64_t llLevelR = static_cast<int64_t>(p.ch[1].lLevel) + p.ch[2].lLevel;
			const uint32_t ulNow = p.ulFrame & (kBlepRing - 1);
			p.allRing[0][ulNow] += (llLevelL * (100 + llSep) + llLevelR * (100 - llSep)) * kBlepUnit;
			p.allRing[1][ulNow] += (llLevelR * (100 + llSep) + llLevelL * (100 - llSep)) * kBlepUnit;
			const uint32_t ulOut = (p.ulFrame - kBlepDelay) & (kBlepRing - 1);
			lL = static_cast<int32_t>(clamp16(floorDiv(p.allRing[0][ulOut] + llBlepDen / 2, llBlepDen)));
			lR = static_cast<int32_t>(clamp16(floorDiv(p.allRing[1][ulOut] + llBlepDen / 2, llBlepDen)));
			p.allRing[0][ulOut] = 0;
			p.allRing[1][ulOut] = 0;
			++p.ulFrame;
		}
		lowPass(p, lL, lR);
		pOut[2 * f] = static_cast<int16_t>(lL);
		pOut[2 * f + 1] = static_cast<int16_t>(lR);
	}
}

}  // namespace paula
}  // namespace ms
