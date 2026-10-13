// ROADMAP 11.1e: the host stand-in for ACE's <ace/types.h>.  The PC build (-DMS_PLATFORM=SDL) and the host ratchet test
// (tests/test_host_rt.py) put include/host/ before everything else on the include path, so the host-clean src/rt files that only
// need the integer typedefs compile without an ACE checkout.  It carries exactly what those files use: the fixed-width typedefs,
// the limits and the attribute macros as no-ops.  It is NOT used by the Amiga build (that one has ACE's real header).
#pragma once
#include <stdint.h>

typedef uint8_t  UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
typedef int8_t   BYTE;
typedef int16_t  WORD;
typedef int32_t  LONG;

typedef UWORD FUBYTE;
typedef UWORD FUWORD;
typedef ULONG FULONG;
typedef WORD  FBYTE;
typedef WORD  FWORD;
typedef LONG  FLONG;

#define PRI_FUBYTE "hu"
#define PRI_FUWORD "hu"
#define PRI_FULONG "u"
#define PRI_FBYTE  "hd"
#define PRI_FWORD  "hd"
#define PRI_FLONG  "d"

#if !defined(ULONG_MAX)
#define ULONG_MAX 0xFFFFFFFFu
#endif
#define UWORD_MAX 0xFFFFu
#define UBYTE_MAX 0xFFu

// Attributes of the Amiga toolchain that mean nothing on the host.
#define INTERRUPT
#define INTERRUPT_END do {} while(0)
#define HWINTERRUPT
#define UNUSED_ARG __attribute__((unused))
#define CHIP
#define CHIP_DATA
#define CHIP_CODE
#define CHIP_BSS
#define FAR
#define ALWAYS_INLINE __attribute__((always_inline))
#define FN_HOTSPOT
#define FN_COLDSPOT
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
