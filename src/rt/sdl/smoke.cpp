// rt/sdl/smoke - see smoke.hpp.
#include "rt/sdl/smoke.hpp"

#include "engine/blit.hpp"
#include "rt/platform/audio.hpp"
#include "rt/platform/blit.hpp"
#include "rt/platform/display.hpp"
#include "engine/endian.hpp"
#include "rt/sdl/display.hpp"
#include "rt/platform/log.hpp"
#include "rt/platform/system.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr uint32_t kPlanes = 5;
constexpr uint32_t kRowBytes = platform::kDisplayWidth / 8;                // 40
constexpr uint32_t kPlaneBytes = kRowBytes * platform::kDisplayHeight;     // $1F40
constexpr uint32_t kScreenBytes = kPlanes * kPlaneBytes;
constexpr uint32_t kBars = platform::kDisplayWidth / 16;                   // 20
constexpr uint32_t kHalf = platform::kDisplayHeight / 2;                   // 100

uint32_t chipAddress(const void *p) {
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

ms::BlitOp makeOp(uint16_t uwCon0, uint8_t ubChannels) {
	ms::BlitOp sOp = {};
	sOp.con0 = uwCon0;
	sOp.afwm = sOp.alwm = 0xFFFF;
	sOp.channels = ubChannels;
	return sOp;
}

// The pattern of smoke.hpp into the screen at ulBase.
void drawPattern(uint32_t ulBase) {
	// clear: one D-only blit over all planes (rows of 20 words, 5 * 200 = 1000 rows), minterm 0
	ms::BlitOp sClear = makeOp(0x0100 | 0x00, ms::BLT_CH_D);
	sClear.ptD = ulBase;
	sClear.size = static_cast<uint16_t>((kPlanes * platform::kDisplayHeight) << 6 | kRowBytes / 2);
	platform::blitRun(sClear);
	// bars: for every plane, every bar whose colour has that plane's bit set gets a one-word column of ones in rows 0..99
	for(uint32_t ulPlane = 0; ulPlane < kPlanes; ++ulPlane) {
		for(uint32_t ulBar = 0; ulBar < kBars; ++ulBar) {
			if(((ulBar >> ulPlane) & 1) == 0) {
				continue;
			}
			ms::BlitOp sBar = makeOp(0x0100 | 0xFF, ms::BLT_CH_D);   // D = 1
			sBar.ptD = ulBase + ulPlane * kPlaneBytes + ulBar * 2;
			sBar.modD = static_cast<int16_t>(kRowBytes - 2);
			sBar.size = static_cast<uint16_t>(kHalf << 6 | 1);
			platform::blitRun(sBar);
		}
	}
	// lower half: rows 0..99 of every plane inverted into rows 100..199 (A -> D, D = not A)
	for(uint32_t ulPlane = 0; ulPlane < kPlanes; ++ulPlane) {
		ms::BlitOp sCopy = makeOp(0x0800 | 0x0100 | 0x0F, ms::BLT_CH_A | ms::BLT_CH_D);
		sCopy.ptA = ulBase + ulPlane * kPlaneBytes;
		sCopy.ptD = ulBase + ulPlane * kPlaneBytes + kHalf * kRowBytes;
		sCopy.size = static_cast<uint16_t>(kHalf << 6 | kRowBytes / 2);
		platform::blitRun(sCopy);
	}
}

}  // namespace

uint16_t smokePaletteValue(uint8_t ubColour) {
	const uint16_t uwR = static_cast<uint16_t>((ubColour * 5) & 15);
	const uint16_t uwG = static_cast<uint16_t>((ubColour * 3 + 4) & 15);
	const uint16_t uwB = static_cast<uint16_t>((ubColour * 7 + 9) & 15);
	return static_cast<uint16_t>((uwR << 8) | (uwG << 4) | uwB);
}

SmokePattern::SmokePattern() : m_isOk(false) {
	void *pScreens = m_sScreens.acquireChip(2 * kScreenBytes);   // both screens, one block (A first)
	if(pScreens == nullptr) {
		platform::logLine("smoke: no chip RAM for the screens\n");
		return;
	}
	platform::DisplaySpec sSpec;
	sSpec.ulScreenA = chipAddress(pScreens);
	sSpec.ulScreenB = sSpec.ulScreenA + kScreenBytes;
	sSpec.ubPlanes = static_cast<uint8_t>(kPlanes);
	if(!platform::displayOpen(sSpec)) {
		return;   // the guard frees the block
	}
	uint16_t auwPalette[32];
	for(uint8_t i = 0; i < 32; ++i) {
		auwPalette[i] = smokePaletteValue(i);
	}
	platform::displayWritePalette12(auwPalette, 32);
	drawPattern(sSpec.ulScreenA);
	platform::displayShow(sSpec.ulScreenA);
	m_isOk = true;
}

