// rt/knight_colours - see rt/knight_colours.hpp.
#include "rt/knight_colours.hpp"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>

#include "engine/celremap.hpp"
#include "engine/display_fx.hpp"
#include "game/party.hpp"
#include "game/state_bind.hpp"
#include "rt/combat_pool.hpp"
#include "rt/fight_roster.hpp"
#include "rt/amiga/serlog.hpp"
#include "rt/guards.hpp"

extern "C" {
extern uint8_t mogCelSlotsKnight[], mogCelSlotsCreature[];   // LAB_05E1, LAB_05E0: the cel table lists of the jobs
extern ULONG mogBackground;                                   // LAB_05C0: the clean background, five planes of 8000 bytes
}

namespace {

using namespace ms::game;

constexpr ULONG CLONE_TABLES = 3;            // KN1, KN2, KN3: one block, without it the knights share a colour
constexpr UBYTE EXTRA_MAX = 7;               // further tables (KN4, KN5, the creature sets that carry a knight figure), one block each, best effort
constexpr ULONG BG_PLANE = 8000;
constexpr ULONG PAL_DRAGON = 0x14, PAL_BALOK = 0x30, PAL_TROLL = 0x40;   // palette modes (display_fx sceneColors, the cel loaders' arena kinds)

// The copies live for the whole fight (allocated by Apply, freed by End; the game's RosterScope guard brackets the pair), so no scope of this
// file can hold them.  They are rt::MemBlock guards in static storage.  A static MemBlock object would register its destructor with
// __cxa_atexit (not linked here), and its constructor would not run anyway; the zeroed .bss bytes ARE a constructed MemBlock (no block, size 0),
// so the guards are used in place and partyColoursEnd releases them.
alignas(rt::MemBlock) char s_blockStore[sizeof(rt::MemBlock)];                      // KN1..KN3
alignas(rt::MemBlock) char s_extraStore[EXTRA_MAX][sizeof(rt::MemBlock)];          // the extra copies
inline rt::MemBlock &blockMain() { return *reinterpret_cast<rt::MemBlock *>(s_blockStore); }
inline rt::MemBlock &blockExtra(UBYTE i) { return *reinterpret_cast<rt::MemBlock *>(s_extraStore[i]); }
UBYTE s_ubExtra;

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

// One more copy for the second knight (best effort: a table that does not fit simply keeps the shared colour).  Only the tables that really
// carry class-colour pixels are copied; a slot a previous arena left behind (no decoded cel there) is skipped.
void cloneExtra(FightRoster &ro, ULONG ulTab, const uint8_t aubMap[32], uint8_t ubExtra) {
	if(s_ubExtra >= EXTRA_MAX || ro.ubAlias >= ms::game::CEL_ALIAS_MAX) {
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
	rt::MemBlock &blk = blockExtra(s_ubExtra);
	if(!blk.acquire(ulBytes, MEMF_CHIP)) {
		logWrite("knight colours: no %lu bytes for the copy of cel set %lx, it keeps the shared colour\n", static_cast<unsigned long>(ulBytes),
			static_cast<unsigned long>(ulTab));
		return;
	}
	void *pBlock = blk.get();
	if(ms::celRemap(pCel, static_cast<uint8_t *>(pBlock), reinterpret_cast<ULONG>(pBlock), ulBytes, aubMap, ubExtra) == 0) {
		blk.release();
		return;
	}

	++s_ubExtra;
	ro.aAlias[ro.ubAlias].ulClone = reinterpret_cast<ULONG>(pBlock);
	ro.aAlias[ro.ubAlias].ulOrig = ulTab;
	++ro.ubAlias;
}

}  // namespace

namespace rt {

void partyColoursApply(UWORD *puwPalette, ULONG ulScene) {
	FightRoster &ro = fightRoster();
	if(!ro.ubActive || ro.ubParty < 2 || !g_party.active || blockMain()) {
		return;
	}
	ULONG ulUsed = backgroundUsage() & 0xFFFF0000ul;
	const ULONG *pCreature = list(mogCelSlotsCreature);
	for(UBYTE i = 0; i < 5; ++i) {                   // the creature cels' own high colours (blood, effects)
		if(pCreature[i] != 0) {
			ms::celColourUsage(reinterpret_cast<const uint8_t *>(pCreature[i]), true, &ulUsed);
		}
	}
	if(ulScene == PAL_DRAGON || ulScene == PAL_BALOK || ulScene == PAL_TROLL) {   // these arenas draw the fifth knight set (KN5: fire and effects in 29..31)
		const ULONG ulKn5 = reinterpret_cast<const ULONG *>(mogCelSlotsKnight)[4];
		if(ulKn5 != 0) {
			ms::celColourUsage(reinterpret_cast<const uint8_t *>(ulKn5), true, &ulUsed);
		}
	}
	ULONG ulFree = ~ulUsed & 0xFFFF0000ul;           // entries 16..31 nobody draws with
	UBYTE aubFree[3];
	UBYTE ubFound = 0;
	for(WORD i = 31; i >= 16 && ubFound < 3; --i) {
		if(ulFree & (1ul << i)) {
			aubFree[ubFound++] = static_cast<UBYTE>(i);
		}
	}
	if(ubFound < 3) {
		logWrite("knight colours: only %u free entries, the knights share a colour\n", static_cast<unsigned>(ubFound));
		return;
	}
	uint8_t aubMap[32];
	for(UBYTE i = 0; i < 32; ++i) {
		aubMap[i] = i;
	}
	aubMap[6] = aubFree[0];                          // the class colours of the knight cels: 6, 7, 8
	aubMap[7] = aubFree[1];
	aubMap[8] = aubFree[2];
	const uint8_t ubExtra = static_cast<uint8_t>(aubFree[0] | aubFree[1] | aubFree[2]);
	const ULONG *pKnight = list(mogCelSlotsKnight);
	ULONG ulBytes = 0;
	for(ULONG t = 0; t < CLONE_TABLES; ++t) {
		ulBytes += (ms::celRemapMaxBytes(reinterpret_cast<const uint8_t *>(pKnight[t])) + 3) & ~3ul;
	}
	if(!blockMain().acquire(ulBytes, MEMF_CHIP)) {
		logWrite("knight colours: no %lu bytes of chip memory, the knights share a colour\n", static_cast<unsigned long>(ulBytes));
		return;
	}
	uint8_t *pOut = static_cast<uint8_t *>(blockMain().get());
	ULONG ulLeft = ulBytes;
	ro.ubAlias = 0;
	for(ULONG t = 0; t < CLONE_TABLES; ++t) {
		const ULONG ulWrote = ms::celRemap(reinterpret_cast<const uint8_t *>(pKnight[t]), pOut, reinterpret_cast<ULONG>(pOut), ulLeft, aubMap, ubExtra);
		if(ulWrote == 0) {
			logWrite("knight colours: copy %lu failed, the knights share a colour\n", static_cast<unsigned long>(t));
			partyColoursEnd();
			ro.ubAlias = 0;
			return;
		}
		ro.aAlias[ro.ubAlias].ulClone = reinterpret_cast<ULONG>(pOut);
		ro.aAlias[ro.ubAlias].ulOrig = pKnight[t];
		++ro.ubAlias;
		const ULONG ulStep = (ulWrote + 3) & ~3ul;
		pOut += ulStep;
		ulLeft -= ulStep;
	}
	// The knight figure also lives outside KN1..KN3: the weapons (KN4), KN5 and the creature sets the knight's scripts switch to ($C0: the ratmen's
	// curse, the Be's grip) or that a creature draws around the knight it holds.  The same colours, the same entries (celSkinCandidates).
	{
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
				bDone = bDone || ro.aAlias[a].ulOrig == aulCand[i];
			}
			if(!bDone) {
				cloneExtra(ro, aulCand[i], aubMap, ubExtra);
			}
		}
	}
	Knight &mate = *ro.apParty[1];                   // the second knight: its list, and its job (made before the colours were)
	for(UBYTE i = 0; i < 3; ++i) {
		ro.aubSkinEntry[i] = aubFree[i];                 // its low-hp warning pulses these entries (game/combat.cpp lowHpWarnings)
	}
	ro.ulSkinOwner = reinterpret_cast<ULONG>(&mate);   // its draws (game/combat_script.cpp drawEvent) take the copies, whatever frame set a script selects
	UWORD auwTriplet[3];
	ms::fighterColors(auwTriplet, mate.ulKind);
	for(UBYTE i = 0; i < 3; ++i) {
		puwPalette[aubFree[i]] = auwTriplet[i];
	}
	logWrite("knight colours: knight 2 uses palette entries %u %u %u (copy %lu bytes)\n", static_cast<unsigned>(aubFree[0]),
		static_cast<unsigned>(aubFree[1]), static_cast<unsigned>(aubFree[2]), static_cast<unsigned long>(ulBytes));
}

void partyColoursEnd() {
	for(UBYTE i = 0; i < s_ubExtra; ++i) {
		blockExtra(i).release();
	}
	s_ubExtra = 0;
	blockMain().release();
}

}  // namespace rt
