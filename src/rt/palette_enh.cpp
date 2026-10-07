// rt/palette_enh - the 24-bit palette of the enhanced display (ROADMAP 4.8a). Logic: src/engine/enhpal.cpp (host-tested);
// this file owns the state and writes it to the AGA colour registers with rtPaletteAgaWrite (src/rt/palette_aga.cpp).
// Without MS_ENHANCED every entry is an empty function (the shims never call them: rt_enh_planes stays 5).
#include "rt/enhanced.hpp"

#if MS_ENHANCED

#include "engine/enhpal.hpp"
#include "engine/palette.hpp"

extern "C" void rtPaletteAgaWrite(void *, const ULONG *pColors, ULONG ulCount);

namespace {

ms::EnhPalReg s_reg;
ms::EnhPal s_pal;
bool s_isInit;

void ensureInit() {
	if(!s_isInit) {
		ms::regInit(s_reg);
		ms::palInit(s_pal);
		s_isInit = true;
	}
}

void writeLive() {
	rtPaletteAgaWrite(nullptr, reinterpret_cast<const ULONG *>(s_pal.live), ms::kEnhColors);
}

}  // namespace

namespace rt {

void enhPaletteAddSidecar(const uint32_t *pPal24) {
	ensureInit();
	ms::regAddSidecar(s_reg, pPal24);
}

void enhPictureLoaded(const uint16_t *pRawWords, uint32_t ulWords, uint32_t ulPlanes) {
	ensureInit();
	uint32_t aPal24[ms::kEnhColors];
	ms::regPictureLoaded(s_reg, pRawWords, ulWords, ulPlanes, aPal24);
}

void enhPaletteSetTarget(const UWORD *pTable12) {
	ensureInit();
	if(!pTable12) {  // the game cancelled the fade
		s_pal.hasTarget = false;
		return;
	}
	uint32_t aPal24[ms::kEnhColors];
	ms::regLookup(s_reg, pTable12, aPal24);
	ms::palSetTarget(s_pal, aPal24);
}

void enhPaletteWriteNow(const UWORD *pTable12) {
	enhPaletteSetLive(pTable12);
	writeLive();
}

void enhPaletteSetLive(const UWORD *pTable12) {
	ensureInit();
	uint32_t aPal24[ms::kEnhColors];
	ms::regLookup(s_reg, pTable12, aPal24);
	ms::palSetNow(s_pal, aPal24);
}

void enhPaletteClear() {
	ensureInit();
	uint32_t aBlack[ms::kEnhColors] = {};
	ms::palSetNow(s_pal, aBlack);
	writeLive();
}

void enhPaletteCycleAdd(uint32_t ulSlot, uint8_t ubFirst, uint8_t ubLast, uint8_t ubDir) {
	ensureInit();
	ms::palCycleAdd(s_pal, ulSlot, ubFirst, ubLast, ubDir);
}

void enhPaletteRampAdd(uint32_t ulSlot, uint8_t ubColor, UWORD uwTarget12) {
	ensureInit();
	ms::palRampAdd(s_pal, ulSlot, ubColor, ms::color12to24(uwTarget12), uwTarget12);
}

void enhPaletteReset() {
	ensureInit();
	ms::palInit(s_pal);
}

void enhPaletteTick(const ms::PaletteVars &v, bool wasTargetSet) {
	ensureInit();
	ms::EnhTick t;
	ms::enhTickFrom(v, wasTargetSet, t);
	if(ms::palTick(s_pal, t)) {
		writeLive();
	}
}

}  // namespace rt

#else  // !MS_ENHANCED

namespace rt {

void enhPaletteAddSidecar(const uint32_t *) {}
void enhPictureLoaded(const uint16_t *, uint32_t, uint32_t) {}
void enhPaletteSetTarget(const UWORD *) {}
void enhPaletteWriteNow(const UWORD *) {}
void enhPaletteSetLive(const UWORD *) {}
void enhPaletteClear() {}
void enhPaletteCycleAdd(uint32_t, uint8_t, uint8_t, uint8_t) {}
void enhPaletteRampAdd(uint32_t, uint8_t, UWORD) {}
void enhPaletteReset() {}
void enhPaletteTick(const ms::PaletteVars &, bool) {}

}  // namespace rt

#endif
