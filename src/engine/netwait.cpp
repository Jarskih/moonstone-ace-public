// engine/netwait - see include/engine/netwait.hpp (ROADMAP 12.11).  New code, no original label.
#include "engine/netwait.hpp"

namespace ms {

namespace {

char upper(char c) {
	return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// Appends szText (upper-cased, at most ubMax characters when ubMax > 0) at ubPos; returns the new length.  Control characters, the three
// punctuation marks the game font has no glyph for ('/', '-' and ':') and
// anything outside printable ASCII become a space.
uint8_t append(char (&acOut)[NW_LINE_MAX], uint8_t ubPos, const char *szText, uint8_t ubMax) {
	uint8_t ubTaken = 0;
	for(; szText && *szText && ubPos + 1 < NW_LINE_MAX && (ubMax == 0 || ubTaken < ubMax); ++szText, ++ubTaken) {
		const unsigned char c = static_cast<unsigned char>(*szText);
		acOut[ubPos++] = (c >= 0x20 && c < 0x7F && c != '/' && c != '-' && c != ':') ? upper(static_cast<char>(c)) : ' ';
	}
	acOut[ubPos] = 0;
	return ubPos;
}

}  // namespace

void netWaitBoxInit(NetWaitBox &b) {
	b.ubKind = NB_NONE;
	b.isDown = true;
	b.ulAskedMs = 0;
}

bool netWaitBoxUpdate(NetWaitBox &b, uint32_t ulWaitedMs, bool isPressed) {
	const bool isPress = isPressed && !b.isDown;
	b.isDown = isPressed;
	if(ulWaitedMs < NW_BOX_DELAY_MS) {
		b.ubKind = NB_NONE;
		return false;
	}
	if(b.ubKind == NB_NONE) {
		b.ubKind = NB_WAITING;
		return false;   // the press that may have brought the question up counts from the next look
	}
	if(b.ubKind == NB_CONFIRM) {
		if(ulWaitedMs - b.ulAskedMs > NW_CONFIRM_MS) {
			b.ubKind = NB_WAITING;   // nobody pressed again: keep waiting
		} else if(isPress) {
			b.ubKind = NB_NONE;
			return true;
		}
		return false;
	}
	if(isPress) {
		b.ubKind = NB_CONFIRM;
		b.ulAskedMs = ulWaitedMs;
	}
	return false;
}

uint8_t netBoxLines(uint8_t ubKind, const char *szName, char (&acLine1)[NW_LINE_MAX], char (&acLine2)[NW_LINE_MAX]) {
	acLine1[0] = 0;
	acLine2[0] = 0;
	if(ubKind == NB_WAITING) {
		append(acLine1, append(acLine1, 0, "WAITING FOR ", 0), szName, NW_NAME_SHOWN);
		append(acLine2, 0, "FIRE OR ESC TO STOP", 0);
		return 2;
	}
	if(ubKind == NB_CONFIRM) {
		append(acLine1, append(acLine1, 0, "CONTINUE WITHOUT ", 0), szName, NW_NAME_SHOWN);
		append(acLine2, 0, "FIRE OR ESC AGAIN", 0);
		return 2;
	}
	return 0;
}

uint8_t netBoxMessage(uint8_t ubWhy, char (&acLine1)[NW_LINE_MAX], char (&acLine2)[NW_LINE_MAX]) {
	acLine1[0] = 0;
	acLine2[0] = 0;
	append(acLine1, 0, ubWhy == NBM_HOST_LEFT ? "HOST LEFT" : (ubWhy == NBM_PLAYERS_LEFT ? "PLAYERS LEFT" : "SESSION OVER"), 0);
	append(acLine2, 0, "PLAYING ON ALONE", 0);
	return 2;
}

uint8_t netNoteLines(uint8_t ubKind, const char *szName, char (&acLine1)[NW_LINE_MAX], char (&acLine2)[NW_LINE_MAX]) {
	acLine1[0] = 0;
	acLine2[0] = 0;
	switch(ubKind) {
		case NOTE_REJOINS:
			append(acLine1, append(acLine1, 0, szName, NW_NAME_SHOWN), " REJOINS", 0);
			append(acLine2, 0, "AT THE NEXT TURN", 0);
			return 2;
		case NOTE_IS_BACK:
			append(acLine1, append(acLine1, 0, szName, NW_NAME_SHOWN), " IS BACK", 0);
			return 1;
		default:
			return 0;
	}
}

}  // namespace ms

