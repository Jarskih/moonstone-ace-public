// rt/platform/display - what the game needs from "the screen" (ROADMAP 11.1, docs/PC_PORT.md section 4).
//
// The game draws into two planar screens (5 bitplanes of 320x200, 6 in the enhanced mode) that it allocates itself; the platform
// shows one of them. Everything below is what rt/display_ace.cpp, rt/display_ops.cpp, rt/sprites.cpp, rt/palette_aga.cpp,
// rt/mog_display.cpp and rt/prg_ops.cpp do to ACE and the custom chips today, one function per distinct action.
//   Amiga  : src/rt/amiga/ (ACE view + raw copper list; colour registers; DIWSTRT/DIWSTOP; SPRxPT through the copper stub).
//   PC     : the "virtual Amiga" backend (ROADMAP 11.6): the planar screens are read from chip RAM every frame and converted to RGB;
//            palette and window writes are latched per frame (11.6a), per line when a copper list is interpreted (11.6b).
// Addresses are the game's 32-bit chip addresses (docs/PC_PORT.md section 6: a pointer on the Amiga and with `-m32`, an offset into the
// emulated chip RAM otherwise). No ACE or OS header is included here.
#pragma once
#include <stdint.h>

namespace rt {
namespace platform {

constexpr uint16_t kDisplayWidth = 320;
constexpr uint16_t kDisplayHeight = 200;
constexpr uint8_t kSpriteSlots = 8;          // hardware sprites 0..7

struct DisplaySpec {
	uint32_t ulScreenA;                      // base of screen A (plane 0 first, planes contiguous)
	uint32_t ulScreenB;                      // base of screen B
	uint8_t ubPlanes;                        // 5, or 6 (MS_ENHANCED)
};

// Create the display over the two screens and make it the visible one (displayAceActivate). False if it cannot be created;
// nothing is left allocated then. Called once, before the first overlay entry.
bool displayOpen(const DisplaySpec &sSpec);
// Destroy it again (displayAceRelease). Idempotent.
void displayClose();
// True between a successful displayOpen and displayClose.
bool displayIsOpen();

// Show the screen at ulScreenBase (screen A or B of the spec) from the next frame on (displayShow: the bitplane pointers).
void displayShow(uint32_t ulScreenBase);

// Colour registers. Both wait for the frame boundary themselves where the hardware needs it.
// 12-bit Amiga colours 0x0RGB for COLOR00..COLOR(ubCount-1) (displayWritePalette: 32 words; prg_ops: palette load).
void displayWritePalette12(const uint16_t *puwColours, uint8_t ubCount);
// The colour registers COLOR00..COLOR31 themselves, for the code that writes them one by one (the palette tick of every VBL: ms::paletteTick over
// a register array).  The Amiga's are the custom-chip registers at $DFF180; the PC's are an image the next converted frame is made from.
volatile uint16_t *displayColorRegs();
// 24-bit 0x00RRGGBB for colours 0..ulCount-1 (<= 256) of the enhanced mode (rtPaletteAgaWrite: BPLCON3 banks + LOCT; ms::PaletteWriter24).
void displayWritePalette24(const uint32_t *pulRgb24, uint32_t ulCount);
// Colour 0 alone (the error flash of mog LAB_0D77: COLOR00 = $0C00, then back).
void displaySetBackdrop(uint16_t uwRgb12);

// Display window (DIWSTRT/DIWSTOP raw register values; the screen-shake effect of mog LAB_042C writes them every tick, ms::DiwShake).
void displaySetWindow(uint16_t uwDiwStrt, uint16_t uwDiwStop);

// Hardware sprites. Sprite ubSprite (0..7) shows the sprite data at ulData (first control word) from the next frame on; ulData = 0
// selects the "no sprite" data the display keeps (copperStubSetSprite). Sprite DMA is switched on once (spriteDmaOn).
void displaySetSprite(uint8_t ubSprite, uint32_t ulData);
void displaySpritesEnable(bool isOn);

}  // namespace platform
}  // namespace rt
