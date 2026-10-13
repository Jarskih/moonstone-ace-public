// engine/nettick - see include/engine/nettick.hpp (ROADMAP 12.4).
#include "engine/nettick.hpp"

namespace ms {

bool keyQueuePush(KeyQueue &q, uint8_t ubCode) {
	const uint8_t ubHead = q.ubHead;
	const uint8_t ubNext = (uint8_t)((ubHead + 1) & (kNetKeyQueueSize - 1));
	if(ubNext == q.ubTail) {
		++q.uwDropped;
		return false;
	}
	q.aubCode[ubHead] = ubCode;
	q.ubHead = ubNext;               // published last: the consumer never sees a half-written slot
	return true;
}

bool keyQueuePop(KeyQueue &q, uint8_t &ubCode) {
	const uint8_t ubTail = q.ubTail;
	if(ubTail == q.ubHead) {
		return false;
	}
	ubCode = q.aubCode[ubTail];
	q.ubTail = (uint8_t)((ubTail + 1) & (kNetKeyQueueSize - 1));
	return true;
}

void keyQueueClear(KeyQueue &q) {
	q.ubTail = q.ubHead;
	q.uwDropped = 0;
}

void localFrameClear(LocalFrame &f) {
	f.joy.uwPort0 = 0;
	f.joy.uwPort1 = 0;
	f.uwMouseDat = 0;
	f.ubMouseButton = 0;
	f.ubKeys = 0;
	for(uint8_t i = 0; i < kNetMaxKeys; ++i) {
		f.aubKey[i] = 0;
	}
}

void localFrameTakeKeys(LocalFrame &f, KeyQueue &q) {
	f.ubKeys = 0;
	uint8_t ubCode;
	while(f.ubKeys < kNetMaxKeys && keyQueuePop(q, ubCode)) {
		f.aubKey[f.ubKeys++] = ubCode;
	}
}

void latchReset(TickLatch &t, uint32_t ulClock) {
	localFrameClear(t.sFrame);
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) {
		t.sPads.auwBits[i] = 0;
	}
	t.ulTick = 0;
	t.ulClock = ulClock;
}

void latchCommit(TickLatch &t, const LocalFrame &f, const volatile uint8_t *pKeyDown, uint32_t ulVbls) {
	t.sFrame = f;
	const PadInputs sIn = {f.joy, pKeyDown, 0, 0};   // the parallel adapter is not read yet (padAdapterDecode)
	padsFill(t.sPads, sIn);
	++t.ulTick;
	t.ulClock += ulVbls;
}

void cursorStepVbl(CursorState &s, uint16_t uwBits) {
	s.uwLock = 1;
	if(uwBits & 1) {
		s.uwX = (uint16_t)(s.uwX + kCursorPixelsPerVbl);
	}
	if(uwBits & 2) {
		s.uwX = (uint16_t)(s.uwX - kCursorPixelsPerVbl);
	}
	if(uwBits & 4) {
		s.uwY = (uint16_t)(s.uwY + kCursorPixelsPerVbl);
	}
	if(uwBits & 8) {
		s.uwY = (uint16_t)(s.uwY - kCursorPixelsPerVbl);
	}
	if(uwBits & 0x10) {
		s.uwLock = 0;
	}
	if((int16_t)s.uwX >= kCursorMaxX + 1) {
		s.uwX = kCursorMaxX;
	}
	if((int16_t)s.uwX < 0) {
		s.uwX = 0;
	}
	if((int16_t)s.uwY >= kCursorMaxY + 1) {
		s.uwY = kCursorMaxY;
	}
	if((int16_t)s.uwY < 0) {
		s.uwY = 0;
	}
}

void cursorStep(CursorState &s, uint16_t uwBits, uint32_t ulVbls) {
	for(uint32_t i = 0; i < ulVbls; ++i) {
		cursorStepVbl(s, uwBits);
	}
}

}  // namespace ms
