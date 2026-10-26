// rt/sdl/audio - rt::platform::audio for the PC (ROADMAP 11.8): Paula model + module player, rendered in virtual time on the game thread,
// pushed once per vblank into an SDL3 audio stream.  The design is explained in audio.hpp.
#include "rt/sdl/audio.hpp"
#include "rt/sdl/audio_settings.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/modplay.hpp"
#include "engine/outfilter.hpp"
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
constexpr uint32_t kDeviceFrames = 512;             // requested device buffer (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES), ~11 ms
constexpr uint32_t kDmaDelayFrames = 40;            // ptplayer waits ~576 CIA ticks (0.8 ms) between DMA start and the repeat pointers
constexpr uint32_t kPadFrames = 480;                // silence queued before the first vblank (10 ms); + 2 vblanks = the 2400-frame prefill
constexpr uint32_t kPadStep = 480;                  // each underrun adds this much pad ...
constexpr uint32_t kPadMax = 960;                   // ... up to 20 ms (prefill 2880 = 60 ms)
constexpr uint32_t kPrefillVblanks = 2;             // the device starts when the pad and this many vblanks are queued
constexpr uint32_t kSteerWindow = 25;               // pushes (half a second) the queue level is averaged over for the drift control
constexpr int32_t kSteerDeadBand = 400;             // frames (8 ms) the average may stray from the prefill before the ratio moves
constexpr float kSteerMaxRatio = 0.004f;            // +-0.4 % playback speed at most
constexpr float kSteerFramesPerRatio = 480000.0f;   // an error of e frames asks for a ratio of e / this (600 frames: 0.125 %)

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
	uint32_t ulPad;                                 // frames of silence queued before the first vblank of a (re)start
	uint32_t ulPrefill;                             // frames queued before the device starts
	uint32_t ulSteerPushes;                         // drift control: pushes in the current window and the sum of the queue after them
	uint64_t ullSteerSum;
	uint32_t ulSteerWindows;                        // windows completed since the (re)start
	int32_t lSteerTarget;                           // the queue level (frames, after a push) to hold
	float fRatio;                                   // the stream's current playback ratio
	ms::OutFilter sFilter;                          // the analogue output stage
};
State s_state;

// ---- the stream (SDL3 push model: the game thread puts, SDL's audio thread pulls) --------------------------------------------------

int16_t s_asChunk[kMaxChunk * 2];

int16_t s_asPad[kPadMax * 2];                       // zeros

void setRatio(float fRatio) {
	if(fRatio != s_state.fRatio) {
		s_state.fRatio = fRatio;
		s_state.pStream->setFrequencyRatio(fRatio);
	}
}

// Drift control: while the game runs in real time, the queue level after the push (averaged over kSteerWindow pushes, so the device's
// buffer-sized pulls average out) is kept at the level the first full window after the (re)start measured, by a small playback ratio.
// Inside the dead band the ratio is exactly 1, so a machine whose two clocks agree never leaves SDL's plain copy path.
void steer(uint32_t ulQueuedAfter) {
	s_state.ullSteerSum += ulQueuedAfter;
	if(++s_state.ulSteerPushes < kSteerWindow) {
		return;
	}
	const int32_t lAverage = static_cast<int32_t>(s_state.ullSteerSum / s_state.ulSteerPushes);
	s_state.ullSteerSum = 0;
	s_state.ulSteerPushes = 0;
	if(s_state.ulSteerWindows < 2) {   // the first window is the start-up transient, the second one sets the level to hold
		if(++s_state.ulSteerWindows == 2) {
			s_state.lSteerTarget = lAverage;
		}
		return;
	}
	const int32_t lError = lAverage - s_state.lSteerTarget;
	float fRatio = 1.0f;
	if(lError > kSteerDeadBand || lError < -kSteerDeadBand) {   // too much queued: play faster; too little: slower
		fRatio += static_cast<float>(lError) / kSteerFramesPerRatio;
		fRatio = fRatio > 1.0f + kSteerMaxRatio ? 1.0f + kSteerMaxRatio : (fRatio < 1.0f - kSteerMaxRatio ? 1.0f - kSteerMaxRatio : fRatio);
	}
	setRatio(fRatio);
}

