// rt/sdl/timer - rt::platform::timer for the PC (ROADMAP 11.5).  The execution model and the pacing are described in timer.hpp.
#include "rt/sdl/timer.hpp"

#include <SDL3/SDL.h>

#include "rt/platform/log.hpp"
#include "rt/platform/timer.hpp"
#include "rt/sdl/audio.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr uint32_t kMaxCatchUp = 3;            // boundaries delivered in one pump when late; beyond that the clock resyncs
constexpr uint32_t kLinesPerFrame = 313;       // PAL: 312.5 lines, VPOS 0..312
constexpr uint32_t kColourClocks = 227;        // HPOS 0..226 per line
constexpr uint32_t kVirtualBeamStep = 131;     // unthrottled beam: colour clocks per query (coprime with the line length)

struct State {
	platform::TickFn pfnVblank;
	platform::TickFn pfnBlitDone;
	FrameHook pfnHook;
	LeaveHook pfnLeave;
	bool isUnthrottled;
	bool isMasterOn;
	bool isVblPending;                         // VERTB requested, not yet serviced
	bool isBlitPending;                        // BLIT requested, not yet serviced
	bool isInPump;                             // a handler or the hook is running
	bool isStarted;
	uint32_t ulFrames;
	uint32_t ulVblCount;                       // VERTB services since the handler was installed
	uint64_t ullFrameStart;                    // counter value of the last boundary
	uint64_t ullNextBoundary;
	uint64_t ullFrameTicks;
	uint32_t ulVirtualBeam;                    // unthrottled: position within the frame in colour clocks
};
State s_state = {nullptr, nullptr, nullptr, nullptr, false, true, false, false, false, false, 0, 0, 0, 0, 0, 0};

// Marks "a handler is running" for the length of a scope (docs/RAII.md); the flag is cleared on every exit path.
class InPumpScope {
public:
	InPumpScope() { s_state.isInPump = true; }
	~InPumpScope() { s_state.isInPump = false; }
	InPumpScope(const InPumpScope &) = delete;
	InPumpScope &operator=(const InPumpScope &) = delete;
	InPumpScope(InPumpScope &&) = delete;
	InPumpScope &operator=(InPumpScope &&) = delete;
};

uint64_t counterNow() {
	return SDL_GetPerformanceCounter();
}

void ensureStarted() {
	if(!s_state.isStarted) {
		timerReset();
	}
}

// Sleep until the performance counter reaches ullTarget: SDL_Delay while more than ~2 ms remain, a short spin for the rest.
void sleepUntil(uint64_t ullTarget) {
	const uint64_t ullFreq = SDL_GetPerformanceFrequency();
	for(;;) {
		const uint64_t ullNow = counterNow();
		if(ullNow >= ullTarget) {
			return;
		}
		const uint64_t ullMs = (ullTarget - ullNow) * 1000 / ullFreq;
		if(ullMs >= 2) {
			SDL_Delay(static_cast<Uint32>(ullMs - 1));
		}
	}
}

// Deliver requests that are pending and not masked: blitter first (BLIT is the lower bit of the two within INTREQ level 3).
void serviceBlit() {
	if(s_state.isBlitPending && s_state.isMasterOn) {
		s_state.isBlitPending = false;
		if(s_state.pfnBlitDone != nullptr) {
			InPumpScope sScope;
			s_state.pfnBlitDone();
		}
	}
}

void serviceVblank() {
	if(s_state.isVblPending && s_state.isMasterOn) {
		s_state.isVblPending = false;
		if(s_state.pfnVblank != nullptr) {
			++s_state.ulVblCount;
			InPumpScope sScope;
			s_state.pfnVblank();
		}
	}
}

// One vertical blank.
void deliverBoundary() {
	++s_state.ulFrames;
	s_state.isVblPending = true;
	serviceBlit();
	serviceVblank();
	{
		// one frame of virtual time for the Paula model; the level-4 audio handler runs inside it, so it is a handler like the others
		InPumpScope sScope;
		audioOnBoundary();
	}
}

// Beam position within the frame in colour clocks (0 .. kLinesPerFrame * kColourClocks - 1).
uint32_t beamPosition() {
	ensureStarted();
	if(s_state.isUnthrottled) {
		s_state.ulVirtualBeam = (s_state.ulVirtualBeam + kVirtualBeamStep) % (kLinesPerFrame * kColourClocks);
		return s_state.ulVirtualBeam;
	}
	const uint64_t ullInFrame = (counterNow() - s_state.ullFrameStart) % s_state.ullFrameTicks;
	return static_cast<uint32_t>(ullInFrame * (625ull * kColourClocks) / (2ull * s_state.ullFrameTicks));   // 312.5 lines per frame
}

}  // namespace

