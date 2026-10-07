// rt/combat_ui - what the screen port (src/rt/combat_ui.cpp, ROADMAP 7.1j) offers to src/rt/combat.cpp (the ScreenOps of the
// meeting / loot screens).
#pragma once
#include <stdint.h>

namespace rt {

void uiPoolClear();                          // LAB_044E
uint32_t uiHitTest(uint32_t ulX, uint32_t ulY);   // LAB_0451: the region record under (x, y) or 0
void uiButtonTables();                       // LAB_0588
void uiStats();                              // LAB_04F8 (the knight in LAB_0632, then LAB_04FE)
void uiItems();                              // LAB_04FE (the inventory in LAB_0632)
void uiList();                               // LAB_04EA
void uiLootLair();                           // LAB_051B
void uiLootDragon();                         // LAB_051D
void uiLootPick(uint32_t ulInventory);       // LAB_051F (A0 = an inventory)
void uiShop();                               // LAB_0522
void uiNextButton();                         // LAB_0524

}  // namespace rt

// The C entries behind the removed asm stubs (ROADMAP 7.1o), called directly by src/rt/{overworld,screens,loot}.cpp.
extern "C" {
uint32_t rtCuiHitTest(uint32_t ulX, uint32_t ulY, uint32_t *pRegion);   // LAB_0451: 1 / 0, *pRegion = the record (A0 of the original)
void rtCuiPoolClear(void);      // LAB_044E
void rtCuiJingleBad(void);      // LAB_05A0
void rtCuiJingleGood(void);     // LAB_05A1
void rtCuiCursorSprite(void);   // LAB_0572
void rtCuiWizard(void);         // LAB_0456
}
