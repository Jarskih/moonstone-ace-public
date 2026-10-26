// rt/sdl/main - entry of the PC build (ROADMAP 11.4, 11.5, 11.6a): SDL up, a window at the largest whole scale that fits the desktop, the 50 Hz loop, the test pattern and the game (rtGameRun;
// the register-ABI layer is typed or adapted since 11.1d, see src/rt/sdl/*_entries.cpp).
//
//   moonstone.exe                  run the game (rtGameRun) from the ADFs in disks/ next to the exe; the window closes it (or Escape while no
//                                  game key handler is installed)
//   moonstone.exe --frames N       leave after N frames
//   moonstone.exe --skip-intro     leave the program overlay's intro at its first poll (the title screen shows at once)
//   moonstone.exe --pattern        the test pattern of rt/sdl/smoke instead of the game (what --smoke runs, with a window)
//   moonstone.exe --fast           unthrottled: frames as fast as the loop can run them (headless tests), no 50 Hz sleep
//   moonstone.exe --dump-frame F   write the last converted frame (320x200 RGB) to F (.png or .ppm) on the way out
//   moonstone.exe --smoke [N]      headless self-test: SDL dummy video and audio drivers, chip RAM and timer self-tests, the test pattern and
//                                  the test tone (rt/sdl/smoke), N frames (default 10), exit code 0 when all ran
//   moonstone.exe --dump-every N DIR   write DIR/f<frame>.png (320x200 RGB) every N frames while the game runs (a film strip of a scripted run)
//   moonstone.exe --warp-knight N AT   at frame AT put the current knight on the map position of record N (test hook: a black knight starts the duel)
//   moonstone.exe --dump-audio F   write every rendered audio frame (48 kHz, 16-bit stereo) to the WAV F (deterministic: one vblank = 960 frames)
//   moonstone.exe --audio-separation N   0..100, 100 = Amiga hard panning (default 70)
//   moonstone.exe --audio-filter off|a500|a500led|a1200|a1200led   the analogue output stage (default a1200)
//   moonstone.exe --audio-render box|blep   band-limited steps (default) or the exact box filter
//   moonstone.exe --audio-save     write the audio settings in force to <user directory>/audio.txt (read at every start; the options above override it)
//   moonstone.exe --disks DIR      the drawer holding the Moonstone ADF images and/or extracted files (default: disks/ next to the exe)
//   moonstone.exe --pref DIR       the user directory (saves, edited mods, exports) instead of SDL's pref path
//   moonstone.exe --read NAME      log the size and CRC-32 of the game file NAME found like the game would (ADF or plain); exit 7 if missing
//   moonstone.exe --keys "..."     scripted keyboard / mouse input (rt/sdl/input.hpp inputScriptParse), logged as "input: frame N ..." lines
//   moonstone.exe --keys-file F    the same script read from the text file F (a long soak script does not fit the 32 KB command line)
//   moonstone.exe --joykeys none|arrows   keyboard joystick for port 1 (default arrows: cursor keys + Right Ctrl / Space)
//   moonstone.exe --host | --join ADDR [--port N] [--name TEXT] [--delay N] [--timeout SEC] [--tcp]   pre-fill the Multiplayer screen like the Amiga
//                                  CLI (HOST / JOIN / PORT / NAME / DELAY / TIMEOUT / TCP, docs/PC_NET.md); the session starts from that screen
//   moonstone.exe --no-grab        do not capture the mouse while the window has the focus
//   moonstone.exe --scale N        open the window at N times 320x200 (default: the largest whole scale that fits the desktop)
//   moonstone.exe --aspect         stretch the pixels 6:5 in height like the Amiga's monitor (default: pixel-perfect square pixels)
//   moonstone.exe --fullscreen     start in fullscreen (F11 toggles it while playing); the picture stays at a whole scale, black bars around it
//   moonstone.exe --crt            start with the CRT effect (scanlines, aperture mask, glow, vignette); F12 toggles it and the choice is remembered
//   moonstone.exe --no-crt         start without it, whatever was remembered
//   moonstone.exe --dump-output F.png [N]   write the composed window (scaled picture, CRT effect, bars) of presented frame N (default 20) as a PNG (debug)
//   moonstone.exe --crash-test K   access|abort|fatal|quit: provoke that condition (tests of the crash reporter)
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // SDL_MAIN_HANDLED: only SDL_SetMainReady, our own main stays
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/platform/display.hpp"
#include "rt/platform/log.hpp"
#include "rt/platform/system.hpp"
#include "rt/platform/timer.hpp"
#include "rt/sdl/debug_warp.hpp"
#include "rt/sdl/audio.hpp"
#include "rt/sdl/audio_settings.hpp"
#include "rt/sdl/chip.hpp"
#include "rt/sdl/crash.hpp"
#include "rt/sdl/crt.hpp"
#include "rt/sdl/display.hpp"
#include "rt/sdl/files.hpp"
#include "rt/game.hpp"
#include "rt/modload.hpp"
#include "rt/net.hpp"
#include "rt/net_session.hpp"
#include "rt/sdl/frame_loop.hpp"
#include "rt/sdl/game.hpp"
#include "rt/sdl/gamedata.hpp"
#include "rt/sdl/imgdump.hpp"
#include "rt/sdl/input.hpp"
#include "rt/sdl/log.hpp"
#include "rt/sdl/sdl_guards.hpp"
#include "rt/sdl/smoke.hpp"
#include "rt/sdl/timer.hpp"

