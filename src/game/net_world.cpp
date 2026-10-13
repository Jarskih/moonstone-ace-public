// game/net_world - see include/game/net_world.hpp (ROADMAP M12, rejoin).  New code, no original label.
//
// One list of fields, three users: the walk below is a template over a "context" that either writes the field (Enc), reads it (Dec) or only folds its
// bytes into a CRC (Crc), so the byte form, the decoder and the hash cannot drift apart.
#include "game/net_world.hpp"

#include "engine/netsnap.hpp"

namespace ms { namespace game {

namespace {

enum Tag : uint32_t { TAG_POOL = 6, TAG_POOL_INV = 7, TAG_NAME = 8, TAG_REGION = 0x10, TAG_OTHER = 0xEE };

uint32_t tagged(uint32_t ulTag, uint32_t ulOffset) {
	return (ulTag << 24) | (ulOffset & 0xFFFFFF);
}

bool inRange(uint32_t ulPtr, uint32_t ulBase, uint32_t ulBytes) {
	return ulBase != 0 && ulPtr >= ulBase && ulPtr - ulBase < ulBytes;
}

// ---- the three contexts ----------------------------------------------------------------------------------------------------

struct Enc {
	static constexpr bool kReads = false;
	NetSnapWriter &w;
	const NetWorldImage &im;
	template<typename T> void v(T &x) {
		if(sizeof(T) == 1) {
			snapPut8(w, static_cast<uint8_t>(x));
		} else if(sizeof(T) == 2) {
			snapPut16(w, static_cast<uint16_t>(x));
		} else {
			snapPut32(w, static_cast<uint32_t>(x));
		}
	}
	void ptr(uint32_t &p) { snapPut32(w, netWorldNormalise(im, p)); }
	void dptr(uint32_t &p) { ptr(p); }
	void bytes(uint8_t *p, uint32_t n) { snapPutBytes(w, p, n); }
	void flag(bool &b) { snapPut8(w, b ? 1 : 0); }
	void fail() {}
};

struct Dec {
	static constexpr bool kReads = true;
	NetSnapReader &r;
	NetWorldImage &im;
	template<typename T> void v(T &x) {
		if(sizeof(T) == 1) {
			x = static_cast<T>(snapGet8(r));
		} else if(sizeof(T) == 2) {
			x = static_cast<T>(snapGet16(r));
		} else {
			x = static_cast<T>(snapGet32(r));
		}
	}
	void ptr(uint32_t &p) { p = netWorldDenormalise(im, snapGet32(r), p); }
	void dptr(uint32_t &p) { ptr(p); }
	void bytes(uint8_t *p, uint32_t n) { snapGetBytes(r, p, n); }
	void flag(bool &b) { b = snapGet8(r) != 0; }
	void fail() { r.isOk = false; }
};

// Folds the bytes the Enc would write into the CRC-32 (so netWorldHash == crc32 of the encoded form).
struct Crc {
	static constexpr bool kReads = false;
	uint32_t ulCrc;
	const NetWorldImage &im;
	void put8(uint8_t ub) { ulCrc = crc32Update(ulCrc, &ub, 1); }
	template<typename T> void v(T &x) {
		if(sizeof(T) == 1) {
			put8(static_cast<uint8_t>(x));
		} else if(sizeof(T) == 2) {
			put8(static_cast<uint8_t>(static_cast<uint16_t>(x) >> 8));
			put8(static_cast<uint8_t>(x));
		} else {
			ulCrc = crc32U32(ulCrc, static_cast<uint32_t>(x));
		}
	}
	void ptr(uint32_t &p) { ulCrc = crc32U32(ulCrc, netWorldNormalise(im, p)); }
	// A pointer to constant data (scripts, tables, names, file names): sent so the receiver can rebuild it, but not part of the hash (it is a function of the
	// build and the creature kind, and a pointer into a hunk the region table does not list is "other" on one machine and known on another).
	void dptr(uint32_t &) { ulCrc = crc32U32(ulCrc, 0); }
	void bytes(uint8_t *p, uint32_t n) { ulCrc = crc32Update(ulCrc, p, n); }
	void flag(bool &b) { put8(b ? 1 : 0); }
	void fail() {}
};

// ---- the fields ------------------------------------------------------------------------------------------------------------

template<class C> void walkKnight(C &c, Knight &k) {
	c.v(k.ulActive);
	c.v(k.uwX);
	c.v(k.uwHeight);
	c.v(k.uwY);
	c.v(k.ubFacing);
	c.v(k.ubInputPort);
	c.v(k.ubAnimPhase);
	c.v(k.ubAnimTimer);
	c.ptr(k.ulHitTarget);
	c.ptr(k.ulAttacker);
	c.dptr(k.ulIdleScript);
	c.dptr(k.ulScript26);
	c.dptr(k.ulHurtScripts);
	c.dptr(k.ulActionScripts);
	c.dptr(k.ulJobParam);
	c.dptr(k.ulDamageTable);
	c.dptr(k.ulWalkScripts);
	c.dptr(k.ulDefenseTable);
	c.v(k.ulKind);
	c.v(k.ulSpare58);
	c.v(k.uwInput);
	c.v(k.uwAction);
	c.v(k.uwTileX);
	c.v(k.uwTileY);
	c.v(k.ubStrength);
	c.v(k.ubConstitution);
	c.v(k.ubEndurance);
	c.v(k.ubLives);
	c.v(k.uwGold);
	c.v(k.ubDaggers);
	c.v(k.ubType);
	c.v(k.uwProgress);
	c.v(k.swHp);
	c.v(k.ubFrogDays);
	c.v(k.ubRecency);
	c.v(k.swHpMax);
	c.v(k.ubDerivedEnd);
	c.v(k.ubDexterity);
	c.v(k.ulSword);
	c.v(k.ulArmour);
	c.ptr(k.ulInventory);
	c.ptr(k.ulEngagedWith);
	c.v(k.ubBehaviourFlags);
	c.v(k.ubBehaviourFlags2);
	c.v(k.ubCooldown);
	c.v(k.ubCooldownLo);
	c.dptr(k.ulName);   // the name TEXT is in the hash (aacName, the name buffers)
	c.bytes(k.ubSpare112, sizeof k.ubSpare112);
	c.v(k.uwReachX);
	c.v(k.uwTooCloseX);
	c.v(k.uwDepthReach);
	c.v(k.uwHitX);
	c.v(k.uwHitY);
	c.v(k.uwMapX);
	c.v(k.uwMapY);
	c.v(k.ubLifeLoss);
	c.v(k.ubTeam);   // the union with ubTargetSlot: one byte
}

template<class C> void walkInventory(C &c, Inventory &inv) {
	c.bytes(reinterpret_cast<uint8_t *>(&inv), sizeof inv);
}

template<class C> void walkLair(C &c, Lair &l) {
	c.ptr(l.ulLoot);
	c.v(l.uwHandlerOfs);
	c.v(l.uwCreaturesLeft);
	c.v(l.uwFlag8);
	c.v(l.swMapX);
	c.v(l.swMapY);
	c.v(l.uwParam14);
	c.dptr(l.ulParam16);

}

// The creature pool: the active slots only, as (count, then index + record each).  The hash walk of net_hash covers the active creatures too, so a
// rejoiner whose slots were filled from this has the host's hash.  A reader first frees every slot of its own: a slot the host does not use is empty.
template<class C> void walkCreatures(C &c, NetStateImage &s) {
	uint8_t ubCount = 0;
	if(!C::kReads) {
		for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) {
			ubCount = static_cast<uint8_t>(ubCount + (s.aCreatures[i].ulActive != 0 ? 1 : 0));
		}
	}
	c.v(ubCount);
	if(C::kReads) {
		for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) {
			s.aCreatures[i].ulActive = 0;
		}
		if(ubCount > NETHASH_CREATURES) {
			c.fail();
			return;
		}
	}
	uint8_t ubSlot = 0;
	for(uint8_t n = 0; n < ubCount; ++n) {
		if(!C::kReads) {
			while(s.aCreatures[ubSlot].ulActive == 0) {
				++ubSlot;
			}
		}
		c.v(ubSlot);
		if(ubSlot >= NETHASH_CREATURES) {
			c.fail();
			return;
		}
		walkKnight(c, s.aCreatures[ubSlot]);
		if(!C::kReads) {
			++ubSlot;
		}
	}
}

template<class C> void walkImage(C &c, NetWorldImage &im) {
	NetStateImage &s = im.st;
	uint32_t ulMagic = NW_MAGIC;
	uint8_t ubVersion = NW_VERSION, ubExtras = NX_COUNT;
	c.v(ulMagic);
	c.v(ubVersion);
	c.v(ubExtras);
	c.v(s.ulTick);
	c.v(s.ulClock);
	c.v(s.ulSeed);
	c.v(s.ulScene);
	c.v(s.ulGore);
	c.ptr(s.ulCurKnight);
	c.ptr(s.ulFirstFighter);
	c.ptr(s.ulOpponent);
	c.ptr(s.ulCurLair);
	c.v(s.uwHumans);
	c.v(s.uwTurnCursor);
	c.v(s.uwMoveSpent);
	c.v(s.uwRoundDone);
	c.v(s.uwTurnBudget);
	c.v(s.uwDay);
	c.v(s.uwLunar);
	c.v(s.uwMoonFrame);
	c.v(s.uwMoonSub);
	c.v(s.ubDefeat);
	c.v(s.ubLocationMode);
	c.v(s.ubFightActive);
	c.flag(s.party.active);
	c.v(s.party.n);
	c.bytes(s.party.aubPad, PARTY_MAX);
	c.v(s.party.ubFocus);
	c.flag(s.party.isNet);
	c.bytes(s.party.aubNode, PARTY_MAX);
	for(uint8_t i = 0; i < NETHASH_KNIGHTS; ++i) {
		walkKnight(c, s.aKnights[i]);
		walkInventory(c, s.aInventories[i]);
		c.bytes(reinterpret_cast<uint8_t *>(s.aacName[i]), NETHASH_NAME);
	}
	walkCreatures(c, s);
	for(uint8_t i = 0; i < NETHASH_LAIRS; ++i) {
		walkLair(c, s.aLairs[i]);
		walkInventory(c, s.aLoot[i]);
	}
	for(uint8_t i = 0; i < NW_NAMES; ++i) {
		c.bytes(im.aubNames[i], NW_NAME_BYTES);
	}
	for(uint8_t i = 0; i < NX_COUNT; ++i) {
		if(netExtraIsPtr(i)) {
			c.ptr(im.aulX[i]);
		} else {
			c.v(im.aulX[i]);
		}
	}
	c.bytes(im.aubPartyMap, sizeof im.aubPartyMap);
	for(uint8_t i = 0; i < NW_POOL; ++i) {
		walkKnight(c, im.aPool[i]);
		walkInventory(c, im.aPoolInv[i]);
	}
}

}  // namespace

