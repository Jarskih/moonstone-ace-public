// Host driver of tests/test_copper.py (ROADMAP 11.6b/c): runs engine/copper, engine/sprites and engine/dispframe over cases read from stdin.
// Numbers are hex.  Memory is given as one hex string.
//   B line clock ir1 ir2                                    -> 'B' 0|1                      (copper::beamCompare)
//   C origin size checked cop1lc cop2lc mem REGS palette256 -> per line 'L ...' / 'P' lines and a final 'E ...' (the copper over one frame)
//   S origin size checked fmode bplcon4 x0 n ptr0..ptr7 mem nev (kind a b line)*nev
//                                                           -> per line 'L v mask' (+ 'I idx G group' when a sprite has data) and 'E ptr0..ptr7'
//   K n bplcon2 brdsprt winlo winhi xor idx group pf        -> 'K idx isSprite'             (sprites::composite)
//   F origin size checked cop1lc outx outy outw outh mem REGS palette256 -> 'R problems status' and 'P' + pixels (dispframe::render)
// REGS = bplcon0 bplcon1 bplcon2 bplcon3 bplcon4 fmode diwstrt diwstop ddfstrt ddfstop dmacon mod1 mod2 bplpt0..7 sprpt0..7
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/copper.hpp"
#include "engine/dispframe.hpp"
#include "engine/sprites.hpp"

using namespace ms;

