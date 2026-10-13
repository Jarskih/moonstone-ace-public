// rt/chipmem - words and longwords in big-endian bytes (chip RAM images, loaded files) for the platform-neutral rt files (ROADMAP 11.10).
//
// engine/endian.hpp gives byte compositions that are right on every CPU; on the 68k they are what the sources spelled out by hand.  This
// header is for the places that used to be plain `*reinterpret_cast<UWORD *>(p)` accesses: on the big-endian target they stay exactly that (one
// move.w / move.l, the Amiga code does not change), on a little-endian host they become the byte forms.  p must be aligned where the 68000 needs
// it (the original's accesses were); the little-endian forms do not mind.
#pragma once
#include <stdint.h>

#include "engine/endian.hpp"

namespace rt {

inline uint16_t chipLd16(const void *p) {
#if MS_BIG_ENDIAN
	return *static_cast<const uint16_t *>(p);
#else
	return ms::be::ld16(static_cast<const uint8_t *>(p));
#endif
}

inline uint32_t chipLd32(const void *p) {
#if MS_BIG_ENDIAN
	return *static_cast<const uint32_t *>(p);
#else
	return ms::be::ld32(static_cast<const uint8_t *>(p));
#endif
}

inline void chipSt16(void *p, uint16_t v) {
#if MS_BIG_ENDIAN
	*static_cast<uint16_t *>(p) = v;
#else
	ms::be::st16(static_cast<uint8_t *>(p), v);
#endif
}

inline void chipSt32(void *p, uint32_t v) {
#if MS_BIG_ENDIAN
	*static_cast<uint32_t *>(p) = v;
#else
	ms::be::st32(static_cast<uint8_t *>(p), v);
#endif
}

}  // namespace rt
