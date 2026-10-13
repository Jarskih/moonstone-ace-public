// engine/modplay - see include/engine/modplay.hpp.
#include "engine/modplay.hpp"

#include "engine/endian.hpp"

namespace ms {

namespace {

constexpr uint16_t kRegDmacon = 0x96;
constexpr uint16_t kRegAud = 0xA0;
constexpr uint16_t kPeriodLow = 113;
constexpr uint16_t kPeriodHigh = 856;
constexpr uint32_t kSampleHeaders = 20;
constexpr uint32_t kSampleHeaderBytes = 30;

// ProTracker finetune 0 period table, notes C-1 .. B-3.
const uint16_t kPeriods[36] = {
	856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
	428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
	214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113};

uint16_t regBase(uint8_t ubCh) { return static_cast<uint16_t>(kRegAud + 0x10u * ubCh); }

// Sample header fields (sample number 1..31).
const uint8_t *sampleHeader(const ModPlayer &mp, uint8_t ubSample) {
	return mp.pMod + kSampleHeaders + kSampleHeaderBytes * (ubSample - 1u);
}
uint16_t smpWords(const ModPlayer &mp, uint8_t n) { return be::ld16(sampleHeader(mp, n) + 22); }
uint8_t smpVol(const ModPlayer &mp, uint8_t n) { return sampleHeader(mp, n)[25]; }
uint16_t smpRepStart(const ModPlayer &mp, uint8_t n) { return be::ld16(sampleHeader(mp, n) + 26); }
uint16_t smpRepLen(const ModPlayer &mp, uint8_t n) { return be::ld16(sampleHeader(mp, n) + 28); }

uint16_t clampPeriod(int32_t lPeriod) {
	if(lPeriod < kPeriodLow) {
		return kPeriodLow;
	}
	if(lPeriod > kPeriodHigh) {
		return kPeriodHigh;
	}
	return static_cast<uint16_t>(lPeriod);
}

// Index of the table entry of the period (the nearest not below... ProTracker takes the first entry <= the period from the top); -1 if none.
int32_t tableIndex(uint16_t uwPeriod) {
	for(int32_t i = 0; i < 36; ++i) {
		if(kPeriods[i] <= uwPeriod) {
			return i;
		}
	}
	return -1;
}

uint8_t scaledVol(const ModPlayer &mp, uint8_t ubVol) {
	return static_cast<uint8_t>((static_cast<uint32_t>(ubVol) * mp.ubMaster) / 64u);
}

void startNote(ModPlayer &mp, const ModHw &hw, uint8_t ubCh) {
	ModChannel &ch = mp.ch[ubCh];
	const uint8_t ubSmp = ch.ubSample;
	const uint16_t uwWords = smpWords(mp, ubSmp);
	const uint16_t uwRepLen = smpRepLen(mp, ubSmp);
	uint16_t uwFirst = uwWords;
	if(uwRepLen > 1) {
		uwFirst = static_cast<uint16_t>(smpRepStart(mp, ubSmp) + uwRepLen);   // intro and the first pass of the loop
		if(uwFirst > uwWords) {
			uwFirst = uwWords;
		}
	}
	hw.pfnWrite16(hw.pCtx, kRegDmacon, static_cast<uint16_t>(1u << ubCh));
	hw.pfnWrite32(hw.pCtx, regBase(ubCh), mp.ulAddr + mp.aulSampleOff[ubSmp - 1u]);
	hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(ubCh) + 4), uwFirst != 0 ? uwFirst : 1);
	ch.isTrigger = true;
}

