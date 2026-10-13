// rt/sdl/audio - rt::platform::audio for the PC (ROADMAP 11.8): Paula model + module player, rendered in virtual time on the game thread,
// pushed once per vblank into an SDL3 audio stream.  The design is explained in audio.hpp.
#include "rt/sdl/audio.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/modplay.hpp"
#include "engine/paula.hpp"
#include "rt/platform/audio.hpp"
#include "rt/platform/log.hpp"
#include "rt/sdl/chip.hpp"
#include "rt/sdl/timer.hpp"

#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "rt/sdl/audio.cpp copies int16 samples straight into the little-endian WAV and the S16 (little-endian) stream"
#endif

namespace rt {
namespace sdl {

namespace {

constexpr uint32_t kMaxQueuedFrames = 16384;        // the stream may hold this much (~340 ms); a chunk that would exceed it is dropped
constexpr uint32_t kMaxChunk = 4096;                // largest chunk one vblank can produce (rate <= 204800)
constexpr uint32_t kDeviceFrames = 1024;            // requested device buffer (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES)
constexpr uint32_t kDmaDelayFrames = 40;            // ptplayer waits ~576 CIA ticks (0.8 ms) between DMA start and the repeat pointers
constexpr uint32_t kPrefillVblanks = 2;             // the device starts when this many vblanks are queued

struct State {
	ms::paula::Paula sPaula;
	ms::ModPlayer sMod;
	platform::AudioHandlerFn pfnHandler;
	bool isModelReady;                              // sPaula has been reset
	bool isOpen;                                    // a session is open: virtual time runs
	uint32_t ulRate;
	uint32_t ulSilence;                             // chip address of a zero word (one-shot samples park on it)
	FILE *pWav;
	uint32_t ulWavFrames;
	AudioStats sStats;
	const SdlAudioStream *pStream;                  // the open session's stream, nullptr without a device
	bool isPlaying;                                 // the stream's device is running (not waiting for its prefill)
	uint32_t ulPrefill;                             // frames queued before the device starts
};
State s_state;

// ---- the stream (SDL3 push model: the game thread puts, SDL's audio thread pulls) --------------------------------------------------

int16_t s_asChunk[kMaxChunk * 2];

// Hands one vblank of finished samples to the device.  The device waits (paused) until ulPrefill frames are queued; a stream that ran dry
// while playing counts an underrun and is paused to buffer up again; a chunk that does not fit counts as dropped (--fast runs, where the
// game is faster than the device).
void streamPush(const int16_t *pFrames, uint32_t ulFrames) {
	const SdlAudioStream *pStream = s_state.pStream;
	if(pStream == nullptr) {
		return;
	}
	const uint32_t ulQueued = static_cast<uint32_t>(pStream->queued()) / 4;
	if(s_state.isPlaying && ulQueued == 0) {
		++s_state.sStats.ulUnderruns;
		pStream->pause();                  // buffer up again before the next try
		s_state.isPlaying = false;
	}
	if(ulQueued + ulFrames > kMaxQueuedFrames) {
		++s_state.sStats.ulDropped;
		return;
	}
	if(!pStream->put(pFrames, static_cast<int>(ulFrames * 4))) {
		++s_state.sStats.ulDropped;
		return;
	}
	if(!s_state.isPlaying && ulQueued + ulFrames >= s_state.ulPrefill) {
		s_state.isPlaying = true;
		pStream->resume();
	}
}

// ---- WAV dump ---------------------------------------------------------------------------------------------------------------------
void put32(uint8_t *p, uint32_t v) {
	p[0] = static_cast<uint8_t>(v);
	p[1] = static_cast<uint8_t>(v >> 8);
	p[2] = static_cast<uint8_t>(v >> 16);
	p[3] = static_cast<uint8_t>(v >> 24);
}

// 44-byte header for ulFrames frames of 16-bit stereo.
void wavHeader(uint8_t *p, uint32_t ulRate, uint32_t ulFrames) {
	const uint32_t ulData = ulFrames * 4;
	memcpy(p, "RIFF", 4);
	put32(p + 4, 36 + ulData);
	memcpy(p + 8, "WAVEfmt ", 8);
	put32(p + 16, 16);
	p[20] = 1; p[21] = 0;            // PCM
	p[22] = 2; p[23] = 0;            // stereo
	put32(p + 24, ulRate);
	put32(p + 28, ulRate * 4);
	p[32] = 4; p[33] = 0;            // block align
	p[34] = 16; p[35] = 0;           // bits
	memcpy(p + 36, "data", 4);
	put32(p + 40, ulData);
}

bool wavOpen(const char *szPath, uint32_t ulRate) {
	s_state.pWav = fopen(szPath, "wb");
	if(s_state.pWav == nullptr) {
		return false;
	}
	uint8_t aubHeader[44];
	wavHeader(aubHeader, ulRate, 0);
	s_state.ulWavFrames = 0;
	return fwrite(aubHeader, 1, 44, s_state.pWav) == 44;
}

void wavClose() {
	if(s_state.pWav == nullptr) {
		return;
	}
	uint8_t aubHeader[44];
	wavHeader(aubHeader, s_state.ulRate, s_state.ulWavFrames);
	fseek(s_state.pWav, 0, SEEK_SET);
	fwrite(aubHeader, 1, 44, s_state.pWav);
	fclose(s_state.pWav);
	s_state.pWav = nullptr;
}

// ---- the model ----------------------------------------------------------------------------------------------------------------------
void modelReset(const ms::paula::Config &sCfg) {
	ms::paula::reset(s_state.sPaula, sCfg, chipView());
	s_state.isModelReady = true;
	s_state.ulRate = sCfg.ulRate;
}

void ensureModel() {
	if(!s_state.isModelReady) {
		ms::paula::Config sCfg;
		sCfg.ulRate = kAudioRate;
		sCfg.ubSeparation = 100;
		sCfg.ulLowPass = 0;
		modelReset(sCfg);
	}
}

bool masterOn(void *) {
	return timerIsMasterEnabled();
}

void callHandler(void *) {
	++s_state.sStats.ulInterrupts;
	s_state.pfnHandler();
}

ms::paula::Hooks makeHooks() {
	ms::paula::Hooks sHooks;
	sHooks.pCtx = nullptr;
	sHooks.pfnMasterOn = masterOn;
	sHooks.pfnInterrupt = s_state.pfnHandler != nullptr ? callHandler : nullptr;
	return sHooks;
}

void modWrite16(void *, uint16_t uwReg, uint16_t uwValue) {
	ms::paula::write16(s_state.sPaula, uwReg, uwValue);
}

void modWrite32(void *, uint16_t uwReg, uint32_t ulValue) {
	ms::paula::write32(s_state.sPaula, uwReg, ulValue);
}

ms::ModHw makeModHw() {
	ms::ModHw sHw;
	sHw.pCtx = nullptr;
	sHw.pfnWrite16 = modWrite16;
	sHw.pfnWrite32 = modWrite32;
	return sHw;
}

// Render a piece of the current vblank; the samples go to the chunk buffer at ulOffset.
void renderPiece(uint32_t ulOffset, uint32_t ulFrames) {
	if(ulFrames != 0) {
		ms::paula::render(s_state.sPaula, s_asChunk + 2 * ulOffset, ulFrames, makeHooks());
	}
}

// One-pole coefficient (Q16) for a cut-off in Hz at the output rate.
uint32_t lowPassCoefficient(uint32_t ulCutoffHz, uint32_t ulRate) {
	const double dA = 1.0 - exp(-2.0 * 3.14159265358979323846 * static_cast<double>(ulCutoffHz) / static_cast<double>(ulRate));
	return static_cast<uint32_t>(dA * 65536.0 + 0.5);
}

// The stream's source format: what the model renders.  SDL converts to the device format itself.  Also asks for the device buffer size
// (a hint, read when the device opens), so call it right before the stream is opened.
SDL_AudioSpec makeWant() {
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
	SDL_AudioSpec sWant;
	memset(&sWant, 0, sizeof(sWant));
	sWant.freq = static_cast<int>(kAudioRate);
	sWant.format = SDL_AUDIO_S16LE;
	sWant.channels = 2;
	return sWant;
}

}  // namespace

AudioOptions audioDefaultOptions() {
	AudioOptions sOptions;
	sOptions.szDumpWav = nullptr;
	sOptions.ubSeparation = 100;
	sOptions.ubFilter = AUDIO_FILTER_OFF;
	return sOptions;
}

AudioStats audioStats() {
	return s_state.sStats;
}

void audioOnBoundary() {
	if(!s_state.isOpen) {
		return;
	}
	const uint64_t ullBoundary = s_state.sStats.ulBoundaries;
	const uint64_t ullRate = s_state.ulRate;
	uint32_t ulFrames = static_cast<uint32_t>(ullRate * (ullBoundary + 1) / kFrameHz - ullRate * ullBoundary / kFrameHz);
	if(ulFrames > kMaxChunk) {
		ulFrames = kMaxChunk;
	}
	++s_state.sStats.ulBoundaries;
	uint32_t ulDone = 0;
	if(s_state.sMod.isPlaying) {
		const ms::ModHw sHw = makeModHw();
		ms::modTick(s_state.sMod, sHw);
		if(ms::modNeedsLoopPointers(s_state.sMod)) {
			ulDone = ulFrames < kDmaDelayFrames ? ulFrames : kDmaDelayFrames;
			renderPiece(0, ulDone);   // the DMA starts and latches LC / LEN, then the repeat pointers follow
			ms::modLoopPointers(s_state.sMod, sHw);
		}
	}
	renderPiece(ulDone, ulFrames - ulDone);
	s_state.sStats.ulFrames += ulFrames;
	streamPush(s_asChunk, ulFrames);
	if(s_state.pWav != nullptr) {
		fwrite(s_asChunk, 4, ulFrames, s_state.pWav);
		s_state.ulWavFrames += ulFrames;
	}
}

AudioSession::AudioSession(const AudioOptions &sOptions)
	: m_isOk(true), m_subsystem(SDL_INIT_AUDIO), m_stream(makeWant()) {
	s_state.sStats = AudioStats();
	ms::paula::Config sCfg;
	sCfg.ulRate = kAudioRate;
	sCfg.ubSeparation = sOptions.ubSeparation > 100 ? 100 : sOptions.ubSeparation;
	sCfg.ulLowPass = sOptions.ubFilter == AUDIO_FILTER_A500 ? lowPassCoefficient(4421, kAudioRate)
		: sOptions.ubFilter == AUDIO_FILTER_A1200 ? lowPassCoefficient(20000, kAudioRate) : 0;
	modelReset(sCfg);
	s_state.sMod = ms::ModPlayer();
	s_state.isPlaying = false;
	s_state.ulPrefill = kPrefillVblanks * (kAudioRate / kFrameHz);
	void *pSilence = m_silence.acquireChip(8);
	s_state.ulSilence = pSilence != nullptr ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pSilence)) : 0;
	if(sOptions.szDumpWav != nullptr && !wavOpen(sOptions.szDumpWav, kAudioRate)) {
		platform::logFormat("audio: cannot create %s\n", sOptions.szDumpWav);
		m_isOk = false;
		return;
	}
	if(!m_subsystem) {
		platform::logFormat("audio: no audio subsystem (%s); the model runs silent\n", SDL_GetError());
	}
	else if(!m_stream) {
		platform::logFormat("audio: no audio device (%s); the model runs silent\n", SDL_GetError());
	}
	else {
		SDL_AudioSpec sHave;
		int lFrames = 0;
		memset(&sHave, 0, sizeof(sHave));
		if(!m_stream.deviceFormat(sHave, lFrames)) {
			sHave = makeWant();
			lFrames = static_cast<int>(kDeviceFrames);
		}
		platform::logFormat("audio: driver %s, %d Hz, %d channels, %d-frame device buffer, model at %u Hz\n", SDL_GetCurrentAudioDriver(),
			sHave.freq, static_cast<int>(sHave.channels), lFrames, static_cast<unsigned>(kAudioRate));
		s_state.pStream = &m_stream;
	}
	s_state.isOpen = true;
}