namespace {

constexpr int kHeadlessScale = 2;       // --smoke / dummy video: a fixed 640x400 window, the same on every machine
constexpr int kMaxScale = 8;
constexpr int kDesktopMarginX = 32;     // room for the window frame
constexpr int kDesktopMarginY = 96;     // title bar and taskbar slack
constexpr uint32_t kSmokeFrames = 10;

struct Options {
	bool isSmoke;
	bool isPattern;                     // --pattern: the test pattern instead of the game
	bool isSkipIntro;                   // --skip-intro
	bool isSmokeCopper;                 // --smoke-copper: the copper / sprite frame of rt/sdl/smoke on top of the pattern (implies --smoke)
	bool isFast;                        // unthrottled
	uint32_t ulFrames;                  // 0 = until the window is closed
	const char *szDumpFrame;            // nullptr = no dump
	rt::sdl::AudioOptions audio;
	uint8_t ubAudioSet;                 // rt::sdl::kAudioSet*: the audio options given on the command line (they override audio.txt)
	bool isAudioSave;                   // --audio-save
	const char *szDisks;                // --disks
	const char *szPref;                 // --pref
	const char *szRead;                 // --read
	const char *szKeys;                 // --keys
	const char *szCrashTest;            // --crash-test
	bool isGrabOff;                     // --no-grab
	bool isJoyKeysOff;                  // --joykeys none
	uint32_t ulDumpEvery;               // --dump-every N DIR: a PNG of the frame every N frames
	const char *szDumpDir;
	uint32_t ulWarpRecord;              // --warp-knight N AT: at frame AT the current knight stands on record N (a test hook for the duel)
	uint32_t ulWarpFrame;
	int lScale;                         // --scale N (0 = fit the desktop)
	bool isAspect;                      // --aspect
	bool isFullscreen;                  // --fullscreen
	bool isCrt;                         // --crt
	bool isNoCrt;                       // --no-crt
	const char *szDumpOutput;           // --dump-output
	uint32_t ulDumpOutputAt;
	rt::NetArgs net;                    // --host / --join / --port / --name / --delay / --timeout / --tcp
};

uint32_t s_ulDumpEvery = 0;
const char *s_szDumpDir = nullptr;
uint32_t s_ulWarpRecord = 0, s_ulWarpFrame = 0;   // --warp-knight N AT (AT = 0: off)
bool s_isWarpDone = false;

// The frame hook of --dump-every: a PNG of the converted frame every N frames.
void dumpEveryFrame() {
	const uint32_t ulFrame = rt::sdl::timerFrames();
	if(s_ulWarpFrame != 0 && !s_isWarpDone && ulFrame >= s_ulWarpFrame) {
		s_isWarpDone = true;
		rt::sdl::debugWarpKnight(s_ulWarpRecord);
	}
	if(s_ulDumpEvery == 0 || ulFrame % s_ulDumpEvery != 0) {
		return;
	}
	char szName[512];
	snprintf(szName, sizeof(szName), "%s/f%05u.png", s_szDumpDir, static_cast<unsigned>(ulFrame));
	rt::sdl::imgDumpWrite(szName, rt::sdl::displayRenderFrame(), rt::platform::kDisplayWidth, rt::platform::kDisplayHeight);
}

uint32_t s_ulVblanks = 0;
rt::sdl::SmokeTone *s_pTone = nullptr;   // the smoke tone, if one is playing

void onVblank() {
	++s_ulVblanks;
	if(s_pTone != nullptr) {
		s_pTone->tick(s_ulVblanks);
	}
}

// Registers the vblank handler of the run and removes it again (the platform keeps the pointer).
class VblankHandlerScope {
public:
	explicit VblankHandlerScope(rt::platform::TickFn pfnHandler) { rt::platform::timerSetVblankHandler(pfnHandler); }
	~VblankHandlerScope() { rt::platform::timerSetVblankHandler(nullptr); }
	VblankHandlerScope(const VblankHandlerScope &) = delete;
	VblankHandlerScope &operator=(const VblankHandlerScope &) = delete;
	VblankHandlerScope(VblankHandlerScope &&) = delete;
	VblankHandlerScope &operator=(VblankHandlerScope &&) = delete;
};

// rt::platform::systemStart ... systemStop.
class PlatformSession {
public:
	PlatformSession() : m_isOk(rt::platform::systemStart()) {}
	~PlatformSession() {
		if(m_isOk) {
			rt::platform::systemStop();
		}
	}
	PlatformSession(const PlatformSession &) = delete;
	PlatformSession &operator=(const PlatformSession &) = delete;
	PlatformSession(PlatformSession &&) = delete;
	PlatformSession &operator=(PlatformSession &&) = delete;
	explicit operator bool() const { return m_isOk; }
private:
	bool m_isOk;
};

// The window's client size: --scale N, else the largest whole scale whose window fits the primary display's usable area (at least 2x);
// headless runs get a fixed 2x window so logs and tests are the same on every machine.
void windowSize(const Options &sOptions, bool isHeadless, int &lW, int &lH) {
	int lScale = (isHeadless && sOptions.lScale == 0) ? kHeadlessScale : sOptions.lScale;   // headless: fixed 2x unless --scale says otherwise (output dumps)
	if(lScale == 0) {
		lScale = kHeadlessScale;
		SDL_Rect sUsable;
		if(SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &sUsable)) {
			for(int s = kHeadlessScale + 1; s <= kMaxScale; ++s) {
				if(rt::platform::kDisplayWidth * s > sUsable.w - kDesktopMarginX ||
				   rt::sdl::displayScaledHeight(s) > sUsable.h - kDesktopMarginY) {
					break;
				}
				lScale = s;
			}
		}
	}
	lW = static_cast<int>(rt::platform::kDisplayWidth) * lScale;
	lH = static_cast<int>(rt::sdl::displayScaledHeight(lScale));
}

