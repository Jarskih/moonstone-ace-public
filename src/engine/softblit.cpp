// engine/softblit - see softblit.hpp.  Reference: Amiga Hardware Reference Manual, "Blitter" chapter (word flow, shifter, masks,
// minterms, descending mode, modulos).
#include "engine/softblit.hpp"
#include "engine/endian.hpp"

namespace ms {
namespace softblit {

namespace {

constexpr uint16_t CON0_USE_A = 0x0800, CON0_USE_B = 0x0400, CON0_USE_C = 0x0200, CON0_USE_D = 0x0100;
constexpr uint16_t CON1_DESC = 0x0002;
constexpr uint16_t CON1_USED = 0xF002;   // shift of B (bits 15..12) and the descending flag: the only BLTCON1 bits the game sets

// One source or destination channel for the length of a blit.
struct Channel {
	bool on;
	uint32_t pt;     // address of the next word
	int32_t mod;     // added (ascending) or subtracted (descending) after every row
};

Channel makeChannel(bool on, uint32_t pt, int16_t mod) {
	Channel c;
	c.on = on;
	c.pt = pt & ~1u;                                        // address bit 0 is ignored by the chip
	c.mod = (int32_t)(int16_t)(mod & ~1);                   // so is bit 0 of a modulo
	return c;
}

// The byte pointer of the word at 'addr', or false when the word lies outside the view.
bool resolve(const ChipView &mem, uint32_t addr, uint8_t *&out) {
	const uint32_t off = addr - mem.origin;
	if(mem.size != 0 && (addr < mem.origin || mem.size < 2u || off > mem.size - 2u)) {
		return false;
	}
	out = mem.bytes + off;
	return true;
}

// The barrel shifter of A or B (ascending): the word that leaves the channel is 'prev:word' shifted right by 'shift', low 16 bits.
// 'prev' is the previous word of the same row (zero at the start of a row).
uint16_t shifted(uint16_t prev, uint16_t word, unsigned shift) {
	return (uint16_t)((((uint32_t)prev << 16) | word) >> shift);
}

// Reads the next word of a source channel into 'word' and steps it; false = bad address.  A disabled channel reads as zero.
bool fetch(const ChipView &mem, Channel &ch, int32_t step, uint16_t &word) {
	word = 0;
	if(!ch.on) {
		return true;
	}
	uint8_t *p;
	if(!resolve(mem, ch.pt, p)) {
		return false;
	}
	word = be::ld16(p);
	ch.pt += (uint32_t)step;
	return true;
}

// End of a row: the modulo moves the pointer on (back, in descending mode).
void endRow(Channel &ch, bool desc) {
	if(ch.on) {
		ch.pt += (uint32_t)(desc ? -ch.mod : ch.mod);
	}
}

}  // namespace

uint16_t minterm(uint8_t lf, uint16_t a, uint16_t b, uint16_t c) {
	uint16_t d = 0;
	for(unsigned i = 0; i < 8; ++i) {
		if(lf & (1u << i)) {
			const uint16_t ma = (i & 4) ? a : (uint16_t)~a;
			const uint16_t mb = (i & 2) ? b : (uint16_t)~b;
			const uint16_t mc = (i & 1) ? c : (uint16_t)~c;
			d |= (uint16_t)(ma & mb & mc);
		}
	}
	return d;
}

const char *statusText(Status st) {
	switch(st) {
		case BLIT_OK: return "ok";
		case BLIT_UNSUPPORTED: return "unsupported blitter mode";
		case BLIT_BAD_ADDRESS: return "blitter address outside chip RAM";
	}
	return "?";
}

Status run(const ChipView &mem, const BlitOp &op, BlitEnd *end) {
	const bool desc = (op.con1 & CON1_DESC) != 0;
	const unsigned shiftA = op.con0 >> 12;
	const unsigned shiftB = op.con1 >> 12;
	const uint8_t lf = (uint8_t)(op.con0 & 0xFF);

	// the register combinations the game never programs
	if(op.con1 & ~CON1_USED) {
		return BLIT_UNSUPPORTED;                            // line, fill, carry-in, unused bits
	}
	if(desc && (shiftA || shiftB)) {
		return BLIT_UNSUPPORTED;                            // a descending shift goes left; never used
	}
	const bool useA = (op.con0 & CON0_USE_A) != 0, useB = (op.con0 & CON0_USE_B) != 0;
	const bool useC = (op.con0 & CON0_USE_C) != 0, useD = (op.con0 & CON0_USE_D) != 0;
	if((useA && !(op.channels & BLT_CH_A)) || (useB && !(op.channels & BLT_CH_B)) || (useC && !(op.channels & BLT_CH_C)) ||
	   (useD && !(op.channels & BLT_CH_D))) {
		return BLIT_UNSUPPORTED;                            // an enabled channel whose registers were never loaded
	}

	unsigned rows = op.size >> 6;
	unsigned words = op.size & 0x3F;
	if(rows == 0) rows = 1024;
	if(words == 0) words = 64;

	Channel a = makeChannel(useA, op.ptA, op.modA), b = makeChannel(useB, op.ptB, op.modB);
	Channel c = makeChannel(useC, op.ptC, op.modC), d = makeChannel(useD, op.ptD, op.modD);
	const int32_t step = desc ? -2 : 2;
	Status status = BLIT_OK;

	for(unsigned row = 0; row < rows && status == BLIT_OK; ++row) {
		uint16_t prevA = 0, prevB = 0;                      // the shifter starts every row empty
		for(unsigned col = 0; col < words; ++col) {
			uint16_t wa, wb, wc;
			if(!fetch(mem, a, step, wa) || !fetch(mem, b, step, wb)) {
				status = BLIT_BAD_ADDRESS;
				break;
			}
			uint8_t *pc = nullptr;
			wc = 0;
			if(c.on) {                                      // C is read like A and B, but D usually writes the word it came from
				if(!resolve(mem, c.pt, pc)) {
					status = BLIT_BAD_ADDRESS;
					break;
				}
				wc = be::ld16(pc);
			}
			if(col == 0) wa &= op.afwm;                     // the masks act on A before the shifter, in processing order
			if(col == words - 1) wa &= op.alwm;             // (a one-word row gets both)
			const uint16_t sa = shifted(prevA, wa, shiftA), sb = shifted(prevB, wb, shiftB);
			prevA = wa;
			prevB = wb;
			if(d.on) {
				uint8_t *pd;
				if(!resolve(mem, d.pt, pd)) {
					status = BLIT_BAD_ADDRESS;
					break;
				}
				be::st16(pd, minterm(lf, sa, sb, wc));
				d.pt += (uint32_t)step;
			}
			if(c.on) c.pt += (uint32_t)step;
		}
		if(status != BLIT_OK) {
			break;
		}
		endRow(a, desc);
		endRow(b, desc);
		endRow(c, desc);
		endRow(d, desc);
	}

	if(end) {
		end->ptA = a.pt;
		end->ptB = b.pt;
		end->ptC = c.pt;
		end->ptD = d.pt;
	}
	return status;
}

}  // namespace softblit
}  // namespace ms
