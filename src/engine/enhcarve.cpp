// engine/enhcarve - see include/engine/enhcarve.hpp. Transcribed from program.asm LAB_0044 (line 920) and
// mog.asm LAB_0004 (line 217); every ADDI/MOVE below names the asm line it stands for.
#include "engine/enhcarve.hpp"

namespace ms {

namespace {

constexpr uint32_t kProgramChipBump = 0x4536C;  // program.asm:921  ADDI.L #$0004536c,LAB_00C2
constexpr uint32_t kProgramFastBump = 0x58116;  // program.asm:938  ADDI.L #$00058116,LAB_00C4
constexpr uint32_t kMogChipBump = 0x5BF18;      // mog.asm:219      ADDI.L #$0005bf18,LAB_05BC
constexpr uint32_t kMogFastBump = 0x5654D;      // mog.asm:248      ADDI.L #$0005654d,LAB_05BE

// A running pointer: D0 of the asm routines. add() takes the ORIGINAL increment.
struct Cursor {
	uint32_t v;
	uint32_t sum;  // original increments added so far (the chain end, for the remainder of the bump)
	uint32_t enhSum;
	bool isEnh;
	void add(uint32_t n) {
		v += carveSize(n, isEnh);
		sum += n;
		enhSum += carveSize(n, isEnh);
	}
};

uint32_t bumpOf(uint32_t origBump, const Cursor &c, bool isEnh) {
	if(!isEnh) {
		return origBump;
	}
	return c.enhSum + carveSize(origBump - c.sum, true);
}

// Fast chain end: the enhanced carve appends the cel read buffer (longword aligned) behind the stretched bump.
// Returns the new bump, 'celOff' the buffer's offset from the arena start (0 without enhancement).
uint32_t fastBumpOf(uint32_t origBump, const Cursor &c, bool isEnh, uint32_t &celOff) {
	const uint32_t bump = bumpOf(origBump, c, isEnh);
	if(!isEnh) {
		celOff = 0;
		return bump;
	}
	celOff = (bump + 3) & ~3u;
	return celOff + kEnhCelReadBufferBytes;
}

}  // namespace

uint32_t carveSize(uint32_t n, bool isEnh) {
	if(!isEnh) {
		return n;
	}
	if(n == kOrigPictureBytes) {
		return kEnhPictureBytes;
	}
	if(n == 0x10E6 || n == 0x0E6E || n == 0x25F6) {  // raw message.piv / ch.piv regions, see enhcarve.hpp
		return kEnhRawPictureBytes;
	}
	switch(n) {  // the nine pictures of the arena pack "Test" (mog LAB_05B9 +8 .. +48): 8/3, see enhcarve.hpp
		case 0x5958: case 0x5149: case 0x4658: case 0x3A55: case 0x6395: case 0x51C4: case 0x4C0A: case 0x51A5: case 0x8A03: {
			uint32_t v = (n * 8 + 2) / 3;  // rounded to nearest
			if((v ^ n) & 1) {
				++v;  // parity of n kept (the odd increments are followed by an ADDQ.L #1 realignment)
			}
			return v;
		}
		default:
			break;
	}
	return n + 2 * ((n + 3) / 4);  // n * 3 / 2, rounded up to keep the parity of n
}

void carveProgram(uint32_t chip, uint32_t fast, bool isEnh, PrgCarve &o) {
	const uint32_t P = kOrigPictureBytes;
	// chip: LAB_0044 lines 921-937
	Cursor c = {chip, 0, 0, isEnh};
	o.secstrt3_0 = c.v;
	o.l00c6 = o.l00c7 = c.v;
	c.add(P);
	o.secstrt3_4 = o.l00c8 = c.v;
	c.add(P);
	o.l00c9 = c.v;
	c.add(P);
	o.l00ca = c.v;
	o.secstrt3_8 = c.v;  // MOVE.L D0,8(A0) is executed twice; the second store (screen 3) wins
	c.add(P);
	o.l0124 = c.v;
	o.chipBump = bumpOf(kProgramChipBump, c, isEnh);
	o.chipNext = chip + o.chipBump;
	// fast: lines 938-959
	Cursor f = {fast, 0, 0, isEnh};
	o.l0045 = f.v;
	f.add(0x7530);
	o.l011a_0 = f.v;
	f.add(P);
	o.l011a_4 = f.v;
	f.add(P);
	o.l011a_20 = f.v;
	f.add(0x10E6);
	o.l011a_16 = f.v;
	f.add(0x5F50);
	o.l00cb = f.v;
	f.add(P);
	o.l00cc = f.v;
	f.add(P);
	o.l00cd = f.v;
	f.add(P);
	o.l00ce = f.v;
	f.add(P);
	o.l00cf = f.v;
	f.add(P);
	uint32_t celOff;
	o.fastBump = fastBumpOf(kProgramFastBump, f, isEnh, celOff);
	o.celRead = isEnh ? fast + celOff : 0;
	o.fastNext = fast + o.fastBump;
}

void carveMog(uint32_t chip, uint32_t fast, bool isEnh, MogCarve &o) {
	const uint32_t P = kOrigPictureBytes;
	// chip: LAB_0004 lines 218-246
	Cursor c = {chip, 0, 0, isEnh};
	o.b8_0 = o.l05c0 = c.v;
	c.add(P);
	o.b8_12 = o.l05c7 = c.v;
	c.add(0x59D8);
	o.b8_4 = c.v;
	c.add(0x11D28);
	o.b8_8 = c.v;
	o.l05ca = c.v + carveSize(P, isEnh);        // D1 = D0 + $9c40
	o.l05cb = c.v + carveSize(0xEA60, isEnh);   // D1 = D0 + $ea60
	c.add(0x13880);
	o.b8_28 = o.l05c8 = c.v;
	c.add(0xC350);
	o.b8_36 = o.l0664 = c.v;
	c.add(0x3A98);
	o.b8_16 = o.l05c1 = c.v;
	c.add(P);
	o.l05c9 = c.v;
	o.chipBump = bumpOf(kMogChipBump, c, isEnh);
	o.chipNext = chip + o.chipBump;
	// fast: lines 247-295
	Cursor f = {fast, 0, 0, isEnh};
	o.b9_0 = o.l05c2 = o.b9_44 = f.v;
	f.add(0xC350);
	o.b9_8 = f.v;
	f.add(0x5958);
	o.b9_12 = f.v;
	f.add(0x5149);
	o.b9_20 = f.v;
	f.add(0x4658);
	o.b9_16 = f.v;
	f.add(0x3A55);
	o.b9_24 = f.v;
	f.add(0x6395);
	o.b9_32 = f.v;
	f.add(0x51C4);
	o.b9_36 = f.v;
	f.add(0x4C0A);
	o.b9_28 = f.v;
	f.add(0x51A5);
	o.b9_92 = f.v;
	f.add(0x8A03);
	f.v += 1;  // ADDQ.L #1,D0
	f.sum += 1;
	f.enhSum += 1;
	o.b9_48 = f.v;
	f.add(0x67F8);
	o.b9_40 = f.v;
	f.add(0x6328);
	o.b9_52 = f.v;
	f.add(0x0E6E);
	o.b9_56 = f.v;
	f.add(0x25F6);
	o.b9_68 = o.l05c6 = f.v;
	f.add(0x01E0);
	o.b9_72 = f.v;
	f.add(0x0240);
	o.b9_84 = f.v;
	f.add(0x2328);
	o.b9_88 = f.v;
	f.add(0x2328);
	o.l05bb = f.v;
	f.add(0x2710);
	o.secstrt14 = f.v;
	f.add(0x0960);
	o.l0a83 = o.l05c3 = f.v;
	f.add(0x1F40);
	uint32_t celOff;
	o.fastBump = fastBumpOf(kMogFastBump, f, isEnh, celOff);
	o.celRead = isEnh ? fast + celOff : 0;
	o.fastNext = fast + o.fastBump;
}

ArenaSizes arenaSizes(bool isEnh, uint32_t scratchChip, uint32_t scratchFast) {
	PrgCarve p;
	MogCarve m;
	carveProgram(0, 0, isEnh, p);
	carveMog(0, 0, isEnh, m);
	ArenaSizes s;
	s.chip = (p.chipBump > m.chipBump ? p.chipBump : m.chipBump) + scratchChip;
	s.fast = (p.fastBump > m.fastBump ? p.fastBump : m.fastBump) + scratchFast;
	return s;
}

}  // namespace ms