void processRow(ModPlayer &mp) {
	const uint8_t *pRow = mp.pMod + kModHeaderBytes + kModPatternBytes * mp.pMod[952 + mp.ubOrder] + 16u * mp.ubRow;
	for(uint8_t c = 0; c < 4; ++c) {
		ModChannel &ch = mp.ch[c];
		const uint8_t *pNote = pRow + 4u * c;
		const uint8_t ubSmp = static_cast<uint8_t>((pNote[0] & 0xF0) | (pNote[2] >> 4));
		const uint16_t uwPeriod = static_cast<uint16_t>(((pNote[0] & 0x0F) << 8) | pNote[1]);
		ch.ubCmd = pNote[2] & 0x0F;
		ch.ubParam = pNote[3];
		if(ubSmp >= 1 && ubSmp <= 31) {
			ch.ubSample = ubSmp;
			ch.ubVol = smpVol(mp, ubSmp) > 64 ? 64 : smpVol(mp, ubSmp);
		}
		ch.isTrigger = false;
		if(uwPeriod != 0 && ch.ubSample != 0) {
			ch.uwPeriod = uwPeriod;
			ch.uwNotePeriod = uwPeriod;
			ch.isTrigger = true;   // the register part is written by modTick, in channel order
		}
		// effects of tick 0
		switch(ch.ubCmd) {
			case 0xC:
				ch.ubVol = ch.ubParam > 64 ? 64 : ch.ubParam;
				break;
			case 0xF:
				if(ch.ubParam >= 1 && ch.ubParam < 32) {
					mp.ubSpeed = ch.ubParam;
				}
				break;
			case 0xB:
				mp.wJumpOrder = ch.ubParam;
				break;
			case 0xD:
				mp.wBreakRow = static_cast<int16_t>((ch.ubParam >> 4) * 10 + (ch.ubParam & 15));
				break;
			default:
				break;
		}
	}
}

void tickEffects(ModPlayer &mp) {
	for(uint8_t c = 0; c < 4; ++c) {
		ModChannel &ch = mp.ch[c];
		switch(ch.ubCmd) {
			case 0:
				if(ch.ubParam != 0) {
					const int32_t lBase = tableIndex(ch.uwNotePeriod);
					int32_t lOff = 0;
					switch(mp.ubTick % 3) {
						case 1: lOff = ch.ubParam >> 4; break;
						case 2: lOff = ch.ubParam & 15; break;
						default: break;
					}
					if(lBase >= 0) {
						const int32_t lIdx = lBase + lOff;
						ch.uwPeriod = lIdx < 36 ? kPeriods[lIdx] : kPeriods[35];
					}
				}
				break;
			case 1:
				ch.uwPeriod = clampPeriod(static_cast<int32_t>(ch.uwPeriod) - ch.ubParam);
				break;
			case 2:
				ch.uwPeriod = clampPeriod(static_cast<int32_t>(ch.uwPeriod) + ch.ubParam);
				break;
			case 0xA: {
				int32_t lVol = ch.ubVol;
				if(ch.ubParam & 0xF0) {
					lVol += ch.ubParam >> 4;
				}
				else {
					lVol -= ch.ubParam & 15;
				}
				ch.ubVol = static_cast<uint8_t>(lVol < 0 ? 0 : (lVol > 64 ? 64 : lVol));
				break;
			}
			default:
				break;
		}
	}
}

void advanceRow(ModPlayer &mp) {
	uint8_t ubRow = static_cast<uint8_t>(mp.ubRow + 1);
	uint8_t ubOrder = mp.ubOrder;
	bool isNewOrder = false;
	if(mp.wBreakRow >= 0) {
		ubRow = static_cast<uint8_t>(mp.wBreakRow < 64 ? mp.wBreakRow : 0);
		isNewOrder = true;
	}
	if(mp.wJumpOrder >= 0) {
		ubOrder = static_cast<uint8_t>(mp.wJumpOrder);
		if(mp.wBreakRow < 0) {
			ubRow = 0;
		}
	}
	else if(isNewOrder || ubRow >= 64) {
		ubOrder = static_cast<uint8_t>(ubOrder + 1);
		if(!isNewOrder) {
			ubRow = 0;
		}
	}
	if(ubOrder >= mp.ubSongLen) {
		ubOrder = 0;   // the original loops to the first position
	}
	mp.ubOrder = ubOrder;
	mp.ubRow = ubRow;
	mp.wJumpOrder = -1;
	mp.wBreakRow = -1;
}

}  // namespace

