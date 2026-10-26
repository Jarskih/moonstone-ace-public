// rt/sdl/display - the PC side of rt::platform::display beyond the platform interface (ROADMAP 11.6a): where the converted frame goes.
//
// Per frame (the frame hook of rt/sdl/timer): the planar screen the game shows (displayShow) is read from the emulated chip RAM
// (rt/sdl/chip), converted with engine/planar over the palette the game wrote (displayWritePalette12/24, displaySetBackdrop), uploaded
// into a streaming 320x200 texture and drawn scaled into the window: the largest whole scale that fits, centred, nearest-neighbour
// filter, black bars around it.  Pixel-perfect by default (square pixels, every source pixel the same whole number of screen pixels);
// displaySetAspect(true) stretches them 6:5 in height instead (a 4:3 picture, like the monitor the game was made for; rows then differ
// by one screen pixel).
// Without a renderer (headless runs) the conversion still happens, so the frame can be dumped (rt/sdl/imgdump).
//
// 11.6b/c: the frame is built line by line from a register image (engine/copper, engine/sprites): an installed copper list
// (displaySetCopperList) changes palette, bitplane pointers, modulos, BPLCONs, the window and the sprite pointers on the lines it asks for,
// the DIW window (displaySetWindow) clips and shifts the picture, hardware sprites (displaySetSprite / displaySpritesEnable) are read
// from chip RAM by the sprite DMA model and composited by BPLCON2.  Not yet: wipes and the enhanced mode's own palette path (11.6d).
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>

#include "engine/guard.hpp"
#include "engine/planar.hpp"
#include "rt/sdl/sdl_guards.hpp"

namespace rt {
namespace sdl {

// The renderer and texture the display presents into.  nullptr renderer = headless.
void displayAttach(SDL_Renderer *pRenderer, SDL_Texture *pTexture);
void displayDetach();

// Convert the current display state (screen, palette) into the frame buffer and return it: kDisplayWidth x kDisplayHeight pixels,
// row-major, 0x00RRGGBB.  Black when no display is open; a screen outside the chip RAM is logged once and shows as colour 0.
const uint32_t *displayRenderFrame();

// displayRenderFrame + upload + scaled draw + SDL_RenderPresent (no present without a renderer).
void displayPresent();

// Debug: write the composed window (scaling, CRT effect, bars; read back from the renderer) as a PNG when the ulFrame-th frame is presented.
void displayDumpOutputAt(const char *szPath, uint32_t ulFrame);

// The scaling rule (--aspect): false = pixel-perfect square pixels (default), true = the Amiga's 6:5 pixel aspect.
void displaySetAspect(bool isAspect);
// Where the 320x200 picture sits in a window of lWinW x lWinH pixels under the current rule (the display and the mouse use the same).
ms::planar::Rect displayFit(int32_t lWinW, int32_t lWinH);
// Height of the picture at whole scale lScale under the current rule (window sizing).
int32_t displayScaledHeight(int32_t lScale);

// Install the copper list the frame runs (COP1LC: a chip address, big-endian instructions ending in a WAIT that never holds, e.g. $FFFF,$FFFE).
// 0 removes it: the frame then shows the latched state unchanged (the 11.6a static picture).  The game itself installs none on the PC (its
// stub and ACE's list only set sprite pointers and bitplane pointers, which displayShow / displaySetSprite carry); this is for tests, the
// smoke frame and replaying recorded Amiga lists.
void displaySetCopperList(uint32_t ulList);

// One log line with the display state the game left (the shown screen, how much of both screens is drawn, the first colours); for the end of a run.
void displayLogState();

// Frames converted since the process started (tests).
uint32_t displayFrameCount();

// The streaming texture of the display for the length of a run, registered with the display module (docs/RAII.md); destroyed before the
// renderer when declared after it.
class DisplaySession {
public:
	explicit DisplaySession(SDL_Renderer *pRenderer)
		// XRGB: the frame's pixels are 0x00RRGGBB (engine/planar); an ARGB texture would read that as alpha 0 and blend to a black window
		// on the GPU renderers (the software renderer of --smoke / the frame dumps never showed it)
		: m_texture(pRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200) {
		if(m_texture) {
			SDL_SetTextureScaleMode(m_texture.get(), SDL_SCALEMODE_NEAREST);   // the pixels stay square-edged when scaled
		}
		displayAttach(pRenderer, m_texture.get());
	}
	~DisplaySession() { displayDetach(); }
	DisplaySession(const DisplaySession &) = delete;
	DisplaySession &operator=(const DisplaySession &) = delete;
	DisplaySession(DisplaySession &&) = delete;
	DisplaySession &operator=(DisplaySession &&) = delete;
	explicit operator bool() const { return static_cast<bool>(m_texture); }
private:
	SdlTexture m_texture;
};
MS_GUARD_PINNED(DisplaySession);

}  // namespace sdl
}  // namespace rt
