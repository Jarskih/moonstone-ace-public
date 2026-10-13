// rt/sdl/display - rt::platform::display for the PC (ROADMAP 11.6a/b/c): the display state the game sets, run through the copper and the
// sprite DMA line by line and converted to RGB each frame.  See display.hpp.  Wipes and the enhanced mode's own palette path are 11.6d.
//
// A frame is built the way the chip builds it, one PAL line at a time (lines 0..311), from a register image (engine/copper Regs):
//   * the image starts as what the CPU side latched: the palette (displayWritePalette*), the shown screen's bitplane pointers
//     (displayShow), the window (displaySetWindow), the sprite pointers and the sprite DMA switch, plus the registers the Amiga
//     display code leaves fixed (DDF $38/$D0, modulos 0, BPLCON0 from the plane count, BPLCON2 $24 -- $224 at six planes, BPLCON4 0,
//     FMODE 0; docs/PC_PORT.md "Copper and sprite usage");
//   * when a copper list is installed (displaySetCopperList) engine/copper runs it against the beam: every instruction that starts before
//     a line's data fetch has been applied when the line is drawn, so palette splits, pointer / modulo / BPLCON / window changes land on
//     the line the list asks for.  Without a list nothing changes during the frame (the 11.6a static picture);
//   * bitplanes: per line the pointers run on by the row length plus the modulo (BPL1MOD odd planes, BPL2MOD even), a pointer write
//     replaces the running pointer; the row is fetched with engine/planar rowIndices from DDFSTRT / DDFSTOP, shown inside DIWSTRT /
//     DIWSTOP, COLOR00 elsewhere;
//   * sprites: engine/sprites reads the control and data lines from chip RAM per line and composites them with the playfield by BPLCON2.
// The output is the standard window: 320 x 200 low-res pixels, line 44 and horizontal position $81 are its top-left corner.  A window moved
// by the screen shake shifts or clips the picture inside it; whatever lies outside the 320 x 200 rectangle is not shown.
#include "rt/platform/display.hpp"

#include <string.h>

#include "engine/copper.hpp"
#include "engine/dispframe.hpp"
#include "engine/planar.hpp"
#include "rt/platform/log.hpp"
#include "rt/sdl/chip.hpp"
#include "rt/sdl/display.hpp"
#include "rt/sdl/log.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr uint32_t kPlaneBytes = (platform::kDisplayWidth / 8u) * platform::kDisplayHeight;   // $1F40: the game's plane stride
constexpr int32_t kAspectNum = 6;       // a pixel is 6/5 as tall as wide: 320x200 fills a 4:3 screen
constexpr int32_t kAspectDen = 5;
constexpr uint16_t kFirstLine = 0x2C;   // line of the top row of the output rectangle (the standard window start)
constexpr int32_t kFirstPixel = 0x81;   // horizontal position (low-res pixels) of its left column
constexpr uint16_t kDefaultBplcon2 = 0x0024;     // Kickstart's value the original inherits: PF1P = PF2P = 4, sprites in front
constexpr uint16_t kBplcon2KillEhb = 0x0200;

struct State {
	bool isOpen;
	platform::DisplaySpec sSpec;
	uint32_t ulShown;                   // base of the screen shown
	ms::planar::Palette sPalette;
	volatile uint16_t auwColor[34];     // COLOR00..COLOR31 as the game wrote them (12-bit); the palette of a frame unless the 24-bit path ran
	bool isRgb24;                       // displayWritePalette24 was used: sPalette holds the colours, the register image is ignored
	uint16_t uwDiwStrt;
	uint16_t uwDiwStop;
	uint32_t aulSprite[platform::kSpriteSlots];
	bool isSpritesOn;
	uint32_t ulCopperList;              // COP1LC of the frame, 0 = none
	uint32_t ulProblemsLogged;
	SDL_Renderer *pRenderer;
	SDL_Texture *pTexture;
	uint32_t ulFrames;
};
State s_display;
uint32_t s_aulFrame[platform::kDisplayWidth * platform::kDisplayHeight];

void reportOnce(uint32_t ulProblem, const char *szText, uint32_t ulA, uint32_t ulB) {
	if((s_display.ulProblemsLogged & ulProblem) != 0) {
		return;
	}
	s_display.ulProblemsLogged |= ulProblem;
	platform::logFormat("display: %s (%08x %08x)\n", szText, static_cast<unsigned>(ulA), static_cast<unsigned>(ulB));
}

