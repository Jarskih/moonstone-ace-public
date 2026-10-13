// engine/netglue - the pure conversions between the deterministic tick's frame (engine/nettick LocalFrame) and the network's input
// frame (engine/netplay NetFrame / NetMerged) (ROADMAP 12.8b).  No ACE, no hardware, no globals.
//
//   netFrameFromLocal   the local frame of a tick -> the NetFrame this node sends: the controllers (both joysticks as the game reads
//                       them plus the keyboard sets read from a SHADOW key-down table that follows the LOCAL keyboard, not the merged
//                       one the game sees), up to NET_KEYS_MAX key events as translated game keys, the mouse delta.
//   localFrameFromMerged  the merged input of the tick -> the frame the game then sees: the joystick words, the key events turned
//                       back into the raw keyboard codes feedGame() takes, the mouse button.
#pragma once
#include <stdint.h>

#include "engine/netplay.hpp"
#include "engine/nettick.hpp"

namespace ms {

// Previous JOY0DAT counter bytes of the local mouse (for the delta).
struct MouseTrack {
	uint8_t ubPrevV, ubPrevH;
};

// The raw keyboard byte of a game key event, as ms::keyDecode(~code, pXlat) decodes it back to the same key and flag: (raw << 1) | released.
// pXlat is the 128-byte raw -> game key table; false when no raw code translates to the key (the event cannot be replayed).
bool netKeyEncodeRaw(const uint8_t *pXlat, const KeyEvent &ev, uint8_t &ubCode);

// Local tick frame -> NetFrame.  pKeyDownShadow: 128 bytes, updated with this frame's key events.  mt follows JOY0DAT.
void netFrameFromLocal(NetFrame &out, const LocalFrame &in, const uint8_t *pXlat, uint8_t *pKeyDownShadow, MouseTrack &mt);

// Merged input -> the frame the game reads this tick (joystick words, key events as raw codes, the mouse button; the mouse counters are
// replaced by 0 so every node's pointer cells see the same counters).
void localFrameFromMerged(LocalFrame &io, const NetMerged &merged, const uint8_t *pXlat);

}  // namespace ms
