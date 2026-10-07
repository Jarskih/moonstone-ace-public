// Host driver of tests/test_combat_load.py: runs one routine of src/game/combat_load.cpp on a big-endian byte memory with logging
// Ops.  Text protocol on stdin (hex numbers), results on stdout:
//   LAB <label> <addr>     address of a Label (SECSTRT_n = 8000 + n)
//   SIZE <addr> <size>     what celSize returns for the file name at <addr> (default $1000)
//   SZ <12 numbers>        Env::sz: message, char, pack, pic[0..8]
//   PAT <hex>              the pattern the DATA regions are made of (repeated)
//   BASE <addr> <hex>      a persistent region
//   DATA <addr> <size>     a persistent region filled with the pattern
//   LOGCOPY                also log Mem::copy as "COPY dst src count"
//   CASE                   reset every region to its initial bytes
//   POKE <addr> <hex>      write bytes
//   RUN <routine> [a0]     run; prints the log lines, "END <fault>", one "DIFF <addr> <hex>" per maximal run of changed bytes, "DONE"
// No STL on purpose: the host toolchain of the repo (clang++ on the MSVC headers) cannot always parse it.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/combat_load.hpp"

using namespace ms::game::cl;

namespace {

struct Region {
	uint32_t ulBase;
	uint32_t ulSize;
	uint8_t *pInit;
	uint8_t *pNow;
	uint8_t *pPre;   // the bytes when RUN started: the diff base
};
Region g_regions[64];
int g_nRegions = 0;
uint8_t g_pattern[8192];
uint32_t g_patternLen = 1;
struct LabelEntry {
	uint16_t uwLabel;
	uint32_t ulAddr;
};
LabelEntry g_labels[512];
int g_nLabels = 0;
struct SizeEntry {
	uint32_t ulAddr, ulSize;
};
SizeEntry g_sizes[256];
int g_nSizes = 0;
Sizes g_sz;
int g_fault = 0;
int g_logCopy = 0;
char g_log[1 << 20];
size_t g_logLen = 0;

uint8_t *at(uint32_t a, uint32_t n) {
	for(int i = 0; i < g_nRegions; ++i) {
		const Region &r = g_regions[i];
		if(a >= r.ulBase && (uint64_t)a + n <= (uint64_t)r.ulBase + r.ulSize) {
			return r.pNow + (a - r.ulBase);
		}
	}
	fprintf(stderr, "FAULT %x %u\n", a, n);
	g_fault = 1;
	static uint8_t aDummy[16];
	return aDummy;
}

void lg(const char *szFmt, ...) {
	char buf[256];
	va_list ap;
	va_start(ap, szFmt);
	vsnprintf(buf, sizeof buf, szFmt, ap);
	va_end(ap);
	g_logLen += (size_t)snprintf(g_log + g_logLen, sizeof g_log - g_logLen, "%s\n", buf);
}

uint8_t rd8(uint32_t a) { return *at(a, 1); }
uint16_t rd16(uint32_t a) {
	const uint8_t *p = at(a, 2);
	return (uint16_t)(p[0] << 8 | p[1]);
}
uint32_t rd32(uint32_t a) {
	const uint8_t *p = at(a, 4);
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
void wr8(uint32_t a, uint8_t v) { *at(a, 1) = v; }
void wr16(uint32_t a, uint16_t v) {
	uint8_t *p = at(a, 2);
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}
void wr32(uint32_t a, uint32_t v) {
	uint8_t *p = at(a, 4);
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}
void copyb(uint32_t d, uint32_t s, uint32_t n) {
	if(g_logCopy) {
		lg("COPY %x %x %x", d, s, n);
	}
	for(uint32_t i = 0; i < n; ++i) {
		wr8(d + i, rd8(s + i));
	}
}
void fillb(uint32_t d, uint8_t v, uint32_t n) {
	for(uint32_t i = 0; i < n; ++i) {
		wr8(d + i, v);
	}
}
uint32_t addr(uint16_t l) {
	for(int i = 0; i < g_nLabels; ++i) {
		if(g_labels[i].uwLabel == l) {
			return g_labels[i].ulAddr;
		}
	}
	fprintf(stderr, "no label %x\n", l);
	g_fault = 1;
	return 0;
}

uint32_t celSize(uint16_t n) {
	const uint32_t a = addr(n);
	lg("CEL_SIZE %x", a);
	for(int i = 0; i < g_nSizes; ++i) {
		if(g_sizes[i].ulAddr == a) {
			return g_sizes[i].ulSize;
		}
	}
	return 0x1000;
}
void celLoad(uint16_t n, uint32_t d) { lg("CEL_LOAD %x %x", addr(n), d); }
void hitLoad(uint16_t n, uint32_t d) { lg("HIT_LOAD %x %x", addr(n), d); }
void fileOpen(uint16_t n) { lg("FILE_OPEN %x", addr(n)); }
void fileRead(uint32_t d, uint32_t c) { lg("FILE_READ %x %x", c, d); }
void fileClose() { lg("FILE_CLOSE"); }
void packDone() { lg("FILE_CLOSE"); }
void planes(uint32_t b) { lg("PLANES %x", b); }
void unpack(uint32_t p) { lg("UNPACK %x", p); }
void pictureLoad(uint16_t n, uint32_t b) { lg("PIC_LOAD %x %x", addr(n), b); }
void palette(uint32_t p) { lg("PALETTE %x", p); }
void blank() { lg("BLANK"); }
void fadeOut() { lg("FADE_OUT"); }
void clearScr(uint32_t s) { lg("CLEAR %x", s); }
void copyScreen(uint32_t s, uint32_t d) { lg("COPY_SCREEN %x %x", s, d); }
void copyScreens() { lg("COPY_SCREENS"); }
void text(uint32_t l) { lg("TEXT %x", l); }
void drawCel(uint32_t s, uint16_t c, uint16_t x, uint16_t y) { lg("DRAW_CEL %x %x %x %x %x", s, c, x, y, rd16(addr(FLAG_0D05))); }
void synth(uint16_t q, uint16_t c) { lg("SYNTH %x %x", q, c); }
void music(uint16_t e) { lg("MUSIC_%04X", e); }
void backdrop(uint32_t s) { lg("BACKDROP %x", s); }
void backdropReset() { lg("BACKDROP_RESET"); }
void hunk9() { lg("HUNK9"); }

int hexByte(const char *p) {
	unsigned v = 0;
	sscanf(p, "%2x", &v);
	return (int)v;
}

// Text after the n-th blank-separated word of the line.
const char *word(const char *szLine, int n) {
	const char *p = szLine;
	for(int i = 0; i < n; ++i) {
		while(*p && *p != ' ') {
			++p;
		}
		while(*p == ' ') {
			++p;
		}
	}
	return p;
}

Region &newRegion(uint32_t ulBase, uint32_t ulSize) {
	Region &r = g_regions[g_nRegions++];
	r.ulBase = ulBase;
	r.ulSize = ulSize;
	r.pInit = (uint8_t *)calloc(ulSize ? ulSize : 1, 1);
	r.pNow = (uint8_t *)calloc(ulSize ? ulSize : 1, 1);
	r.pPre = (uint8_t *)calloc(ulSize ? ulSize : 1, 1);
	return r;
}

}  // namespace

int main() {
	Env e;
	e.m = Mem{rd8, rd16, rd32, wr8, wr16, wr32, copyb, fillb};
	e.o = Ops{celSize, celLoad, hitLoad, fileOpen, fileRead, fileClose, packDone, planes, unpack, pictureLoad, palette, blank,
	          fadeOut, clearScr, copyScreen, copyScreens, text, drawCel, synth, music, backdrop, backdropReset, hunk9};
	e.addr = addr;
	e.sz = g_sz;
	static char szLine[(1 << 21)];
	while(fgets(szLine, sizeof szLine, stdin)) {
		char szCmd[32];
		unsigned a = 0, b = 0;
		if(sscanf(szLine, "%31s", szCmd) != 1) {
			continue;
		}
		if(!strcmp(szCmd, "LOGCOPY")) {
			g_logCopy = 1;
		} else if(!strcmp(szCmd, "LAB")) {
			sscanf(szLine, "%*s %x %x", &a, &b);
			g_labels[g_nLabels].uwLabel = (uint16_t)a;
			g_labels[g_nLabels++].ulAddr = b;
		} else if(!strcmp(szCmd, "SIZE")) {
			sscanf(szLine, "%*s %x %x", &a, &b);
			g_sizes[g_nSizes].ulAddr = a;
			g_sizes[g_nSizes++].ulSize = b;
		} else if(!strcmp(szCmd, "PAT")) {
			const char *p = word(szLine, 1);
			g_patternLen = 0;
			while(*p && *p != '\n' && *p != '\r') {
				g_pattern[g_patternLen++] = (uint8_t)hexByte(p);
				p += 2;
			}
		} else if(!strcmp(szCmd, "SZ")) {
			unsigned v[12] = {0};
			sscanf(szLine, "%*s %x %x %x %x %x %x %x %x %x %x %x %x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
			       &v[8], &v[9], &v[10], &v[11]);
			g_sz.ulRawMessage = v[0];
			g_sz.ulRawChar = v[1];
			g_sz.ulRawPack = v[2];
			for(int i = 0; i < 9; ++i) {
				g_sz.aulPic[i] = v[3 + i];
			}
			e.sz = g_sz;
		} else if(!strcmp(szCmd, "BASE")) {
			sscanf(szLine, "%*s %x", &a);
			const char *p = word(szLine, 2);
			size_t n = 0;
			while(p[2 * n] && p[2 * n] != '\n' && p[2 * n] != '\r') {
				++n;
			}
			Region &r = newRegion(a, (uint32_t)n);
			for(size_t i = 0; i < n; ++i) {
				r.pInit[i] = (uint8_t)hexByte(p + 2 * i);
			}
		} else if(!strcmp(szCmd, "DATA")) {
			sscanf(szLine, "%*s %x %x", &a, &b);
			Region &r = newRegion(a, b);
			for(uint32_t i = 0; i < b; ++i) {
				r.pInit[i] = g_pattern[i % g_patternLen];
			}
		} else if(!strcmp(szCmd, "CASE")) {
			for(int i = 0; i < g_nRegions; ++i) {
				memcpy(g_regions[i].pNow, g_regions[i].pInit, g_regions[i].ulSize);
			}
			g_logLen = 0;
			g_log[0] = 0;
			g_fault = 0;
		} else if(!strcmp(szCmd, "POKE")) {
			sscanf(szLine, "%*s %x", &a);
			const char *p = word(szLine, 2);
			while(*p && *p != '\n' && *p != '\r') {
				*at(a++, 1) = (uint8_t)hexByte(p);
				p += 2;
			}
		} else if(!strcmp(szCmd, "RUN")) {
			char szName[64];
			unsigned a0 = 0;
			sscanf(szLine, "%*s %63s %x", szName, &a0);
			for(int k = 0; k < g_nRegions; ++k) {
				memcpy(g_regions[k].pPre, g_regions[k].pNow, g_regions[k].ulSize);
			}
#define ROUTINE(fn) \
	if(!strcmp(szName, #fn)) { \
		fn(e); \
	}
			ROUTINE(driveInit) ROUTINE(loadKnights) ROUTINE(loadHe) ROUTINE(loadTroggSpear) ROUTINE(loadTroggAxe)
			ROUTINE(loadRatmen) ROUTINE(loadMudmen) ROUTINE(loadBalok) ROUTINE(loadDragon) ROUTINE(loadBe) ROUTINE(loadDemon)
			ROUTINE(loadTroll) ROUTINE(loadKiMi) ROUTINE(drawMoon) ROUTINE(loadAssets) ROUTINE(selectScreen)
			ROUTINE(placeHighWood) ROUTINE(placeWaterDeep) ROUTINE(loadWizard) ROUTINE(messageNext) ROUTINE(loadPack)
			ROUTINE(arenaPicture) ROUTINE(clearTables) ROUTINE(fillTables) ROUTINE(screenFromChar)
			ROUTINE(screenFromMessage) ROUTINE(startSynths)
			if(!strcmp(szName, "messageText")) {
				messageText(e, a0);
			}
			if(!strcmp(szName, "messageTextRecoloured")) {
				messageTextRecoloured(e, a0);
			}
			fputs(g_log, stdout);
			printf("END %d\n", g_fault);
			for(int k = 0; k < g_nRegions; ++k) {
				const Region &r = g_regions[k];
				uint32_t i = 0;
				while(i < r.ulSize) {
					if(r.pNow[i] == r.pPre[i]) {
						++i;
						continue;
					}
					uint32_t j = i;
					while(j < r.ulSize && r.pNow[j] != r.pPre[j]) {
						++j;
					}
					printf("DIFF %x ", r.ulBase + i);
					for(uint32_t q = i; q < j; ++q) {
						printf("%02x", r.pNow[q]);
					}
					printf("\n");
					i = j;
				}
			}
			printf("DONE\n");
			fflush(stdout);
		}
	}
	return 0;
}