// The latched state as the register image the frame starts from.
void initialRegs(ms::copper::Regs &sRegs) {
	ms::copper::regsReset(sRegs);
	if(!s_display.isRgb24) {
		for(uint32_t i = 0; i < 32; ++i) {
			s_display.sPalette.rgb[i] = ms::planar::rgb12(s_display.auwColor[i]);   // AGA without LOCT: low nibble = high nibble
		}
	}
	sRegs.palette = s_display.sPalette;
	const uint8_t ubPlanes = s_display.sSpec.ubPlanes;
	for(uint8_t i = 0; i < ubPlanes; ++i) {
		sRegs.bplpt[i] = s_display.ulShown + i * kPlaneBytes;
	}
	sRegs.bplcon0 = ubPlanes >= 8 ? 0x0210 : static_cast<uint16_t>((ubPlanes << 12) | 0x0200);
	sRegs.bplcon2 = static_cast<uint16_t>(ubPlanes == 6 ? (kDefaultBplcon2 | kBplcon2KillEhb) : kDefaultBplcon2);   // 64 real colours in the enhanced mode
	sRegs.diwstrt = s_display.uwDiwStrt;
	sRegs.diwstop = s_display.uwDiwStop;
	for(uint8_t i = 0; i < platform::kSpriteSlots; ++i) {
		sRegs.sprpt[i] = s_display.aulSprite[i];
	}
	if(!s_display.isSpritesOn) {
		sRegs.dmacon = static_cast<uint16_t>(sRegs.dmacon & ~ms::copper::kDmaSpr);
	}
}

}  // namespace

void displayAttach(SDL_Renderer *pRenderer, SDL_Texture *pTexture) {
	s_display.pRenderer = pRenderer;
	s_display.pTexture = pTexture;
}

void displayDetach() {
	s_display.pRenderer = nullptr;
	s_display.pTexture = nullptr;
}

const uint32_t *displayRenderFrame() {
	++s_display.ulFrames;
	if(!s_display.isOpen) {
		memset(s_aulFrame, 0, sizeof(s_aulFrame));
		return s_aulFrame;
	}
	ms::copper::Regs sRegs;
	initialRegs(sRegs);
	ms::dispframe::Output sOut;
	sOut.originX = kFirstPixel;
	sOut.originY = kFirstLine;
	sOut.width = platform::kDisplayWidth;
	sOut.height = platform::kDisplayHeight;
	sOut.pixels = s_aulFrame;
	ms::dispframe::Result sResult;
	ms::dispframe::render(chipView(), sRegs, s_display.ulCopperList, sOut, sResult);
	using namespace ms::dispframe;
	if(sResult.problems & PROBLEM_BAD_ADDRESS) {
		reportOnce(PROBLEM_BAD_ADDRESS, "bitplanes outside the emulated chip RAM (shown as colour 0 there), plane 1 at", s_display.ulShown, sResult.badBitplaneAddr);
	}
	if(sResult.problems & PROBLEM_MODE) {
		reportOnce(PROBLEM_MODE, "BPLCON0 selects hires / HAM / dual playfield / interlace / super-hires (not supported)", sRegs.bplcon0, 0);
	}
	if(sResult.problems & PROBLEM_SCROLL) {
		reportOnce(PROBLEM_SCROLL, "BPLCON1 scroll is not supported", 0, 0);
	}
	if(sResult.problems & PROBLEM_SPRITE_ADDRESS) {
		reportOnce(PROBLEM_SPRITE_ADDRESS, "sprite data outside the emulated chip RAM", 0, 0);
	}
	if(sResult.problems & PROBLEM_SPRITE_RES) {
		reportOnce(PROBLEM_SPRITE_RES, "hires / super-hires sprites are not supported", 0, 0);
	}
	if(sResult.problems & PROBLEM_FMODE) {
		reportOnce(PROBLEM_FMODE, "FMODE bitplane fetch modes are not supported", 0, 0);
	}
	if(sResult.problems & PROBLEM_COPPER) {
		reportOnce(PROBLEM_COPPER, ms::copper::statusText(sResult.copperStatus), sResult.copperErrAddr,
		           (static_cast<uint32_t>(sResult.copperErrWord1) << 16) | sResult.copperErrWord2);
	}
	return s_aulFrame;
}

void displayLogState() {
	if(!s_display.isOpen) {
		platform::logLine("display: closed\n");
		return;
	}
	const ms::softblit::ChipView sChip = chipView();
	uint32_t aulUsed[2] = {0, 0};
	const uint32_t aulBase[2] = {s_display.sSpec.ulScreenA, s_display.sSpec.ulScreenB};
	for(uint32_t s = 0; s < 2; ++s) {
		const uint8_t *pBytes = sChip.bytes + (aulBase[s] - sChip.origin);
		for(uint32_t i = 0; i < kPlaneBytes * s_display.sSpec.ubPlanes; ++i) {
			aulUsed[s] += pBytes[i] != 0;
		}
	}
	platform::logFormat("display: chip view %08x + %x, DIW %04x %04x, sprites %d\n", static_cast<unsigned>(sChip.origin), static_cast<unsigned>(sChip.size),
	                    s_display.uwDiwStrt, s_display.uwDiwStop, s_display.isSpritesOn ? 1 : 0);
	platform::logFormat("display: shown %s (%08x), non-zero bytes A %u B %u, colours %03x %03x %03x %03x %03x %03x %03x %03x\n",
	                    s_display.ulShown == s_display.sSpec.ulScreenA ? "A" : "B", static_cast<unsigned>(s_display.ulShown), static_cast<unsigned>(aulUsed[0]),
	                    static_cast<unsigned>(aulUsed[1]), s_display.auwColor[0], s_display.auwColor[1], s_display.auwColor[2], s_display.auwColor[3],
	                    s_display.auwColor[4], s_display.auwColor[5], s_display.auwColor[6], s_display.auwColor[7]);
}

