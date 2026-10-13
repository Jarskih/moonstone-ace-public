// rt/lobby_draw - the drawing primitives of the lobby screen (game/scene_lobby.hpp LobbyOps) that the game's cel renderer does not have:
// a solid bar (the dark box behind a picked player's name), a recolour for the greyed START, and text in the small menu font.  They write
// straight into the bitplanes of the screen the menu code is drawing into (mogDrawScreen: 320 x 200, 5 planes of 8000 bytes, 6 with the enhanced
// art), clipped to the screen.  Colours are palette entries (0..31).  Docs: docs/NETPLAY.md 4.2b.
#pragma once
#include <stdint.h>

namespace rt {

// Solid rectangle of one palette entry.
void lobbyBar(uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH, uint8_t ubColour);
// Every pixel of the rectangle that is not colour 0 takes the colour (text drawn by the printer turns grey).
void lobbyRecolour(uint16_t uwX, uint16_t uwY, uint16_t uwW, uint16_t uwH, uint8_t ubColour);
// Text in the small menu font (the font of the waiting box), left aligned at x; the printer's font and flag are put back.
void lobbySmallString(const uint8_t *pText, uint16_t uwX, uint16_t uwY);
// The width in pixels lobbySmallString gives the text (the lobby cuts the names to fit a portrait).
uint16_t lobbySmallWidth(const uint8_t *pText);

}  // namespace rt
