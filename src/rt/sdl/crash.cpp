// rt/sdl/crash - see crash.hpp (ROADMAP 11.9).
#include "rt/sdl/crash.hpp"

#include <SDL3/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/platform/log.hpp"
#include "rt/sdl/log.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rt {
namespace sdl {

namespace {

constexpr size_t kReportMax = 6144;
char s_szReport[kReportMax];            // static: a crash on a wrecked stack or heap must not allocate
volatile int s_isInCrash = 0;
volatile int s_iQuitRequests = 0;
bool s_isInstalled = false;

// Appends printf-style text to the report buffer (no-op when full).
void add(size_t &n, const char *szFormat, ...) __attribute__((format(printf, 2, 3)));
void add(size_t &n, const char *szFormat, ...) {
	if(n >= kReportMax - 1) {
		return;
	}
	va_list args;
	va_start(args, szFormat);
	const int lWritten = vsnprintf(s_szReport + n, kReportMax - n, szFormat, args);
	va_end(args);
	if(lWritten > 0) {
		n += static_cast<size_t>(lWritten) < kReportMax - n ? static_cast<size_t>(lWritten) : kReportMax - n - 1;
	}
}

// Writes a file without stdio's buffers and locks.
void writeFileRaw(const char *szPath, const char *szText) {
#ifdef _WIN32
	HANDLE hFile = CreateFileA(szPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(hFile != INVALID_HANDLE_VALUE) {
		DWORD ulWritten = 0;
		WriteFile(hFile, szText, static_cast<DWORD>(strlen(szText)), &ulWritten, nullptr);
		CloseHandle(hFile);
	}
#else
	FILE *pFile = fopen(szPath, "w");
	if(pFile != nullptr) {
		fputs(szText, pFile);
		fclose(pFile);
	}
#endif
}

void crashLogPath(char *szOut, size_t cbOut) {
	snprintf(szOut, cbOut, "%scrash.log", logDirectory());
}

void showBox(const char *szLogPath) {
	if(uiIsQuiet()) {
		platform::logLine("moonstone: quiet run, message box suppressed\n");
		return;
	}
	char szText[700];
	snprintf(szText, sizeof(szText), "Moonstone has stopped because of an error.\n\nA report was written to:\n%s\n\nPlease send that file with a bug report.", szLogPath);
#ifdef _WIN32
	MessageBoxA(nullptr, szText, "Moonstone", MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonstone", szText, nullptr);
#endif
}

#ifdef _WIN32

struct CodeName {
	DWORD ulCode;
	const char *szName;
};
const CodeName kCodeNames[] = {
	{0xC0000005, "EXCEPTION_ACCESS_VIOLATION"}, {0xC0000006, "EXCEPTION_IN_PAGE_ERROR"}, {0xC000001D, "EXCEPTION_ILLEGAL_INSTRUCTION"},
	{0xC0000094, "EXCEPTION_INT_DIVIDE_BY_ZERO"}, {0xC0000095, "EXCEPTION_INT_OVERFLOW"}, {0xC0000096, "EXCEPTION_PRIV_INSTRUCTION"},
	{0xC00000FD, "EXCEPTION_STACK_OVERFLOW"}, {0xC000008C, "EXCEPTION_ARRAY_BOUNDS_EXCEEDED"}, {0xC000008E, "EXCEPTION_FLT_DIVIDE_BY_ZERO"},
	{0xC0000374, "STATUS_HEAP_CORRUPTION"}, {0xC0000409, "STATUS_STACK_BUFFER_OVERRUN"}, {0x80000002, "EXCEPTION_DATATYPE_MISALIGNMENT"},
	{0x80000003, "EXCEPTION_BREAKPOINT"},
};

const char *codeName(DWORD ulCode) {
	for(const CodeName &sName : kCodeNames) {
		if(sName.ulCode == ulCode) {
			return sName.szName;
		}
	}
	return "unknown exception";
}

// "module.dll+0x1234" for an address inside a loaded module, else false.
bool moduleOffset(const void *pAddr, char *szOut, size_t cbOut) {
	HMODULE hModule = nullptr;
	if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(pAddr), &hModule)
		|| hModule == nullptr) {
		return false;
	}
	char szPath[260];
	if(GetModuleFileNameA(hModule, szPath, sizeof(szPath)) == 0) {
		return false;
	}
	const char *szBase = szPath;
	for(const char *p = szPath; *p != 0; ++p) {
		if(*p == '\\' || *p == '/') {
			szBase = p + 1;
		}
	}
	snprintf(szOut, cbOut, "%s+0x%X", szBase, static_cast<unsigned>(reinterpret_cast<uintptr_t>(pAddr) - reinterpret_cast<uintptr_t>(hModule)));
	return true;
}

LONG WINAPI onException(EXCEPTION_POINTERS *pInfo) {
	if(s_isInCrash) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	s_isInCrash = 1;
	const EXCEPTION_RECORD *pRec = pInfo->ExceptionRecord;
	const CONTEXT *pCtx = pInfo->ContextRecord;
	size_t n = 0;
	char szWhere[300];
	add(n, "Moonstone crash report\n\n");
	add(n, "exception: 0x%08lX %s\n", static_cast<unsigned long>(pRec->ExceptionCode), codeName(pRec->ExceptionCode));
	if(moduleOffset(pRec->ExceptionAddress, szWhere, sizeof(szWhere))) {
		add(n, "at:        %p (%s)\n", pRec->ExceptionAddress, szWhere);
	}
	else {
		add(n, "at:        %p\n", pRec->ExceptionAddress);
	}
	if((pRec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || pRec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && pRec->NumberParameters >= 2) {
		const ULONG_PTR ulKind = pRec->ExceptionInformation[0];
		add(n, "access:    %s address %p\n", ulKind == 0 ? "read of" : (ulKind == 1 ? "write to" : "execution of"),
			reinterpret_cast<void *>(pRec->ExceptionInformation[1]));
	}
#if defined(_M_IX86) || defined(__i386__)
	add(n, "\nregisters:\n eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX\n esi=%08lX edi=%08lX ebp=%08lX esp=%08lX\n eip=%08lX eflags=%08lX\n",
		pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx, pCtx->Esi, pCtx->Edi, pCtx->Ebp, pCtx->Esp, pCtx->Eip, pCtx->EFlags);
	const uintptr_t uStackPtr = pCtx->Esp;
#else
	add(n, "\nregisters:\n rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX\n rsi=%016llX rdi=%016llX rbp=%016llX rsp=%016llX\n rip=%016llX\n",
		static_cast<unsigned long long>(pCtx->Rax), static_cast<unsigned long long>(pCtx->Rbx), static_cast<unsigned long long>(pCtx->Rcx),
		static_cast<unsigned long long>(pCtx->Rdx), static_cast<unsigned long long>(pCtx->Rsi), static_cast<unsigned long long>(pCtx->Rdi),
		static_cast<unsigned long long>(pCtx->Rbp), static_cast<unsigned long long>(pCtx->Rsp), static_cast<unsigned long long>(pCtx->Rip));
	const uintptr_t uStackPtr = static_cast<uintptr_t>(pCtx->Rsp);
#endif
	// Return addresses: every stack word that points into a loaded module (a stack scan, no unwinding: the build has no frame pointers).
	const NT_TIB *pTib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
	const uintptr_t uStackTop = reinterpret_cast<uintptr_t>(pTib->StackBase);
	add(n, "\nstack words that point into code:\n");
	unsigned uFound = 0;
	for(uintptr_t uAt = uStackPtr & ~static_cast<uintptr_t>(sizeof(void *) - 1); uAt + sizeof(void *) <= uStackTop && uAt < uStackPtr + 2048 && uFound < 24;
		uAt += sizeof(void *)) {
		const uintptr_t uWord = *reinterpret_cast<const uintptr_t *>(uAt);
		if(uWord > 0x10000 && moduleOffset(reinterpret_cast<const void *>(uWord), szWhere, sizeof(szWhere))) {
			add(n, " %s\n", szWhere);
			++uFound;
		}
	}
	add(n, "\nlog: %smoonstone.log\n", logDirectory());
	crashWriteReport(s_szReport);
	return EXCEPTION_EXECUTE_HANDLER;
}

BOOL WINAPI onConsoleEvent(DWORD ulEvent) {
	switch(ulEvent) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			break;
		default:
			return FALSE;
	}
	if(s_iQuitRequests++ > 0) {
		return FALSE;                    // the game did not leave after the first request: default handling ends the process
	}
	platform::logFormat("moonstone: console event %lu, leaving\n", static_cast<unsigned long>(ulEvent));
	SDL_Event sQuit;
	memset(&sQuit, 0, sizeof(sQuit));
	sQuit.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&sQuit);
	return TRUE;
}

void installPlatform() {
	SetUnhandledExceptionFilter(onException);
	SetConsoleCtrlHandler(onConsoleEvent, TRUE);
}

void removePlatform() {
	SetConsoleCtrlHandler(onConsoleEvent, FALSE);
	SetUnhandledExceptionFilter(nullptr);
}

#else

void installPlatform() {}
void removePlatform() {}

#endif

const char *signalName(int lSignal) {
	switch(lSignal) {
		case SIGSEGV: return "SIGSEGV (invalid memory access)";
		case SIGFPE: return "SIGFPE (arithmetic error)";
		case SIGILL: return "SIGILL (illegal instruction)";
		case SIGABRT: return "SIGABRT (abort)";
#ifdef SIGBUS
		case SIGBUS: return "SIGBUS (bus error)";
#endif
		default: return "signal";
	}
}

void onSignal(int lSignal) {
	if(s_isInCrash) {
		_Exit(3);
	}
	s_isInCrash = 1;
	size_t n = 0;
	add(n, "Moonstone crash report\n\nsignal: %d %s\n\nlog: %smoonstone.log\n", lSignal, signalName(lSignal), logDirectory());
	crashWriteReport(s_szReport);
	_Exit(3);
}

// Windows: the structured-exception filter reports faults (it has the registers); the C runtime would otherwise turn them into SIGSEGV /
// SIGFPE / SIGILL first, so only abort() is left to a signal handler there.
const int kSignals[] = {
#ifdef _WIN32
	SIGABRT,
#else
	SIGSEGV, SIGFPE, SIGILL, SIGABRT,
#ifdef SIGBUS
	SIGBUS,
#endif
#endif
};

}  // namespace

