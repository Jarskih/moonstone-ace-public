// engine/autoplay - see include/engine/autoplay.hpp.
#include "engine/autoplay.hpp"

#include "engine/input.hpp"

namespace ms {

namespace {

struct KeyName { const char *szName; uint8_t ubCode; };

// Raw Amiga key codes (the same values as ACE's KEY_*, checked by static_asserts in rt/autoplay.cpp).
const KeyName s_keys[] = {
	{"space", 0x40}, {"return", 0x44}, {"enter", 0x44}, {"esc", 0x45}, {"escape", 0x45}, {"tab", 0x42},
	{"backspace", 0x41}, {"del", 0x46}, {"help", 0x5F},
	{"up", 0x4C}, {"down", 0x4D}, {"right", 0x4E}, {"left", 0x4F},
	{"f1", 0x50}, {"f2", 0x51}, {"f3", 0x52}, {"f4", 0x53}, {"f5", 0x54}, {"f6", 0x55}, {"f7", 0x56}, {"f8", 0x57},
	{"f9", 0x58}, {"f10", 0x59},
	{"lshift", 0x60}, {"rshift", 0x61}, {"capslock", 0x62}, {"ctrl", 0x63}, {"lalt", 0x64}, {"ralt", 0x65},
	{"lamiga", 0x66}, {"ramiga", 0x67},
	{"num0", 0x0F}, {"num1", 0x1D}, {"num2", 0x1E}, {"num3", 0x1F}, {"num4", 0x2D}, {"num5", 0x2E}, {"num6", 0x2F},
	{"num7", 0x3D}, {"num8", 0x3E}, {"num9", 0x3F}, {"numenter", 0x43}, {"numperiod", 0x3C},
	{"minus", 0x0B}, {"equals", 0x0C}, {"backslash", 0x0D}, {"comma", 0x38}, {"period", 0x39}, {"slash", 0x3A},
	{"semicolon", 0x29}, {"apostrophe", 0x2A}, {"lbracket", 0x1A}, {"rbracket", 0x1B},
};

char lower(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// Case-insensitive compare of the n chars at a with the lower-case C string b.
bool eq(const char *a, unsigned n, const char *b) {
	unsigned i = 0;
	for(; i < n; ++i) {
		if(!b[i] || lower(a[i]) != b[i]) {
			return false;
		}
	}
	return b[i] == 0;
}

// Single character key: letters in the three keyboard rows, digits.
int charCode(char c) {
	c = lower(c);
	static const char s_row1[] = "qwertyuiop";   // 0x10..
	static const char s_row2[] = "asdfghjkl";    // 0x20..
	static const char s_row3[] = "zxcvbnm";      // 0x31..
	for(unsigned i = 0; s_row1[i]; ++i) if(s_row1[i] == c) return 0x10 + (int)i;
	for(unsigned i = 0; s_row2[i]; ++i) if(s_row2[i] == c) return 0x20 + (int)i;
	for(unsigned i = 0; s_row3[i]; ++i) if(s_row3[i] == c) return 0x31 + (int)i;
	if(c >= '1' && c <= '9') return 0x01 + (c - '1');
	if(c == '0') return 0x0A;
	return -1;
}

int keyCodeN(const char *p, unsigned n) {
	if(n == 1) {
		return charCode(p[0]);
	}
	for(const KeyName &k : s_keys) {
		if(eq(p, n, k.szName)) {
			return k.ubCode;
		}
	}
	return -1;
}

struct Tok { const char *p; unsigned n; };

bool parseUint(const Tok &t, uint32_t &ulOut) {
	if(!t.n || t.n > 9) {
		return false;
	}
	uint32_t v = 0;
	for(unsigned i = 0; i < t.n; ++i) {
		if(t.p[i] < '0' || t.p[i] > '9') {
			return false;
		}
		v = v * 10 + (uint32_t)(t.p[i] - '0');
	}
	ulOut = v;
	return true;
}

bool parseNum(const Tok &t, uint32_t &ulOut) {   // decimal or 0x hex
	if(t.n > 2 && t.p[0] == '0' && lower(t.p[1]) == 'x') {
		if(t.n > 10) {
			return false;
		}
		uint32_t v = 0;
		for(unsigned i = 2; i < t.n; ++i) {
			const char c = lower(t.p[i]);
			const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
			if(d < 0) {
				return false;
			}
			v = (v << 4) | (uint32_t)d;
		}
		ulOut = v;
		return true;
	}
	return parseUint(t, ulOut);
}

bool setText(ApEvent &e, const Tok &t) {
	if(!t.n || t.n >= kApTextLen) {
		return false;
	}
	for(unsigned i = 0; i < t.n; ++i) {
		const char c = t.p[i];
		const bool isOk = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
		if(!isOk) {
			return false;
		}
		e.szText[i] = c;
	}
	e.szText[t.n] = 0;
	return true;
}

// "up+fire" -> kJoy bits; "none" -> 0. false on an unknown word.
bool parseJoy(const Tok &t, uint8_t &ubBits) {
	ubBits = 0;
	unsigned i = 0;
	while(i < t.n) {
		unsigned j = i;
		while(j < t.n && t.p[j] != '+') {
			++j;
		}
		const unsigned n = j - i;
		const char *p = t.p + i;
		if(eq(p, n, "up")) ubBits |= kJoyUp;
		else if(eq(p, n, "down")) ubBits |= kJoyDown;
		else if(eq(p, n, "left")) ubBits |= kJoyLeft;
		else if(eq(p, n, "right")) ubBits |= kJoyRight;
		else if(eq(p, n, "fire")) ubBits |= kJoyFire;
		else if(!eq(p, n, "none")) return false;
		i = j + 1;
	}
	return t.n != 0;
}

constexpr unsigned kMaxTok = 9;
constexpr unsigned kMaxLineEvents = 2 * kApTypeLen;

// Builds the events of one tokenised line into aOut; returns the count, or -1 if the line is invalid.
int buildLine(const Tok *aTok, unsigned uTok, ApEvent *aOut) {
	ApEvent e = {};
	if(uTok >= 1 && (eq(aTok[0].p, aTok[0].n, "wait") || eq(aTok[0].p, aTok[0].n, "sync"))) {   // no "frame N" prefix: N = 0
		static const Tok s_tokFrame = {"frame", 5}, s_tokZero = {"0", 1};
		Tok aShift[kMaxTok];
		if(uTok + 2 > kMaxTok) {
			return -1;
		}
		aShift[0] = s_tokFrame;
		aShift[1] = s_tokZero;
		for(unsigned i = 0; i < uTok; ++i) {
			aShift[i + 2] = aTok[i];
		}
		return buildLine(aShift, uTok + 2, aOut);
	}
	if(uTok < 3 || !eq(aTok[0].p, aTok[0].n, "frame") || !parseUint(aTok[1], e.ulFrame)) {
		return -1;
	}
	const Tok &k = aTok[2];
	unsigned uOut = 0;
	if(eq(k.p, k.n, "wait") && uTok >= 4 && eq(aTok[3].p, aTok[3].n, "input")) {   // wait input [K] [max F]
		unsigned i = 4;
		e.ubKind = AP_WAIT_INPUT;
		e.ulValue = 5;
		uint32_t ulV = 0;
		if(i < uTok && parseUint(aTok[i], ulV)) {
			e.ulValue = ulV;
			++i;
		}
		for(; i < uTok; i += 2) {   // gap G / max F, either order
			if(i + 1 >= uTok || !parseUint(aTok[i + 1], ulV) || ulV > 65535) {
				return -1;
			}
			if(eq(aTok[i].p, aTok[i].n, "max")) {
				e.uwMax = (uint16_t)ulV;
			}
			else if(eq(aTok[i].p, aTok[i].n, "gap") && ulV < 256) {
				e.ubBits = (uint8_t)ulV;
			}
			else {
				return -1;
			}
		}
		aOut[uOut++] = e;
	}
	else if(eq(k.p, k.n, "wait") && (uTok == 6 || uTok == 8) && eq(aTok[3].p, aTok[3].n, "var")) {   // wait var NAME VALUE [max F]
		uint32_t ulV = 0;
		e.ubKind = AP_WAIT_VAR;
		if(!setText(e, aTok[4]) || !parseNum(aTok[5], ulV)) {
			return -1;
		}
		e.ulValue = ulV;
		if(uTok == 8) {
			uint32_t ulMax = 0;
			if(!eq(aTok[6].p, aTok[6].n, "max") || !parseUint(aTok[7], ulMax) || ulMax > 65535) {
				return -1;
			}
			e.uwMax = (uint16_t)ulMax;
		}
		aOut[uOut++] = e;
	}
	else if((eq(k.p, k.n, "wait") && (uTok == 5 || uTok == 7)) || (eq(k.p, k.n, "sync") && uTok == 3)) {
		const bool isSync = eq(k.p, k.n, "sync");
		e.ubKind = AP_SYNC;
		if(!isSync) {
			const bool isFile = eq(aTok[3].p, aTok[3].n, "file");
			if(!isFile && !eq(aTok[3].p, aTok[3].n, "log")) {
				return -1;
			}
			e.ubKind = isFile ? AP_WAIT_FILE : AP_WAIT_LOG;
			if(!setText(e, aTok[4])) {
				return -1;
			}
			if(uTok == 7) {
				uint32_t ulMax = 0;
				if(!eq(aTok[5].p, aTok[5].n, "max") || !parseUint(aTok[6], ulMax) || ulMax > 65535) {
					return -1;
				}
				e.uwMax = (uint16_t)ulMax;
			}
		}
		aOut[uOut++] = e;
	}
	else if(eq(k.p, k.n, "mash") && uTok == 5 && (eq(aTok[3].p, aTok[3].n, "joy0") || eq(aTok[3].p, aTok[3].n, "joy1"))) {
		uint32_t ulF = 0;
		if(!eq(aTok[4].p, aTok[4].n, "off") && (!parseUint(aTok[4], ulF) || ulF < 1 || ulF > 255)) {
			return -1;
		}
		e.ubKind = AP_MASH;
		e.ubCode = (uint8_t)(aTok[3].p[3] - '0');
		e.ubBits = (uint8_t)ulF;
		aOut[uOut++] = e;
	}
	else if(eq(k.p, k.n, "poke") && uTok == 5) {
		e.ubKind = AP_POKE;
		uint32_t ulV = 0;
		if(!setText(e, aTok[3]) || !parseNum(aTok[4], ulV)) {
			return -1;
		}
		e.ulValue = ulV;
		aOut[uOut++] = e;
	}
	else if(eq(k.p, k.n, "key") && uTok == 5) {
		const int iCode = keyCodeN(aTok[3].p, aTok[3].n);
		const Tok &a = aTok[4];
		if(iCode < 0) {
			return -1;
		}
		e.ubKind = AP_KEY;
		e.ubCode = (uint8_t)iCode;
		if(eq(a.p, a.n, "down") || eq(a.p, a.n, "up")) {
			e.isDown = eq(a.p, a.n, "down");
			aOut[uOut++] = e;
		}
		else if(eq(a.p, a.n, "tap")) {
			e.isDown = 1;
			aOut[uOut++] = e;
			e.isDown = 0;
			e.ulFrame += kApTapFrames;
			aOut[uOut++] = e;
		}
		else {
			return -1;
		}
	}
	else if((eq(k.p, k.n, "joy0") || eq(k.p, k.n, "joy1")) && uTok >= 4 && uTok <= 6) {
		e.ubKind = AP_JOY;
		e.ubCode = (uint8_t)(k.p[3] - '0');
		if(!parseJoy(aTok[3], e.ubBits)) {
			return -1;
		}
		if(uTok >= 5) {   // pulse [N]: held for N frames (default rt/autoplay PULSE_FRAMES), then released by the harness
			uint32_t ulN = 0;
			if(!eq(aTok[4].p, aTok[4].n, "pulse") || (uTok == 6 && (!parseUint(aTok[5], ulN) || ulN < 1 || ulN > 255))) {
				return -1;
			}
			e.isDown = 1;
			e.uwMax = (uint16_t)ulN;
		}
		aOut[uOut++] = e;
	}
	else if(eq(k.p, k.n, "type") && uTok == 4) {
		const Tok &t = aTok[3];
		if(t.n == 0 || t.n >= kApTypeLen) {
			return -1;
		}
		const uint32_t ulBase = e.ulFrame;
		for(unsigned i = 0; i < t.n; ++i) {
			const int iCode = t.p[i] == '_' ? 0x40 : charCode(t.p[i]);
			if(iCode < 0) {
				return -1;
			}
			e.ubKind = AP_KEY;
			e.ubCode = (uint8_t)iCode;
			e.ulFrame = ulBase + i * kApTypeStep;
			e.isDown = 1;
			aOut[uOut++] = e;
			e.isDown = 0;
			e.ulFrame += kApTapFrames;
			aOut[uOut++] = e;
		}
	}
	else if((eq(k.p, k.n, "shot") || eq(k.p, k.n, "log")) && uTok == 4) {
		e.ubKind = eq(k.p, k.n, "shot") ? AP_SHOT : AP_LOG;
		if(!setText(e, aTok[3])) {
			return -1;
		}
		aOut[uOut++] = e;
	}
	else if(eq(k.p, k.n, "quit") && uTok == 3) {
		e.ubKind = AP_QUIT;
		aOut[uOut++] = e;
	}
	else {
		return -1;
	}
	return (int)uOut;
}

}  // namespace

int apKeyCode(const char *szName) {
	unsigned n = 0;
	while(szName[n]) {
		++n;
	}
	return keyCodeN(szName, n);
}

ApParse apParse(const char *pText, uint32_t ulLen, ApEvent *pOut, uint16_t uwMax) {
	ApParse sRes = {0, 0, 0, 0};
	uint16_t uwSegStart = 0;   // first event of the current segment: nothing sorts in front of it
	uint32_t ulPos = 0;
	uint16_t uwLine = 0;
	while(ulPos < ulLen) {
		uint32_t ulEnd = ulPos;
		while(ulEnd < ulLen && pText[ulEnd] != '\n') {
			++ulEnd;
		}
		++uwLine;
		const char *pLine = pText + ulPos;
		uint32_t ulLineLen = ulEnd - ulPos;
		ulPos = ulEnd + 1;
		for(uint32_t i = 0; i < ulLineLen; ++i) {
			if(pLine[i] == '#' || pLine[i] == ';') {
				ulLineLen = i;
				break;
			}
		}
		Tok aTok[kMaxTok];
		unsigned uTok = 0;
		bool isLong = false;
		for(uint32_t i = 0; i < ulLineLen;) {
			const char c = pLine[i];
			if(c == ' ' || c == '\t' || c == '\r') {
				++i;
				continue;
			}
			uint32_t j = i;
			while(j < ulLineLen && pLine[j] != ' ' && pLine[j] != '\t' && pLine[j] != '\r') {
				++j;
			}
			if(uTok == kMaxTok) {
				isLong = true;
				break;
			}
			aTok[uTok].p = pLine + i;
			aTok[uTok].n = (unsigned)(j - i);
			++uTok;
			i = j;
		}
		if(uTok == 0) {
			continue;
		}
		ApEvent aOut[kMaxLineEvents];
		const int iOut = isLong ? -1 : buildLine(aTok, uTok, aOut);
		if(iOut < 0) {
			++sRes.uwErrors;
			if(!sRes.uwFirstErrLine) {
				sRes.uwFirstErrLine = uwLine;
			}
			continue;
		}
		for(int i = 0; i < iOut; ++i) {
			if(sRes.uwCount >= uwMax) {
				sRes.isOverflow = 1;
				break;
			}
			// stable insertion by frame within the segment: keeps file order within a frame
			const bool isBarrier = apIsBarrier(aOut[i].ubKind);
			uint16_t at = sRes.uwCount++;
			if(isBarrier) {   // a barrier goes behind everything listed before it, whatever their frames
				if(at > uwSegStart && pOut[at - 1].ulFrame > aOut[i].ulFrame) {
					aOut[i].ulFrame = pOut[at - 1].ulFrame;
				}
			}
			else {
				while(at > uwSegStart && pOut[at - 1].ulFrame > aOut[i].ulFrame) {
					pOut[at] = pOut[at - 1];
					--at;
				}
			}
			pOut[at] = aOut[i];
			if(isBarrier) {
				uwSegStart = sRes.uwCount;
			}
		}
	}
	return sRes;
}

}  // namespace ms