bool modStart(ModPlayer &mp, const uint8_t *pModule, uint32_t ulModuleAddr, uint32_t ulSilenceAddr) {
	uint8_t *pBytes = reinterpret_cast<uint8_t *>(&mp);
	for(uint32_t i = 0; i < sizeof(ModPlayer); ++i) {
		pBytes[i] = 0;
	}
	if(pModule == nullptr || pModule[950] == 0) {
		return false;
	}
	mp.pMod = pModule;
	mp.ulAddr = ulModuleAddr;
	mp.ulSilence = ulSilenceAddr;
	mp.ubSongLen = pModule[950] > 128 ? 128 : pModule[950];
	uint8_t ubLast = 0;
	for(uint8_t i = 0; i < 128; ++i) {
		if(pModule[952 + i] > ubLast) {
			ubLast = pModule[952 + i];
		}
	}
	uint32_t ulOff = kModHeaderBytes + (static_cast<uint32_t>(ubLast) + 1) * kModPatternBytes;
	for(uint8_t i = 1; i <= 31; ++i) {
		mp.aulSampleOff[i - 1] = ulOff;
		ulOff += static_cast<uint32_t>(be::ld16(pModule + kSampleHeaders + kSampleHeaderBytes * (i - 1u) + 22)) * 2u;
	}
	mp.ubSpeed = 6;
	mp.ubMaster = 64;
	mp.wJumpOrder = -1;
	mp.wBreakRow = -1;
	mp.isPlaying = true;
	return true;
}

void modTick(ModPlayer &mp, const ModHw &hw) {
	if(!mp.isPlaying) {
		return;
	}
	if(mp.ubTick == 0) {
		processRow(mp);
	}
	else {
		tickEffects(mp);
	}
	uint16_t uwStart = 0;
	for(uint8_t c = 0; c < 4; ++c) {
		ModChannel &ch = mp.ch[c];
		if(ch.isTrigger && mp.ubTick == 0) {
			startNote(mp, hw, c);
			uwStart = static_cast<uint16_t>(uwStart | (1u << c));
		}
		else {
			ch.isTrigger = false;
		}
		hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(c) + 6), ch.uwPeriod);
		hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(c) + 8), scaledVol(mp, ch.ubVol));
	}
	if(uwStart != 0) {
		hw.pfnWrite16(hw.pCtx, kRegDmacon, static_cast<uint16_t>(0x8000 | uwStart));
	}
	if(++mp.ubTick >= mp.ubSpeed) {
		mp.ubTick = 0;
		advanceRow(mp);
	}
}

bool modNeedsLoopPointers(const ModPlayer &mp) {
	for(uint8_t c = 0; c < 4; ++c) {
		if(mp.ch[c].isTrigger) {
			return true;
		}
	}
	return false;
}

void modLoopPointers(ModPlayer &mp, const ModHw &hw) {
	for(uint8_t c = 0; c < 4; ++c) {
		ModChannel &ch = mp.ch[c];
		if(!ch.isTrigger) {
			continue;
		}
		ch.isTrigger = false;
		const uint16_t uwRepLen = smpRepLen(mp, ch.ubSample);
		if(uwRepLen > 1) {
			hw.pfnWrite32(hw.pCtx, regBase(c), mp.ulAddr + mp.aulSampleOff[ch.ubSample - 1u] + 2u * smpRepStart(mp, ch.ubSample));
			hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(c) + 4), uwRepLen);
		}
		else {
			hw.pfnWrite32(hw.pCtx, regBase(c), mp.ulSilence);
			hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(c) + 4), 1);
		}
	}
}

void modSetMaster(ModPlayer &mp, const ModHw &hw, uint8_t ubMaster) {
	mp.ubMaster = ubMaster > 64 ? 64 : ubMaster;
	if(!mp.isPlaying) {
		return;
	}
	for(uint8_t c = 0; c < 4; ++c) {
		hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(c) + 8), scaledVol(mp, mp.ch[c].ubVol));
	}
}

void modStop(ModPlayer &mp, const ModHw &hw) {
	if(!mp.isPlaying) {
		return;
	}
	mp.isPlaying = false;
	hw.pfnWrite16(hw.pCtx, kRegDmacon, 0x000F);
	for(uint8_t c = 0; c < 4; ++c) {
		hw.pfnWrite16(hw.pCtx, static_cast<uint16_t>(regBase(c) + 8), 0);
	}
}

}  // namespace ms
