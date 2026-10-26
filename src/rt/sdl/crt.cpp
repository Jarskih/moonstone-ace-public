// rt/sdl/crt - see crt.hpp.
#include "rt/sdl/crt.hpp"

#include <math.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <vector>

#include "rt/platform/log.hpp"
#include "rt/sdl/files.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr float kScanDepth = 0.50f;     // how dark the gap between two scanlines gets (0 = none, 1 = black)
constexpr float kBeamCentre = 0.35f;    // where in a source row the beam is brightest (0..1); off-centre so a 2x scale still has a dark row
constexpr float kMaskDepth = 0.20f;     // aperture grille: the two channels a column does not carry are dimmed by this much (scale >= 3)
constexpr float kVignette = 0.28f;      // darkening of the corners
constexpr uint8_t kGlow = 110;           // the bilinear pass is added at kGlow / 255
constexpr int32_t kMinScale = 2;

struct TextureDeleter {
	void operator()(SDL_Texture *pTexture) const { SDL_DestroyTexture(pTexture); }
};

bool s_isOn = false;
std::unique_ptr<SDL_Texture, TextureDeleter> s_pOverlay;
int32_t s_overlayW = 0;
int32_t s_overlayH = 0;
SDL_Renderer *s_pOverlayRenderer = nullptr;

std::string prefFile() {
	return std::string(filesPrefDir()) + "crt.txt";
}

uint8_t toByte(float f) {
	return static_cast<uint8_t>(f <= 0.0f ? 0 : (f >= 1.0f ? 255 : static_cast<int>(f * 255.0f + 0.5f)));
}

bool ensureOverlay(SDL_Renderer *pRenderer, int32_t w, int32_t h, int32_t srcW, int32_t srcH) {
	if(s_pOverlay && s_overlayW == w && s_overlayH == h && s_pOverlayRenderer == pRenderer) {
		return true;
	}
	s_pOverlay.reset(SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, w, h));
	s_overlayW = w;
	s_overlayH = h;
	s_pOverlayRenderer = pRenderer;
	if(!s_pOverlay) {
		platform::logFormat("crt: overlay %dx%d failed: %s\n", w, h, SDL_GetError());
		return false;
	}
	std::vector<uint32_t> aPixels(static_cast<size_t>(w) * static_cast<size_t>(h));
	for(int32_t y = 0; y < h; ++y) {
		for(int32_t x = 0; x < w; ++x) {
			aPixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = crtOverlayPixel(x, y, w, h, srcW, srcH);
		}
	}
	SDL_UpdateTexture(s_pOverlay.get(), nullptr, aPixels.data(), w * static_cast<int>(sizeof(uint32_t)));
	SDL_SetTextureBlendMode(s_pOverlay.get(), SDL_BLENDMODE_MOD);
	SDL_SetTextureScaleMode(s_pOverlay.get(), SDL_SCALEMODE_NEAREST);
	return true;
}

}  // namespace

uint32_t crtOverlayPixel(int32_t x, int32_t y, int32_t w, int32_t h, int32_t srcW, int32_t srcH) {
	// scanlines: the position inside the source row; the gain falls off with the distance from the beam centre
	const float fRow = (static_cast<float>(y) + 0.5f) * static_cast<float>(srcH) / static_cast<float>(h);
	float fDist = fabsf((fRow - floorf(fRow)) - kBeamCentre);
	fDist = fDist > 0.5f ? 1.0f - fDist : fDist;
	const float fScan = 1.0f - kScanDepth * (2.0f * fDist) * (2.0f * fDist);
	// aperture grille: one RGB triad per three output columns (only when the scale is big enough to show it)
	float afMask[3] = {1.0f, 1.0f, 1.0f};
	if(w >= 3 * srcW) {
		for(int32_t c = 0; c < 3; ++c) {
			afMask[c] = (x % 3 == c) ? 1.0f : 1.0f - kMaskDepth;
		}
	}
	// vignette: radial, 0 in the middle, 1 in the corners
	const float fNx = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(w)) - 1.0f;
	const float fNy = (2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(h)) - 1.0f;
	const float fVig = 1.0f - kVignette * 0.5f * (fNx * fNx + fNy * fNy);
	const float fGain = fScan * fVig;
	return (static_cast<uint32_t>(toByte(fGain * afMask[0])) << 16) | (static_cast<uint32_t>(toByte(fGain * afMask[1])) << 8) |
	       static_cast<uint32_t>(toByte(fGain * afMask[2]));
}

void crtSetEnabled(bool isOn) {
	s_isOn = isOn;
}

bool crtIsEnabled() {
	return s_isOn;
}

void crtToggle() {
	s_isOn = !s_isOn;
	platform::logFormat("crt: %s\n", s_isOn ? "on" : "off");
	FILE *pFile = fopen(prefFile().c_str(), "wb");
	if(pFile != nullptr) {
		fputs(s_isOn ? "1\n" : "0\n", pFile);
		fclose(pFile);
	}
}

void crtLoadPref() {
	FILE *pFile = fopen(prefFile().c_str(), "rb");
	if(pFile == nullptr) {
		return;
	}
	const int iChar = fgetc(pFile);
	fclose(pFile);
	s_isOn = iChar == '1';
}

void crtDrawPicture(SDL_Renderer *pRenderer, SDL_Texture *pPicture, const SDL_FRect &sDst) {
	SDL_SetTextureScaleMode(pPicture, SDL_SCALEMODE_NEAREST);
	SDL_SetTextureBlendMode(pPicture, SDL_BLENDMODE_NONE);
	SDL_RenderTexture(pRenderer, pPicture, nullptr, &sDst);
	const int32_t w = static_cast<int32_t>(sDst.w);
	const int32_t h = static_cast<int32_t>(sDst.h);
	if(!s_isOn || w < kMinScale * 320 || h < kMinScale * 200 || !ensureOverlay(pRenderer, w, h, 320, 200)) {
		return;
	}
	SDL_SetTextureScaleMode(pPicture, SDL_SCALEMODE_LINEAR);
	SDL_SetTextureBlendMode(pPicture, SDL_BLENDMODE_ADD);
	SDL_SetTextureColorMod(pPicture, kGlow, kGlow, kGlow);
	SDL_RenderTexture(pRenderer, pPicture, nullptr, &sDst);
	SDL_SetTextureColorMod(pPicture, 255, 255, 255);
	SDL_SetTextureBlendMode(pPicture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(pPicture, SDL_SCALEMODE_NEAREST);
	SDL_RenderTexture(pRenderer, s_pOverlay.get(), nullptr, &sDst);
}

void crtRelease() {
	s_pOverlay.reset();
	s_overlayW = s_overlayH = 0;
	s_pOverlayRenderer = nullptr;
}

}  // namespace sdl
}  // namespace rt