SmokePattern::~SmokePattern() {
	if(m_isOk) {
		platform::displayClose();
	}
}

namespace {

constexpr uint16_t kLine0 = 44;                 // S: the first line of the picture
constexpr uint32_t kSpriteCount = 8;
constexpr uint16_t kSpriteX[kSpriteCount] = {20, 26, 60, 64, 100, 100, 2, 310};
constexpr uint16_t kSpriteY[kSpriteCount] = {10, 12, 40, 44, 80, 80, 2, 130};
constexpr uint16_t kSpriteH[kSpriteCount] = {16, 16, 16, 16, 16, 16, 10, 40};
constexpr uint32_t kSpriteBytes = 4 + 4 * 40 + 4;   // control line, up to 40 data lines, terminator
constexpr uint32_t kListWords = 160;

// Big-endian word writer into chip RAM.
class Words {
public:
	explicit Words(uint8_t *pubAt) : m_pub(pubAt) {}
	void word(uint16_t uw) { ms::be::st16(m_pub, uw); m_pub += 2; }
	void move(uint16_t uwReg, uint16_t uwValue) { word(uwReg); word(uwValue); }
	void wait(uint16_t uwLine, uint16_t uwClock) { word(static_cast<uint16_t>((uwLine << 8) | (uwClock & 0xFE) | 1)); word(0xFFFE); }
	uint8_t *at() const { return m_pub; }
private:
	uint8_t *m_pub;
};

}  // namespace

SmokeCopper::SmokeCopper(bool isActive, uint32_t ulScreenBase) : m_isOk(false) {
	if(!isActive) {
		return;
	}
	uint8_t *pubMem = static_cast<uint8_t *>(m_sMem.acquireChip(kListWords * 2 + kSpriteCount * kSpriteBytes));
	if(pubMem == nullptr) {
		platform::logLine("smoke: no chip RAM for the copper list\n");
		return;
	}
	// sprites first (after the list)
	uint8_t *pubSprites = pubMem + kListWords * 2;
	for(uint32_t n = 0; n < kSpriteCount; ++n) {
		uint8_t *pubBlock = pubSprites + n * kSpriteBytes;
		const uint16_t uwHStart = static_cast<uint16_t>(0x81 + kSpriteX[n]);
		const uint16_t uwVStart = static_cast<uint16_t>(kLine0 + kSpriteY[n]);
		const uint16_t uwVStop = static_cast<uint16_t>(uwVStart + kSpriteH[n]);
		Words sBlock(pubBlock);
		sBlock.word(static_cast<uint16_t>(((uwVStart & 0xFF) << 8) | ((uwHStart >> 1) & 0xFF)));
		sBlock.word(static_cast<uint16_t>(((uwVStop & 0xFF) << 8) | (n == 5 ? 0x80 : 0) | (((uwVStart >> 8) & 1) << 2) | (((uwVStop >> 8) & 1) << 1) | (uwHStart & 1)));
		for(uint32_t r = 0; r < kSpriteH[n]; ++r) {
			sBlock.word(static_cast<uint16_t>((0x9C63u * (r + 1) + 0x1234u * n) & 0xFFFF));
			sBlock.word(static_cast<uint16_t>((0x5A3Du * (r + 3) + 0x0F0Fu * n) & 0xFFFF));
		}
		sBlock.word(0);
		sBlock.word(0);
		platform::displaySetSprite(static_cast<uint8_t>(n), chipAddress(pubBlock));
	}
	Words sList(pubMem);
	auto at = [&](uint16_t uwOffset, uint16_t uwClock) { sList.wait(static_cast<uint16_t>(kLine0 + uwOffset), uwClock); };
	at(30, 6);
	sList.move(0x182, 0x0F00);
	sList.move(0x184, 0x00F0);
	at(50, 6);
	sList.move(0x104, 0x0001);
	at(52, 6);
	sList.move(0x106, 0x2000);
	sList.move(0x18A, 0x0ABC);
	sList.move(0x106, 0x2200);
	sList.move(0x18A, 0x0123);
	sList.move(0x106, 0x0000);
	at(54, 6);
	sList.move(0x10C, 0x0022);
	at(70, 6);
	sList.move(0x10C, 0x0000);
	sList.move(0x108, 0xFFD8);
	sList.move(0x10A, 0xFFD8);
	at(86, 6);
	sList.move(0x104, 0x0003);
	at(90, 6);
	sList.move(0x10C, 0x0011);
	sList.move(0x108, 0);
	sList.move(0x10A, 0);
	at(128, 6);
	sList.move(0x104, 0x0004);
	at(150, 6);
	for(uint32_t p = 0; p < 5; ++p) {
		const uint32_t ulPlane = ulScreenBase + p * kPlaneBytes;
		sList.move(static_cast<uint16_t>(0x0E0 + 4 * p), static_cast<uint16_t>(ulPlane >> 16));
		sList.move(static_cast<uint16_t>(0x0E2 + 4 * p), static_cast<uint16_t>(ulPlane & 0xFFFF));
	}
	at(160, 6);
	sList.move(0x096, 0x0020);
	at(168, 6);
	sList.move(0x096, 0x8020);
	at(180, 222);
	sList.move(0x180, 0x0FF0);
	sList.word(0xFFFF);
	sList.word(0xFFFE);
	platform::displaySetWindow(0x3085, 0xF4BF);
	platform::displaySpritesEnable(true);
	displaySetCopperList(chipAddress(pubMem));
	m_isOk = true;
}

