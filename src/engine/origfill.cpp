// engine/origfill - stream an original hunk executable once, copy the fill runs out of its hunks, check its CRC-32 (ROADMAP 10.2a; contract in include/engine/origfill.hpp).
#include "engine/origfill.hpp"

namespace ms {

namespace {

// The CRC-32 table, built by the compiler (no global constructor runs on the Amiga).
struct CrcTable {
	uint32_t a[256];
	constexpr CrcTable() : a() {
		for(uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for(int k = 0; k < 8; ++k) {
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			a[i] = c;
		}
	}
};
constexpr CrcTable kCrc;

constexpr uint32_t HUNK_NAME = 0x3E8, HUNK_CODE = 0x3E9, HUNK_DATA = 0x3EA, HUNK_BSS = 0x3EB, HUNK_RELOC32 = 0x3EC;
constexpr uint32_t HUNK_SYMBOL = 0x3F0, HUNK_DEBUG = 0x3F1, HUNK_END = 0x3F2, HUNK_HEADER = 0x3F3;
constexpr uint32_t CHUNK = 2048;

// Buffered reader over fnRead that keeps the CRC and the byte count.
class Stream {
public:
	Stream(OrigReadFn fnRead, void *pCtx, uint32_t ulSize) : m_fnRead(fnRead), m_pCtx(pCtx), m_ulSize(ulSize) {}

	// Next `n` bytes (n <= CHUNK) as a pointer into the buffer, or nullptr at the end of the file.
	const uint8_t *take(uint32_t n) {
		if(m_ulFill - m_ulPos < n) {
			if(!refill(n)) {
				return nullptr;
			}
		}
		const uint8_t *p = m_aBuf + m_ulPos;
		m_ulPos += n;
		return p;
	}

	bool readLong(uint32_t &ulOut) {
		const uint8_t *p = take(4);
		if(!p) {
			return false;
		}
		ulOut = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
		return true;
	}

	bool skip(uint32_t n) {
		while(n) {
			const uint32_t k = n < CHUNK ? n : CHUNK;
			if(!take(k)) {
				return false;
			}
			n -= k;
		}
		return true;
	}

	bool atEnd() const { return m_ulPos == m_ulFill && m_ulRead == m_ulSize; }
	uint32_t consumed() const { return m_ulRead - (m_ulFill - m_ulPos); }
	uint32_t crc() const { return ~m_ulCrc; }

private:
	bool refill(uint32_t n) {
		uint32_t ulKeep = m_ulFill - m_ulPos;
		for(uint32_t i = 0; i < ulKeep; ++i) {
			m_aBuf[i] = m_aBuf[m_ulPos + i];
		}
		m_ulPos = 0;
		m_ulFill = ulKeep;
		while(m_ulFill < n) {
			uint32_t ulWant = sizeof(m_aBuf) - m_ulFill;
			if(ulWant > m_ulSize - m_ulRead) {
				ulWant = m_ulSize - m_ulRead;
			}
			if(!ulWant) {
				return false;
			}
			const uint32_t ulGot = m_fnRead(m_pCtx, m_aBuf + m_ulFill, ulWant);
			if(!ulGot || ulGot > ulWant) {
				return false;
			}
			m_ulCrc = ~crc32Update(~m_ulCrc, m_aBuf + m_ulFill, ulGot);
			m_ulFill += ulGot;
			m_ulRead += ulGot;
		}
		return true;
	}

