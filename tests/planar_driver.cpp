// Host driver of tests/test_planar.py (ROADMAP 11.6a): runs ms::planar over cases read from stdin and prints the result.
//   R planes width height mod1 mod2 ptr0..ptr7 origin size checked memhex palette256 ehb   -> 'S status' and 'P' + width*height pixels (6 hex digits each)
//     (mod1 / mod2 as 16-bit hex; checked 0 = unchecked view (size ignored); palette256 = 256 hex RRGGBB words; ehb 1 = ehbExpand first)
//   I ... (the same fields as R)                      -> like R but prints the colour numbers (rowIndices) as 'P' + 2 hex digits each
//   A hi lo                                           -> 'A' rgbAga(hi, lo), rgb12(hi)
//   F winW winH imgW imgH num den                     -> 'F' x y w h (fitRect)
// Numbers are hex except the F fields, which are decimal.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/planar.hpp"

using namespace ms::planar;

namespace {

uint8_t g_mem[1 << 18];
Mode g_mode;

int nib(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

bool readCase(bool isIndices) {
	unsigned planes, width, height, mod1, mod2, ptr[8], origin, size, checked;
	if(scanf("%x %x %x %x %x", &planes, &width, &height, &mod1, &mod2) != 5) return false;
	for(int i = 0; i < 8; ++i) if(scanf("%x", &ptr[i]) != 1) return false;
	if(scanf("%x %x %x", &origin, &size, &checked) != 3) return false;
	static char hex[1 << 19];
	if(scanf("%s", hex) != 1) return false;
	const size_t n = strlen(hex) / 2;
	for(size_t i = 0; i < n; ++i) g_mem[i] = (uint8_t)(nib(hex[2 * i]) * 16 + nib(hex[2 * i + 1]));
	Palette pal;
	for(int i = 0; i < 256; ++i) { unsigned v; if(scanf("%x", &v) != 1) return false; pal.rgb[i] = v; }
	unsigned ehb;
	if(scanf("%x", &ehb) != 1) return false;
	if(ehb) ehbExpand(pal);
	g_mode.planes = (uint8_t)planes;
	g_mode.width = (uint16_t)width;
	g_mode.height = (uint16_t)height;
	for(int i = 0; i < 8; ++i) g_mode.ptr[i] = ptr[i];
	g_mode.mod1 = (int16_t)mod1;
	g_mode.mod2 = (int16_t)mod2;
	ChipView view;
	view.bytes = g_mem;
	view.origin = origin;
	view.size = checked ? size : 0;
	static uint32_t out[(4096 + 3) * 64];
	if(isIndices) {
		Status st = PLANAR_OK;
		static uint8_t row[4096];
		printf("P");
		for(unsigned y = 0; y < height && st != PLANAR_BAD_MODE; ++y) {
			Status s = rowIndices(view, g_mode, (uint16_t)y, row);
			if(s != PLANAR_OK) st = s;
			for(unsigned x = 0; s != PLANAR_BAD_MODE && x < width; ++x) printf("%02x", row[x]);
		}
		printf("\nS %d\n", (int)st);
	}
	else {
		const unsigned pitch = width + 3;   // not equal to the width: the row stride of the output is part of the contract
		const Status st = render(view, g_mode, pal, out, pitch);
		printf("S %d\nP", (int)st);
		if(st != PLANAR_BAD_MODE) for(unsigned y = 0; y < height; ++y) for(unsigned x = 0; x < width; ++x) printf("%06x", out[y * pitch + x] & 0xFFFFFF);
		printf("\n");
	}
	return true;
}

}  // namespace

int main() {
	char cmd[8];
	while(scanf("%7s", cmd) == 1) {
		if(cmd[0] == 'R' || cmd[0] == 'I') {
			if(!readCase(cmd[0] == 'I')) return 2;
		}
		else if(cmd[0] == 'A') {
			unsigned hi, lo;
			if(scanf("%x %x", &hi, &lo) != 2) return 2;
			printf("A %06x %06x\n", rgbAga((uint16_t)hi, (uint16_t)lo), rgb12((uint16_t)hi));
		}
		else if(cmd[0] == 'F') {
			int a[6];
			if(scanf("%d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) return 2;
			const Rect r = fitRect(a[0], a[1], a[2], a[3], a[4], a[5]);
			printf("F %d %d %d %d\n", r.x, r.y, r.w, r.h);
		}
		else return 3;
	}
	return 0;
}
