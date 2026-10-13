// game/party_turn - Moonstone 2 co-op: show WHOSE TURN it is on the place screens (ROADMAP 8.5a; docs/MOONSTONE2.md 2.8).
// Not original behaviour: the runtime uses this only inside a co-op shop queue (rt::ShopVisit); the classic game never gets here.
//
// Two cues, all pure choices made here and drawn by src/rt/party_turn.cpp:
//   1. the banner "SIR <NAME>'S TURN" over the picture until the first input or kTurnBannerFrames,
//   2. the knight's token and name in the upper-left corner for the whole turn (owner 2026-10-09; the pointer keeps its normal colours).
//
// Pure: no ACE, no OS, no globals.  tests/test_party_turn.py runs it on the host.
#pragma once
#include <stdint.h>

namespace ms { namespace game {

constexpr uint32_t kTurnBannerFrames = 75;     // 1.5 s at 50 Hz: the banner stays at most this long
constexpr uint32_t kTurnTextMax = 40;          // buffer size for the banner / name line, NUL included

// "SIR <NAME>'S TURN" into pOut (NUL-terminated, upper case).  A name that already starts with "SIR " is not prefixed twice; an
// empty name reads "SIR KNIGHT'S TURN".  Returns the length without the NUL.  A name that does not fit is cut (never overruns).
uint32_t turnBannerText(const uint8_t *pName, char *pOut, uint32_t ulCap);

// The name line that stays on the screen: the name upper-cased and cut to ulCap - 1 characters.  Returns its length.
uint32_t turnNameText(const uint8_t *pName, char *pOut, uint32_t ulCap);

// True when the banner is over: input was seen, or it has stood for kTurnBannerFrames.
inline bool turnBannerOver(uint32_t ulFramesShown, bool bInput) { return bInput || ulFramesShown >= kTurnBannerFrames; }

}}  // namespace ms::game