void timerSetUnthrottled(bool isUnthrottled) {
	s_state.isUnthrottled = isUnthrottled;
}

bool timerIsUnthrottled() {
	return s_state.isUnthrottled;
}

void timerReset() {
	s_state.isStarted = true;
	s_state.ulFrames = 0;
	s_state.ulVblCount = 0;
	s_state.isVblPending = false;
	s_state.isBlitPending = false;
	s_state.ulVirtualBeam = 0;
	s_state.ullFrameTicks = SDL_GetPerformanceFrequency() / kFrameHz;
	s_state.ullFrameStart = counterNow();
	s_state.ullNextBoundary = s_state.ullFrameStart + s_state.ullFrameTicks;
}

uint32_t timerPump() {
	ensureStarted();
	if(s_state.isInPump) {
		return 0;
	}
	serviceBlit();
	serviceVblank();   // a request that was held back by the master switch
	uint32_t ulDelivered = 0;
	if(s_state.isUnthrottled) {
		deliverBoundary();
		ulDelivered = 1;
	}
	else {
		const uint64_t ullNow = counterNow();
		while(ullNow >= s_state.ullNextBoundary && ulDelivered < kMaxCatchUp) {
			deliverBoundary();
			s_state.ullFrameStart = s_state.ullNextBoundary;
			s_state.ullNextBoundary += s_state.ullFrameTicks;
			++ulDelivered;
		}
		if(ullNow >= s_state.ullNextBoundary) {   // more than three frames behind (a breakpoint, a window drag): do not burst
			s_state.ullFrameStart = ullNow;
			s_state.ullNextBoundary = ullNow + s_state.ullFrameTicks;
		}
	}
	if(ulDelivered != 0 && s_state.pfnHook != nullptr) {
		{
			InPumpScope sScope;
			s_state.pfnHook();
		}
		if(s_state.pfnLeave != nullptr) {
			s_state.pfnLeave();   // the game's way out of a closed window: does not return when it fires
		}
	}
	return ulDelivered;
}

void timerWaitFrame() {
	ensureStarted();
	while(!s_state.isInPump) {
		if(!s_state.isUnthrottled) {
			sleepUntil(s_state.ullNextBoundary);
		}
		if(timerPump() != 0) {
			return;
		}
	}
}

void timerSetFrameHook(FrameHook pfnHook) {
	s_state.pfnHook = pfnHook;
}

void timerSetLeaveHook(LeaveHook pfnHook) {
	s_state.pfnLeave = pfnHook;
}

uint32_t timerFrames() {
	return s_state.ulFrames;
}

void timerRaiseBlitDone() {
	s_state.isBlitPending = true;
	if(!s_state.isInPump) {
		serviceBlit();
	}
}

bool timerIsMasterEnabled() {
	return s_state.isMasterOn;
}

// ---- self test ------------------------------------------------------------------------------------------------------------------------

namespace {

uint32_t s_ulTicks = 0;
uint32_t s_ulBlits = 0;
uint32_t s_ulWhileWaiting = 0;
void onTick() { ++s_ulTicks; }
void onBlit() { ++s_ulBlits; }
void onWhileWaiting() { ++s_ulWhileWaiting; }

bool check(bool isOk, const char *szWhat) {
	if(!isOk) {
		platform::logFormat("timer self-test: FAILED: %s\n", szWhat);
	}
	return isOk;
}

// Wall-clock bounds depend on machine load (parallel builds, tests): a miss is logged, never a failure.
void checkTiming(bool isOk, const char *szWhat) {
	if(!isOk) {
		platform::logFormat("timer self-test: warning (machine load?): %s\n", szWhat);
	}
}

// Restores the timer to its start-up state when the self test ends, on every path.
class SelfTestScope {
public:
	SelfTestScope() : m_isUnthrottled(s_state.isUnthrottled), m_pfnHook(s_state.pfnHook) {
		s_state.pfnHook = nullptr;
		s_state.isMasterOn = true;
		s_ulTicks = s_ulBlits = s_ulWhileWaiting = 0;
	}
	~SelfTestScope() {
		platform::timerSetVblankHandler(nullptr);
		platform::timerSetBlitDoneHandler(nullptr);
		s_state.isMasterOn = true;
		s_state.pfnHook = m_pfnHook;
		s_state.isUnthrottled = m_isUnthrottled;
		timerReset();
	}
	SelfTestScope(const SelfTestScope &) = delete;
	SelfTestScope &operator=(const SelfTestScope &) = delete;
	SelfTestScope(SelfTestScope &&) = delete;
	SelfTestScope &operator=(SelfTestScope &&) = delete;
private:
	bool m_isUnthrottled;
	FrameHook m_pfnHook;
};

}  // namespace