namespace {

uint8_t g_mem[1 << 17];
uint32_t g_out[512 * 320];

int nib(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

bool readMem(unsigned origin, unsigned size, unsigned checked, softblit::ChipView &view) {
	static char hex[1 << 18];
	if(scanf("%s", hex) != 1) return false;
	const size_t n = strlen(hex) / 2;
	for(size_t i = 0; i < n; ++i) g_mem[i] = (uint8_t)(nib(hex[2 * i]) * 16 + nib(hex[2 * i + 1]));
	view.bytes = g_mem;
	view.origin = origin;
	view.size = checked ? size : 0;
	return true;
}

bool readRegs(copper::Regs &r) {
	unsigned v[13];
	for(int i = 0; i < 13; ++i) if(scanf("%x", &v[i]) != 1) return false;
	r.bplcon0 = (uint16_t)v[0]; r.bplcon1 = (uint16_t)v[1]; r.bplcon2 = (uint16_t)v[2]; r.bplcon3 = (uint16_t)v[3];
	r.bplcon4 = (uint16_t)v[4]; r.fmode = (uint16_t)v[5]; r.diwstrt = (uint16_t)v[6]; r.diwstop = (uint16_t)v[7];
	r.ddfstrt = (uint16_t)v[8]; r.ddfstop = (uint16_t)v[9]; r.dmacon = (uint16_t)v[10]; r.bpl1mod = (int16_t)v[11]; r.bpl2mod = (int16_t)v[12];
	for(int i = 0; i < 8; ++i) { unsigned p; if(scanf("%x", &p) != 1) return false; r.bplpt[i] = p; }
	for(int i = 0; i < 8; ++i) { unsigned p; if(scanf("%x", &p) != 1) return false; r.sprpt[i] = p; }
	r.bplptWritten = r.sprptWritten = 0;
	return true;
}

bool readPalette(planar::Palette &pal) {
	for(int i = 0; i < 256; ++i) { unsigned v; if(scanf("%x", &v) != 1) return false; pal.rgb[i] = v; }
	return true;
}

// The register line without the line number (compared with the previous line's: only changes are printed).
void formatRegs(char *buf, const copper::Regs &r) {
	int n = sprintf(buf, "%x %x %x %x %x %x %x %x %x %x %x %x %x", r.bplcon0, r.bplcon1, r.bplcon2, r.bplcon3, r.bplcon4, r.fmode,
	                r.diwstrt, r.diwstop, r.ddfstrt, r.ddfstop, r.dmacon, (unsigned)(uint16_t)r.bpl1mod, (unsigned)(uint16_t)r.bpl2mod);
	for(int i = 0; i < 8; ++i) n += sprintf(buf + n, " %x", (unsigned)r.bplpt[i]);
	for(int i = 0; i < 8; ++i) n += sprintf(buf + n, " %x", (unsigned)r.sprpt[i]);
	sprintf(buf + n, " %x %x", r.bplptWritten, r.sprptWritten);
}

bool caseC() {
	unsigned origin, size, checked, cop1, cop2;
	if(scanf("%x %x %x %x %x", &origin, &size, &checked, &cop1, &cop2) != 5) return false;
	softblit::ChipView view;
	if(!readMem(origin, size, checked, view)) return false;
	copper::Regs regs;
	copper::regsReset(regs);
	if(!readRegs(regs) || !readPalette(regs.palette)) return false;
	copper::Player pl;
	copper::begin(pl, view, cop1);
	pl.cop2lc = cop2;
	planar::Palette prev = regs.palette;
	char prevText[512] = "";
	char text[512];
	for(unsigned line = 0; line < copper::kLines; ++line) {
		copper::runTo(pl, regs, (uint16_t)line, regs.ddfstrt);
		formatRegs(text, regs);
		if(strcmp(text, prevText) != 0) printf("L %x %s\n", line, text);
		strcpy(prevText, text);
		bool isChanged = false;
		for(int i = 0; i < 256; ++i) isChanged = isChanged || prev.rgb[i] != regs.palette.rgb[i];
		if(isChanged) {
			printf("P %x", line);
			for(int i = 0; i < 256; ++i) if(prev.rgb[i] != regs.palette.rgb[i]) printf(" %x:%06x", i, (unsigned)regs.palette.rgb[i]);
			printf("\n");
		}
		prev = regs.palette;
		regs.bplptWritten = regs.sprptWritten = 0;
	}
	copper::runFrame(pl, regs);
	printf("E %d %x %x %d %x %x %x %x %x %x\n", (int)pl.status, pl.instructions, pl.now, pl.isIdle ? 1 : 0, (unsigned)pl.errAddr,
	       pl.errWord1, pl.errWord2, (unsigned)pl.cop1lc, (unsigned)pl.cop2lc, (unsigned)pl.pc);
	return true;
}

bool caseS() {
	unsigned origin, size, checked, fmode, bplcon4, n;
	int x0;
	if(scanf("%x %x %x %x %x %d %x", &origin, &size, &checked, &fmode, &bplcon4, &x0, &n) != 7) return false;
	uint32_t ptr[8];
	for(int i = 0; i < 8; ++i) { unsigned p; if(scanf("%x", &p) != 1) return false; ptr[i] = p; }
	softblit::ChipView view;
	if(!readMem(origin, size, checked, view)) return false;
	unsigned nev;
	if(scanf("%x", &nev) != 1) return false;
	static unsigned ev[64][4];
	if(nev > 64) return false;
	for(unsigned i = 0; i < nev; ++i) if(scanf("%x %x %x %x", &ev[i][0], &ev[i][1], &ev[i][2], &ev[i][3]) != 4) return false;
	sprites::Set set;
	sprites::begin(set, ptr);
	static sprites::LineBuf lb;
	bool dmaOn = true;
	int status = 0;
	for(unsigned line = 0; line < copper::kLines; ++line) {
		for(unsigned i = 0; i < nev; ++i) {
			if(ev[i][3] != line) continue;
			if(ev[i][0] == 0) sprites::setPointer(set, (uint8_t)ev[i][1], ev[i][2]);
			else dmaOn = ev[i][1] != 0;
		}
		if(sprites::step(set, view, (uint16_t)line, dmaOn, (uint16_t)fmode) != sprites::SPRITES_OK) status = 1;
		unsigned mask = 0;
		for(int c = 0; c < 8; ++c) if(set.ch[c].isVisible) mask |= 1u << c;
		printf("L %x %x\n", line, mask);
		if(mask != 0) {
			sprites::drawLine(set, x0, (uint16_t)n, (uint16_t)bplcon4, lb);
			printf("I ");
			for(unsigned x = 0; x < n; ++x) printf("%02x", lb.idx[x]);
			printf(" G ");
			for(unsigned x = 0; x < n; ++x) printf("%x", lb.group[x]);
			printf("\n");
		}
	}
	printf("E %d", status);
	for(int c = 0; c < 8; ++c) printf(" %x", (unsigned)set.ch[c].ptr);
	printf("\n");
	return true;
}

bool caseK() {
	unsigned n, bplcon2, brd, xr;
	int lo, hi;
	if(scanf("%x %x %x %d %d %x", &n, &bplcon2, &brd, &lo, &hi, &xr) != 6) return false;
	static char hex[1 << 12];
	static sprites::LineBuf lb;
	static uint8_t pf[sprites::kMaxLine], isSpr[sprites::kMaxLine];
	if(n > sprites::kMaxLine) return false;
	if(scanf("%s", hex) != 1) return false;
	for(unsigned i = 0; i < n; ++i) lb.idx[i] = (uint8_t)(nib(hex[2 * i]) * 16 + nib(hex[2 * i + 1]));
	if(scanf("%s", hex) != 1) return false;
	for(unsigned i = 0; i < n; ++i) lb.group[i] = (uint8_t)nib(hex[i]);
	if(scanf("%s", hex) != 1) return false;
	for(unsigned i = 0; i < n; ++i) pf[i] = (uint8_t)(nib(hex[2 * i]) * 16 + nib(hex[2 * i + 1]));
	sprites::Compose c;
	c.bplcon2 = (uint16_t)bplcon2;
	c.isBorderSprites = brd != 0;
	c.lWinLo = lo;
	c.lWinHi = hi;
	c.ubPlayfieldXor = (uint8_t)xr;
	sprites::composite(lb, (uint16_t)n, c, pf, isSpr);
	printf("K ");
	for(unsigned i = 0; i < n; ++i) printf("%02x", pf[i]);
	printf(" ");
	for(unsigned i = 0; i < n; ++i) printf("%x", isSpr[i]);
	printf("\n");
	return true;
}

bool caseF() {
	unsigned origin, size, checked, cop1, outy, outw, outh;
	int outx;
	if(scanf("%x %x %x %x %d %x %x %x", &origin, &size, &checked, &cop1, &outx, &outy, &outw, &outh) != 8) return false;
	softblit::ChipView view;
	if(!readMem(origin, size, checked, view)) return false;
	copper::Regs regs;
	copper::regsReset(regs);
	if(!readRegs(regs) || !readPalette(regs.palette)) return false;
	dispframe::Output out;
	out.originX = outx;
	out.originY = (uint16_t)outy;
	out.width = (uint16_t)outw;
	out.height = (uint16_t)outh;
	out.pixels = g_out;
	dispframe::Result res;
	dispframe::render(view, regs, cop1, out, res);
	printf("R %x %d\nP", (unsigned)res.problems, (int)res.copperStatus);
	for(unsigned i = 0; i < outw * outh; ++i) printf("%06x", (unsigned)(g_out[i] & 0xFFFFFF));
	printf("\n");
	return true;
}

}  // namespace

int main() {
	char cmd[8];
	while(scanf("%7s", cmd) == 1) {
		bool ok = false;
		switch(cmd[0]) {
			case 'B': {
				unsigned line, clock, ir1, ir2;
				ok = scanf("%x %x %x %x", &line, &clock, &ir1, &ir2) == 4;
				if(ok) printf("B %d\n", copper::beamCompare((uint16_t)line, (uint16_t)clock, (uint16_t)ir1, (uint16_t)ir2) ? 1 : 0);
				break;
			}
			case 'C': ok = caseC(); break;
			case 'S': ok = caseS(); break;
			case 'K': ok = caseK(); break;
			case 'F': ok = caseF(); break;
			default: break;
		}
		if(!ok) {
			fprintf(stderr, "bad input at command %s\n", cmd);
			return 2;
		}
	}
	return 0;
}
