// rt/sdl/sdl_guards - the acquire/release pairs of the PC platform as stack guards (ROADMAP 11.4, docs/RAII.md).
//
// SDL_Init/SDL_Quit, the window and the renderer each get a guard whose destructor gives the resource back, so an early return
// out of main cannot leak them.  Guards live on the stack only (global constructors do not run in this program) and are pinned:
// no copy, no move.  Destruction order is the reverse of declaration, so declare the runtime first, then the window, then the renderer.
#pragma once
#include <SDL3/SDL.h>

#include "engine/guard.hpp"

namespace rt {
namespace sdl {

// SDL_Init(flags) ... SDL_Quit().
class SdlRuntime {
public:
	explicit SdlRuntime(SDL_InitFlags ulFlags) : m_isOk(SDL_Init(ulFlags)) {}
	~SdlRuntime() {
		if(m_isOk) {
			SDL_Quit();
		}
	}
	SdlRuntime(const SdlRuntime &) = delete;
	SdlRuntime &operator=(const SdlRuntime &) = delete;
	SdlRuntime(SdlRuntime &&) = delete;
	SdlRuntime &operator=(SdlRuntime &&) = delete;
	explicit operator bool() const { return m_isOk; }
private:
	bool m_isOk;
};
MS_GUARD_PINNED(SdlRuntime);

// SDL_CreateWindow ... SDL_DestroyWindow.
class SdlWindow {
public:
	SdlWindow(const char *szTitle, int wWidth, int wHeight, SDL_WindowFlags ulFlags)
		: m_pWindow(SDL_CreateWindow(szTitle, wWidth, wHeight, ulFlags)) {}   // SDL3 centres a new window by itself
	~SdlWindow() {
		if(m_pWindow != nullptr) {
			SDL_DestroyWindow(m_pWindow);
		}
	}
	SdlWindow(const SdlWindow &) = delete;
	SdlWindow &operator=(const SdlWindow &) = delete;
	SdlWindow(SdlWindow &&) = delete;
	SdlWindow &operator=(SdlWindow &&) = delete;
	explicit operator bool() const { return m_pWindow != nullptr; }
	SDL_Window *get() const { return m_pWindow; }
private:
	SDL_Window *m_pWindow;
};
MS_GUARD_PINNED(SdlWindow);

// SDL_CreateRenderer ... SDL_DestroyRenderer.  szDriver is a render driver name ("software") or nullptr for SDL's first choice.
class SdlRenderer {
public:
	SdlRenderer(SDL_Window *pWindow, const char *szDriver) : m_pRenderer(SDL_CreateRenderer(pWindow, szDriver)) {}
	~SdlRenderer() {
		if(m_pRenderer != nullptr) {
			SDL_DestroyRenderer(m_pRenderer);
		}
	}
	SdlRenderer(const SdlRenderer &) = delete;
	SdlRenderer &operator=(const SdlRenderer &) = delete;
	SdlRenderer(SdlRenderer &&) = delete;
	SdlRenderer &operator=(SdlRenderer &&) = delete;
	explicit operator bool() const { return m_pRenderer != nullptr; }
	SDL_Renderer *get() const { return m_pRenderer; }
private:
	SDL_Renderer *m_pRenderer;
};
MS_GUARD_PINNED(SdlRenderer);

// SDL_CreateTexture ... SDL_DestroyTexture.  Must be destroyed before the renderer it was made for (declare it after the renderer).
class SdlTexture {
public:
	SdlTexture(SDL_Renderer *pRenderer, SDL_PixelFormat ulFormat, SDL_TextureAccess lAccess, int wWidth, int wHeight)
		: m_pTexture(pRenderer != nullptr ? SDL_CreateTexture(pRenderer, ulFormat, lAccess, wWidth, wHeight) : nullptr) {}
	~SdlTexture() {
		if(m_pTexture != nullptr) {
			SDL_DestroyTexture(m_pTexture);
		}
	}
	SdlTexture(const SdlTexture &) = delete;
	SdlTexture &operator=(const SdlTexture &) = delete;
	SdlTexture(SdlTexture &&) = delete;
	SdlTexture &operator=(SdlTexture &&) = delete;
	explicit operator bool() const { return m_pTexture != nullptr; }
	SDL_Texture *get() const { return m_pTexture; }
private:
	SDL_Texture *m_pTexture;
};
MS_GUARD_PINNED(SdlTexture);

// SDL_InitSubSystem(flags) ... SDL_QuitSubSystem(flags).  For a subsystem that may be missing on the machine (audio without a device).
class SdlSubsystem {
public:
	explicit SdlSubsystem(SDL_InitFlags ulFlags) : m_ulFlags(ulFlags), m_isOk(SDL_InitSubSystem(ulFlags)) {}
	~SdlSubsystem() {
		if(m_isOk) {
			SDL_QuitSubSystem(m_ulFlags);
		}
	}
	SdlSubsystem(const SdlSubsystem &) = delete;
	SdlSubsystem &operator=(const SdlSubsystem &) = delete;
	SdlSubsystem(SdlSubsystem &&) = delete;
	SdlSubsystem &operator=(SdlSubsystem &&) = delete;
	explicit operator bool() const { return m_isOk; }
private:
	SDL_InitFlags m_ulFlags;
	bool m_isOk;
};
MS_GUARD_PINNED(SdlSubsystem);

// SDL_OpenAudioDeviceStream ... SDL_DestroyAudioStream (ROADMAP 11.8, SDL3 push model).  Opens the default playback device with a stream
// that converts from the caller's format; the device starts paused.  The game thread feeds it with put() (thread-safe inside SDL), SDL's
// own audio thread pulls from it, nothing of ours runs on that thread.  The destructor closes the device (the stream owns it), which
// waits for SDL's thread, so nothing of ours is read by it.
class SdlAudioStream {
public:
	explicit SdlAudioStream(const SDL_AudioSpec &sSpec)
		: m_pStream(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sSpec, nullptr, nullptr)) {}
	~SdlAudioStream() {
		if(m_pStream != nullptr) {
			SDL_DestroyAudioStream(m_pStream);
		}
	}
	SdlAudioStream(const SdlAudioStream &) = delete;
	SdlAudioStream &operator=(const SdlAudioStream &) = delete;
	SdlAudioStream(SdlAudioStream &&) = delete;
	SdlAudioStream &operator=(SdlAudioStream &&) = delete;
	explicit operator bool() const { return m_pStream != nullptr; }
	bool put(const void *pData, int lBytes) const { return m_pStream != nullptr && SDL_PutAudioStreamData(m_pStream, pData, lBytes); }
	// Bytes the device has not pulled yet.
	int queued() const { return m_pStream != nullptr ? SDL_GetAudioStreamQueued(m_pStream) : 0; }
	void resume() const {
		if(m_pStream != nullptr) {
			SDL_ResumeAudioStreamDevice(m_pStream);
		}
	}
	void pause() const {
		if(m_pStream != nullptr) {
			SDL_PauseAudioStreamDevice(m_pStream);
		}
	}
	// The device's own format and buffer size in sample frames (for the log); false when unknown.
	bool deviceFormat(SDL_AudioSpec &sSpec, int &lFrames) const {
		return m_pStream != nullptr && SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(m_pStream), &sSpec, &lFrames);
	}
private:
	SDL_AudioStream *m_pStream;
};
MS_GUARD_PINNED(SdlAudioStream);

}  // namespace sdl
}  // namespace rt
