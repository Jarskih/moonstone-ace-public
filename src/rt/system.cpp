#include "rt/system.hpp"
#include "rt/autoplay.hpp"
#include "rt/guards.hpp"

#include <stdint.h>
#include <ace/managers/system.h>
#include <ace/managers/blit.h>
#include <ace/managers/copper.h>
#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include <ace/managers/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>

// proto/intuition.h inlines use this global; ACE does not open intuition.library (fatal() does, on demand)
struct IntuitionBase *IntuitionBase = nullptr;

namespace rt {

void systemCreate() {
	::systemCreate();
	logOpen(0);
	autoplayInit();  // MS_AUTOPLAY only: serial log + PROGDIR:autoplay.txt
	memCreate();
	timerCreate();
	blitManagerCreate();
	copCreate();
}

void systemDestroy() {
	copDestroy();
	blitManagerDestroy();
	timerDestroy();
	memDestroy();
	logClose();
	::systemDestroy();
}

namespace {

ULONG strLen(const char *sz) {
	ULONG n = 0;
	while(sz[n]) {
		++n;
	}
	return n;
}

// Appends one line to PROGDIR:boot.log (NEWFILE on the first call of a run). Needs the OS: callers hold systemUse().
void bootLogRaw(const char *szLine) {
	static bool s_isFirst = true;
	// No requester when the volume is write-protected or not validated (a boot floppy): the log is just skipped.
	NoRequesters sQuiet;
	// Only on an HD install (a data/ drawer next to the exe): a floppy boot disk is never written (docs/INSTALL.md).
	static bool s_isHdChecked, s_isHd;
	if(!s_isHdChecked) {
		s_isHdChecked = true;
		DosLock sData("PROGDIR:data", SHARED_LOCK);
		s_isHd = static_cast<bool>(sData);
	}
	if(!s_isHd) {
		return;
	}
	DosHandle sLog("PROGDIR:boot.log", s_isFirst ? MODE_NEWFILE : MODE_READWRITE);
	if(!sLog) {
		return;
	}
	const BPTR fh = sLog.get();
	if(!s_isFirst) {
		Seek(fh, 0, OFFSET_END);
	}
	s_isFirst = false;
	Write(fh, const_cast<char *>(szLine), strLen(szLine));
}

}  // namespace

void bootLog(const char *szLine) {
	logWrite("%s", szLine);  // ACE log (serial with MS_AUTOPLAY, nothing in a plain Release)
	SystemAccess sOs;
	bootLogRaw(szLine);
}

void bootLogMemory(const char *szWhen) {
	char sz[160];
	const unsigned long ulChip = memGetFreeChipSize();
	const unsigned long ulFast = AvailMem(MEMF_FAST);
	const unsigned long ulChipMax = AvailMem(MEMF_CHIP | MEMF_LARGEST);
	const unsigned long ulFastMax = AvailMem(MEMF_FAST | MEMF_LARGEST);
	// no printf in the runtime: the ACE log formatter does it for the serial/log sink, the file gets the same text
	char *p = sz;
	auto put = [&](const char *t) { while(*t) *p++ = *t++; };
	auto num = [&](unsigned long v) {
		char d[12];
		int i = 0;
		do { d[i++] = static_cast<char>('0' + v % 10); v /= 10; } while(v);
		while(i) *p++ = d[--i];
	};
	put("mem "); put(szWhen); put(": free chip "); num(ulChip); put(" (largest "); num(ulChipMax);
	put("), free fast "); num(ulFast); put(" (largest "); num(ulFastMax); put(")\n");
	*p = 0;
	bootLog(sz);
}

void fatal(const char *szMsg) {
	// ROADMAP 10.2b: messages may run to several lines (the missing-disk report of rt/origload).
	char sz[600];
	ULONG n = 0;
	for(const char *t = "moonstone-ace: "; *t; ++t) sz[n++] = *t;
	for(const char *t = szMsg; *t && n < sizeof(sz) - 3; ++t) sz[n++] = *t;
	sz[n++] = '\n';
	sz[n] = 0;
	bootLog(sz);
	SystemAccess sOs;
	BPTR out = Output();
	if(out) {
		Write(out, sz, n);
	}
	else {
		// Started from an icon: no console, so the classic recoverable alert: one record per line {x hi, x lo, y, text, 0,
		// more-lines flag}; the message's lines are wrapped at 76 characters (an 8-pixel font on a 640 pixel alert).
		IntuitionBase = reinterpret_cast<struct IntuitionBase *>(OpenLibrary(reinterpret_cast<CONST_STRPTR>("intuition.library"), 36));
		if(IntuitionBase) {
			auto sCloseIntuition = ms::scopeExit([] {
				CloseLibrary(reinterpret_cast<struct Library *>(IntuitionBase));
				IntuitionBase = nullptr;
			});
			char al[900];
			ULONG k = 0, uLines = 0;
			ULONG i = 0;
			while(i + 1 < n && uLines < 20 && k < sizeof(al) - 90) {
				ULONG uLen = 0, uBreak = 0;
				while(i + uLen + 1 < n && sz[i + uLen] != '\n' && uLen < 76) {
					if(sz[i + uLen] == ' ') uBreak = uLen;
					++uLen;
				}
				if(uLen == 76 && sz[i + uLen] != '\n' && uBreak) uLen = uBreak;
				if(uLines) al[k++] = 1;                    // the previous record continues
				const ULONG uY = 14 + 10 * uLines;
				al[k++] = 0; al[k++] = 16; al[k++] = static_cast<char>(uY);
				for(ULONG j = 0; j < uLen; ++j) al[k++] = sz[i + j];
				al[k++] = 0;
				++uLines;
				i += uLen;
				while(i + 1 < n && (sz[i] == '\n' || sz[i] == ' ')) ++i;
			}
			al[k++] = 0;                                    // no more records
			DisplayAlert(RECOVERY_ALERT, reinterpret_cast<CONST_STRPTR>(al), 14 + 10 * uLines + 8);
		}
	}
}

}  // namespace rt

// Stack-smash guard (was provided by ace/generic/main.h)
extern "C" {
uintptr_t __stack_chk_guard = 0xe2dee396;
__attribute__((noreturn)) void __stack_chk_fail(void) {
	logWrite("ERR: STACK SMASHED\n");
	while(1) continue;
}
}
