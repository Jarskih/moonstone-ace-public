// rt/scene_town - asm-callable entries into src/game/scene_town.cpp (ROADMAP 6.7), patched over the town and meeting
// screen handlers of mog (asm/patches/mog.scene_town.json).  The transactions run in C++; the drawing (LAB_04D4), the
// sound/text player (LAB_0BB3) and everything else of the screens stay asm and are reached through the two trampolines
// rtTownRedraw / rtTownSound, in the order the original called them.  
//
// (ROADMAP 7.1s: the nine rt_town_* register-contract shims the patch sites used to jump into are gone: no asm is linked and
// the C++ callers in loot.cpp / overworld.cpp / screens.cpp call these functions directly.)

#include <stdint.h>

#include "game/api/data.hpp"
#include "game/rules.hpp"
#include "game/scene_town.hpp"
#include "game/state_bind.hpp"
#include "rt/party_ai.hpp"
#include "rt/party_loot.hpp"

extern "C" {
extern uint16_t mogInvChanged;              // "something changed" counter of the inventory screens (+1 per transaction) (LAB_0689)
extern uint16_t mogCursorX;              // mouse x (LAB_097F)
extern uint16_t mogDonation;              // healer donation (LAB_0976)
extern uint16_t mogDamageDiv;              // progress points a stat costs (3) (LAB_06DE)
extern uint16_t mogMarketPrices[12];          // market price list, written by LAB_0588 (LAB_0691)
extern ms::game::Inventory mogMarketStock;   // market stock (LAB_0690)
extern uint32_t mogDicePlayer;              // Knight* of the dice player (LAB_0F59)
extern uint32_t mogRandomSeed;              // the game's random seed (LAB_0973)
extern uint16_t mogDiceBet;              // the bet (LAB_0F62)
extern uint8_t mogDice[3];            // the three dice (LAB_0F5F)
extern uint8_t mogDicePayoutTable[66];           // payout table rows (6 bytes each, 11 rows) (LAB_0F63)
// Message texts embedded in the code hunk (strings after the handler's RTS) the sound/text player takes in A0.
extern const uint8_t mogSoundTextGoldTake[], mogSoundTextTempleStat[], mogSoundTextSell[], mogSoundTextBuy[];  // LAB_056C, LAB_056D, LAB_056F, LAB_0570

// The wrong-record LAB_0013 call of the market buy; the real routine is game_rules.cpp's entry.
void rtKnightRecalcHp(ms::game::Knight *pKnight);

void rtTownRedraw(void);
void rtTownSound(const uint8_t *pText);
void rtScreenRedraw(void);                    // src/rt/combat.cpp: LAB_04D4
}

namespace {

using namespace ms::game;

inline Knight *knightAt(uint32_t ulAddr) { return reinterpret_cast<Knight *>((uintptr_t)ulAddr); }
inline Inventory *invAt(uint32_t ulAddr) { return reinterpret_cast<Inventory *>((uintptr_t)ulAddr); }
// LAB_0013 reads the inventory through Knight +96; the pure code takes it explicitly.
inline Inventory &invOf(const Knight &k) { return *invAt(k.ulInventory); }

}  // namespace

// rt_town_shop_click: patch over LAB_0558 (state 5 click). In: D1.w = slot ($5C armour, $58 sword, $4C dagger), D3 =
// offer mask. Returns 1 when something was bought.
extern "C" __attribute__((used, externally_visible)) uint32_t rtTownShopClick(uint32_t ulSlot, uint32_t ulMask) {
	Knight &k = *knightAt(mogUiKnight);  // LAB_068B
	const uint8_t ubMask = (uint8_t)ulMask;
	ShopEcho echo = {};                                                // co-op: the computer knights buy the same (ROADMAP 8.3b)
	echo.ubMask = ubMask;
	bool bBought = false;
	switch (ulSlot & 0xFFFF) {
		case 0x5C: echo.kind = ShopBuy::Armour; bBought = shopBuyArmour(k, invOf(k), ubMask, ms::game::g_gameData); break;
		case 0x58: echo.kind = ShopBuy::Sword; bBought = shopBuySword(k, ubMask, ms::game::g_gameData); break;
		case 0x4C: echo.kind = ShopBuy::Dagger; bBought = shopBuyDagger(k, ms::game::g_gameData); break;
		default: return 0;
	}
	if (bBought) rt::computerEchoBuy(mogUiKnight, echo);
	return bBought;
}