void crashWriteReport(const char *szReport) {
	char szPath[512];
	crashLogPath(szPath, sizeof(szPath));
	writeFileRaw(szPath, szReport);
	fputs(szReport, stderr);
	fflush(stderr);
	platform::logLine("\n");
	platform::logLine(szReport);
	showBox(szPath);
}

bool crashRaiseTest(const char *szKind) {
	if(strcmp(szKind, "access") == 0) {
		volatile int *pNull = nullptr;
		*pNull = 1;
		return true;
	}
	if(strcmp(szKind, "abort") == 0) {
		abort();
	}
	if(strcmp(szKind, "fatal") == 0) {
		platform::fatal("crash test: fatal error requested");
		return true;
	}
	if(strcmp(szKind, "quit") == 0) {
		SDL_Event sQuit;
		memset(&sQuit, 0, sizeof(sQuit));
		sQuit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&sQuit);
		return true;
	}
	return false;
}

CrashReporterScope::CrashReporterScope() {
	platform::crashReporterInstall();
}

CrashReporterScope::~CrashReporterScope() {
	platform::crashReporterRemove();
}

}  // namespace sdl

namespace platform {

void crashReporterInstall() {
	if(sdl::s_isInstalled) {
		return;
	}
	sdl::s_isInstalled = true;
	sdl::s_isInCrash = 0;
	sdl::s_iQuitRequests = 0;
	sdl::installPlatform();
	for(const int lSignal : sdl::kSignals) {
		signal(lSignal, sdl::onSignal);
	}
}

void crashReporterRemove() {
	if(!sdl::s_isInstalled) {
		return;
	}
	sdl::s_isInstalled = false;
	for(const int lSignal : sdl::kSignals) {
		signal(lSignal, SIG_DFL);
	}
	sdl::removePlatform();
}

}  // namespace platform
}  // namespace rt
