// engine/rnc - see rnc.hpp. Transcribed from program.asm LAB_0190 (+ LAB_0196..LAB_01AE helpers).
// There is no twin in mog.asm.
//
// The stream is read BACKWARDS from its end (pRead, asm A6) and the output is written backwards from its
// end (pOut, asm A3). Bits come from a byte shifted out at the top; the byte read after the last bit
// carries the previous top bit in (the ROXL after LSL), i.e. an ordinary MSB-first bit reader.
// Items, repeated until the read pointer reaches the stream start:
//   literals: bit 0 -> none; "10" -> 1; "11" -> a count read in widths 2,2,3,10 bits (a field that is
//             all ones continues with the next width), plus a base, then count+1 bytes taken from
//             the stream as they come (pre-decrement), written pre-decrement.
//   (if the stream is used up after the literals, stop)
//   match:    length code (unary up to 4) -> 2..9 with extra bits, or 10 + 10 bits;
//             offset code: for length 2, 6 or 9 bits (+64 for the 9-bit form); otherwise a unary code
//             selecting 12/5/8 bits with a base of 0x120/0/0x20. Copies `length` bytes downwards from
//             pOut + offset + length - 1 (offset 0: from pOut + 1, so the last byte repeats).
//
// The asm has no bounds: a corrupt stream runs wild. libmoon_assets' rnc1.c is a different, guessed
// format (18-byte header, Huffman tables) and decodes none of the game's files; the asm is the reference.
#include "engine/rnc.hpp"

