// rt/serlog - see serlog.hpp.
#include "rt/serlog.hpp"
#include "rt/autoplay.hpp"

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY

#include <stdarg.h>
#include <stdio.h>
#include <ace/managers/misc_resource.h>
#include <ace/managers/system.h>
#include <ace/utils/custom.h>
#include <hardware/custom.h>

namespace rt {

namespace {
bool s_isOpen;
}

void serLogOpen() {
	if(miscResourceIsUsed(MISC_SUBRESOURCE_SERIAL) || miscResourceTryUse(MISC_SUBRESOURCE_SERIAL)) {
		g_pCustom->serper = SERPER(115200);
		s_isOpen = true;
	}
}

void serLogWrite(const char *szText) {
	if(!s_isOpen) {
		return;
	}
	for(const char *p = szText; *p; ++p) {
		ULONG ulSpin = 0;
		while(!(g_pCustom->serdatr & (1 << 13))) {  // TBE: transmit buffer empty
			if(++ulSpin > 400000UL) {
				return;                              // nothing listens: do not hang the game
			}
		}
		g_pCustom->serdat = (UWORD)(0x100 | (UBYTE)*p);  // bit 8 = stop bit
	}
}

void serLogf(const char *szFormat, ...) {
	char szBuf[192];
	va_list vaArgs;
	va_start(vaArgs, szFormat);
	vsnprintf(szBuf, sizeof(szBuf), szFormat, vaArgs);
	va_end(vaArgs);
	serLogWrite(szBuf);
	autoplayNoteLine(szBuf);
}

}  // namespace rt

#endif