AudioSession::~AudioSession() {
	s_state.isOpen = false;
	s_state.pStream = nullptr;
	if(s_state.sMod.isPlaying) {
		ms::modStop(s_state.sMod, makeModHw());
	}
	s_state.ulSilence = 0;
	wavClose();
	const AudioStats sStats = audioStats();
	platform::logFormat("audio: %u vblanks, %u frames, %u interrupts, %u dropped chunks, %u underruns\n", static_cast<unsigned>(sStats.ulBoundaries),
		static_cast<unsigned>(sStats.ulFrames), static_cast<unsigned>(sStats.ulInterrupts), static_cast<unsigned>(sStats.ulDropped),
		static_cast<unsigned>(sStats.ulUnderruns));
	// m_stream closes first (members go in reverse order), which stops SDL's audio thread; the silence block is freed last
}

}  // namespace sdl

namespace platform {

void paulaWrite16(uint16_t uwReg, uint16_t uwValue) {
	sdl::ensureModel();
	ms::paula::write16(sdl::s_state.sPaula, uwReg, uwValue);
}

void paulaWrite32(uint16_t uwReg, uint32_t ulValue) {
	sdl::ensureModel();
	ms::paula::write32(sdl::s_state.sPaula, uwReg, ulValue);
}

uint16_t paulaRead16(uint16_t uwReg) {
	sdl::ensureModel();
	return ms::paula::read16(sdl::s_state.sPaula, uwReg);
}

void audioSetChannelHandler(AudioHandlerFn pfnHandler) {
	sdl::s_state.pfnHandler = pfnHandler;
}

bool musicPlay(const uint8_t *pModule, bool) {   // isPal: the PC ticks at the vblank (50 Hz) in every case
	sdl::ensureModel();
	if(sdl::s_state.sMod.isPlaying) {
		return true;
	}
	return ms::modStart(sdl::s_state.sMod, pModule, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pModule)), sdl::s_state.ulSilence);
}

void musicStop() {
	if(sdl::s_state.isModelReady) {
		ms::modStop(sdl::s_state.sMod, sdl::makeModHw());
	}
}

void musicSetVolume(uint8_t ubMaster) {
	sdl::ensureModel();
	ms::modSetMaster(sdl::s_state.sMod, sdl::makeModHw(), ubMaster);
}

}  // namespace platform
}  // namespace rt
