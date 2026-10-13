// Host driver of tests/test_pc_input.py (ROADMAP 11.9): runs ms engine/pcinput over cases read from stdin, one case per line.
//   K u                       keymapRaw(u)                                  -> "K u raw"   (raw as two hex digits, "--" for none)
//   E u d u d ...             a fresh keyboard state, host key edges (d = 1 down, 0 up) -> "E <P|R|I><raw> ... F <raw> ..."  (press, release,
//                             ignored; then the keys keyboardNextHeld pops after the last event)
//   J A|N u d u d ...         the arrows / the empty joystick key set, edges     -> "J bits bits ..." (hex joyKeyBits after each edge)
//   D bits                    joyEncode(bits)                               -> "D bits enc dec" (dec = ms::joyDirections(enc))
//   P x y                     padStickBits(x, y)                            -> "P bits"
//   M sx sy f f f ...         mouse: scales (16.16 counts per pixel, hex), frames of "dx,dy;dx,dy" moves   -> per frame "M dat x y"
//                             (dat = JOY0DAT, x y = the game's pointer after ms::mouseStep of that sample, start 100,100, counters at 0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/input.hpp"
#include "engine/pcinput.hpp"

namespace {

void rawOut(uint8_t ubRaw) {
	if(ubRaw == ms::kRawNone) {
		printf("--");
	}
	else {
		printf("%02X", ubRaw);
	}
}

}  // namespace

int main() {
	static char szLine[1 << 16];
	while(fgets(szLine, sizeof(szLine), stdin) != nullptr) {
		char *szTok = strtok(szLine, " \r\n");
		if(szTok == nullptr) {
			continue;
		}
		const char cCmd = szTok[0];
		if(cCmd == 'K') {
			const unsigned uUsage = (unsigned)atoi(strtok(nullptr, " \r\n"));
			printf("K %u ", uUsage);
			rawOut(ms::keymapRaw((uint16_t)uUsage));
			printf("\n");
		}
		else if(cCmd == 'E') {
			ms::KeyboardState sState;
			ms::keyboardReset(sState);
			printf("E");
			for(char *szU = strtok(nullptr, " \r\n"); szU != nullptr; szU = strtok(nullptr, " \r\n")) {
				const unsigned uUsage = (unsigned)atoi(szU);
				const bool isDown = atoi(strtok(nullptr, " \r\n")) != 0;
				uint8_t ubRaw = 0xEE;
				const ms::KeyAction eAction = ms::keyboardEvent(sState, (uint16_t)uUsage, isDown, &ubRaw);
				printf(" %c", eAction == ms::KEY_PRESS ? 'P' : (eAction == ms::KEY_RELEASE ? 'R' : 'I'));
				if(eAction != ms::KEY_IGNORE) {
					printf("%02X", ubRaw);
				}
			}
			printf(" F");
			uint8_t ubHeld = 0;
			while(ms::keyboardNextHeld(sState, &ubHeld)) {
				printf(" %02X", ubHeld);
			}
			printf("\n");
		}
		else if(cCmd == 'J') {
			const ms::JoyKeySet &sSet = strtok(nullptr, " \r\n")[0] == 'A' ? ms::kJoyKeysArrows : ms::kJoyKeysNone;
			ms::JoyKeyState sState = {0};
			printf("J");
			for(char *szU = strtok(nullptr, " \r\n"); szU != nullptr; szU = strtok(nullptr, " \r\n")) {
				const unsigned uUsage = (unsigned)atoi(szU);
				const bool isDown = atoi(strtok(nullptr, " \r\n")) != 0;
				ms::joyKeyEvent(sState, sSet, (uint16_t)uUsage, isDown);
				printf(" %x", ms::joyKeyBits(sState));
			}
			printf("\n");
		}
		else if(cCmd == 'D') {
			const unsigned uBits = (unsigned)strtoul(strtok(nullptr, " \r\n"), nullptr, 16);
			const uint16_t uwEnc = ms::joyEncode((uint16_t)uBits);
			printf("D %x %x %x\n", uBits, uwEnc, ms::joyDirections(uwEnc));
		}
		else if(cCmd == 'P') {
			const int lX = atoi(strtok(nullptr, " \r\n"));
			const int lY = atoi(strtok(nullptr, " \r\n"));
			printf("P %x\n", ms::padStickBits((int16_t)lX, (int16_t)lY));
		}
		else if(cCmd == 'M') {
			const int32_t lSx = (int32_t)strtol(strtok(nullptr, " \r\n"), nullptr, 16);
			const int32_t lSy = (int32_t)strtol(strtok(nullptr, " \r\n"), nullptr, 16);
			ms::MouseCounters sMouse;
			ms::mouseReset(sMouse);
			ms::MouseState sGame = {};
			sGame.wX = 100;
			sGame.wY = 100;
			sGame.wButton = -1;
			sGame.wButton2 = -1;
			for(char *szFrame = strtok(nullptr, " \r\n"); szFrame != nullptr; szFrame = strtok(nullptr, " \r\n")) {
				ms::mouseBeginFrame(sMouse);
				if(strcmp(szFrame, "-") != 0) {
					const char *p = szFrame;
					int lDx = 0, lDy = 0, lUsed = 0;
					while(*p != 0 && sscanf(p, "%d,%d%n", &lDx, &lDy, &lUsed) == 2) {
						ms::mouseMove(sMouse, lDx, lDy, lSx, lSy);
						p += lUsed;
						if(*p == ';') {
							++p;
						}
					}
				}
				ms::mouseStep(sGame, ms::mouseDat(sMouse), false);
				printf("M %04X %d %d\n", ms::mouseDat(sMouse), sGame.wX, sGame.wY);
			}
		}
	}
	return 0;
}