// rt_town_market_click: patch over LAB_0562 (state 6 click). In: D1.w = slot, D3 = mask; the mouse x (LAB_097F) picks
// buy (>= $A0, signed word compare) or sell.
extern "C" __attribute__((used, externally_visible)) void rtTownMarketClick(uint32_t ulSlot, uint32_t ulMask) {
	Knight &k = *knightAt(mogUiKnight);
	Inventory &own = *invAt(mogUiInventory);  // LAB_068C
	const uint16_t uwSlot = (uint16_t)ulSlot;
	const uint8_t ubMask = (uint8_t)ulMask;
	if ((int16_t)mogCursorX >= 0xA0) {
		rtTownSound(mogSoundTextBuy + 1);                                // the buy sound plays before the price check
		Inventory stockBefore;                                           // the computer knights echo the buy from the stock as it was
		for (unsigned i = 0; i < sizeof(Inventory); ++i) {               // (a byte loop that stays one: the m68k build has no memcpy to turn it into)
			reinterpret_cast<uint8_t *>(&stockBefore)[i] = reinterpret_cast<const uint8_t *>(&mogMarketStock)[i];
			asm volatile("" ::: "memory");
		}


		const MarketResult res = marketBuy(k, own, mogMarketStock, mogMarketPrices, uwSlot, ubMask);
		if (res.bRecalcOnInventory) rtKnightRecalcHp(reinterpret_cast<Knight *>(&own));   // QUIRK, see marketBuy
		if (res.bDone) {
			ShopEcho echo = {};
			echo.kind = ShopBuy::MarketItem;
			echo.ubMask = ubMask;
			echo.uwSlot = uwSlot;
			echo.aPrices = mogMarketPrices;
			echo.pStock = &stockBefore;
			rt::computerEchoBuy(mogUiKnight, echo);
			rtTownRedraw();
		}
	} else {
		marketSell(k, own, mogMarketStock, mogMarketPrices, uwSlot, ubMask, ms::game::g_gameData);
		rtTownRedraw();
		rtTownSound(mogSoundTextSell + 1);                                // the sell sound follows the redraw
	}
}

extern "C" __attribute__((used, externally_visible)) void rtTownExchangeArmour(void) {
	rt::LootShare share;                                               // co-op: the other members copy the take (ROADMAP 8.5)
	rt::lootShareBegin(share);
	if (exchangeArmour(*knightAt(mogUiKnight), *knightAt(mogUiKnight2))) {  // LAB_068D
		rt::lootShareArmour(share);
		++mogInvChanged;
	}
}

extern "C" __attribute__((used, externally_visible)) void rtTownExchangeSword(void) {
	Knight &me = *knightAt(mogUiKnight);
	Knight &other = *knightAt(mogUiKnight2);
	rt::LootShare share;
	rt::lootShareBegin(share);
	// LAB_068C / LAB_068E are the inventories of the two records (LAB_058A); LAB_068E is only touched for a sharp sword.
	if (exchangeSword(me, other, *invAt(mogUiInventory), *invAt(mogUiInventory2[0]), mogSceneId == SCENE_CREATURE)) {  // LAB_068E, LAB_068F
		rt::lootShareSword(share);
		++mogInvChanged;
	}
}

extern "C" __attribute__((used, externally_visible)) void rtTownExchangeGold(void) {
	Knight &me = *knightAt(mogUiKnight);
	// Creature screen (state 2): the lair's gold word (Lair +8), else the other knight's gold.
	uint16_t *pSrc = mogSceneId == SCENE_CREATURE ? reinterpret_cast<uint16_t *>((uintptr_t)mogCurLair + 8)  // LAB_08C6
	                                   : &knightAt(mogUiKnight2)->uwGold;
	rt::LootShare share;
	rt::lootShareBegin(share);
	const GoldTake res = exchangeGold(me, *pSrc, ms::game::g_gameData);
	if (res.bCount) rt::lootShareGold(share);
	if (res.bSound) rtTownSound(mogSoundTextGoldTake);
	if (res.bCount) ++mogInvChanged;
}

extern "C" __attribute__((used, externally_visible)) void rtTownTempleStat(uint32_t ulSlot) {
	rtTownSound(mogSoundTextTempleStat);
	Knight &k = *knightAt(mogUiKnight);
	const uint16_t uwProgress = k.uwProgress;
	templeBuyStat(k, invOf(k), (uint16_t)ulSlot, mogDamageDiv);
	if (k.uwProgress != uwProgress) {                                   // a stat was bought: the computer knights buy it too (ROADMAP 8.3b)
		ShopEcho echo = {};
		echo.kind = ShopBuy::TempleStat;
		echo.uwSlot = (uint16_t)ulSlot;
		echo.uwAmount = mogDamageDiv;
		rt::computerEchoBuy(mogUiKnight, echo);
	}
}


extern "C" __attribute__((used, externally_visible)) uint32_t rtTownDaggerTopup(Knight *pMe, Knight *pOther) {
	if (!exchangeDaggers(*pMe, *pOther, ms::game::g_gameData)) return 0;
	++mogInvChanged;
	return 1;
}

extern "C" __attribute__((used, externally_visible)) void rtTownKnightRest(Knight *pKnight) {
	knightRest(*pKnight);
}

extern "C" __attribute__((used, externally_visible)) void rtTownCastleBlessing(Knight *pKnight) {
	castleBlessing(*pKnight, ms::game::g_gameData);
}

extern "C" __attribute__((used, externally_visible)) void rtTownSound(const uint8_t *) {}

// rtTownRedraw: LAB_04D4 (rebuild and redraw the screen); the C++ above sees the normal calling convention (the register trampoline that
// kept every register is not needed for C++ callers, ROADMAP 11.1d).
// rtTownSound: LAB_0BB3 with A0 = message text.  The text / sound player was a bare RTS in the game: nothing to do (ROADMAP 7.1q).
extern "C" __attribute__((used, externally_visible)) void rtTownRedraw(void) {
	rtScreenRedraw();
}