bool timerSelfTest() {
	SelfTestScope sScope;
	bool isOk = true;

	// unthrottled: one boundary per pump, the handler runs once per boundary, nothing sleeps
	timerSetUnthrottled(true);
	timerReset();
	platform::timerSetVblankHandler(onTick);
	platform::timerSetBlitDoneHandler(onBlit);
	const uint64_t ullFastStart = counterNow();
	for(int i = 0; i < 1000; ++i) {
		timerPump();
	}
	const uint64_t ullFastTicks = counterNow() - ullFastStart;
	isOk = check(s_ulTicks == 1000 && platform::timerVblankCount() == 1000 && timerFrames() == 1000, "1000 unthrottled pumps = 1000 vblanks") && isOk;
	checkTiming(ullFastTicks < SDL_GetPerformanceFrequency(), "1000 unthrottled frames take less than a second");

	// the master switch holds a vblank back; several held-back boundaries collapse into one pending request, delivered by the next pump
	{
		platform::MasterIrqOff sOff;
		isOk = check(!timerIsMasterEnabled(), "MasterIrqOff clears the master switch") && isOk;
		for(int i = 0; i < 5; ++i) {
			timerPump();
		}
		isOk = check(s_ulTicks == 1000 && timerFrames() == 1005, "masked boundaries do not call the handler") && isOk;
		timerRaiseBlitDone();
		isOk = check(s_ulBlits == 0, "a masked blitter interrupt waits") && isOk;
	}
	isOk = check(timerIsMasterEnabled() && s_ulTicks == 1000, "unmasking does not run the handler inside timerMasterIrq") && isOk;
	timerPump();
	isOk = check(s_ulTicks == 1002 && s_ulBlits == 1, "after the unmask the next pump delivers the held vblank and the held blit (once each), plus its own boundary") && isOk;
	timerRaiseBlitDone();
	isOk = check(s_ulBlits == 2, "an unmasked blitter interrupt is delivered at once") && isOk;

	// the virtual beam is deterministic after a reset and stays inside the frame
	timerReset();
	const uint16_t uwA1 = platform::timerBeamVhposr();
	const uint16_t uwA2 = platform::timerBeamVhposr();
	timerReset();
	isOk = check(platform::timerBeamVhposr() == uwA1 && platform::timerBeamVhposr() == uwA2, "the unthrottled beam repeats after a reset") && isOk;
	for(int i = 0; i < 2000; ++i) {
		isOk = check(platform::timerBeamLine() <= 312, "beam line in 0..312") && isOk;
	}

	// timerWaitBeam unthrottled: calls the poll once and delivers one boundary
	timerReset();
	s_ulTicks = s_ulWhileWaiting = 0;
	platform::timerWaitBeam(0xF5, onWhileWaiting);
	isOk = check(s_ulWhileWaiting == 1 && s_ulTicks == 1, "unthrottled timerWaitBeam polls once and takes one frame") && isOk;

	// throttled: 25 frames take about half a second, one handler call each
	timerSetUnthrottled(false);
	timerReset();
	s_ulTicks = 0;
	const uint64_t ullStart = counterNow();
	for(int i = 0; i < 25; ++i) {
		timerWaitFrame();
	}
	const double dSeconds = static_cast<double>(counterNow() - ullStart) / static_cast<double>(SDL_GetPerformanceFrequency());
	isOk = check(s_ulTicks >= 25, "25 throttled frames deliver at least 25 vblanks") && isOk;
	checkTiming(s_ulTicks <= 28, "25 throttled frames deliver at most 28 vblanks");
	isOk = check(dSeconds >= 0.40, "25 throttled frames take at least 0.4 s (pacing works)") && isOk;
	checkTiming(dSeconds <= 1.5, "25 throttled frames take at most 1.5 s");

	// the throttled beam runs forward within a frame and timerWaitBeam returns just after the requested line
	timerWaitFrame();
	const uint16_t uwEarly = platform::timerBeamLine();
	SDL_Delay(4);
	const uint16_t uwLater = platform::timerBeamLine();
	checkTiming(uwEarly < 200 && uwLater > uwEarly && uwLater <= 312, "the beam advances with time (4 ms = 62 lines)");
	s_ulWhileWaiting = 0;
	platform::timerWaitBeam(0x90, onWhileWaiting);
	const uint16_t uwAfter = platform::timerBeamLine();
	checkTiming((uwAfter >= 0x91 && uwAfter < 0x91 + 100) || uwAfter < 40, "timerWaitBeam(0x90) returns after the beam left line 0x90");
	checkTiming(s_ulWhileWaiting >= 1, "timerWaitBeam polls while it waits");

	if(isOk) {
		platform::logLine("timer self-test: ok\n");
	}
	return isOk;
}

}  // namespace sdl