bool netExtraIsPtr(uint8_t ubExtra) {
	return (ubExtra >= NX_DRAGON_TOUCH0 && ubExtra <= NX_DRAGON_TOUCH3) || ubExtra == NX_ACTIVE_CURRENT || ubExtra == NX_ROAM_LAIR ||
	       (ubExtra >= NX_OPP_PTR0 && ubExtra <= NX_OPP_PTR3);
}

uint32_t netWorldNormalise(const NetWorldImage &im, uint32_t ulPtr) {
	if(ulPtr == 0) {
		return 0;
	}
	const uint32_t ulArrays = netNormalisePtr(im.st, ulPtr);
	if((ulArrays >> 24) != TAG_OTHER) {
		return ulArrays;
	}
	if(inRange(ulPtr, im.ulPoolBase, NW_POOL * sizeof(Knight))) {
		return tagged(TAG_POOL, ulPtr - im.ulPoolBase);
	}
	if(inRange(ulPtr, im.ulPoolInvBase, NW_POOL * sizeof(Inventory))) {
		return tagged(TAG_POOL_INV, ulPtr - im.ulPoolInvBase);
	}
	for(uint8_t i = 0; i < NW_NAME_TARGETS; ++i) {
		if(im.aulNameTarget[i] != 0 && im.aulNameTarget[i] == ulPtr) {
			return tagged(TAG_NAME, i);
		}
	}
	for(uint8_t i = 0; i < im.ubRegions && i < NW_REGIONS; ++i) {
		if(inRange(ulPtr, im.aulRegionBase[i], im.aulRegionSize[i])) {
			return tagged(TAG_REGION + i, ulPtr - im.aulRegionBase[i]);
		}
	}
	return tagged(TAG_OTHER, 0);
}

