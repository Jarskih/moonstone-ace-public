// engine/synthinst - mog's synth instrument rows (S_45 LAB_10A2) decoded into ms::SynthInstrument (ROADMAP 10.2a; the same
// rule as tools/gen_synth_tables.py, which wrote them into a compiled-in table before). Pure: host tests compare the two.
#include "engine/synth_data.hpp"

namespace ms {

namespace {

// LAB_0FD4 as ranges of instrument indices ((LAB_10Ax offset - 168) / 14) -> sample bank; later rules overwrite earlier ones.
// Rows 0..8 are the S_45 waveforms (bank 0).
struct BankRule {
	uint8_t ubFrom, ubTo, ubBank;
};
constexpr BankRule kBankRules[] = {
	{9, 17, 1},      // LAB_10A3..LAB_10A4: += LAB_05C7
	{17, 88, 2},     // LAB_10A4..LAB_10A7: += LAB_05C8
	{88, 96, 5},     // LAB_10A7..LAB_10A8: += LAB_05CB
	{96, 103, 2},    // LAB_10A8..LAB_10A9: += LAB_05C8
	{39, 41, 1},     // LAB_10A5, +14: MOVE.L LAB_05C7,6(A0)
	{58, 61, 1},     // LAB_10A6, +14, +28: LAB_05C7
	{103, 113, 3},   // LAB_10A9..LAB_10AA: LAB_05C9
	{113, 115, 4},   // LAB_10AA..LAB_10AB: LAB_05CA
	{115, 128, 2},   // LAB_10AB..LAB_10AC: LAB_05C8
	{128, 130, 3},   // LAB_10AC..LAB_10AD: LAB_05C9
	{130, 131, 2},   // LAB_10AD..LAB_10AE: LAB_05C8
};

uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

}  // namespace

void synthDecodeInstruments(const uint8_t *pRaw, SynthInstrument *pOut) {
	for(unsigned i = 0; i < kSynthInstCount; ++i) {
		const uint8_t *p = pRaw + i * kSynthInstRow;
		SynthInstrument &s = pOut[i];
		s.wLoop = (int16_t)be16(p);
		s.uwLoopOff = be16(p + 2);
		s.uwWords = be16(p + 4);
		s.ulOffset = be32(p + 6);
		s.uwPitch = (uint16_t)be32(p + 10);
		uint8_t ubBank = 0;
		if(i >= 9) {
			for(const BankRule &r : kBankRules) {
				if(r.ubFrom <= i && i < r.ubTo) {
					ubBank = r.ubBank;
				}
			}
		}
		s.ubBank = ubBank;
	}
}

}  // namespace ms
