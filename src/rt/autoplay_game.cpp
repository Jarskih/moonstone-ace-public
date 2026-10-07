// rt/autoplay_game - the game-state hooks of the headless test harness (`poke <name> <value>` script lines, docs/AUTOPLAY.md).
// Compiled only with MS_AUTOPLAY and the real game asm linked. They exist so a boot test can reach a place without walking there
// (and read what the map looks like): they change game variables from the VBL interrupt, so use them on a settled screen (the map).
// Not part of any original behaviour; nothing here is built into a normal build.
#include "rt/autoplay.hpp"

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY

#include "game/state_bind.hpp"
#include "rt/serlog.hpp"

extern "C" volatile uint16_t mogCursorX, mogCursorY;   // cursor of the shop screens (LAB_097F, LAB_0980)
extern "C" uint32_t mogRecordTable;                          // the click-region pool of the screen loop (24 bytes each, ended by width 0) (SECSTRT_14)

extern "C" uint16_t mogRectCount;   // LAB_0645: dirty rects counted this frame (the list takes 45)
extern "C" uint32_t mogRectList, mogRectListA, mogRectListB;   // LAB_0641, LAB_063E, LAB_063F: dirty-rect lists (8 bytes: x y w h)
extern "C" uint16_t mogDragonActive;   // LAB_0667: the dragon is flying on the map

