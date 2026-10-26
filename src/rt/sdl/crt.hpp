// rt/sdl/crt - the optional CRT look of the PC build (off by default; --crt, F12 toggles).  Pure presentation: it only changes how the already
// converted 320x200 picture is drawn into its output rectangle, nothing the game, the input or --dump-frame sees.
//
// Design (docs/PC_PORT.md "CRT effect"): three plain SDL renderer passes, no shader, so it works on every renderer SDL picks (Direct3D 11 / 12,
// OpenGL, software):
//   1. the picture, nearest-filtered at the whole scale (the pixel-perfect base),
//   2. the same texture bilinear-filtered, added at ~30 % (SDL_BLENDMODE_ADD): the soft glow / horizontal bleed between neighbouring pixels,
//   3. an overlay texture multiplied over the picture (SDL_BLENDMODE_MOD): scanlines, the RGB aperture-grille mask and a vignette.  It is
//      generated on the CPU once per output size (a few ms, not per frame), so the per-frame cost is two extra textured quads.
// Not done: barrel curvature (needs a mesh or a shader; it would also break the pixel-perfect scale).
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>

namespace rt {
namespace sdl {

void crtSetEnabled(bool isOn);
bool crtIsEnabled();
// Flips the state (F12), logs it and remembers it in the user directory (crt.txt).
void crtToggle();
// The remembered choice of the last session (a missing file = off).  Not called when --crt / --no-crt was given.
void crtLoadPref();

// Draws pPicture (nearest filtered) into sDst like SDL_RenderTexture, with the effect when it is on and the scale is large enough (>= 2x).
void crtDrawPicture(SDL_Renderer *pRenderer, SDL_Texture *pPicture, const SDL_FRect &sDst);
// Drops the generated overlay (before the renderer goes away).
void crtRelease();

// The overlay pixel (0x00RRGGBB multiplier) for output pixel (x, y) of a picture w x h output pixels large showing srcW x srcH source pixels.
// Pure, so a unit test or a tool can look at the pattern.
uint32_t crtOverlayPixel(int32_t x, int32_t y, int32_t w, int32_t h, int32_t srcW, int32_t srcH);

}  // namespace sdl
}  // namespace rt
