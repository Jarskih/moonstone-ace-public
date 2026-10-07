// engine/rle - see rle.hpp. Transcribed from program.asm LAB_0448 (mog LAB_0C6D has the same routine).
//
// Everything is a stream of bits, MSB first, on both sides. Each item starts with a 2-bit opcode:
//   00  copy 16 + 4n source bits to the output; n = next 4 bits.        (asm: LAB_0469 -> LAB_047E/047A/0473)
//   01  copy 4 source bits to the output.                               (asm: LAB_0462 -> LAB_0473)
//   10  one bit v, then n = next 6 bits: n + 9 output bits of value v.  (asm: LAB_0452)
//   11  one bit v, then n = next 2 bits: n + 5 output bits of value v.  (asm: fallthrough after LAB_044D)
// The loop ends once the remaining-bit count (D7) is <= 0; it is only tested between items.
//
// The asm has aligned word/long fast paths (LAB_0473, LAB_047A, LAB_047E) and byte-wise fast paths for long
// fills (LAB_0455, LAB_045D); all of them produce what a plain bit loop produces, so this does the bit loop.
// Each output byte is cleared as soon as the cursor enters it. The asm does not clear the very first byte
// (its OR-based copies assume it is zero); here it is cleared too.
//
// moonshard's libmoon_assets rle_stile.c has the opcodes in a different order and different run lengths (it
// was written against .stile files that are not RLE streams at all); the asm is the reference here.
#include "engine/rle.hpp"

namespace ms {

namespace {

class BitReader {
public:
	explicit BitReader(const uint8_t *pSrc): m_pSrc(pSrc) {}

	uint32_t bit() {
		const uint32_t ulBit = (*m_pSrc >> m_ubPos) & 1;
		if(m_ubPos == 0) {
			m_ubPos = 7;
			++m_pSrc;
		}
		else {
			--m_ubPos;
		}
		return ulBit;
	}

	uint32_t bits(uint8_t ubCount) {
		uint32_t ulValue = 0;
		while(ubCount--) {
			ulValue = (ulValue << 1) | bit();
		}
		return ulValue;
	}

	const uint8_t *cursor() const { return m_pSrc; }

private:
	const uint8_t *m_pSrc;
	uint8_t m_ubPos = 7;  // next bit within *m_pSrc, 7 = MSB
};

class BitWriter {
public:
	explicit BitWriter(uint8_t *pDst): m_pDst(pDst) { *m_pDst = 0; }

	void put(uint32_t ulBit) {
		if(ulBit) {
			*m_pDst |= static_cast<uint8_t>(1u << m_ubPos);
		}
		else {
			*m_pDst &= static_cast<uint8_t>(~(1u << m_ubPos));
		}
		if(m_ubPos == 0) {
			m_ubPos = 7;
			*++m_pDst = 0;
		}
		else {
			--m_ubPos;
		}
	}

	void fill(uint32_t ulBit, uint32_t ulCount) {
		while(ulCount--) {
			put(ulBit);
		}
	}

	void copy(BitReader &Src, uint32_t ulCount) {
		while(ulCount--) {
			put(Src.bit());
		}
	}

	const uint8_t *cursor() const { return m_pDst; }

private:
	uint8_t *m_pDst;
	uint8_t m_ubPos = 7;  // next bit within *m_pDst, 7 = MSB
};

}  // namespace

uint32_t rleDecode(const uint8_t *pSrc, int32_t bitCount, uint8_t *pDst, const uint8_t **ppSrcEnd) {
	BitReader Src(pSrc);
	BitWriter Dst(pDst);
	int32_t lLeft = bitCount;
	while(lLeft > 0) {
		switch(Src.bits(2)) {
			case 0: {
				const uint32_t ulRun = 16 + 4 * Src.bits(4);
				lLeft -= static_cast<int32_t>(ulRun);
				Dst.copy(Src, ulRun);
			} break;
			case 1:
				lLeft -= 4;
				Dst.copy(Src, 4);
				break;
			case 2: {
				const uint32_t ulFill = Src.bit();
				const uint32_t ulRun = Src.bits(6) + 9;
				lLeft -= static_cast<int32_t>(ulRun);
				Dst.fill(ulFill, ulRun);
			} break;
			default: {
				const uint32_t ulFill = Src.bit();
				const uint32_t ulRun = Src.bits(2) + 5;
				lLeft -= static_cast<int32_t>(ulRun);
				Dst.fill(ulFill, ulRun);
			} break;
		}
	}
	if(ppSrcEnd) {
		*ppSrcEnd = Src.cursor();
	}
	return static_cast<uint32_t>(Dst.cursor() - pDst);
}

}  // namespace ms