namespace rt {

namespace {

using ms::game::Knight;
using ms::game::Lair;

bool eqName(const char *a, const char *b) {
	for(; *a && *b; ++a, ++b) {
		const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
		if(ca != *b) {
			return false;
		}
	}
	return *a == *b;
}

uint8_t s_ubWho;   // the knight the pokes act on (`poke who N`, 0..4): knight 0 is the human of a one-player game

Knight *currentKnight() {
	return &mogKnights[s_ubWho];  // LAB_0613
}

// A field of the current knight: byte offset into the Knight record (or into its Inventory) and size.
struct Field {
	const char *szName;
	uint8_t ubOfs;
	uint8_t ubSize;
	bool isInv;
};

const Field s_fields[] = {
	{"mapx", offsetof(Knight, uwMapX), 2, false},
	{"mapy", offsetof(Knight, uwMapY), 2, false},
	{"gold", offsetof(Knight, uwGold), 2, false},
	{"hp", offsetof(Knight, swHp), 2, false},
	{"hpmax", offsetof(Knight, swHpMax), 2, false},
	{"lives", offsetof(Knight, ubLives), 1, false},
	{"progress", offsetof(Knight, uwProgress), 2, false},
	{"strength", offsetof(Knight, ubStrength), 1, false},
	{"constitution", offsetof(Knight, ubConstitution), 1, false},
	{"endurance", offsetof(Knight, ubEndurance), 1, false},
	{"daggers", offsetof(Knight, ubDaggers), 1, false},
	{"frog", offsetof(Knight, ubFrogDays), 1, false},   // days the knight skips its turn (the black knights: poke 100 to park them)
	{"keys", offsetof(ms::game::Inventory, ubKeys), 1, true},
	{"moonstones", offsetof(ms::game::Inventory, ubMoonstones), 1, true},
};

void put(uint8_t *p, uint8_t ubSize, ULONG ulV) {   // big-endian store
	for(uint8_t i = 0; i < ubSize; ++i) {
		p[ubSize - 1 - i] = static_cast<uint8_t>(ulV >> (8 * i));
	}
}

// Put the current knight on a box (x, y) so the proximity scan (LAB_0069) finds it.
void warp(int16_t swX, int16_t swY) {
	Knight *pK = currentKnight();
	pK->uwMapX = static_cast<uint16_t>(swX);
	pK->uwMapY = static_cast<uint16_t>(swY);
	serLogf("AUTOPLAY warp %d %d\n", static_cast<int>(swX), static_cast<int>(swY));
}

void dump() {
	const Knight *pCur = currentKnight();
	serLogf("AUTOPLAY dump: players=%u turn=%u who=%u (kind%lu) at %u,%u lives=%u hp=%d/%d gold=%u progress=%u day=%u spent=%u scene=%lu\n",
		static_cast<unsigned>(mogHumanPlayers.uw), static_cast<unsigned>(mogTurnCursor), static_cast<unsigned>(s_ubWho),  // LAB_05C5, LAB_0654
		static_cast<unsigned long>(pCur->ulKind), static_cast<unsigned>(pCur->uwMapX),
		static_cast<unsigned>(pCur->uwMapY), static_cast<unsigned>(pCur->ubLives), static_cast<int>(pCur->swHp), static_cast<int>(pCur->swHpMax),
		static_cast<unsigned>(pCur->uwGold), static_cast<unsigned>(pCur->uwProgress), static_cast<unsigned>(mogDayCounter),  // LAB_06C0
		static_cast<unsigned>(mogMoveSpent), static_cast<unsigned long>(mogSceneId));  // LAB_0655, LAB_068F
	for(unsigned i = 0; i < 5; ++i) {
		const Knight &k = mogKnights[i];
		serLogf("AUTOPLAY dump: knight%u kind=%lu at %u,%u lives=%u hp=%d\n", i, static_cast<unsigned long>(k.ulKind),
			static_cast<unsigned>(k.uwMapX), static_cast<unsigned>(k.uwMapY), static_cast<unsigned>(k.ubLives), static_cast<int>(k.swHp));
	}
	for(unsigned i = 0; i < 9 && mogMapNodes[i].swId >= 0; ++i) {  // LAB_069F
		serLogf("AUTOPLAY dump: node%u id=0x%x at %u,%u\n", i, static_cast<unsigned>(mogMapNodes[i].swId),
			static_cast<unsigned>(mogMapNodes[i].uwX), static_cast<unsigned>(mogMapNodes[i].uwY));
	}
	const Lair *pLairs = reinterpret_cast<const Lair *>(mogHeapTable[17]);  // LAB_05B9
	for(unsigned i = 0; pLairs && i < 24; ++i) {
		serLogf("AUTOPLAY dump: lair%u at %d,%d left=%u\n", i, static_cast<int>(pLairs[i].swMapX), static_cast<int>(pLairs[i].swMapY),
			static_cast<unsigned>(pLairs[i].uwFlag8));
	}
}

}  // namespace

bool autoplayPeek(const char *szName, ULONG &ulValue) {
	if(eqName(szName, "scene")) {   // LAB_068F: the screen state machine (2 = creature loot, 3 = wizard item, 5 smith, 6 market, 9 status / temple...)
		ulValue = mogSceneId;
		return true;
	}
	if(eqName(szName, "turn")) {
		ulValue = mogTurnCursor;
		return true;
	}
	if(eqName(szName, "day")) {
		ulValue = mogDayCounter;
		return true;
	}
	if(eqName(szName, "spent")) {
		ulValue = mogMoveSpent;
		return true;
	}
	if(eqName(szName, "players")) {
		ulValue = mogHumanPlayers.uw;
		return true;
	}
	if(eqName(szName, "defeat")) {   // LAB_05DC: bit 0 = the first fighter lost the last fight
		ulValue = mogDefeatBits.ub;  // LAB_05DC
		return true;
	}
	if(eqName(szName, "who")) {
		ulValue = s_ubWho;
		return true;
	}
	if(eqName(szName, "dragon")) {   // the dragon is flying (spawned on a map start from day 2)
		ulValue = mogDragonActive;
		return true;
	}
	if(eqName(szName, "dragonlives")) {   // lives byte of the dragon record (negative = killed)
		ulValue = mogKnights[4].ubLives;
		return true;
	}
	for(const Field &f : s_fields) {
		if(eqName(szName, f.szName)) {
			const Knight *pK = currentKnight();
			const uint8_t *p = (f.isInv ? reinterpret_cast<const uint8_t *>(pK->ulInventory) : reinterpret_cast<const uint8_t *>(pK)) + f.ubOfs;
			ULONG ulV = 0;
			for(uint8_t i = 0; i < f.ubSize; ++i) {
				ulV = (ulV << 8) | p[i];
			}
			ulValue = ulV;
			return true;
		}
	}
	return false;
}

bool autoplayPoke(const char *szName, ULONG ulValue) {
	if(eqName(szName, "regions")) {   // the click regions of the current shop / loot screen
		const uint8_t *p = reinterpret_cast<const uint8_t *>(mogRecordTable);
		auto w = [](const uint8_t *q) { return static_cast<unsigned>((q[0] << 8) | q[1]); };
		auto l = [](const uint8_t *q) { return (static_cast<unsigned long>(q[0]) << 24) | (static_cast<unsigned long>(q[1]) << 16) | (static_cast<unsigned long>(q[2]) << 8) | q[3]; };
		unsigned uRep = 0;   // the stat bars register one identical region per stat point: collapse repeats
		for(unsigned i = 0; i < 120 && w(p + 4) != 0; ++i, p += 24) {
			if(i && l(p + 8) == l(p - 16) && w(p + 12) == w(p - 12) && w(p + 14) == w(p - 10)) {
				++uRep;
				continue;
			}
			if(uRep) {
				serLogf("AUTOPLAY   (+%u identical)\n", uRep);
				uRep = 0;
			}
			serLogf("AUTOPLAY region%u at %u,%u size %ux%u obj=%lx id=%lx type=%u slot=%x\n", i, w(p + 12), w(p + 14), w(p + 4), w(p + 6), l(p + 8), l(p + 16), w(p + 20), w(p + 22));
		}
		if(uRep) {
			serLogf("AUTOPLAY   (+%u identical)\n", uRep);
		}
		return true;
	}
	if(eqName(szName, "cursorx") || eqName(szName, "cursory")) {   // the joystick cursor of the town / shop screens (LAB_097F / LAB_0980)
		if(ulValue > 0x13A) {
			return false;
		}
		(szName[6] == 'x' || szName[6] == 'X' ? mogCursorX : mogCursorY) = static_cast<uint16_t>(ulValue);
		return true;
	}
	if(eqName(szName, "who")) {
		if(ulValue >= 5) {
			return false;
		}
		s_ubWho = static_cast<uint8_t>(ulValue);
		return true;
	}
	if(eqName(szName, "day")) {   // the day counter: the dragon spawns on a map start from day 2 (LAB_0DCB)
		mogDayCounter = static_cast<decltype(mogDayCounter)>(ulValue);
		return true;
	}
	if(eqName(szName, "dragon_hit")) {   // put the flying dragon on the current knight and make it the target: the next map frame attacks
		if(!mogDragonActive) {
			return false;
		}
		const Knight *pK = currentKnight();
		mogKnights[4].uwX = static_cast<uint16_t>(pK->uwMapX + 10);   // the scan box is (x - 10, y) (LAB_0074)
		mogKnights[4].uwY = pK->uwMapY;
		mogKnights[4].ulEngagedWith = reinterpret_cast<uint32_t>(pK);
		serLogf("AUTOPLAY dragon_hit at %u,%u\n", static_cast<unsigned>(pK->uwMapX), static_cast<unsigned>(pK->uwMapY));
		return true;
	}
	if(eqName(szName, "rects")) {   // log the dirty-rect count of this frame and the maximum seen by these pokes (fights: the list is capped at 45)
		static uint16_t s_uwMax;
		if(mogRectCount > s_uwMax) {
			s_uwMax = mogRectCount;
		}
		serLogf("AUTOPLAY rects now=%u max=%u\n", static_cast<unsigned>(mogRectCount), static_cast<unsigned>(s_uwMax));
		return true;
	}
	if(eqName(szName, "fightdump")) {   // the fight records (knight 0 and the dragon) and the dirty-rect list in use
		for(unsigned i = 0; i < 5; i += 4) {
			const Knight &k = mogKnights[i];
			serLogf("AUTOPLAY fight rec%u use=%lx x=%u z=%u y=%u face=%u act=%u in=%x type=%x hp=%d idle=%lx\n", i, static_cast<unsigned long>(k.ulActive),
				static_cast<unsigned>(k.uwX), static_cast<unsigned>(k.uwHeight), static_cast<unsigned>(k.uwY), static_cast<unsigned>(k.ubFacing),
				static_cast<unsigned>(k.uwAction), static_cast<unsigned>(k.uwInput), static_cast<unsigned>(k.ubType), static_cast<int>(k.swHp),
				static_cast<unsigned long>(k.ulIdleScript));
		}
		serLogf("AUTOPLAY rectlist cur=%lx A=%lx B=%lx count=%u\n", static_cast<unsigned long>(mogRectList), static_cast<unsigned long>(mogRectListA),
			static_cast<unsigned long>(mogRectListB), static_cast<unsigned>(mogRectCount));
		for(unsigned w = 0; w < 2; ++w) {
			const uint16_t *p = reinterpret_cast<const uint16_t *>(w ? mogRectListB : mogRectListA);
			for(unsigned i = 0; p && i < 50 && p[2] != 0xFFFF; ++i, p += 4) {
				serLogf("AUTOPLAY   rect%c%u x=%u y=%u w=%u h=%u\n", w ? 'B' : 'A', i, static_cast<unsigned>(p[0]), static_cast<unsigned>(p[1]),
					static_cast<unsigned>(p[2]), static_cast<unsigned>(p[3]));
			}
		}
		return true;
	}
	if(eqName(szName, "dump")) {
		dump();
		return true;
	}
	if(eqName(szName, "warp_node")) {
		if(ulValue >= 9 || mogMapNodes[ulValue].swId < 0) {
			return false;
		}
		warp(static_cast<int16_t>(mogMapNodes[ulValue].uwX), static_cast<int16_t>(mogMapNodes[ulValue].uwY));
		return true;
	}
	if(eqName(szName, "warp_lair")) {
		const Lair *pLairs = reinterpret_cast<const Lair *>(mogHeapTable[17]);
		if(!pLairs || ulValue >= 24) {
			return false;
		}
		warp(pLairs[ulValue].swMapX, pLairs[ulValue].swMapY);
		return true;
	}
	if(eqName(szName, "warp_knight")) {   // on top of another knight: the proximity scan then offers the duel
		if(ulValue >= 5) {
			return false;
		}
		warp(static_cast<int16_t>(mogKnights[ulValue].uwMapX), static_cast<int16_t>(mogKnights[ulValue].uwMapY));
		return true;
	}
	for(const Field &f : s_fields) {
		if(eqName(szName, f.szName)) {
			Knight *pK = currentKnight();
			uint8_t *pBase = f.isInv ? reinterpret_cast<uint8_t *>(pK->ulInventory) : reinterpret_cast<uint8_t *>(pK);
			put(pBase + f.ubOfs, f.ubSize, ulValue);
			return true;
		}
	}
	return false;
}

}  // namespace rt

#endif
