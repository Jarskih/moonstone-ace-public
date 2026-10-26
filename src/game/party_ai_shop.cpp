// game/party_ai_shop - see include/game/party_ai_shop.hpp.  New rules (ROADMAP 8.3b), no original label; every purchase goes through the
// original's own rule function (rules/shops.cpp, rules/stats.cpp, rules/healing.cpp), so prices, caps and quirks are the human's.
#include "game/party_ai_shop.hpp"

#include "game/rules/healing.hpp"
#include "game/rules/levelling.hpp"
#include "game/rules/shops.hpp"
#include "game/rules/stats.hpp"

namespace ms { namespace game {

namespace {

inline const uint8_t *bytes(const Inventory &inv) { return reinterpret_cast<const uint8_t *>(&inv); }
inline uint8_t *bytes(Inventory &inv) { return reinterpret_cast<uint8_t *>(&inv); }

// A struct copy as a byte loop: the m68k build has no memcpy (a plain `a = b` of a 132-byte record would call it).
template <typename T> void copyOf(T &dst, const T &src) {
	uint8_t *pDst = reinterpret_cast<uint8_t *>(&dst);
	const uint8_t *pSrc = reinterpret_cast<const uint8_t *>(&src);
	for(unsigned i = 0; i < sizeof(T); ++i) {
		pDst[i] = pSrc[i];
		asm volatile("" ::: "memory");                             // (keeps the loop a loop: the compiler would turn it back into the memcpy call)
	}

}

// LAB_0690 slots that are flag words (keys $14, moonstones $16), not counts.
inline bool isFlagSlot(uint16_t uwSlot) { return uwSlot == 0x16 || uwSlot == 0x14; }

bool echoArmour(Knight &k, Inventory &inv, const ShopEcho &e, const GameData &d) {
	const uint32_t ulArmour = shopArmourOffer(e.ubMask);
	if(ulArmour == 0 || static_cast<int32_t>(k.ulArmour) >= static_cast<int32_t>(ulArmour)) {
		return false;                                              // it already wears that armour or a better one: a human could buy the downgrade, the computer does not
	}
	return shopBuyArmour(k, inv, e.ubMask, d);
}

bool echoMarket(Knight &k, Inventory &inv, const ShopEcho &e, const GameData &d) {
	(void)d;
	if(e.aPrices == 0 || e.pStock == 0 || e.uwSlot >= sizeof(Inventory)) {
		return false;
	}
	Inventory stock;                                               // a copy: the merchant's real stock was reduced by the human's purchase
	copyOf(stock, *e.pStock);
	if(isFlagSlot(e.uwSlot)) {
		if((bytes(stock)[e.uwSlot] & e.ubMask) == 0 || (bytes(inv)[e.uwSlot] & e.ubMask) != 0) {
			return false;                                          // not on offer, or it owns it already
		}
	} else if(bytes(stock)[e.uwSlot] == 0) {
		return false;
	}
	const MarketResult res = marketBuy(k, inv, stock, e.aPrices, e.uwSlot, e.ubMask);
	if(res.bRecalcOnInventory) {
		knightRecalcHp(k, inv);                                    // the original recalculates the wrong record here (a quirk of the human's click): the echo does it right
	}
	return res.bDone;
}

bool echoTemple(Knight &k, Inventory &inv, const ShopEcho &e) {
	if(e.uwSlot >= sizeof(Knight) || !progressCanAfford(k, e.uwAmount)) {
		return false;
	}
	if(reinterpret_cast<const uint8_t *>(&k)[e.uwSlot] >= STAT_LIMIT) {
		return false;                                              // the stat is at its limit
	}
	const uint16_t uwBefore = k.uwProgress;
	templeBuyStat(k, inv, e.uwSlot, e.uwAmount);
	return k.uwProgress != uwBefore;                               // (templeBuyStat itself refuses a dexterity point the rules forbid)
}

bool echoHealer(Knight &k, const ShopEcho &e, const GameData &d) {
	if(e.uwAmount < d.healer.uwHealPrice || !canAfford(k, e.uwAmount)) {
		return false;
	}
	uint16_t uwDonation = e.uwAmount;                              // healerApply changes nothing when it heals nothing (mask 0): pay only for a heal
	if(healerApply(k, uwDonation, d) == 0) {
		return false;
	}
	k.uwGold = static_cast<uint16_t>(k.uwGold - e.uwAmount);
	return true;
}

}  // namespace

bool partyEchoBuy(Knight &k, Inventory &inv, const ShopEcho &e, const GameData &d) {
	switch(e.kind) {
	case ShopBuy::Armour: return echoArmour(k, inv, e, d);
	case ShopBuy::Sword: return shopBuySword(k, e.ubMask, d);
	case ShopBuy::Dagger: return shopBuyDagger(k, d);
	case ShopBuy::MarketItem: return echoMarket(k, inv, e, d);
	case ShopBuy::TempleStat: return echoTemple(k, inv, e);
	case ShopBuy::Healer: return echoHealer(k, e, d);
	case ShopBuy::None: break;
	}
	return false;
}

}}  // namespace ms::game