namespace platform {

void timerSetVblankHandler(TickFn pfnHandler) {
	sdl::s_state.pfnVblank = pfnHandler;
	if(pfnHandler != nullptr) {
		sdl::s_state.ulVblCount = 0;
	}
	sdl::s_state.isVblPending = false;
}

void timerSetBlitDoneHandler(TickFn pfnHandler) {
	sdl::s_state.pfnBlitDone = pfnHandler;
	sdl::s_state.isBlitPending = false;
}

uint32_t timerVblankCount() {
	return sdl::s_state.ulVblCount;
}

uint16_t timerBeamLine() {
	return static_cast<uint16_t>(sdl::beamPosition() / sdl::kColourClocks);
}

uint16_t timerBeamVhposr() {
	const uint32_t ulPos = sdl::beamPosition();
	const uint32_t ulLine = ulPos / sdl::kColourClocks;
	return static_cast<uint16_t>(((ulLine & 0xFF) << 8) | (ulPos % sdl::kColourClocks));
}

uint16_t timerBeamVposr() {
	return static_cast<uint16_t>(timerBeamLine() >> 8);   // line bit 8 (VPOSR bit 0); no long-frame / chip-id bits
}

void timerWaitBeam(uint8_t ubLine, TickFn pfnWhileWaiting) {
	sdl::ensureStarted();
	if(sdl::s_state.isUnthrottled) {
		if(pfnWhileWaiting != nullptr) {
			pfnWhileWaiting();
		}
		sdl::timerPump();   // one frame boundary stands for "the beam came round"
		return;
	}
	// Absolute counter value at which the beam leaves line ubLine: line N spans [N, N + 1) * (frame / 312.5) after the frame start.
	// If that moment of this frame has passed, it is the one of the next frame.
	const uint64_t ullFrame = sdl::s_state.ullFrameTicks;
	uint64_t ullTarget = sdl::s_state.ullFrameStart + (static_cast<uint64_t>(ubLine) + 1) * 2 * ullFrame / 625;
	if(sdl::counterNow() >= ullTarget) {
		ullTarget += ullFrame;
	}
	if(sdl::counterNow() >= ullTarget) {
		// More than a frame behind (a network game waited for its peer: rt/net_session sleeps outside the pump): the loop below would not run,
		// no boundary would ever be delivered, the frame start would stay stale and every later wait would return at once (the game then
		// spins at full speed with a frozen frame counter).  Deliver what is due, which also re-bases the frame start, and aim again.
		sdl::timerPump();
		ullTarget = sdl::s_state.ullFrameStart + (static_cast<uint64_t>(ubLine) + 1) * 2 * ullFrame / 625;
		if(sdl::counterNow() >= ullTarget) {
			ullTarget += ullFrame;
		}
	}
	while(sdl::counterNow() < ullTarget) {
		if(pfnWhileWaiting != nullptr) {
			pfnWhileWaiting();
		}
		sdl::timerPump();
		if(sdl::counterNow() >= ullTarget) {
			break;
		}
		sdl::sleepUntil(ullTarget < sdl::s_state.ullNextBoundary ? ullTarget : sdl::s_state.ullNextBoundary);
	}
}

void timerMasterIrq(bool isEnabled) {
	sdl::s_state.isMasterOn = isEnabled;
}

void timerIdle() {
	sdl::ensureStarted();
	if(sdl::timerPump() == 0 && !sdl::s_state.isUnthrottled) {
		SDL_Delay(1);   // nothing is due: give the CPU back until the next boundary
	}
}

}  // namespace platform
}  // namespace rt