uint32_t netWorldDenormalise(const NetWorldImage &im, uint32_t ulNorm, uint32_t ulKeep) {
	if(ulNorm == 0) {
		return 0;
	}
	const uint32_t ulTag = ulNorm >> 24;
	const uint32_t ulOff = ulNorm & 0xFFFFFF;
	uint32_t ulBase = 0;
	switch(ulTag) {
		case 1: ulBase = im.st.ulKnightsBase; break;
		case 2: ulBase = im.st.ulCreaturesBase; break;
		case 3: ulBase = im.st.ulInventoriesBase; break;
		case 4: ulBase = im.st.ulLootBase; break;
		case 5: ulBase = im.st.ulLairBase; break;
		case TAG_POOL: ulBase = im.ulPoolBase; break;
		case TAG_POOL_INV: ulBase = im.ulPoolInvBase; break;
		case TAG_NAME: return ulOff < NW_NAME_TARGETS && im.aulNameTarget[ulOff] != 0 ? im.aulNameTarget[ulOff] : ulKeep;
		default:
			if(ulTag >= TAG_REGION && ulTag < TAG_REGION + im.ubRegions && ulTag - TAG_REGION < NW_REGIONS) {
				ulBase = im.aulRegionBase[ulTag - TAG_REGION];
			}
			break;
	}
	return ulBase != 0 ? ulBase + ulOff : ulKeep;
}

