// engine/addrcell - 32-bit operands that may be address cells of the owned data images (ROADMAP 11.3, docs/PC_ENDIAN.md 2).
//
// tools/gen_data.py turns every relocated longword of the original's data (DC.L LABEL) into a native pointer member, wherever it
// sits: a jump target inside a fight script, the text pointer of a table entry.  The bytes around it stay big-endian.  Code that
// walks such data as big-endian bytes (script interpreters, table scanners) reads its longwords with ms::addr::ld32: a native load
// when the longword is one of those pointer cells, be::ld32 otherwise.  On the 68k both are the same bytes, so it is be::ld32.
// The little-endian platform registers the generated cell table (rt::g_ownedPtrCells) at start-up with ms::addr::setCells; with
// nothing registered (host tests) every longword reads as big-endian bytes.
#pragma once
#include <stdint.h>

#include "engine/endian.hpp"

namespace ms { namespace addr {

// One owned object and the offsets of its pointer cells (ascending).
struct CellObject {
	const unsigned char *pBeg;
	uint32_t ulSize;
	const uint32_t *pOffsets;
	uint32_t ulCount;
};

#if MS_BIG_ENDIAN
inline void setCells(const CellObject *, uint32_t) {}
inline bool isCell(const void *) { return false; }
inline uint32_t ld32(const uint8_t *p) { return be::ld32(p); }
#else
void setCells(const CellObject *pObjects, uint32_t ulCount);
bool isCell(const void *p);   // p is the first byte of a registered pointer cell
inline uint32_t ld32(const uint8_t *p) {
	if(isCell(p)) {
		uint32_t v;
		__builtin_memcpy(&v, p, 4);
		return v;
	}
	return be::ld32(p);
}
#endif

}}  // namespace ms::addr