// Hands one vblank of finished samples to the device.  The device waits (paused) until ulPrefill frames are queued (the first vblank is
// preceded by ulPad frames of silence); a stream that ran dry while playing counts an underrun, is paused to buffer up again, and the pad
// grows; a chunk that does not fit counts as dropped (--fast runs, where the game is faster than the device).
void streamPush(const int16_t *pFrames, uint32_t ulFrames) {
	const SdlAudioStream *pStream = s_state.pStream;
	if(pStream == nullptr) {
		return;
	}
	uint32_t ulQueued = static_cast<uint32_t>(pStream->queued()) / 4;
	if(s_state.isPlaying) {
		if(ulQueued < s_state.sStats.ulMinQueued) {
			s_state.sStats.ulMinQueued = ulQueued;
		}
		if(ulQueued == 0) {
			++s_state.sStats.ulUnderruns;
			pStream->pause();                  // buffer up again before the next try
			s_state.isPlaying = false;
			if(s_state.ulPad < kPadMax) {
				s_state.ulPad += kPadStep;
				s_state.ulPrefill += kPadStep;
			}
			s_state.ulSteerPushes = 0;
			s_state.ullSteerSum = 0;
			s_state.ulSteerWindows = 0;
			setRatio(1.0f);
		}
	}
	if(ulQueued + ulFrames > kMaxQueuedFrames) {
		++s_state.sStats.ulDropped;
		return;
	}
	if(!s_state.isPlaying && ulQueued == 0 && pStream->put(s_asPad, static_cast<int>(s_state.ulPad * 4))) {
		ulQueued += s_state.ulPad;             // the pad goes in front of the first vblank of a (re)start
	}
	if(!pStream->put(pFrames, static_cast<int>(ulFrames * 4))) {
		++s_state.sStats.ulDropped;
		return;
	}
	if(!s_state.isPlaying && ulQueued + ulFrames >= s_state.ulPrefill) {
		s_state.isPlaying = true;
		pStream->resume();
	}
	else if(s_state.isPlaying && !timerIsUnthrottled()) {
		steer(ulQueued + ulFrames);
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
		sCfg.ubRender = ms::paula::RENDER_BOX;
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

// The stream's source format: what the model renders.  SDL converts to the device format itself.  Also asks for the device buffer size
// (a hint, read when the device opens), so call it right before the stream is opened.
SDL_AudioSpec makeWant() {
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
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
	sOptions.ubSeparation = kAudioDefaultSeparation;
	sOptions.ubFilter = ms::FILTER_A1200;
	sOptions.ubRender = ms::paula::RENDER_BLEP;
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
	ms::filterRun(s_state.sFilter, s_asChunk, ulFrames);
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
	s_state.sStats.ulMinQueued = UINT32_MAX;
	ms::paula::Config sCfg;
	sCfg.ulRate = kAudioRate;
	sCfg.ubSeparation = sOptions.ubSeparation > 100 ? 100 : sOptions.ubSeparation;
	sCfg.ulLowPass = 0;
	sCfg.ubRender = sOptions.ubRender == ms::paula::RENDER_BOX ? ms::paula::RENDER_BOX : ms::paula::RENDER_BLEP;
	modelReset(sCfg);
	ms::filterInit(s_state.sFilter, sOptions.ubFilter, kAudioRate);
	s_state.sMod = ms::ModPlayer();
	s_state.isPlaying = false;
	s_state.ulPad = kPadFrames;
	s_state.ulPrefill = kPadFrames + kPrefillVblanks * (kAudioRate / kFrameHz);
	s_state.ulSteerPushes = 0;
	s_state.ullSteerSum = 0;
	s_state.ulSteerWindows = 0;
	s_state.fRatio = 1.0f;
	platform::logFormat("audio: render %s, separation %u, filter %s\n", audioRenderName(sCfg.ubRender), static_cast<unsigned>(sCfg.ubSeparation),
		audioFilterName(s_state.sFilter.ubModel));
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
	platform::logFormat("audio: %u vblanks, %u frames, %u interrupts, %u dropped chunks, %u underruns, least queued %d frames\n",
		static_cast<unsigned>(sStats.ulBoundaries), static_cast<unsigned>(sStats.ulFrames), static_cast<unsigned>(sStats.ulInterrupts),
		static_cast<unsigned>(sStats.ulDropped), static_cast<unsigned>(sStats.ulUnderruns),
		sStats.ulMinQueued == UINT32_MAX ? -1 : static_cast<int>(sStats.ulMinQueued));
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
