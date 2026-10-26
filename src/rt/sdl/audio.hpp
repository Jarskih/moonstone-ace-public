// rt/sdl/audio - the PC side of rt::platform::audio beyond the platform interface (ROADMAP 11.8): the Paula model, the module player
// and the SDL audio device.
//
// Threading model (SDL3 push model; the answer to "SDL plays on its own thread"): the game stays single-threaded and so does the chip.
// The Paula model (engine/paula) is advanced by the GAME thread in VIRTUAL TIME: every 50 Hz vertical blank the timer calls
// audioOnBoundary(), which first lets the module player tick, then renders exactly one frame's worth of output samples (48000 / 50 = 960
// frames) with the registers the game wrote during that frame.  The level-4 (AUDx) interrupt handler of the synth is called from
// inside that render, at the exact chip time of the buffer end, on the game thread, between two statements of the timer pump -- so the
// synth's state needs no lock, and the handler's LC/LEN writes land exactly where the real chip would have used them.  The rendered
// samples are handed over with SDL_PutAudioStreamData into an SDL audio stream (SDL_OpenAudioDeviceStream) once per vblank; SDL's own
// audio thread pulls from the stream and converts to the device format.  No callback, no ring and no atomics of ours: the stream is the
// only thing that crosses threads, and SDL locks it.  SDL's thread never touches the model, a register or the game.
//
// Why not "write commands into a queue, render in the audio thread": the interrupt handler would then have to run on the audio thread (or
// be postponed by up to a buffer), and the output would depend on its scheduling; here the samples are a pure function of the register
// writes and the vblank count, so --dump-audio (and the tests) get exactly what the speakers get, and --fast runs are reproducible.  The
// price is latency: the device is held paused until 2400 frames (50 ms: 10 ms of silent pad + 2 vblanks) are queued, plus the device buffer
// (512 frames requested, ~11 ms).  Why that much: the device pulls whole buffers, so the queue must always hold more than one buffer
// (512) after the vblank-sized pushes (960) have taken their share; 2400 leaves ~19 ms of slack for a late vblank.
// Accounting: a chunk that would take the queue past 16384 frames (~340 ms) is dropped (the game is faster than the device: --fast); a
// stream found empty after playing started counts an underrun, pauses the device and buffers up again with a larger pad (up to 60 ms in
// all, so repeated underruns buy margin).  The smallest queue seen before a push is reported (AudioStats::ulMinQueued).
// Clock drift: the vblank timer and the sound device run on different clocks, so over minutes the queue would creep to empty (an
// underrun) or to full (dropped chunks).  While the game runs in real time the queue level (averaged over a second) steers the stream's
// playback ratio by at most +-0.4 % (SDL resamples; 0.4 % is 7 cents) with a dead band, and the ratio is exactly 1 otherwise.  The WAV
// dump is taken from the model, before any of this.
//
// A session is a stack guard (docs/RAII.md): audio subsystem, device and WAV dump are released in reverse order on every exit path.
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>

#include "engine/guard.hpp"
#include "rt/platform/system.hpp"
#include "rt/sdl/sdl_guards.hpp"

namespace rt {
namespace sdl {

constexpr uint32_t kAudioRate = 48000;

constexpr uint8_t kAudioDefaultSeparation = 70;

struct AudioOptions {
	const char *szDumpWav;   // write every rendered frame to this 16-bit stereo WAV (nullptr = no dump)
	uint8_t ubSeparation;    // 0..100, 100 = Amiga hard panning (channels 0, 3 left; 1, 2 right)
	uint8_t ubFilter;        // ms::FilterModel (engine/outfilter): the analogue output stage applied to the model's frames
	uint8_t ubRender;        // ms::paula::Render: band-limited steps (default) or the exact box filter
};

// Defaults: no dump, separation 70, the A1200 filter (fixed stage only), band-limited steps.
AudioOptions audioDefaultOptions();

struct AudioStats {
	uint32_t ulBoundaries;   // vblanks rendered
	uint32_t ulFrames;       // sample frames produced
	uint32_t ulDropped;      // chunks the stream had no room for (the device is slower than the game: --fast)
	uint32_t ulUnderruns;    // vblanks that found the stream empty after playing started
	uint32_t ulMinQueued;    // the fewest frames the stream held just before a push while playing (the slack against a late vblank)
	uint32_t ulInterrupts;   // AUDx interrupt handler calls
};
AudioStats audioStats();

// Called by the timer after the vblank handler of every frame boundary (also in unthrottled runs): one frame of virtual time.  No-op
// when no session is open.
void audioOnBoundary();

// Opens the model, the device (a missing audio device is logged and ignored: the model, the dump and the music still run) and the dump.
// operator bool is false only when the dump file could not be created.
class AudioSession {
public:
	explicit AudioSession(const AudioOptions &sOptions);
	~AudioSession();
	AudioSession(const AudioSession &) = delete;
	AudioSession &operator=(const AudioSession &) = delete;
	AudioSession(AudioSession &&) = delete;
	AudioSession &operator=(AudioSession &&) = delete;
	explicit operator bool() const { return m_isOk; }
	bool hasDevice() const { return static_cast<bool>(m_stream); }
private:
	bool m_isOk;
	SdlSubsystem m_subsystem;
	SdlAudioStream m_stream;
	platform::MemBlock m_silence;
};
MS_GUARD_PINNED(AudioSession);

}  // namespace sdl
}  // namespace rt