bool parseOptions(int argc, char **argv, Options &sOut) {
	sOut.isSmoke = false;
	sOut.isPattern = false;
	sOut.isSkipIntro = false;
	sOut.isSmokeCopper = false;
	sOut.isFast = false;
	sOut.ulFrames = 0;
	sOut.szDumpFrame = nullptr;
	sOut.audio = rt::sdl::audioDefaultOptions();
	sOut.ubAudioSet = 0;
	sOut.isAudioSave = false;
	sOut.szDisks = sOut.szPref = sOut.szRead = sOut.szKeys = sOut.szCrashTest = nullptr;
	sOut.isGrabOff = sOut.isJoyKeysOff = false;
	sOut.ulDumpEvery = 0;
	sOut.szDumpDir = nullptr;
	sOut.lScale = 0;
	sOut.isAspect = sOut.isFullscreen = false;
	sOut.isCrt = sOut.isNoCrt = false;
	sOut.szDumpOutput = nullptr;
	sOut.ulDumpOutputAt = 20;
	sOut.net = rt::NetArgs();
	sOut.net.uwPort = rt::NET_DEFAULT_PORT;
	for(int i = 1; i < argc; ++i) {
		const bool isNumberNext = i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9';
		if(strcmp(argv[i], "--smoke") == 0) {
			sOut.isSmoke = true;
			sOut.ulFrames = isNumberNext ? static_cast<uint32_t>(atoi(argv[++i])) : kSmokeFrames;
		}
		else if(strcmp(argv[i], "--pattern") == 0) {
			sOut.isPattern = true;
		}
		else if(strcmp(argv[i], "--skip-intro") == 0) {
			sOut.isSkipIntro = true;
		}
		else if(strcmp(argv[i], "--smoke-copper") == 0) {
			sOut.isSmoke = true;
			sOut.isSmokeCopper = true;
			if(sOut.ulFrames == 0) {
				sOut.ulFrames = kSmokeFrames;
			}
		}
		else if(strcmp(argv[i], "--frames") == 0 && isNumberNext) {
			sOut.ulFrames = static_cast<uint32_t>(atoi(argv[++i]));
		}
		else if(strcmp(argv[i], "--fast") == 0) {
			sOut.isFast = true;
		}
		else if(strcmp(argv[i], "--dump-frame") == 0 && i + 1 < argc && rt::sdl::imgDumpNameIsSupported(argv[i + 1])) {
			sOut.szDumpFrame = argv[++i];
		}
		else if(strcmp(argv[i], "--dump-audio") == 0 && i + 1 < argc) {
			sOut.audio.szDumpWav = argv[++i];
		}
		else if(strcmp(argv[i], "--audio-separation") == 0 && isNumberNext) {
			const int lSep = atoi(argv[++i]);
			sOut.audio.ubSeparation = static_cast<uint8_t>(lSep < 0 ? 0 : (lSep > 100 ? 100 : lSep));
			sOut.ubAudioSet |= rt::sdl::kAudioSetSeparation;
		}
		else if(strcmp(argv[i], "--audio-filter") == 0 && i + 1 < argc) {
			if(!rt::sdl::audioFilterFromName(argv[++i], sOut.audio.ubFilter)) {
				fprintf(stderr, "moonstone: --audio-filter %s: expected off, a500, a500led, a1200 or a1200led\n", argv[i]);
				return false;
			}
			sOut.ubAudioSet |= rt::sdl::kAudioSetFilter;
		}
		else if(strcmp(argv[i], "--audio-render") == 0 && i + 1 < argc) {
			if(!rt::sdl::audioRenderFromName(argv[++i], sOut.audio.ubRender)) {
				fprintf(stderr, "moonstone: --audio-render %s: expected box or blep\n", argv[i]);
				return false;
			}
			sOut.ubAudioSet |= rt::sdl::kAudioSetRender;
		}
		else if(strcmp(argv[i], "--audio-save") == 0) {
			sOut.isAudioSave = true;
		}
		else if(strcmp(argv[i], "--scale") == 0 && isNumberNext) {
			const int lScale = atoi(argv[++i]);
			sOut.lScale = lScale < 1 ? 1 : (lScale > kMaxScale ? kMaxScale : lScale);
		}
		else if(strcmp(argv[i], "--aspect") == 0) {
			sOut.isAspect = true;
		}
		else if(strcmp(argv[i], "--crt") == 0) {
			sOut.isCrt = true;
		}
		else if(strcmp(argv[i], "--no-crt") == 0) {
			sOut.isNoCrt = true;
		}
		else if(strcmp(argv[i], "--dump-output") == 0 && i + 1 < argc && rt::sdl::imgDumpNameIsSupported(argv[i + 1])) {
			sOut.szDumpOutput = argv[++i];
			if(i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
				sOut.ulDumpOutputAt = static_cast<uint32_t>(atoi(argv[++i]));
			}
		}
		else if(strcmp(argv[i], "--fullscreen") == 0) {
			sOut.isFullscreen = true;
		}
		else if(strcmp(argv[i], "--disks") == 0 && i + 1 < argc) {
			sOut.szDisks = argv[++i];
		}
		else if(strcmp(argv[i], "--pref") == 0 && i + 1 < argc) {
			sOut.szPref = argv[++i];
		}
		else if(strcmp(argv[i], "--read") == 0 && i + 1 < argc) {
			sOut.szRead = argv[++i];
		}
		else if(strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
			sOut.szKeys = argv[++i];
		}
		else if(strcmp(argv[i], "--keys-file") == 0 && i + 1 < argc) {
			static char s_acKeysFile[1 << 20];   // the script text (a soak script of 90 000 frames is ~200 KB)
			FILE *pFile = fopen(argv[++i], "rb");
			const size_t ulRead = pFile ? fread(s_acKeysFile, 1, sizeof(s_acKeysFile) - 1, pFile) : 0;
			if(pFile) {
				fclose(pFile);
			}
			if(pFile == nullptr || ulRead == 0) {
				fprintf(stderr, "moonstone: cannot read the --keys-file %s\n", argv[i]);
				return false;
			}
			s_acKeysFile[ulRead] = 0;
			sOut.szKeys = s_acKeysFile;
		}
		else if(strcmp(argv[i], "--crash-test") == 0 && i + 1 < argc) {
			sOut.szCrashTest = argv[++i];
		}

		else if(strcmp(argv[i], "--no-grab") == 0) {
			sOut.isGrabOff = true;
		}
		else if(strcmp(argv[i], "--joykeys") == 0 && i + 1 < argc) {
			sOut.isJoyKeysOff = strcmp(argv[++i], "none") == 0;
		}
		else if(strcmp(argv[i], "--dump-every") == 0 && i + 2 < argc && isNumberNext) {
			sOut.ulDumpEvery = static_cast<uint32_t>(atoi(argv[++i]));
			sOut.szDumpDir = argv[++i];
		}
		else if(strcmp(argv[i], "--warp-knight") == 0 && i + 2 < argc && isNumberNext) {
			sOut.ulWarpRecord = static_cast<uint32_t>(atoi(argv[++i]));
			sOut.ulWarpFrame = static_cast<uint32_t>(atoi(argv[++i]));
		}
		else if(strcmp(argv[i], "--host") == 0) {
			sOut.net.isHost = true;
		}
		else if(strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
			sOut.net.isJoin = true;
			snprintf(sOut.net.szJoin, sizeof(sOut.net.szJoin), "%s", argv[++i]);
		}
		else if(strcmp(argv[i], "--port") == 0 && isNumberNext) {
			sOut.net.uwPort = static_cast<uint16_t>(atoi(argv[++i]));
		}
		else if(strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
			snprintf(sOut.net.szName, sizeof(sOut.net.szName), "%s", argv[++i]);
		}
		else if(strcmp(argv[i], "--delay") == 0 && isNumberNext) {
			sOut.net.uwDelay = static_cast<uint16_t>(atoi(argv[++i]));
		}
		else if(strcmp(argv[i], "--timeout") == 0 && isNumberNext) {
			sOut.net.uwTimeoutSec = static_cast<uint16_t>(atoi(argv[++i]));
		}
		else if(strcmp(argv[i], "--tcp") == 0) {
			sOut.net.isTcpOnly = true;
		}
		else {
			fprintf(stderr, "usage: moonstone [--frames N] [--fast] [--skip-intro] [--pattern] [--dump-frame file.png|file.ppm] [--dump-audio file.wav]\n"
				"                [--audio-separation 0..100] [--audio-filter off|a500|a500led|a1200|a1200led] [--audio-render box|blep] [--audio-save]\n"
				"                [--smoke [N]] [--smoke-copper]\n"
				"                [--disks DIR] [--pref DIR] [--read NAME] [--keys \"...\" | --keys-file F] [--joykeys none|arrows] [--no-grab] [--crash-test K]\n"
				"                [--host | --join ADDR] [--port N] [--name TEXT] [--delay N] [--timeout SEC] [--tcp]\n");
			return false;
		}
	}
	return true;
}