namespace ms {

namespace {

constexpr uint8_t LITERAL_WIDTH[4] = {10, 3, 2, 2};  // LAB_019F, indexed by the step counter (3 first)
constexpr uint8_t LITERAL_BASE[4] = {14, 7, 4, 1};   // LAB_01A0
constexpr uint8_t LENGTH_BITS[5] = {10, 2, 1, 0, 0};  // LAB_01A8
constexpr uint8_t LENGTH_BASE[5] = {10, 6, 4, 3, 2};  // LAB_01A9 + 1
constexpr uint8_t OFFSET_BITS[3] = {12, 5, 8};        // LAB_01B0 + 1 (stored as count - 1)
constexpr uint16_t OFFSET_BASE[3] = {0x0120, 0x0000, 0x0020};  // LAB_01B1

uint32_t readBe32(const uint8_t *p) {
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// LAB_01A1: the bit reader. The first byte (the stream's last) is loaded without a bit taken from it.
class BitReader {
public:
	BitReader(const uint8_t *pEnd): m_pRead(pEnd), m_ubBits(*--m_pRead) {}

	bool bit() {
		const bool isSet = m_ubBits & 0x80;
		m_ubBits <<= 1;
		if(m_ubBits)
			return isSet;
		const uint8_t ubNext = *--m_pRead;
		m_ubBits = uint8_t((ubNext << 1) | (isSet ? 1 : 0));
		return ubNext & 0x80;
	}

	// ROXL loop: n bits, first one ends up highest.
	uint16_t bits(uint8_t n) {
		uint16_t uwValue = 0;
		while(n--)
			uwValue = uint16_t((uwValue << 1) | (bit() ? 1 : 0));
		return uwValue;
	}

	uint8_t byte() { return *--m_pRead; }
	const uint8_t *pos() const { return m_pRead; }

private:
	const uint8_t *m_pRead;
	uint8_t m_ubBits;
};

// LAB_0198: a run of literal bytes, possibly empty.
void copyLiterals(BitReader &r, uint8_t *&pOut) {
	if(!r.bit())
		return;
	uint16_t uwCount = 0;  // bytes - 1
	if(r.bit()) {
		uint8_t ubStep = 3;
		for(;;) {
			const uint8_t ubWidth = LITERAL_WIDTH[ubStep];
			uwCount = r.bits(ubWidth);
			const uint16_t uwAllOnes = uint16_t(~(0xFFFFu << ubWidth));
			if(ubStep == 0 || uwCount != uwAllOnes)
				break;
			--ubStep;
		}
		uwCount = uint16_t(uwCount + LITERAL_BASE[ubStep]);
	}
	for(uint32_t n = uint32_t(uwCount) + 1; n; --n)
		*--pOut = r.byte();
}

// LAB_01A3: match length, 2..1033.
uint16_t readLength(BitReader &r) {
	int8_t bStep = 3;
	while(r.bit()) {
		if(--bStep < 0)
			break;
	}
	const uint8_t ubIdx = uint8_t(bStep + 1);
	return uint16_t(r.bits(LENGTH_BITS[ubIdx]) + LENGTH_BASE[ubIdx]);
}

// LAB_01AA: match offset.
uint16_t readOffset(BitReader &r, uint16_t uwLength) {
	if(uwLength == 2) {  // LAB_01AE
		uint8_t ubBits = 6;
		uint16_t uwBase = 0;
		if(r.bit()) {
			ubBits = 9;
			uwBase = 64;
		}
		return uint16_t(r.bits(ubBits) + uwBase);
	}
	int8_t bStep = 1;
	while(r.bit()) {
		if(--bStep < 0)
			break;
	}
	const uint8_t ubIdx = uint8_t(bStep + 1);
	return uint16_t(r.bits(OFFSET_BITS[ubIdx]) + OFFSET_BASE[ubIdx]);
}

// Decodes the stream [pStart, pStart + packedLen) so that it ends at pOutEnd; returns where it begins.
uint8_t *unpack(const uint8_t *pStart, uint32_t packedLen, uint8_t *pOutEnd) {
	BitReader r(pStart + packedLen);
	uint8_t *pOut = pOutEnd;
	for(;;) {
		copyLiterals(r, pOut);
		if(r.pos() <= pStart)
			break;
		const uint16_t uwLength = readLength(r);
		const uint16_t uwOffset = readOffset(r, uwLength);
		const uint16_t uwLast = uint16_t(uwLength - 1);  // SUBQ.W #1,D6; the copy counts it as a word
		const uint8_t *pCopy = uwOffset ? pOut + int16_t(uwOffset) + int16_t(uwLast) : pOut + 1;
		for(uint32_t n = uint32_t(uwLast) + 1; n; --n)
			*--pOut = *--pCopy;
	}
	return pOut;
}

}  // namespace

uint32_t rncDecode(const uint8_t *pSrc, uint8_t *pDst) {
	if(readBe32(pSrc) != RNC_MAGIC)
		return 0;
	uint8_t *pOutEnd = pDst + readBe32(pSrc + 4);
	const uint8_t *pStart = pSrc + RNC_HEADER_SIZE;
	const uint8_t *pOutStart = unpack(pStart, readBe32(pSrc + 8), pOutEnd);
	const uint32_t ulSize = uint32_t(pOutEnd - pOutStart);
	if(pOutStart != pDst) {
		for(uint32_t i = 0; i < ulSize; ++i)
			pDst[i] = pOutStart[i];
	}
	return ulSize;
}

uint32_t rncDecodeInPlace(uint8_t *pBuf) {
	if(readBe32(pBuf) != RNC_MAGIC)
		return 0;
	uint8_t *pStart = pBuf + RNC_HEADER_SIZE;
	uint8_t *pOutEnd = pStart + readBe32(pBuf + 4) + RNC_LEEWAY;
	uint8_t *pOutStart = unpack(pStart, readBe32(pBuf + 8), pOutEnd);
	const uint32_t ulSize = uint32_t(pOutEnd - pOutStart);
	if(!ulSize)
		return 0;
	// LAB_01B2: slide the result down to the start of the buffer, clear what is left up to pOutEnd.
	uint8_t *pDst = pBuf;
	for(uint32_t i = 0; i < ulSize; ++i)
		*pDst++ = pOutStart[i];
	for(uint32_t n = uint32_t(pOutStart - pBuf); n; --n)
		*pDst++ = 0;
	return ulSize;
}

}  // namespace ms
