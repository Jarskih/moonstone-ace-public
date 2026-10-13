// engine/netglue - see include/engine/netglue.hpp (ROADMAP 12.8b).  New code, no original label.
#include "engine/netglue.hpp"

#include "engine/input.hpp"
#include "engine/pad.hpp"

namespace ms {

bool netKeyEncodeRaw(const uint8_t *pXlat, const KeyEvent &ev, uint8_t &ubCode) {
	for(uint8_t idx = 0; idx < kKeyTableSize; ++idx) {
		if(pXlat[idx] == ev.ubKey) {
			const uint8_t ubRaw = static_cast<uint8_t>(~idx & 0x7F);   // the table is indexed by the inverted raw code (keyDecode)
			ubCode = static_cast<uint8_t>((ubRaw << 1) | (ev.isDown ? 0 : 1));
			return true;
		}
	}
	return false;
}

void netFrameFromLocal(NetFrame &out, const LocalFrame &in, const uint8_t *pXlat, uint8_t *pKeyDownShadow, MouseTrack &mt) {
	netFrameClear(out);
	for(uint8_t i = 0; i < in.ubKeys && i < kNetMaxKeys; ++i) {
		const KeyEvent ev = keyDecode(static_cast<uint8_t>(~in.aubKey[i]), pXlat);   // what feedGame() would decode
		if(ev.ubKey < kKeyTableSize) {
			pKeyDownShadow[ev.ubKey] = ev.isDown ? 1 : 0;
		}
		netFrameAddKey(out, ev);   // more than NET_KEYS_MAX events in one tick: the rest is dropped (a person types slower)
	}
	PadFrame pads;
	const PadInputs sIn = {in.joy, pKeyDownShadow, 0, 0};
	padsFill(pads, sIn);
	netFrameSetPads(out, pads);
	const uint8_t ubV = static_cast<uint8_t>(in.uwMouseDat >> 8);
	const uint8_t ubH = static_cast<uint8_t>(in.uwMouseDat);
	netFrameSetMouse(out, mouseDelta(mt.ubPrevH, ubH), mouseDelta(mt.ubPrevV, ubV), in.ubMouseButton != 0);
	mt.ubPrevV = ubV;
	mt.ubPrevH = ubH;
}

void localFrameFromMerged(LocalFrame &io, const NetMerged &merged, const uint8_t *pXlat) {
	io.joy = merged.joy;
	io.uwMouseDat = 0;
	io.ubMouseButton = merged.isMouseButton ? 1 : 0;
	io.ubKeys = 0;
	for(uint8_t i = 0; i < merged.ubKeyCount && i < NET_KEYS_MAX; ++i) {
		uint8_t ubCode = 0;
		if(netKeyEncodeRaw(pXlat, merged.aKeys[i], ubCode)) {
			io.aubKey[io.ubKeys++] = ubCode;
		}
	}
}

}  // namespace ms