// CRC-32 (IEEE, as zlib): update with a block, start from 0xFFFFFFFF, the result is the complement.
uint32_t crcUpdate(uint32_t ulState, const uint8_t *pData, uint32_t ulCount) {
	for(uint32_t i = 0; i < ulCount; ++i) {
		ulState ^= pData[i];
		for(int k = 0; k < 8; ++k) {
			ulState = (ulState >> 1) ^ (0xEDB88320u & (0u - (ulState & 1u)));
		}
	}
	return ulState;
}

// Reads a game file twice, so that a test can compare the bytes with the ones it put in: whole (size, CRC-32) and, after a seek to half
// the size, the second half (tail CRC-32); a seek past the end must be refused.
bool gameFileCrc(const char *szName, uint32_t &ulSize, uint32_t &ulCrc, uint32_t &ulTailCrc, char (&szOrigin)[96]) {
	rt::sdl::GameFile file(szName);
	if(!file) {
		return false;
	}
	ulSize = file.size();
	snprintf(szOrigin, sizeof(szOrigin), "%s", file.origin());
	uint32_t ulState = 0xFFFFFFFFu;
	uint8_t aBuf[4096];
	for(uint32_t ulGot = file.read(aBuf, sizeof(aBuf)); ulGot > 0; ulGot = file.read(aBuf, sizeof(aBuf))) {
		ulState = crcUpdate(ulState, aBuf, ulGot);
	}
	ulCrc = ~ulState;
	if(file.seek(ulSize + 1) || !file.seek(ulSize / 2)) {
		return false;
	}
	ulState = 0xFFFFFFFFu;
	for(uint32_t ulGot = file.read(aBuf, sizeof(aBuf)); ulGot > 0; ulGot = file.read(aBuf, sizeof(aBuf))) {
		ulState = crcUpdate(ulState, aBuf, ulGot);
	}
	ulTailCrc = ~ulState;
	return true;
}