void displayPresent() {
	const uint32_t *pulFrame = displayRenderFrame();
	if(s_display.pRenderer == nullptr || s_display.pTexture == nullptr) {
		return;
	}
	SDL_UpdateTexture(s_display.pTexture, nullptr, pulFrame, platform::kDisplayWidth * static_cast<int>(sizeof(uint32_t)));
	int wWidth = 0;
	int wHeight = 0;
	SDL_GetCurrentRenderOutputSize(s_display.pRenderer, &wWidth, &wHeight);
	const ms::planar::Rect sRect = ms::planar::fitRect(wWidth, wHeight, platform::kDisplayWidth, platform::kDisplayHeight, kAspectNum, kAspectDen);
	const SDL_FRect sDst = {static_cast<float>(sRect.x), static_cast<float>(sRect.y), static_cast<float>(sRect.w), static_cast<float>(sRect.h)};
	SDL_SetRenderDrawColor(s_display.pRenderer, 0, 0, 0, 255);
	SDL_RenderClear(s_display.pRenderer);
	SDL_RenderTexture(s_display.pRenderer, s_display.pTexture, nullptr, &sDst);
	SDL_RenderPresent(s_display.pRenderer);
}

uint32_t displayFrameCount() {
	return s_display.ulFrames;
}

void displaySetCopperList(uint32_t ulList) {
	s_display.ulCopperList = ulList;
}

}  // namespace sdl

namespace platform {

bool displayOpen(const DisplaySpec &sSpec) {
	if(sSpec.ubPlanes < 1 || sSpec.ubPlanes > 6) {
		logFormat("displayOpen: %u planes\n", static_cast<unsigned>(sSpec.ubPlanes));
		return false;
	}
	sdl::State &sState = sdl::s_display;
	sState.isOpen = true;
	sState.sSpec = sSpec;
	sState.ulShown = sSpec.ulScreenB;   // both overlays start with screen B shown and A drawn (docs/DISPLAY.md section 7)
	ms::planar::paletteClear(sState.sPalette);
	for(volatile uint16_t &uwColor : sState.auwColor) {
		uwColor = 0;
	}
	sState.isRgb24 = false;
	sState.uwDiwStrt = 0x2C81;          // the original window: 320x200 at line 44 (docs/DISPLAY.md section 7)
	sState.uwDiwStop = 0xF4C1;
	for(uint32_t &ulSprite : sState.aulSprite) {
		ulSprite = 0;
	}
	sState.isSpritesOn = false;
	sState.ulCopperList = 0;
	sState.ulProblemsLogged = 0;
	return true;
}

void displayClose() {
	sdl::s_display.isOpen = false;
	sdl::s_display.ulCopperList = 0;
}

bool displayIsOpen() {
	return sdl::s_display.isOpen;
}

void displayShow(uint32_t ulScreenBase) {
	sdl::s_display.ulShown = ulScreenBase;   // from the next converted frame on
}

void displayWritePalette12(const uint16_t *puwColours, uint8_t ubCount) {
	sdl::s_display.isRgb24 = false;
	for(uint8_t i = 0; i < ubCount && i < 32; ++i) {
		sdl::s_display.auwColor[i] = puwColours[i];
	}
}

volatile uint16_t *displayColorRegs() {
	return sdl::s_display.auwColor;
}

void displayWritePalette24(const uint32_t *pulRgb24, uint32_t ulCount) {
	if(ulCount > ms::planar::kPaletteSize) {
		ulCount = ms::planar::kPaletteSize;
	}
	sdl::s_display.isRgb24 = true;
	for(uint32_t i = 0; i < ulCount; ++i) {
		sdl::s_display.sPalette.rgb[i] = pulRgb24[i] & 0xFFFFFFu;
	}
}

void displaySetBackdrop(uint16_t uwRgb12) {
	sdl::s_display.auwColor[0] = uwRgb12;
	sdl::s_display.sPalette.rgb[0] = ms::planar::rgb12(uwRgb12);
}

void displaySetWindow(uint16_t uwDiwStrt, uint16_t uwDiwStop) {
	sdl::s_display.uwDiwStrt = uwDiwStrt;   // applies to the next converted frame (the shake writes it every tick)
	sdl::s_display.uwDiwStop = uwDiwStop;
}

void displaySetSprite(uint8_t ubSprite, uint32_t ulData) {
	if(ubSprite < kSpriteSlots) {
		sdl::s_display.aulSprite[ubSprite] = ulData;
	}
}

void displaySpritesEnable(bool isOn) {
	sdl::s_display.isSpritesOn = isOn;
}

}  // namespace platform
}  // namespace rt
