// rt/sdl/log - rt::platform::log for the PC (ROADMAP 11.4): stderr plus moonstone.log next to the executable.
#include "rt/sdl/log.hpp"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>

#include "rt/platform/log.hpp"
#include "rt/platform/system.hpp"

namespace rt {
namespace sdl {

namespace {
FILE *s_pLogFile = nullptr;   // closed again by logClose (LogSession)
char s_szLogDir[480];         // directory of the executable with trailing '/', "" if unknown
bool s_isQuiet = false;
}

void logOpen() {
	if(s_pLogFile != nullptr) {
		return;
	}
	char szPath[512] = "moonstone.log";
	const char *szBase = SDL_GetBasePath();   // SDL3: owned by SDL, not freed
	if(szBase != nullptr) {
		snprintf(s_szLogDir, sizeof(s_szLogDir), "%s", szBase);
		snprintf(szPath, sizeof(szPath), "%smoonstone.log", s_szLogDir);
	}
	s_pLogFile = fopen(szPath, "w");
	if(s_pLogFile == nullptr) {
		fprintf(stderr, "moonstone: cannot write %s, logging to stderr only\n", szPath);
	}
}

void logClose() {
	if(s_pLogFile != nullptr) {
		fclose(s_pLogFile);
		s_pLogFile = nullptr;
	}
}

const char *logDirectory() {
	return s_szLogDir;
}

void uiSetQuiet(bool isQuiet) {
	s_isQuiet = isQuiet;
}

bool uiIsQuiet() {
	return s_isQuiet;
}

void stubHit(const char *szName, const char *szTask, bool &isReported) {
	if(isReported) {
		return;
	}
	isReported = true;
	platform::logFormat("STUB %s (ROADMAP %s) called\n", szName, szTask);
}

}  // namespace sdl

namespace platform {

void logLine(const char *szText) {
	fputs(szText, stderr);
	if(sdl::s_pLogFile != nullptr) {
		fputs(szText, sdl::s_pLogFile);
		fflush(sdl::s_pLogFile);
	}
}

void logFormat(const char *szFormat, ...) {
	char szBuffer[192];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(szBuffer, sizeof(szBuffer), szFormat, args);
	va_end(args);
	logLine(szBuffer);
}

void bootLog(const char *szLine) {
	logLine(szLine);
}

void bootLogMemory(const char *szWhen) {
	logFormat("memory after %s: chip %u fast %u\n", szWhen, (unsigned)memFreeChip(), (unsigned)memFreeFast());
}

void fatal(const char *szMessage) {
	logFormat("FATAL: %s\n", szMessage);
	if(!sdl::uiIsQuiet()) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonstone", szMessage, nullptr);
	}
}

}  // namespace platform

// The Amiga's rt/system.cpp (an A file) defines rt::fatal on top of Intuition; the host-clean flow.cpp calls it.
void fatal(const char *szMessage) {
	platform::fatal(szMessage);
}

void bootLog(const char *szLine) {
	platform::bootLog(szLine);
}

void bootLogMemory(const char *szWhen) {
	platform::bootLogMemory(szWhen);
}

}  // namespace rt