// ROADMAP 11.10: the game itself.  Everything it needs (window, renderer, display texture, audio, input, game data) is up in run().  The frame
// session delivers the 50 Hz boundaries to the game's own waits, pumps the SDL events and presents the display; rtGameRun returns when the
// window is closed or --frames ran out (it leaves the overlay from inside the wait that sees the request).
int runGame(const Options &sOptions) {
	rt::sdl::gameSetSkipIntro(sOptions.isSkipIntro);
	rt::modsLoad();   // the mod data files and options.ini (ROADMAP 9.4c, 11.9): the Amiga does this on its splash, before the game
	uint32_t ulRan = 0;
	{
		rt::NetTransport net;   // the sockets of a network game (ROADMAP 12.14): idle until the Multiplayer screen opens one; closed on the way out
		rt::netSessionSetArgs(sOptions.net);
		s_ulDumpEvery = sOptions.ulDumpEvery;
		s_szDumpDir = sOptions.szDumpDir;
		s_ulWarpRecord = sOptions.ulWarpRecord;
		s_ulWarpFrame = sOptions.ulWarpFrame;
		rt::sdl::FrameSession frames(sOptions.ulFrames, (sOptions.ulDumpEvery != 0 || sOptions.ulWarpFrame != 0) ? dumpEveryFrame : nullptr);
		rtGameRun();
		rt::netSessionShutdown();   // BYE(normal) to the peers of a running network game (ROADMAP 12.11)
		ulRan = rt::sdl::timerFrames();
	}
	rt::platform::logFormat("moonstone: %u frames, %u vblanks\n", static_cast<unsigned>(ulRan), static_cast<unsigned>(rt::platform::timerVblankCount()));
	rt::sdl::displayLogState();
	int iResult = 0;
	if(sOptions.szDumpFrame != nullptr &&
	   !rt::sdl::imgDumpWrite(sOptions.szDumpFrame, rt::sdl::displayRenderFrame(), rt::platform::kDisplayWidth, rt::platform::kDisplayHeight)) {
		iResult = 4;
	}
	rt::platform::displayClose();
	return iResult;
}