	OrigReadFn m_fnRead;
	void *m_pCtx;
	uint32_t m_ulSize;
	uint32_t m_ulRead = 0;
	uint32_t m_ulCrc = 0xFFFFFFFFu;
	uint32_t m_ulPos = 0, m_ulFill = 0;
	uint8_t m_aBuf[CHUNK + 4];
};

// Copy the part of every run of hunk `uwHunk` that lies in [ulOff, ulOff + ulLen) of the hunk from pData.
void copyRuns(const OrigFillTable *pTables, unsigned uTables, uint16_t uwHunk, uint32_t ulOff, const uint8_t *pData, uint32_t ulLen) {
	for(unsigned t = 0; t < uTables; ++t) {
		for(uint32_t i = 0; i < pTables[t].ulCount; ++i) {
			const OrigFillRun &r = pTables[t].pRuns[i];
			if(r.uwHunk != uwHunk || !r.ulSize) {
				continue;
			}
			const uint32_t ulBeg = r.ulSrc > ulOff ? r.ulSrc : ulOff;
			const uint32_t ulEnd = (r.ulSrc + r.ulSize < ulOff + ulLen) ? r.ulSrc + r.ulSize : ulOff + ulLen;
			for(uint32_t o = ulBeg; o < ulEnd; ++o) {
				r.pDst[o - r.ulSrc] = pData[o - ulOff];
			}
		}
	}
}

// Every run must lie inside its hunk (a facts/version mismatch would otherwise go unnoticed).
bool runsFit(const OrigFillTable *pTables, unsigned uTables, uint16_t uwHunk, uint32_t ulHunkSize) {
	for(unsigned t = 0; t < uTables; ++t) {
		for(uint32_t i = 0; i < pTables[t].ulCount; ++i) {
			const OrigFillRun &r = pTables[t].pRuns[i];
			if(r.uwHunk == uwHunk && r.ulSize && r.ulSrc + r.ulSize > ulHunkSize) {
				return false;
			}
		}
	}
	return true;
}

OrigFillResult parse(Stream &s, const OrigFillTable *pTables, unsigned uTables) {
	uint32_t v;
	if(!s.readLong(v)) {
		return ORIG_SHORT;
	}
	if(v != HUNK_HEADER) {
		return ORIG_NOT_HUNK;
	}
	for(;;) {                                   // resident library names
		if(!s.readLong(v)) {
			return ORIG_SHORT;
		}
		if(!v) {
			break;
		}
		if(!s.skip(4 * v)) {
			return ORIG_SHORT;
		}
	}
	uint32_t ulTable, ulFirst, ulLast;
	if(!s.readLong(ulTable) || !s.readLong(ulFirst) || !s.readLong(ulLast)) {
		return ORIG_SHORT;
	}
	if(ulLast < ulFirst || ulTable != ulLast - ulFirst + 1 || ulTable > 0xFFFF) {
		return ORIG_BAD_HUNK;
	}
	if(!s.skip(4 * ulTable)) {                  // the size table (the block headers repeat the sizes)
		return ORIG_SHORT;
	}
	uint32_t ulHunk = ulFirst;
	bool isAny = false;
	while(!s.atEnd()) {
		uint32_t ulType;
		if(!s.readLong(ulType)) {
			return ORIG_SHORT;
		}
		ulType &= 0x3FFFFFFFu;                      // memory flags may ride on the type word
		if(ulType == HUNK_CODE || ulType == HUNK_DATA || ulType == HUNK_BSS) {
			uint32_t ulSize;
			if(!s.readLong(ulSize)) {
				return ORIG_SHORT;
			}
			ulSize = (ulSize & 0x3FFFFFFFu) * 4;
			if(isAny) {
				++ulHunk;
			}
			isAny = true;
			if(ulHunk - ulFirst >= ulTable) {
				return ORIG_BAD_HUNK;
			}
			if(!runsFit(pTables, uTables, (uint16_t)ulHunk, ulSize)) {
				return ORIG_BAD_HUNK;
			}
			if(ulType == HUNK_BSS) {
				continue;
			}
			for(uint32_t ulOff = 0; ulOff < ulSize;) {
				const uint32_t n = (ulSize - ulOff) < CHUNK ? ulSize - ulOff : CHUNK;
				const uint8_t *p = s.take(n);
				if(!p) {
					return ORIG_SHORT;
				}
				copyRuns(pTables, uTables, (uint16_t)ulHunk, ulOff, p, n);
				ulOff += n;
			}
		}
		else if(ulType == HUNK_RELOC32) {
			for(;;) {
				uint32_t ulCount;
				if(!s.readLong(ulCount)) {
					return ORIG_SHORT;
				}
				if(!ulCount) {
					break;
				}
				if(ulCount > 0x100000 || !s.skip(4 + 4 * ulCount)) {
					return ORIG_SHORT;
				}
			}
		}
		else if(ulType == HUNK_END) {
		}
		else if(ulType == HUNK_SYMBOL) {
			for(;;) {
				uint32_t n;
				if(!s.readLong(n)) {
					return ORIG_SHORT;
				}
				if(!n) {
					break;
				}
				if(!s.skip(4 * n + 4)) {
					return ORIG_SHORT;
				}
			}
		}
		else if(ulType == HUNK_DEBUG || ulType == HUNK_NAME) {
			uint32_t n;
			if(!s.readLong(n) || !s.skip(4 * n)) {
				return ORIG_SHORT;
			}
		}
		else {
			return ORIG_BAD_HUNK;
		}
	}
	return ORIG_OK;
}

}  // namespace

uint32_t crc32Update(uint32_t ulCrc, const uint8_t *pData, uint32_t ulSize) {
	uint32_t c = ~ulCrc;
	for(uint32_t i = 0; i < ulSize; ++i) {
		c = kCrc.a[(c ^ pData[i]) & 0xFF] ^ (c >> 8);
	}
	return ~c;
}

OrigFillResult origFill(OrigReadFn fnRead, void *pCtx, uint32_t ulSize, uint32_t ulCrc, const OrigFillTable *pTables,
	unsigned uTables, uint32_t *pCrc) {
	Stream s(fnRead, pCtx, ulSize);
	OrigFillResult e = parse(s, pTables, uTables);
	if(pCrc) {
		*pCrc = s.crc();
	}
	if(e != ORIG_OK) {
		return e;
	}
	if(s.consumed() != ulSize) {
		return ORIG_LONG;
	}
	return s.crc() == ulCrc ? ORIG_OK : ORIG_CRC;
}

const char *origFillText(OrigFillResult eResult) {
	switch(eResult) {
		case ORIG_OK: return "ok";
		case ORIG_SHORT: return "the file is too short";
		case ORIG_NOT_HUNK: return "not an Amiga executable";
		case ORIG_BAD_HUNK: return "an unexpected hunk layout";
		case ORIG_LONG: return "the file is too long";
		case ORIG_CRC: return "a different version (checksum)";
	}
	return "?";
}

}  // namespace ms
