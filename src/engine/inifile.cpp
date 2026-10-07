// engine/inifile - see inifile.hpp.
#include "engine/inifile.hpp"

namespace ms {

static bool isIdent(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
static bool isBlank(char c) { return c == ' ' || c == '\t'; }
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

void iniOpen(IniReader &r, const char *pText, uint32_t ulLen) {
	r.p = pText;
	r.ulLeft = ulLen;
	r.ulLine = 0;
	if(ulLen >= 3 && (uint8_t)pText[0] == 0xEF && (uint8_t)pText[1] == 0xBB && (uint8_t)pText[2] == 0xBF) {
		r.p += 3;
		r.ulLeft -= 3;
	}
}

static uint8_t fail(IniLine &o, const char *pWhy) {
	o.ubTok = INI_ERROR;
	o.pError = pWhy;
	return INI_ERROR;
}

// One line [p, pEnd) without its end of line.  INI_END = nothing on this line.
static uint8_t scanLine(const char *p, const char *pEnd, IniLine &o) {
	if(pEnd - p > INI_MAX_LINE) return fail(o, "line too long (max 120 bytes)");
	for(const char *q = p; q < pEnd; ++q)
		if((uint8_t)*q < 0x20 && *q != '\t') return fail(o, "bad character (control code)");
	while(p < pEnd && isBlank(*p)) ++p;
	if(p == pEnd || *p == '#' || *p == ';') return INI_END;

	if(*p == '[') {
		o.ubHeader = 1;
		++p;
		const char *pClose = p;
		while(pClose < pEnd && *pClose != ']') ++pClose;
		if(pClose == pEnd) return fail(o, "section header missing ']'");
		IniSlice tok[2];
		uint8_t n = 0;
		const char *q = p;
		while(q < pClose) {
			while(q < pClose && isBlank(*q)) ++q;
			if(q == pClose) break;
			const char *s = q;
			while(q < pClose && isIdent(*q)) ++q;
			if(q == s || (q < pClose && !isBlank(*q))) return fail(o, "bad section header (letters, digits and _ only)");
			if(n == 2) return fail(o, "bad section header (expected [kind name])");
			tok[n].p = s;
			tok[n].n = (uint16_t)(q - s);
			++n;
		}
		if(n == 0) return fail(o, "bad section header (expected [kind name])");
		const char *r = pClose + 1;
		while(r < pEnd && isBlank(*r)) ++r;
		if(r < pEnd && *r != '#' && *r != ';') return fail(o, "text after section header");
		o.ubTok = INI_SECTION;
		o.kind = tok[0];
		o.name.p = n == 2 ? tok[1].p : tok[0].p;
		o.name.n = n == 2 ? tok[1].n : 0;
		return INI_SECTION;
	}

	const char *k = p;
	while(p < pEnd && isIdent(*p)) ++p;
	o.key.p = k;
	o.key.n = (uint16_t)(p - k);
	while(p < pEnd && isBlank(*p)) ++p;
	if(p == pEnd || *p != '=') return fail(o, o.key.n ? "expected 'key = value'" : "bad key name");
	if(o.key.n == 0) return fail(o, "bad key name");
	++p;
	while(p < pEnd && isBlank(*p)) ++p;
	const char *v = p;
	bool bQuote = false;
	for(; p < pEnd; ++p) {
		if(*p == '"') bQuote = !bQuote;
		else if(!bQuote && (*p == '#' || *p == ';')) break;
	}
	if(bQuote) return fail(o, "unterminated string");
	while(p > v && isBlank(p[-1])) --p;
	if(p == v) return fail(o, "missing value");
	o.value.p = v;
	o.value.n = (uint16_t)(p - v);
	o.ubTok = INI_PAIR;
	return INI_PAIR;
}

uint8_t iniNext(IniReader &r, IniLine &o) {
	while(r.ulLeft) {
		const char *p = r.p, *pEnd = p + r.ulLeft;
		const char *q = p;
		while(q < pEnd && *q != '\n') ++q;
		uint32_t used = (uint32_t)(q - p) + (q < pEnd ? 1u : 0u);
		const char *pText = q;
		if(pText > p && pText[-1] == '\r') --pText;
		r.p += used;
		r.ulLeft -= used;
		++r.ulLine;
		o.ulLine = r.ulLine;
		o.pError = 0;
		o.ubHeader = 0;
		uint8_t t = scanLine(p, pText, o);
		if(t != INI_END) return t;
	}
	o.ubTok = INI_END;
	o.ulLine = r.ulLine;
	return INI_END;
}

uint8_t iniParseInt(IniSlice s, int32_t &out) {
	const char *p = s.p, *pEnd = s.p + s.n;
	bool bNeg = false;
	if(p < pEnd && (*p == '-' || *p == '+')) {
		bNeg = *p == '-';
		++p;
	}
	uint32_t base = 10;
	if(p < pEnd && *p == '$') {
		base = 16;
		++p;
	}
	if(p == pEnd) return INI_NUM_BAD;
	uint32_t v = 0;
	bool bBig = false;
	for(; p < pEnd; ++p) {
		uint32_t d;
		char c = *p;
		if(c >= '0' && c <= '9') d = (uint32_t)(c - '0');
		else if(base == 16 && c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
		else if(base == 16 && c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
		else return INI_NUM_BAD;
		if(v > (0xFFFFFFFFu - d) / base) bBig = true;
		else v = v * base + d;
	}
	if(bBig) return INI_NUM_RANGE;
	if(bNeg) {
		if(v > 0x80000000u) return INI_NUM_RANGE;
		out = (int32_t)(0u - v);
	} else {
		if(v > 0x7FFFFFFFu) return INI_NUM_RANGE;
		out = (int32_t)v;
	}
	return INI_NUM_OK;
}

bool iniParseString(IniSlice s, IniSlice &inside) {
	if(s.n < 2 || s.p[0] != '"' || s.p[s.n - 1] != '"') return false;
	for(uint16_t i = 1; i + 1 < s.n; ++i)
		if(s.p[i] == '"') return false;
	inside.p = s.p + 1;
	inside.n = (uint16_t)(s.n - 2);
	return true;
}

bool iniEqual(IniSlice s, const char *pName) {
	uint16_t i = 0;
	for(; i < s.n; ++i)
		if(!pName[i] || lower(pName[i]) != lower(s.p[i])) return false;
	return pName[i] == 0;
}

void iniListOpen(IniList &l, IniSlice value) {
	l.p = value.p;
	l.pEnd = value.p + value.n;
	l.bDone = false;
}

bool iniListNext(IniList &l, IniSlice &item) {
	if(l.bDone) return false;
	const char *s = l.p;
	bool bQuote = false;
	const char *q = s;
	for(; q < l.pEnd; ++q) {
		if(*q == '"') bQuote = !bQuote;
		else if(*q == ',' && !bQuote) break;
	}
	if(q == l.pEnd) l.bDone = true;
	l.p = q + 1;
	const char *e = q;
	while(s < e && isBlank(*s)) ++s;
	while(e > s && isBlank(e[-1])) --e;
	item.p = s;
	item.n = (uint16_t)(e - s);
	return true;
}

}  // namespace ms
