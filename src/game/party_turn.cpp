// game/party_turn - see party_turn.hpp.
#include "game/party_turn.hpp"

namespace ms { namespace game {

namespace {

inline char upper(uint8_t ub) { return (ub >= 'a' && ub <= 'z') ? (char)(ub - 'a' + 'A') : (char)ub; }

// Appends pText (upper-cased) to pOut at ulPos, never past ulCap - 1; returns the new length.
uint32_t append(char *pOut, uint32_t ulPos, uint32_t ulCap, const uint8_t *pText) {
	for (; pText && *pText && ulPos + 1 < ulCap; ++pText) pOut[ulPos++] = upper(*pText);
	pOut[ulPos] = 0;
	return ulPos;
}

uint32_t appendLiteral(char *pOut, uint32_t ulPos, uint32_t ulCap, const char *szText) {
	return append(pOut, ulPos, ulCap, reinterpret_cast<const uint8_t *>(szText));
}

bool startsWithSir(const uint8_t *p) {
	return p && upper(p[0]) == 'S' && p[1] && upper(p[1]) == 'I' && p[2] && upper(p[2]) == 'R' && (p[3] == ' ' || p[3] == '_');   // the game's name buffers keep a space as '_' (the name entry's own code)
}

}  // namespace

uint32_t turnNameText(const uint8_t *pName, char *pOut, uint32_t ulCap) {
	if (ulCap == 0) return 0;
	pOut[0] = 0;
	return append(pOut, 0, ulCap, pName);
}

uint32_t turnBannerText(const uint8_t *pName, char *pOut, uint32_t ulCap) {
	if (ulCap == 0) return 0;
	pOut[0] = 0;
	if (!pName || !pName[0]) return appendLiteral(pOut, 0, ulCap, "SIR KNIGHT'S TURN");
	uint32_t n = 0;
	if (!startsWithSir(pName)) n = appendLiteral(pOut, n, ulCap, "SIR ");
	n = append(pOut, n, ulCap, pName);
	static const char kSuffix[] = "'S TURN";
	if (n + sizeof kSuffix <= ulCap) n = appendLiteral(pOut, n, ulCap, kSuffix);   // dropped whole when it does not fit: a cut " TU" would read as a bug
	return n;
}

}}  // namespace ms::game
