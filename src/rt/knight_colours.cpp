// rt/knight_colours - see rt/knight_colours.hpp.
#include "rt/knight_colours.hpp"

#include "engine/celremap.hpp"
#include "engine/display_fx.hpp"
#include "game/party.hpp"
#include "game/state_bind.hpp"
#include "rt/chipalloc.hpp"
#include "rt/combat_pool.hpp"
#include "rt/fight_roster.hpp"
#include "rt/logwrite.hpp"

extern "C" {
extern uint8_t mogCelSlotsKnight[], mogCelSlotsCreature[];   // LAB_05E1, LAB_05E0: the cel table lists of the jobs
extern ULONG mogBackground;                                   // LAB_05C0: the clean background, five planes of 8000 bytes
}

namespace {

using namespace ms::game;

constexpr ULONG CLONE_TABLES = 3;            // KN1, KN2, KN3 (HE1..HE3 for the creature list): one block, without it the fighter shares a colour
constexpr ULONG BG_PLANE = 8000;
constexpr ULONG PAL_DUEL = 0x0C, PAL_DRAGON = 0x14, PAL_BALOK = 0x30, PAL_TROLL = 0x40;   // palette modes (display_fx sceneColors, the cel loaders' arena kinds)

// The copies live for the whole fight (allocated by Apply, freed by End; the game's RosterScope guard brackets the pair), so no scope of this
// file can hold them.  They are rt::ChipBlock guards in static storage.  A static ChipBlock object would register its destructor with
// __cxa_atexit (not linked here), and its constructor would not run anyway; the zeroed .bss bytes ARE a constructed ChipBlock (no block, size 0),
// so the guards are used in place and partyColoursEnd releases them.  One block per copied set of tables, in the order they were made.
constexpr UBYTE BLOCK_MAX = 24;
alignas(rt::ChipBlock) char s_blockStore[BLOCK_MAX][sizeof(rt::ChipBlock)];
inline rt::ChipBlock &blockAt(UBYTE i) { return *reinterpret_cast<rt::ChipBlock *>(s_blockStore[i]); }
UBYTE s_ubBlocks;

// The two cel lists a fighter draws from.  In a creature scene only the knight list (class colours = palette entries 6..8) is a knight's; in the
// knights' duel (scene $0C) the second fighter draws from the creature list (HE1..HE3) and its class colours are entries 9..11.
constexpr UBYTE GROUP_KNIGHT = 0, GROUP_CREATURE = 1;
constexpr UBYTE SKIN_MAX = 8;

// One set of colours: the copies of one list's tables that its owners (the fighters of that class on that list) draw.
struct Skin {
	UBYTE ubGroup;
	ULONG ulClass;
	ULONG aulOwner[ROSTER_MAX * 2];
	UBYTE ubOwners;
	UBYTE aubEntry[3];
	bool isBorrowed;                                // the class is the other list's palette class: its entries (6..8 / 9..11) are used, none of the free ones
};

inline ULONG *list(uint8_t *p) { return reinterpret_cast<ULONG *>(p); }

// The colour indices the background picture uses (bit i = index i).
ULONG backgroundUsage() {
	const UBYTE *pPlane = reinterpret_cast<const UBYTE *>(mogBackground);
	ULONG ulUsed = 0;
	for(ULONG i = 0; i < BG_PLANE; ++i) {
		const UBYTE b0 = pPlane[i], b1 = pPlane[BG_PLANE + i], b2 = pPlane[2 * BG_PLANE + i], b3 = pPlane[3 * BG_PLANE + i], b4 = pPlane[4 * BG_PLANE + i];
		if((b0 | b1 | b2 | b3 | b4) == 0) {
			ulUsed |= 1;
			continue;
		}
		if(b4 == 0) {
			continue;                               // no plane-4 bit: no index of 16 or more in this byte
		}
		for(UBYTE bit = 0; bit < 8; ++bit) {
			const UBYTE m = static_cast<UBYTE>(0x80u >> bit);
			ulUsed |= 1ul << (((b0 & m) ? 1 : 0) | ((b1 & m) ? 2 : 0) | ((b2 & m) ? 4 : 0) | ((b3 & m) ? 8 : 0) | ((b4 & m) ? 16 : 0));
		}
	}
	return ulUsed;
}

// Registers the copy at ulClone of the table ulTab for every owner of the skin.
void addAliases(FightRoster &ro, const Skin &sk, ULONG ulTab, ULONG ulClone) {
	for(UBYTE o = 0; o < sk.ubOwners; ++o) {
		ro.aAlias[ro.ubAlias].ulClone = ulClone;
		ro.aAlias[ro.ubAlias].ulOrig = ulTab;
		ro.aAlias[ro.ubAlias].ulOwner = sk.aulOwner[o];
		++ro.ubAlias;
	}
}

// One more copy for a skin (best effort: a table that does not fit simply keeps the shared colour).  Only the tables that really
// carry class-colour pixels are copied; a slot a previous arena left behind (no decoded cel there) is skipped.
void cloneExtra(FightRoster &ro, const Skin &sk, ULONG ulTab, const uint8_t aubMap[32], uint8_t ubExtra) {
	if(s_ubBlocks >= BLOCK_MAX || ro.ubAlias + sk.ubOwners > CEL_ALIAS_MAX) {
		return;
	}
	const uint8_t *pCel = reinterpret_cast<const uint8_t *>(ulTab);
	if(!ms::celIsDecoded(pCel, ulTab)) {
		return;
	}
	ULONG ulUsed = 0;
	ms::celColourUsage(pCel, false, &ulUsed);
	if((ulUsed & ms::CEL_CLASS_COLOURS) == 0) {
		return;
	}
	const ULONG ulBytes = (ms::celRemapMaxBytes(pCel) + 3) & ~3ul;
	rt::ChipBlock &blk = blockAt(s_ubBlocks);
	if(!blk.acquire(ulBytes)) {
		logWrite("knight colours: no %lu bytes for the copy of cel set %lx, it keeps the shared colour\n", static_cast<unsigned long>(ulBytes),
			static_cast<unsigned long>(ulTab));
		return;
	}
	void *pBlock = blk.get();
	if(ms::celRemap(pCel, static_cast<uint8_t *>(pBlock), reinterpret_cast<ULONG>(pBlock), ulBytes, aubMap, ubExtra) == 0) {
		blk.release();
		return;
	}
	++s_ubBlocks;
	addAliases(ro, sk, ulTab, reinterpret_cast<ULONG>(pBlock));
}

// The copies of one skin: the first three tables of its list (one block), and for the knight list the other sets the knight figure is drawn
// from.  False (and nothing left behind) when the main block did not fit.
bool buildSkin(FightRoster &ro, const Skin &sk, ULONG ulScene) {
	const ULONG *pKnight = list(mogCelSlotsKnight);
	const ULONG *pCreature = list(mogCelSlotsCreature);
	const ULONG *pTabs = sk.ubGroup == GROUP_KNIGHT ? pKnight : pCreature;
	const UBYTE ubFirst = sk.ubGroup == GROUP_KNIGHT ? 6 : 9;   // the class colours of the list's cels
	uint8_t aubMap[32];
	for(UBYTE i = 0; i < 32; ++i) {
		aubMap[i] = i;
	}
	for(UBYTE i = 0; i < 3; ++i) {
		aubMap[ubFirst + i] = sk.aubEntry[i];
	}
	const uint8_t ubExtra = static_cast<uint8_t>(sk.aubEntry[0] | sk.aubEntry[1] | sk.aubEntry[2]);
	if(s_ubBlocks >= BLOCK_MAX || ro.ubAlias + sk.ubOwners * CLONE_TABLES > CEL_ALIAS_MAX) {
		return false;
	}
	ULONG ulBytes = 0;
	for(ULONG t = 0; t < CLONE_TABLES; ++t) {
		if(!ms::celIsDecoded(reinterpret_cast<const uint8_t *>(pTabs[t]), pTabs[t])) {
			return false;
		}
		ulBytes += (ms::celRemapMaxBytes(reinterpret_cast<const uint8_t *>(pTabs[t])) + 3) & ~3ul;
	}
	rt::ChipBlock &blkMain = blockAt(s_ubBlocks);
	if(!blkMain.acquire(ulBytes)) {
		logWrite("knight colours: no %lu bytes of chip memory, the fighters of class %lu share a colour\n", static_cast<unsigned long>(ulBytes),
			static_cast<unsigned long>(sk.ulClass));
		return false;
	}
	const UBYTE ubBlock0 = s_ubBlocks, ubAlias0 = ro.ubAlias;
	++s_ubBlocks;
	uint8_t *pOut = static_cast<uint8_t *>(blkMain.get());
	ULONG ulLeft = ulBytes;
	for(ULONG t = 0; t < CLONE_TABLES; ++t) {
		const ULONG ulWrote = ms::celRemap(reinterpret_cast<const uint8_t *>(pTabs[t]), pOut, reinterpret_cast<ULONG>(pOut), ulLeft, aubMap, ubExtra);
		if(ulWrote == 0) {
			logWrite("knight colours: copy %lu failed, the fighters of class %lu share a colour\n", static_cast<unsigned long>(t),
				static_cast<unsigned long>(sk.ulClass));
			while(s_ubBlocks > ubBlock0) {
				blockAt(--s_ubBlocks).release();
			}
			ro.ubAlias = ubAlias0;
			return false;
		}
		addAliases(ro, sk, pTabs[t], reinterpret_cast<ULONG>(pOut));
		const ULONG ulStep = (ulWrote + 3) & ~3ul;
		pOut += ulStep;
		ulLeft -= ulStep;
	}
	if(sk.ubGroup != GROUP_KNIGHT) {
		return true;
	}
	// The knight figure also lives outside KN1..KN3: the weapons (KN4), KN5 and the creature sets the knight's scripts switch to ($C0: the ratmen's
	// curse, the Be's grip) or that a creature draws around the knight it holds.  The same colours, the same entries (celSkinCandidates).
	uint32_t aulKn[5], aulCr[5], aulCand[10];
	for(UBYTE i = 0; i < 5; ++i) {
		aulKn[i] = pKnight[i];
		aulCr[i] = pCreature[i];
	}
	uint8_t ubMandatory = 0;                         // (KN1..KN3 are the block above)
	const bool bKn5 = ulScene == PAL_DRAGON || ulScene == PAL_BALOK || ulScene == PAL_TROLL;
	const UBYTE ubCand = ms::celSkinCandidates(aulKn, bKn5, aulCr, aulCand, &ubMandatory);
	(void)ubMandatory;
	for(UBYTE i = 0; i < ubCand; ++i) {
		bool bDone = false;
		for(UBYTE a = 0; a < ro.ubAlias; ++a) {
			bDone = bDone || (ro.aAlias[a].ulOrig == aulCand[i] && ro.aAlias[a].ulOwner == sk.aulOwner[0]);
		}
		if(!bDone) {
			cloneExtra(ro, sk, aulCand[i], aubMap, ubExtra);
		}
	}
	return true;
}

// The class of a fighter record (the long at +54, LAB_0403).
inline ULONG classOf(ULONG ulRecord) { return *reinterpret_cast<const ULONG *>(ulRecord + 54); }

// The list a fighter draws its figure from when the fight starts (its job parameter, set by the arena): the knight list or the creature list.
inline int groupOf(const Knight &k) {
	if(k.ulJobParam == reinterpret_cast<ULONG>(mogCelSlotsKnight)) {
		return GROUP_KNIGHT;
	}
	if(k.ulJobParam == reinterpret_cast<ULONG>(mogCelSlotsCreature)) {
		return GROUP_CREATURE;
	}
	return -1;
}

}  // namespace

