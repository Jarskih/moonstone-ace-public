// engine/pad - the co-op controller mapping (ROADMAP 8.2).  See include/engine/pad.hpp.  New code, no original label: the
// original reads two joystick ports only (LAB_00EE, engine/input.cpp).
#include "engine/pad.hpp"

namespace ms {

namespace {

// Opposite directions cancel (left + right -> neither, up + down -> neither).
uint16_t cancelOpposites(uint16_t uwBits) {
	if((uwBits & (kJoyLeft | kJoyRight)) == (kJoyLeft | kJoyRight)) {
		uwBits = static_cast<uint16_t>(uwBits & ~(kJoyLeft | kJoyRight));
	}
	if((uwBits & (kJoyUp | kJoyDown)) == (kJoyUp | kJoyDown)) {
		uwBits = static_cast<uint16_t>(uwBits & ~(kJoyUp | kJoyDown));
	}
	return uwBits;
}

bool isDown(const volatile uint8_t *pKeyDown, uint8_t ubCode) {
	return ubCode != 0 && pKeyDown[ubCode & 0x7F] != 0;
}

const char *const kNames[PAD_SOURCE_COUNT] = {
	"Joystick 1", "Joystick 2", "Keys arrows", "Keys WASD", "Adapter 3", "Adapter 4",
};

}  // namespace

uint16_t padKeyBits(const PadKeySet &set, const volatile uint8_t *pKeyDown) {
	if(!pKeyDown) {
		return 0;
	}
	uint16_t uwBits = 0;
	if(isDown(pKeyDown, set.ubRight)) uwBits |= kJoyRight;
	if(isDown(pKeyDown, set.ubLeft)) uwBits |= kJoyLeft;
	if(isDown(pKeyDown, set.ubDown)) uwBits |= kJoyDown;
	if(isDown(pKeyDown, set.ubUp)) uwBits |= kJoyUp;
	if(isDown(pKeyDown, set.ubFire) || isDown(pKeyDown, set.ubFire2)) uwBits |= kJoyFire;
	return cancelOpposites(uwBits);
}

void padsFill(PadFrame &f, const PadInputs &in) {
	f.auwBits[PAD_JOY1] = in.joy.uwPort1;
	f.auwBits[PAD_JOY0] = in.joy.uwPort0;
	f.auwBits[PAD_KEYS_ARROWS] = padKeyBits(kPadKeysArrows, in.pKeyDown);
	f.auwBits[PAD_KEYS_WASD] = padKeyBits(kPadKeysWasd, in.pKeyDown);
	f.auwBits[PAD_ADAPTER3] = in.uwAdapter3;
	f.auwBits[PAD_ADAPTER4] = in.uwAdapter4;
}

uint16_t padBits(const PadFrame &f, uint8_t ubSource) {
	return ubSource < PAD_SOURCE_COUNT ? f.auwBits[ubSource] : 0;
}

uint16_t padsAny(const PadFrame &f) {
	uint16_t uwBits = 0;
	for(uint8_t i = 0; i < PAD_SOURCE_COUNT; ++i) {
		uwBits |= f.auwBits[i];
	}
	return uwBits;
}

PadAdapterBits padAdapterDecode(uint8_t ubPrb, uint8_t ubCiabPra) {
	(void)ubPrb;
	(void)ubCiabPra;
	return PadAdapterBits{0, 0};   // TODO(ROADMAP 8.2): pinout of the owner's adapter (see pad.hpp)
}

const char *padName(uint8_t ubSource) {
	return ubSource < PAD_SOURCE_COUNT ? kNames[ubSource] : "None";
}

}  // namespace ms