SmokeCopper::~SmokeCopper() {
	if(m_isOk) {
		displaySetCopperList(0);
		platform::displaySpritesEnable(false);
		platform::displaySetWindow(0x2C81, 0xF4C1);
		for(uint8_t n = 0; n < platform::kSpriteSlots; ++n) {
			platform::displaySetSprite(n, 0);
		}
	}
}

namespace {

constexpr uint16_t kRegDmacon = 0x96;
constexpr uint16_t kRegAud = 0xA0;
constexpr uint32_t kWaveBytes = 56;

uint16_t chanReg(uint8_t ubCh, uint16_t uwOffset) {
	return static_cast<uint16_t>(kRegAud + 0x10 * ubCh + uwOffset);
}

}  // namespace

SmokeTone::SmokeTone(bool isActive) : m_isOk(false) {
	if(!isActive) {
		return;
	}
	uint8_t *pWaves = static_cast<uint8_t *>(m_sWaves.acquireChip(kWaveBytes));
	if(pWaves == nullptr) {
		platform::logLine("smoke: no chip RAM for the tone\n");
		return;
	}
	for(uint32_t i = 0; i < 32; ++i) {
		pWaves[i] = static_cast<uint8_t>(i < 16 ? 100 : -100);                          // W0
	}
	for(uint32_t i = 0; i < 16; ++i) {
		pWaves[32 + i] = static_cast<uint8_t>(-120 + 16 * static_cast<int32_t>(i));     // W1
	}
	static const int8_t kTriangle[8] = {-100, -50, 0, 50, 100, 50, 0, -50};
	for(uint32_t i = 0; i < 8; ++i) {
		pWaves[48 + i] = static_cast<uint8_t>(kTriangle[i]);                            // W2
	}
	const uint32_t ulBase = chipAddress(pWaves);
	const uint32_t aulLc[4] = {ulBase, ulBase + 32, ulBase + 48, ulBase + 32};
	const uint16_t auwLen[4] = {16, 8, 4, 8};
	const uint16_t auwPer[4] = {428, 300, 500, 360};
	const uint16_t auwVol[4] = {64, 40, 20, 30};
	for(uint8_t c = 0; c < 4; ++c) {
		platform::paulaWrite32(chanReg(c, 0), aulLc[c]);
		platform::paulaWrite16(chanReg(c, 4), auwLen[c]);
		platform::paulaWrite16(chanReg(c, 6), auwPer[c]);
		platform::paulaWrite16(chanReg(c, 8), auwVol[c]);
	}
	platform::paulaWrite16(kRegDmacon, 0x800F);
	m_isOk = true;
}

SmokeTone::~SmokeTone() {
	if(m_isOk) {
		platform::paulaWrite16(kRegDmacon, 0x000F);
		for(uint8_t c = 0; c < 4; ++c) {
			platform::paulaWrite16(chanReg(c, 8), 0);
		}
	}
}

void SmokeTone::tick(uint32_t ulVblank) {
	if(!m_isOk) {
		return;
	}
	const uint32_t ulBase = chipAddress(m_sWaves.get());
	switch(ulVblank) {
		case 3:
			platform::paulaWrite16(chanReg(0, 6), 214);
			break;
		case 5:
			platform::paulaWrite16(chanReg(1, 8), 10);
			break;
		case 7:
			platform::paulaWrite16(kRegDmacon, 0x0004);
			break;
		case 9:
			platform::paulaWrite32(chanReg(3, 0), ulBase);
			platform::paulaWrite16(chanReg(3, 4), 16);
			break;
		case 11:
			platform::paulaWrite16(kRegDmacon, 0x0002);
			break;
		case 12:
			platform::paulaWrite16(kRegDmacon, 0x8002);
			break;
		default:
			break;
	}
}

}  // namespace sdl
}  // namespace rt