int run(const Options &sOptions) {
	// no message boxes in headless runs, scripted (--keys) runs and the crash tests: nobody can click them, and an open box keeps the
	// process (and the locked exe) alive
	const char *szVideo = getenv("SDL_VIDEO_DRIVER");
	const bool isDummyVideo = szVideo != nullptr && strcmp(szVideo, "dummy") == 0;
	rt::sdl::uiSetQuiet(sOptions.isSmoke || sOptions.szCrashTest != nullptr || sOptions.szKeys != nullptr || isDummyVideo);
	if(sOptions.szPref != nullptr) {
		rt::sdl::filesSetPrefDir(sOptions.szPref);
	}
	if(sOptions.isSmoke) {
		// headless: no window on screen, no audio device needed
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");   // SDL_VIDEO_DRIVER / SDL_AUDIO_DRIVER in the environment (SDL3 names)
		SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
	}
	SDL_SetMainReady();
	rt::sdl::SdlRuntime sdl(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
	if(!sdl) {
		rt::platform::logFormat("SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	PlatformSession platformSession;
	if(!platformSession) {
		rt::platform::fatal("cannot start the platform");
		return 1;
	}
	rt::sdl::displaySetAspect(sOptions.isAspect);
	if(sOptions.isCrt || sOptions.isNoCrt) {
		rt::sdl::crtSetEnabled(sOptions.isCrt && !sOptions.isNoCrt);
	}
	else if(!sOptions.isSmoke && !isDummyVideo) {
		rt::sdl::crtLoadPref();   // the choice F12 remembered; headless runs never read the player's settings
	}
	if(sOptions.szDumpOutput != nullptr) {
		rt::sdl::displayDumpOutputAt(sOptions.szDumpOutput, sOptions.ulDumpOutputAt);
	}
	const char *szDriver = SDL_GetCurrentVideoDriver();
	const bool isHeadless = sOptions.isSmoke || (szDriver != nullptr && strcmp(szDriver, "dummy") == 0);
	int lWindowW = 0;
	int lWindowH = 0;
	windowSize(sOptions, isHeadless, lWindowW, lWindowH);
	const SDL_WindowFlags ulWindowFlags = isHeadless ? 0 : (SDL_WINDOW_RESIZABLE | (sOptions.isFullscreen ? SDL_WINDOW_FULLSCREEN : 0));
	rt::sdl::SdlWindow window("Moonstone", lWindowW, lWindowH, ulWindowFlags);
	if(!window) {
		rt::platform::logFormat("SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}
	rt::sdl::SdlRenderer renderer(window.get(), sOptions.isSmoke ? SDL_SOFTWARE_RENDERER : nullptr);
	if(!renderer) {
		rt::platform::logFormat("SDL_CreateRenderer failed: %s\n", SDL_GetError());
		return 1;
	}
	rt::sdl::DisplaySession display(renderer.get());   // declared after the renderer: the texture goes first
	if(!display) {
		rt::platform::logFormat("SDL_CreateTexture failed: %s\n", SDL_GetError());
		return 1;
	}
	rt::platform::logFormat("moonstone: SDL %d.%d.%d, video driver %s, window %dx%d, %s\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
		SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver(), lWindowW, lWindowH, sOptions.isFast ? "unthrottled" : "50 Hz");
	if(sOptions.isSmoke && (!rt::sdl::chipSelfTest() || !rt::sdl::timerSelfTest())) {
		return 3;
	}
	// audio.txt in the user directory gives the defaults of the audio options the command line did not set; a headless smoke run without
	// --pref ignores it (the tests must not depend on the developer's own file)
	rt::sdl::AudioOptions audioOptions = sOptions.audio;
	if(sOptions.szPref != nullptr || !sOptions.isSmoke) {
		if(!rt::sdl::audioSettingsLoadSave(rt::sdl::filesPrefDir(), audioOptions, sOptions.ubAudioSet, sOptions.isAudioSave)) {
			return 5;
		}
	}
	rt::sdl::AudioSession audio(audioOptions);   // after the platform start (the silent word lives in chip RAM)
	if(!audio) {
		return 5;
	}
	rt::sdl::timerSetUnthrottled(sOptions.isFast);
	// ROADMAP 11.9: input, game data, scripted keys.  The input session resets the state, so it comes before the script and the probe.
	rt::sdl::InputOptions inputOptions = rt::sdl::inputDefaultOptions();
	inputOptions.isGrabMouse = !sOptions.isGrabOff && !sOptions.isSmoke;
	if(sOptions.isJoyKeysOff) {
		inputOptions.pJoyKeys = &ms::kJoyKeysNone;
	}
	rt::sdl::InputSession input(inputOptions);
	if(sOptions.szKeys != nullptr && !rt::sdl::inputScriptParse(sOptions.szKeys)) {
		return 64;
	}
	const bool isProbe = sOptions.isSmoke || sOptions.szKeys != nullptr;
	const rt::sdl::InputProbe probe(isProbe);
	const rt::sdl::GameDataSession gameData(sOptions.szDisks);
	// --read a,b,c : each name looked up like the game does
	for(const char *szNames = sOptions.szRead; szNames != nullptr && *szNames != 0;) {
		char szName[96];
		size_t nName = 0;
		while(*szNames != 0 && *szNames != ',' && nName + 1 < sizeof(szName)) {
			szName[nName++] = *szNames++;
		}
		szName[nName] = 0;
		if(*szNames == ',') {
			++szNames;
		}
		uint32_t ulSize = 0, ulCrc = 0, ulTail = 0;
		char szOrigin[96];
		if(!gameFileCrc(szName, ulSize, ulCrc, ulTail, szOrigin)) {
			rt::platform::logFormat("gamedata: %s not found\n", szName);
			return 7;
		}
		rt::platform::logFormat("gamedata: %s size %u crc32 %08X tail %08X from %s\n", szName, static_cast<unsigned>(ulSize), static_cast<unsigned>(ulCrc),
			static_cast<unsigned>(ulTail), szOrigin);
	}
	if(sOptions.isSmoke && sOptions.szPref != nullptr && !rt::sdl::filesSelfTest()) {
		return 8;
	}
	if(sOptions.szCrashTest != nullptr && !rt::sdl::crashRaiseTest(sOptions.szCrashTest)) {
		rt::platform::logFormat("unknown --crash-test kind %s\n", sOptions.szCrashTest);
		return 64;
	}
	if(!sOptions.isSmoke && !sOptions.isPattern && sOptions.szCrashTest == nullptr) {
		return runGame(sOptions);
	}
	// the test pattern of rt/sdl/smoke
	rt::sdl::SmokePattern pattern;
	if(!pattern) {
		return 1;
	}
	rt::sdl::SmokeCopper copperFrame(sOptions.isSmokeCopper, pattern.screenBase());
	if(sOptions.isSmokeCopper && !copperFrame) {
		return 1;
	}
	rt::sdl::SmokeTone tone(sOptions.isSmoke);
	if(sOptions.isSmoke) {
		if(!tone) {
			return 1;
		}
		s_pTone = &tone;
	}
	const VblankHandlerScope vblank(onVblank);
	const uint32_t ulRan = rt::sdl::frameLoopRun(sOptions.ulFrames, nullptr);
	s_pTone = nullptr;
	rt::platform::logFormat("moonstone: %u frames, %u vblanks\n", static_cast<unsigned>(ulRan), static_cast<unsigned>(s_ulVblanks));
	if(sOptions.szDumpFrame != nullptr &&
	   !rt::sdl::imgDumpWrite(sOptions.szDumpFrame, rt::sdl::displayRenderFrame(), rt::platform::kDisplayWidth, rt::platform::kDisplayHeight)) {
		return 4;
	}
	if(sOptions.isSmoke && (ulRan != sOptions.ulFrames || s_ulVblanks < ulRan)) {
		return 2;
	}
	return 0;
}

}  // namespace

int main(int argc, char **argv) {
	Options sOptions;
	if(!parseOptions(argc, argv, sOptions)) {
		return 64;
	}
	rt::sdl::LogSession log;
	rt::sdl::CrashReporterScope crash;   // after the log: the report goes to files next to it
	return run(sOptions);
}