bool netWorldEncode(const NetWorldImage &im, uint8_t *pOut, uint32_t ulCap, uint32_t &ulLen) {
	NetSnapWriter w;
	snapWriterInit(w, pOut, ulCap);
	Enc e = {w, im};
	walkImage(e, const_cast<NetWorldImage &>(im));   // the encoder only reads
	ulLen = w.ulLen;
	return w.isOk;
}

bool netWorldDecode(const uint8_t *pIn, uint32_t ulLen, NetWorldImage &im) {
	NetSnapReader r;
	snapReaderInit(r, pIn, ulLen);
	// the header first: a wrong magic / version / extra count must not touch the image
	if(snapGet32(r) != NW_MAGIC || snapGet8(r) != NW_VERSION || snapGet8(r) != NX_COUNT || !r.isOk) {
		return false;
	}
	snapReaderInit(r, pIn, ulLen);
	Dec d = {r, im};
	walkImage(d, im);
	if(!r.isOk || r.ulPos != ulLen || im.st.party.n > PARTY_MAX) {
		return false;
	}
	return true;
}

uint32_t netWorldHash(const NetWorldImage &im) {
	Crc c = {crc32Begin(), im};
	walkImage(c, const_cast<NetWorldImage &>(im));
	return crc32End(c.ulCrc);
}

bool netWorldFightIdle(const NetWorldImage &im) {
	return im.st.ubFightActive == 0;
}

}}  // namespace ms::game