namespace rt {

void partyColoursApply(UWORD *puwPalette, ULONG ulScene) {
	FightRoster &ro = fightRoster();
	if(!ro.ubActive || ro.ubParty < 2 || !g_party.active || s_ubBlocks != 0) {
		return;
	}
	ULONG ulUsed = backgroundUsage() & 0xFFFF0000ul;
	const ULONG *pCreature = list(mogCelSlotsCreature);
	for(UBYTE i = 0; i < 5; ++i) {                   // the creature cels' own high colours (blood, effects)
		// A slot a previous fight left behind (a duel loads no creature set, so the dragon's slots are still listed) points at memory the new
		// scene's files may have overwritten: only a decoded cel header is scanned.  The scan follows the plane offsets of the header, so on
		// junk it ran off the end of the heap on the PC (read of a wild address after a dragon fight, then a black-knight duel); the 68k read
		// whatever lay at the wild address.
		if(pCreature[i] != 0 && ms::celIsDecoded(reinterpret_cast<const uint8_t *>(pCreature[i]), pCreature[i])) {
			ms::celColourUsage(reinterpret_cast<const uint8_t *>(pCreature[i]), true, &ulUsed);
		}
	}
	if(ulScene == PAL_DRAGON || ulScene == PAL_BALOK || ulScene == PAL_TROLL) {   // these arenas draw the fifth knight set (KN5: fire and effects in 29..31)
		const ULONG ulKn5 = reinterpret_cast<const ULONG *>(mogCelSlotsKnight)[4];
		if(ulKn5 != 0 && ms::celIsDecoded(reinterpret_cast<const uint8_t *>(ulKn5), ulKn5)) {
			ms::celColourUsage(reinterpret_cast<const uint8_t *>(ulKn5), true, &ulUsed);
		}
	}
	ULONG ulFree = ~ulUsed & 0xFFFF0000ul;           // entries 16..31 nobody draws with
	UBYTE aubFree[16];
	UBYTE ubFound = 0;
	for(WORD i = 31; i >= 16; --i) {
		if(ulFree & (1ul << i)) {
			aubFree[ubFound++] = static_cast<UBYTE>(i);
		}
	}
	if(ubFound < 3) {
		logWrite("knight colours: only %u free entries, the knights share a colour\n", static_cast<unsigned>(ubFound));
		return;
	}

	// Who needs colours of its own.  A creature scene: the second party knight (the first has the scene's class colours in 6..8).  The duel
	// (scene $0C): every fighter whose class differs from the class its list's palette entries hold (the first fighter's class in 6..8 for the
	// knight list, the second fighter's in 9..11 for the creature list), whatever its team is: a black knight that walks onto the party is
	// the first fighter, and the second black knight draws from the same list as the engaged party knight.
	Skin aSkin[SKIN_MAX];
	UBYTE ubSkins = 0;
	UBYTE ubNext = 0;                                // the next unused entry of aubFree
	if(ulScene != PAL_DUEL) {
		Knight &mate = *ro.apParty[1];
		aSkin[0].ubGroup = GROUP_KNIGHT;
		aSkin[0].ulClass = mate.ulKind;
		aSkin[0].aulOwner[0] = ro.aulPartyAddr[1];
		aSkin[0].ubOwners = 1;
		aSkin[0].isBorrowed = false;
		ubSkins = 1;
	}
	else {
		const ULONG aulBase[2] = {classOf(mogActive.ulCurrent), classOf(mogActive.ulOpponent)};
		const UBYTE ubFighters = static_cast<UBYTE>(ro.ubParty + ro.ubFoes);
		for(UBYTE f = 0; f < ubFighters; ++f) {      // the party first: their colours matter most when the entries run out
			const bool bParty = f < ro.ubParty;
			const Knight &k = bParty ? *ro.apParty[f] : *ro.apFoe[f - ro.ubParty];
			const ULONG ulAddr = bParty ? ro.aulPartyAddr[f] : ro.aulFoeAddr[f - ro.ubParty];
			const int iGroup = groupOf(k);
			if(iGroup < 0 || k.ulKind == aulBase[iGroup]) {
				continue;
			}
			UBYTE s = 0;
			while(s < ubSkins && !(aSkin[s].ubGroup == iGroup && aSkin[s].ulClass == k.ulKind)) {
				++s;
			}
			if(s == ubSkins) {
				if(ubSkins >= SKIN_MAX) {
					continue;
				}
				aSkin[s].ubGroup = static_cast<UBYTE>(iGroup);
				aSkin[s].ulClass = k.ulKind;
				aSkin[s].ubOwners = 0;
				aSkin[s].isBorrowed = aulBase[1 - iGroup] == k.ulKind;   // e.g. the second black knight on the creature list while the first fighter (6..8) is a black knight
				if(aSkin[s].isBorrowed) {
					for(UBYTE i = 0; i < 3; ++i) {
						aSkin[s].aubEntry[i] = static_cast<UBYTE>((iGroup == GROUP_KNIGHT ? 9 : 6) + i);
					}
				}
				++ubSkins;
			}
			aSkin[s].aulOwner[aSkin[s].ubOwners++] = ulAddr;
		}
	}

	ro.ubAlias = 0;
	ro.ulSkinOwner = 0;
	for(UBYTE s = 0; s < ubSkins; ++s) {
		if(!aSkin[s].isBorrowed) {
			if(ubNext + 3 > ubFound) {
				logWrite("knight colours: out of free entries, the fighters of class %lu share a colour\n", static_cast<unsigned long>(aSkin[s].ulClass));
				continue;
			}
			for(UBYTE i = 0; i < 3; ++i) {
				aSkin[s].aubEntry[i] = aubFree[ubNext + i];
			}
		}
		if(!buildSkin(ro, aSkin[s], ulScene)) {
			continue;
		}
		if(!aSkin[s].isBorrowed) {
			ubNext = static_cast<UBYTE>(ubNext + 3);
			UWORD auwTriplet[3];
			ms::fighterColors(auwTriplet, static_cast<uint32_t>(aSkin[s].ulClass));
			for(UBYTE i = 0; i < 3; ++i) {
				puwPalette[aSkin[s].aubEntry[i]] = auwTriplet[i];
			}
		}
		for(UBYTE o = 0; o < aSkin[s].ubOwners; ++o) {
			if(aSkin[s].aulOwner[o] == ro.aulPartyAddr[1]) {   // the second party knight: its low-hp warning pulses these entries (game/combat.cpp lowHpWarnings)
				ro.ulSkinOwner = ro.aulPartyAddr[1];
				for(UBYTE i = 0; i < 3; ++i) {
					ro.aubSkinEntry[i] = aSkin[s].aubEntry[i];
				}
			}
		}
		logWrite("knight colours: class %lu (%u fighter%s, %s list) uses palette entries %u %u %u\n", static_cast<unsigned long>(aSkin[s].ulClass),
			static_cast<unsigned>(aSkin[s].ubOwners), aSkin[s].ubOwners == 1 ? "" : "s", aSkin[s].ubGroup == GROUP_KNIGHT ? "knight" : "creature",
			static_cast<unsigned>(aSkin[s].aubEntry[0]), static_cast<unsigned>(aSkin[s].aubEntry[1]), static_cast<unsigned>(aSkin[s].aubEntry[2]));
	}
	if(ulScene != PAL_DUEL && ro.ulSkinOwner != 0) {
		logWrite("knight colours: knight 2 uses palette entries %u %u %u (%u blocks)\n", static_cast<unsigned>(ro.aubSkinEntry[0]),
			static_cast<unsigned>(ro.aubSkinEntry[1]), static_cast<unsigned>(ro.aubSkinEntry[2]), static_cast<unsigned>(s_ubBlocks));
	}
}

void partyColoursEnd() {
	while(s_ubBlocks > 0) {
		blockAt(--s_ubBlocks).release();
	}
}

}  // namespace rt
