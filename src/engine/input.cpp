// engine/input - see include/engine/input.hpp (ROADMAP 7.1d). Transcribes program.asm 6245-6283 (LAB_032A tail), 6446-6577
// (LAB_034D, LAB_0359, LAB_035E) and mog.asm 2107-2160 (LAB_00EE, LAB_00F3); mog S_20 is the byte-identical twin of program S_15.
#include "engine/input.hpp"

namespace ms {

uint16_t joyDirections(uint16_t uwJoyDat) {
	uint16_t uwOut = 0;
	if(uwJoyDat & 0x0002) {                                          // BTST #1,D0
		uwOut |= kJoyRight;
	}
	if(uwJoyDat & 0x0200) {                                          // BTST #9,D0
		uwOut |= kJoyLeft;
	}
	const uint16_t uwX = (uint16_t)(((uwJoyDat << 1) ^ uwJoyDat) & 0xFFFF);  // LSL.W #1,D2 ; EOR.W D0,D2
	if(uwX & 0x0002) {                                               // BTST #1,D2
		uwOut |= kJoyDown;
	}
	if(uwX & 0x0200) {                                               // BTST #9,D2
		uwOut |= kJoyUp;
	}
	return uwOut;
}

JoyBits joyRead(uint16_t uwJoy0Dat, uint16_t uwJoy1Dat, bool isFire0, bool isFire1) {
	JoyBits sOut;
	uint16_t uwP1 = joyDirections(uwJoy1Dat);
	if(isFire1) {                                                    // BTST #7,CIAA_PRA ; BNE.S
		uwP1 |= kJoyFire;
	}
	sOut.uwPort1 = uwP1;
	uint16_t uwP0 = joyDirections(uwJoy0Dat);
	if((uwP0 & 3) == 3) {                                            // ANDI #3 ; EORI #3 ; BNE.S -> else MOVEQ #0,D1
		uwP0 = 0;
	}
	if((uwP0 & 0x0C) == 0x0C) {
		uwP0 = 0;
	}
	if(isFire0) {                                                    // BTST #6,CIAA_PRA
		uwP0 |= kJoyFire;
	}
	sOut.uwPort0 = uwP0;
	return sOut;
}

int8_t mouseDelta(uint8_t ubPrev, uint8_t ubNow) {
	if(ubPrev >= ubNow) {                                            // SUB.B D0,D2 : no borrow
		const uint8_t ubD = (uint8_t)(ubPrev - ubNow);
		if(ubD >= 0x80) {                                            // counter wrapped up: 0xFF - d (sic)
			return (int8_t)(0xFF - ubD);
		}
		return (int8_t)-(int8_t)ubD;                                 // LAB_034E: NEG
	}
	const uint8_t ubN = (uint8_t)(ubNow - ubPrev);                   // LAB_034F: NEG.B D2
	if(ubN >= 0x80) {                                                // counter wrapped down
		return (int8_t)-(int8_t)(0xFF - ubN);
	}
	return (int8_t)ubN;
}

void mouseClamp(MouseState &s) {
	if(s.wX < -7) {                                                  // CMPI.W #$fff9 ; BGE
		s.wX = -7;
	}
	if(s.wX > 0x013F) {                                              // CMPI.W #$013f ; BLE
		s.wX = 0x013F;
	}
	if(s.wY < -7) {
		s.wY = -7;
	}
	if(s.wY > 0x00C7) {
		s.wY = 0x00C7;
	}
}

bool mouseStep(MouseState &s, uint16_t uwJoy0Dat, bool isLeftDown) {
	const uint8_t ubV = (uint8_t)(uwJoy0Dat >> 8);                   // MOVE.B JOY0DAT,D0
	const uint8_t ubH = (uint8_t)uwJoy0Dat;                          // MOVE.B EXT_0035,D1 (JOY0DAT + 1)
	const int16_t wDy = mouseDelta(s.ubPrevV, ubV);                  // D4
	const int16_t wDx = mouseDelta(s.ubPrevH, ubH);                  // D5
	s.wY = (int16_t)(s.wY + wDy);                                    // ADD.W D4,LAB_0375
	s.wX = (int16_t)(s.wX + wDx);                                    // ADD.W D5,SECSTRT_17
	bool isWritten = false;
	if(wDy != 0 || wDx != 0) {
		s.ulIdleLong = 0x10;
		s.uwIdleWord = 0;
		isWritten = true;
	}
	mouseClamp(s);                                                   // JSR LAB_0359
	s.wButton = -1;
	s.wButton2 = -1;
	if(isLeftDown) {
		s.ulIdleLong = 0x10;
		s.wButton = 0;
		s.uwIdleWord = 0;
		isWritten = true;
	}
	s.ubPrevV = ubV;
	s.ubPrevH = ubH;
	return isWritten;
}

KeyEvent keyDecode(uint8_t ubSdr, const uint8_t *pXlat) {
	const uint8_t ubRor = (uint8_t)((ubSdr >> 1) | (ubSdr << 7));    // ROR.B #1,D0
	KeyEvent sEvent;
	sEvent.ubKey = pXlat[ubRor & 0x7F];
	sEvent.isDown = (ubRor & 0x80) != 0;
	return sEvent;
}

void keyApply(const KeyEvent &sEvent, volatile uint8_t *pDown, volatile uint8_t *pLast) {
	if(sEvent.isDown) {
		*pLast = sEvent.ubKey;                                       // MOVE.B D0,LAB_0362
	}
	pDown[sEvent.ubKey] = sEvent.isDown ? 1 : 0;                     // MOVE.B D0,0(A1,D1.W)
}

void keyClearAll(volatile uint8_t *pDown) {
	for(uint8_t i = 0; i < kKeyTableSize; ++i) {                     // MOVE.L #$7f,D0 ; CLR.B (A0)+ ; DBF
		pDown[i] = 0;
	}
}

}  // namespace ms
