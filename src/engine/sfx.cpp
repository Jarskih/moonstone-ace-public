// engine/sfx - mog sound-request front end, see include/engine/sfx.hpp (ROADMAP 7.1b). Transcribes mog.asm
// 19274-19351 (S_16): SECSTRT_16..LAB_0AA2, LAB_0AA9.
#include "engine/sfx.hpp"

namespace ms {

uint8_t sfxPick(SfxState &s) {
	if((s.ubBusy & kSfxMask) == kSfxMask) {                      // MOVE.B LAB_0AA6,D1 ; ANDI.B #$0F ; CMP.B #$0F ; BEQ.S LAB_0AA4
		return kSfxFull;
	}
	do {                                                          // LAB_0AA3
		s.uwNext = (uint16_t)((s.uwNext + 1) & 3);                // ADDI.W #1 ; ANDI.W #3
	} while(s.ubBusy & (1u << s.uwNext));                         // BTST D1,LAB_0AA6 ; BNE.S LAB_0AA3
	return (uint8_t)s.uwNext;
}

void sfxReserve(SfxState &s, uint8_t ubChannel) {
	s.ubBusy |= (uint8_t)(1u << ubChannel);
}

void sfxRelease(SfxState &s, uint8_t ubChannel) {
	s.ubBusy &= (uint8_t)(kSfxMask & ~(1u << ubChannel));
}

void sfxClearBusy(SfxState &s) {
	s.ubBusy = 0;
}

void sfxInit(SfxState &s) {
	s.ubBusy = 0;
	s.uwNext = 0;
}

uint8_t sfxStopAll(SfxState &s, uint8_t apChosen[kSfxChannels]) {
	sfxClearBusy(s);
	for(uint8_t i = 0; i < kSfxChannels; ++i) {
		sfxRelease(s, i);
		apChosen[i] = sfxPick(s);
	}
	return apChosen[kSfxChannels - 1];
}

}  // namespace ms
