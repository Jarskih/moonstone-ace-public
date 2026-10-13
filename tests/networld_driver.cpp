// Host test driver of the world snapshot (ROADMAP M12 rejoin, tests/test_networld.py).  Plain C++ for clang++ (stdio allowed).
// Prints "FAIL <what>" per failed check and "OK <n> checks" at the end; exit code 1 on failure.
//   netsnap     the byte writer / reader (overruns are sticky and write nothing), the chunk arithmetic, the receiving side (order, CRC, lengths)
//   net_world   a random world image at one set of addresses is encoded, decoded over a different image at other addresses and hashes equal (every pointer is
//               rebuilt with the receiver's addresses, "other" pointers keep the receiver's value), the size fits NET_SNAP_MAX, damaged bytes are refused, the
//               hash equals the CRC of the bytes, a changed field (any one of a long list) changes the hash
//   net_rejoin  (game side) the rejoin effects on records, bindings and the party, and the drop queue forgetting a node
#include <stdio.h>
#include <string.h>

#include "engine/netsnap.hpp"
#include "game/net_rejoin.hpp"
#include "game/net_world.hpp"

using namespace ms;
using namespace ms::game;

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}
static char g_what[200];
static void checkf(bool ok, const char *fmt, int a = 0, int b = 0) {
	if(ok) { ++g_checks; return; }
	snprintf(g_what, sizeof g_what, fmt, a, b);
	check(false, g_what);
}

static uint64_t g_rng = 0x2545F4914F6CDD1DULL;
static uint32_t rnd() {
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 7;
	g_rng ^= g_rng << 17;
	return static_cast<uint32_t>(g_rng >> 16);
}
static uint32_t rndN(uint32_t n) { return rnd() % n; }

// ---- netsnap ------------------------------------------------------------------------------------------------------------------------
static void testSnap() {
	uint8_t buf[64];
	NetSnapWriter w;
	snapWriterInit(w, buf, sizeof buf);
	snapPut8(w, 0x12);
	snapPut16(w, 0x3456);
	snapPut32(w, 0x789ABCDE);
	const uint8_t raw[3] = {1, 2, 3};
	snapPutBytes(w, raw, 3);
	check(w.isOk && w.ulLen == 10, "writer: 1 + 2 + 4 + 3 bytes");
	check(buf[0] == 0x12 && buf[1] == 0x34 && buf[2] == 0x56 && buf[3] == 0x78 && buf[6] == 0xDE && buf[7] == 1, "writer: big-endian");
	NetSnapReader r;
	snapReaderInit(r, buf, w.ulLen);
	check(snapGet8(r) == 0x12 && snapGet16(r) == 0x3456 && snapGet32(r) == 0x789ABCDE, "reader: the same values back");
	uint8_t aubBack[3];
	snapGetBytes(r, aubBack, 3);
	check(r.isOk && r.ulPos == 10 && aubBack[2] == 3, "reader: bytes back, at the end");
	check(snapGet8(r) == 0 && !r.isOk, "reader: a read past the end fails and returns 0");
	check(snapGet32(r) == 0 && !r.isOk, "reader: the failure is sticky");

	uint8_t small[4];
	memset(small, 0xEE, sizeof small);
	snapWriterInit(w, small, 3);
	snapPut32(w, 0x01020304);
	check(!w.isOk && small[3] == 0xEE, "writer: an overrun fails and writes nothing past the capacity");
	snapPut8(w, 1);
	check(!w.isOk, "writer: the failure is sticky");

	check(snapChunkCount(1) == 1 && snapChunkCount(NET_CHUNK_BYTES) == 1 && snapChunkCount(NET_CHUNK_BYTES + 1) == 2 && snapChunkCount(NET_SNAP_MAX) == NET_SNAP_MAX / NET_CHUNK_BYTES,
		"chunk count");
	check(snapChunkLen(800, 0) == 384 && snapChunkLen(800, 1) == 384 && snapChunkLen(800, 2) == 32 && snapChunkLen(800, 3) == 0, "chunk lengths of 800 bytes");

	// the receiving side
	static uint8_t blob[NET_SNAP_MAX];
	for(uint32_t i = 0; i < sizeof blob; ++i) blob[i] = static_cast<uint8_t>(rnd());
	static const uint32_t kBytes[] = {1u, 383u, 384u, 385u, 800u, 3000u, NET_SNAP_MAX};
	for(uint32_t bytes : kBytes) {
		static NetSnapRx rx;
		const uint32_t crc = crc32(blob, bytes);
		const uint16_t chunks = snapChunkCount(bytes);
		check(snapRxBegin(rx, bytes, chunks, crc), "rx: begin");
		bool isOk = true;
		for(uint16_t i = 0; i < chunks; ++i) isOk = snapRxPut(rx, i, blob + snapChunkOffset(i), snapChunkLen(bytes, i)) && isOk;
		check(isOk && snapRxDone(rx, chunks, crc) && memcmp(rx.aubBuf, blob, bytes) == 0, "rx: all chunks in order, CRC right, bytes equal");
		check(!snapRxDone(rx, chunks, crc ^ 1), "rx: END with another CRC");
		check(!snapRxDone(rx, static_cast<uint16_t>(chunks + 1), crc), "rx: END with another chunk count");
	}
	{
		static NetSnapRx rx;
		check(!snapRxBegin(rx, 0, 0, 0) && rx.isFailed, "rx: 0 bytes");
		check(!snapRxBegin(rx, NET_SNAP_MAX + 1, snapChunkCount(NET_SNAP_MAX + 1), 0), "rx: above NET_SNAP_MAX");
		check(!snapRxBegin(rx, 800, 2, 0), "rx: a chunk count that does not fit the bytes");
		snapRxBegin(rx, 800, 3, crc32(blob, 800));
		check(!snapRxPut(rx, 1, blob, 384) && rx.isFailed, "rx: a chunk out of order");
		snapRxBegin(rx, 800, 3, crc32(blob, 800));
		snapRxPut(rx, 0, blob, 384);
		check(!snapRxPut(rx, 1, blob + 384, 383) && rx.isFailed, "rx: a chunk of the wrong length");
		snapRxBegin(rx, 800, 3, crc32(blob, 800));
		for(uint16_t i = 0; i < 3; ++i) snapRxPut(rx, i, blob + snapChunkOffset(i), snapChunkLen(800, i));
		rx.aubBuf[100] ^= 1;
		check(!snapRxDone(rx, 3, crc32(blob, 800)), "rx: a flipped bit fails the CRC");
		snapRxBegin(rx, 800, 3, crc32(blob, 800));
		snapRxPut(rx, 0, blob, 384);
		check(!snapRxDone(rx, 3, crc32(blob, 800)), "rx: END before the last chunk");
	}
}

// ---- the world image ---------------------------------------------------------------------------------------------------------------------

// What a pointer field points at in the generated image.
enum PtrKind { PK_NONE, PK_KNIGHT, PK_CREATURE, PK_INVENTORY, PK_LOOT, PK_LAIR, PK_POOL, PK_POOLINV, PK_NAME, PK_REGION, PK_OTHER, PK_KINDS };

struct Layout {   // where the things are in one machine's address space
	uint32_t ulKnights, ulInv, ulCreatures, ulLoot, ulLairs, ulPool, ulPoolInv;
	uint32_t aulName[NW_NAME_TARGETS];
	uint32_t aulRegion[3];
};
static const uint32_t kRegionSize[3] = {5000, 40000, 300};

static Layout layoutAt(uint32_t ulBase) {
	Layout l;
	l.ulKnights = ulBase + 0x1000;
	l.ulInv = ulBase + 0x2000;
	l.ulCreatures = ulBase + 0x3000;
	l.ulLoot = ulBase + 0x6000;
	l.ulLairs = ulBase + 0x7000;
	l.ulPool = ulBase + 0x8000;
	l.ulPoolInv = ulBase + 0x9000;
	for(uint8_t i = 0; i < NW_NAME_TARGETS; ++i) l.aulName[i] = ulBase + 0xA000 + 32 * i;
	l.aulRegion[0] = ulBase + 0x10000;
	l.aulRegion[1] = ulBase + 0x20000;
	l.aulRegion[2] = ulBase + 0x40000;
	return l;
}

static void setBases(NetWorldImage &im, const Layout &l) {
	im.st.ulKnightsBase = l.ulKnights;
	im.st.ulInventoriesBase = l.ulInv;
	im.st.ulCreaturesBase = l.ulCreatures;
	im.st.ulLootBase = l.ulLoot;
	im.st.ulLairBase = l.ulLairs;
	im.ulPoolBase = l.ulPool;
	im.ulPoolInvBase = l.ulPoolInv;
	for(uint8_t i = 0; i < NW_NAME_TARGETS; ++i) im.aulNameTarget[i] = l.aulName[i];
	im.ubRegions = 3;
	for(uint8_t i = 0; i < 3; ++i) {
		im.aulRegionBase[i] = l.aulRegion[i];
		im.aulRegionSize[i] = kRegionSize[i];
	}
}

// A pointer of the kind at the layout (idx picks the element / offset), PK_OTHER = a value in no region.
static uint32_t ptrOf(const Layout &l, PtrKind k, uint32_t idx, uint32_t ulOther) {
	switch(k) {
		case PK_NONE: return 0;
		case PK_KNIGHT: return l.ulKnights + (idx % 5) * sizeof(Knight);
		case PK_CREATURE: return l.ulCreatures + (idx % 20) * sizeof(Knight);
		case PK_INVENTORY: return l.ulInv + (idx % 5) * sizeof(Inventory);
		case PK_LOOT: return l.ulLoot + (idx % 24) * sizeof(Inventory);
		case PK_LAIR: return l.ulLairs + (idx % 24) * sizeof(Lair);
		case PK_POOL: return l.ulPool + (idx % NW_POOL) * sizeof(Knight);
		case PK_POOLINV: return l.ulPoolInv + (idx % NW_POOL) * sizeof(Inventory);
		case PK_NAME: return l.aulName[idx % NW_NAME_TARGETS];
		case PK_REGION: return l.aulRegion[idx % 3] + (idx * 7) % kRegionSize[idx % 3];
		default: return ulOther;
	}
}

// The pointer fields of a record as references.
#define KNIGHT_PTRS(k) {&(k).ulHitTarget, &(k).ulAttacker, &(k).ulIdleScript, &(k).ulScript26, &(k).ulHurtScripts, &(k).ulActionScripts, &(k).ulJobParam, \
	&(k).ulDamageTable, &(k).ulWalkScripts, &(k).ulDefenseTable, &(k).ulInventory, &(k).ulEngagedWith, &(k).ulName}

// The logical content: the same random stream gives the same record content for every layout; the pointer VALUES follow the layout.  ulOther is what an
// "other" pointer holds (a different number per machine).
static void generate(NetWorldImage &im, const Layout &l, uint64_t ullSeed, uint32_t ulOther) {
	g_rng = ullSeed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
	for(int w = 0; w < 16; ++w) rnd();   // warm up: xorshift outputs of related seeds are alike at first
	memset(&im, 0, sizeof im);
	setBases(im, l);
	NetStateImage &s = im.st;
	auto fillBytes = [](void *p, size_t n) { uint8_t *b = static_cast<uint8_t *>(p); for(size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rnd()); };
	auto fillKnight = [&](Knight &k) {
		fillBytes(&k, sizeof k);
		uint32_t *const ap[] = KNIGHT_PTRS(k);
		for(uint32_t *p : ap) {
			const bool isState = p == &k.ulHitTarget || p == &k.ulAttacker || p == &k.ulInventory || p == &k.ulEngagedWith;   // the hash covers these: always known
			const PtrKind kind = static_cast<PtrKind>(rndN(isState ? PK_KINDS - 1 : PK_KINDS));
			*p = ptrOf(l, kind, rnd(), ulOther + (p == &k.ulName ? 4 : 0));
		}

	};
	for(uint8_t i = 0; i < NETHASH_KNIGHTS; ++i) {
		fillKnight(s.aKnights[i]);
		fillBytes(&s.aInventories[i], sizeof(Inventory));
		fillBytes(s.aacName[i], NETHASH_NAME);
	}
	for(uint8_t i = 0; i < NETHASH_LAIRS; ++i) {
		fillBytes(&s.aLairs[i], sizeof(Lair));
		s.aLairs[i].ulLoot = ptrOf(l, rndN(2) ? PK_LOOT : PK_NONE, rnd(), 0);
		s.aLairs[i].ulParam16 = ptrOf(l, rndN(2) ? PK_REGION : PK_OTHER, rnd(), ulOther);
		fillBytes(&s.aLoot[i], sizeof(Inventory));
	}
	// creatures: slots left over from earlier fights are active on the map too (about half of them); an inactive slot's content is not sent
	for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) {
		fillKnight(s.aCreatures[i]);
		s.aCreatures[i].ulActive = rndN(2) != 0 ? 1U + rndN(0x10000) : 0;   // "active" is a flag whatever the pointer value
	}
	s.ulTick = rnd();
	s.ulClock = rnd();
	s.ulSeed = rnd();
	s.ulScene = rndN(40);
	s.ulGore = rndN(2);
	s.ulCurKnight = ptrOf(l, PK_KNIGHT, rnd(), 0);
	s.ulFirstFighter = ptrOf(l, rndN(2) ? PK_KNIGHT : PK_NONE, rnd(), 0);
	s.ulOpponent = ptrOf(l, rndN(2) ? PK_KNIGHT : PK_POOL, rnd(), 0);
	s.ulCurLair = ptrOf(l, PK_LAIR, rnd(), 0);
	s.uwHumans = static_cast<uint16_t>(1 + rndN(4));
	s.uwTurnCursor = static_cast<uint16_t>(rndN(4));
	s.uwMoveSpent = static_cast<uint16_t>(rnd());
	s.uwRoundDone = static_cast<uint16_t>(rnd());
	s.uwTurnBudget = static_cast<uint16_t>(rnd());
	s.uwDay = static_cast<uint16_t>(rnd());
	s.uwLunar = static_cast<uint16_t>(rndN(8));
	s.uwMoonFrame = static_cast<uint16_t>(45 + rndN(5));
	s.uwMoonSub = static_cast<uint16_t>(rndN(4));
	s.ubDefeat = static_cast<uint8_t>(rnd());
	s.ubLocationMode = static_cast<uint8_t>(rnd());
	s.ubFightActive = 0;
	s.party.active = rndN(2) != 0;
	s.party.n = static_cast<uint8_t>(1 + rndN(PARTY_MAX));
	for(uint8_t i = 0; i < PARTY_MAX; ++i) { s.party.aubPad[i] = static_cast<uint8_t>(rndN(6)); s.party.aubNode[i] = static_cast<uint8_t>(rndN(4)); }
	s.party.ubFocus = static_cast<uint8_t>(rnd());
	s.party.isNet = rndN(2) != 0;
	for(uint8_t i = 0; i < NW_NAMES; ++i) fillBytes(im.aubNames[i], NW_NAME_BYTES);
	for(uint8_t i = 0; i < NX_COUNT; ++i) im.aulX[i] = netExtraIsPtr(i) ? ptrOf(l, rndN(2) ? PK_KNIGHT : PK_NONE, rnd(), 0) : rnd();
	fillBytes(im.aubPartyMap, sizeof im.aubPartyMap);
	for(uint8_t i = 0; i < NW_POOL; ++i) { fillKnight(im.aPool[i]); fillBytes(&im.aPoolInv[i], sizeof(Inventory)); }
}

static NetWorldImage g_a, g_b;
static int g_nOther, g_nRegion, g_nActive;
static NetWorldImage g_c;
static uint8_t g_blob[NET_SNAP_MAX + 64];

static void testWorld() {
	const Layout lA = layoutAt(0x00100000), lB = layoutAt(0x00A40000);
	for(int round = 0; round < 60; ++round) {
		const uint64_t seed = 0x1234567ULL * (round + 1);
		generate(g_a, lA, seed, 0xAAAA0001);                 // the host
		// the receiver holds a completely different game of its own (another seed: every record, scalar, extra, pool record and party field differs), at its
		// own addresses, with fights going in its local game
		generate(g_b, lB, seed ^ 0x9E3779B97F4A7C15ULL, 0xBBBB0002);
		for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) g_b.st.aCreatures[i].ulActive = rndN(2);   // the receiver has creatures of its own, in other slots
		check(netWorldHash(g_a) != netWorldHash(g_b), "the receiver's own game differs from the host's");

		Knight aPre[NETHASH_KNIGHTS];
		memcpy(aPre, g_b.st.aKnights, sizeof aPre);
		uint32_t ulLen = 0;
		check(netWorldEncode(g_a, g_blob, NET_SNAP_MAX, ulLen), "encode fits NET_SNAP_MAX");
		checkf(ulLen > 2000 && ulLen <= NET_SNAP_MAX, "the encoded world is %d bytes (limit %d)", static_cast<int>(ulLen), NET_SNAP_MAX);

		const bool isDecoded = netWorldDecode(g_blob, ulLen, g_b);
		check(isDecoded, "decode");
		check(netWorldHash(g_a) == netWorldHash(g_b), "decoded world hashes equal to the sent one (pointers rebuilt on the receiver's addresses)");
		check(netStateHash(g_a.st) == netStateHash(g_b.st), "... and so does net_hash's standard hash of the state part");
		bool isPoolEqual = netWorldFightIdle(g_b);
		for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) {
			const bool isHostActive = g_a.st.aCreatures[i].ulActive != 0;
			isPoolEqual = isPoolEqual && (g_b.st.aCreatures[i].ulActive != 0) == isHostActive;
			g_nActive += isHostActive ? 1 : 0;
			if(isHostActive) {   // the record, pointers rebuilt on the receiver's addresses: the standard hash walks it, so compare through a one-creature image
				isPoolEqual = isPoolEqual && g_b.st.aCreatures[i].uwX == g_a.st.aCreatures[i].uwX && g_b.st.aCreatures[i].swHp == g_a.st.aCreatures[i].swHp;
				const uint32_t ulHostTarget = g_a.st.aCreatures[i].ulHitTarget;
				if(ulHostTarget >= lA.ulCreatures && ulHostTarget < lA.ulCreatures + NETHASH_CREATURES * sizeof(Knight)) {
					isPoolEqual = isPoolEqual && g_b.st.aCreatures[i].ulHitTarget == lB.ulCreatures + (ulHostTarget - lA.ulCreatures);
				}
			}
		}
		check(isPoolEqual, "decode gives the receiver exactly the host's active creature slots (its own were freed)");
		// the decoded image sent on again decodes to the same world (the codec is idempotent)
		static uint8_t blob2[NET_SNAP_MAX + 64];
		uint32_t ulLen2 = 0;
		check(netWorldEncode(g_b, blob2, NET_SNAP_MAX, ulLen2) && ulLen2 == ulLen, "re-encoding the decoded image gives the same size");
		generate(g_c, lA, seed ^ 0x13572468ULL, 0xCCCC0003);
		check(netWorldDecode(blob2, ulLen2, g_c) && netWorldHash(g_c) == netWorldHash(g_a), "... and decoding it elsewhere gives the same world");
		// pointers really are the receiver's: a knight pointer to the record of knight 3 is lB.ulKnights + 3 * sizeof(Knight) wherever the host had it
		bool isRebased = true;
		for(uint8_t i = 0; i < NETHASH_KNIGHTS; ++i) {
			const uint32_t ulHost = g_a.st.aKnights[i].ulInventory;
			const uint32_t ulRecv = g_b.st.aKnights[i].ulInventory;
			if(ulHost >= lA.ulInv && ulHost < lA.ulInv + 5 * sizeof(Inventory)) {
				isRebased = isRebased && ulRecv == lB.ulInv + (ulHost - lA.ulInv);
			}
			const uint32_t ulName = g_a.st.aKnights[i].ulName;
			for(uint8_t t = 0; t < NW_NAME_TARGETS; ++t) {
				if(ulName == lA.aulName[t]) isRebased = isRebased && g_b.st.aKnights[i].ulName == lB.aulName[t];
			}
		}
		// the script / table pointers into a data region keep their offset in the receiver's region
		for(uint8_t i = 0; i < NETHASH_KNIGHTS; ++i) {
			uint32_t *const apH[] = KNIGHT_PTRS(g_a.st.aKnights[i]);
			uint32_t *const apR[] = KNIGHT_PTRS(g_b.st.aKnights[i]);
			for(size_t f = 0; f < sizeof apH / sizeof apH[0]; ++f) {
				for(uint8_t rg = 0; rg < 3; ++rg) {
					if(*apH[f] >= lA.aulRegion[rg] && *apH[f] < lA.aulRegion[rg] + kRegionSize[rg]) {
						isRebased = isRebased && *apR[f] == lB.aulRegion[rg] + (*apH[f] - lA.aulRegion[rg]);
						++g_nRegion;
					}
				}
			}
		}
		check(isRebased, "inventory, name and region pointers point into the receiver's own arrays");
		// a pointer the host has in no known region ("other") leaves the receiver's own value in place
		bool isKept = true;
		int nOther = 0;
		for(uint8_t i = 0; i < NETHASH_KNIGHTS; ++i) {
			uint32_t *const apH[] = KNIGHT_PTRS(g_a.st.aKnights[i]);
			uint32_t *const apR[] = KNIGHT_PTRS(g_b.st.aKnights[i]);
			uint32_t *const apP[] = KNIGHT_PTRS(aPre[i]);
			for(size_t f = 0; f < sizeof apH / sizeof apH[0]; ++f) {
				if(*apH[f] == 0xAAAA0001 || *apH[f] == 0xAAAA0005) {
					isKept = isKept && *apR[f] == *apP[f];
					++nOther;
				}
			}
		}
		check(isKept, "an 'other' pointer keeps the receiver's own value");
		g_nOther += nOther;
	}
	check(g_nOther > 20 && g_nRegion > 20, "the generated worlds contain 'other' and region pointers");

	// pointer normalisation by kind
	generate(g_a, lA, 99, 0xAAAA0001);
	for(int kind = 0; kind < PK_KINDS; ++kind) {
		for(uint32_t idx = 0; idx < 30; ++idx) {
			const uint32_t ulPtr = ptrOf(lA, static_cast<PtrKind>(kind), idx, 0xAAAA0F01);
			const uint32_t ulNorm = netWorldNormalise(g_a, ulPtr);
			generate(g_b, lB, 99, 0xBBBB0001);
			const uint32_t ulBack = netWorldDenormalise(g_b, ulNorm, 0x77770001);
			if(kind == PK_NONE) {
				check(ulNorm == 0 && ulBack == 0, "norm: 0 stays 0");
			} else if(kind == PK_OTHER) {
				check((ulNorm >> 24) == 0xEE && ulBack == 0x77770001, "norm: an unknown pointer is 'other' and the receiver keeps its own");
			} else {
				check(ulBack == ptrOf(lB, static_cast<PtrKind>(kind), idx, 0), "norm: kind round trip onto the other layout");
			}
		}
	}
	// a tag this machine cannot resolve (no such base) keeps the local value
	generate(g_b, lB, 99, 1);
	g_b.ubRegions = 1;
	check(netWorldDenormalise(g_b, (0x12u << 24) | 5, 0x1234) == 0x1234, "a region beyond this machine's table keeps the local value");
	g_b.ulPoolBase = 0;
	check(netWorldDenormalise(g_b, (6u << 24) | 5, 0x4321) == 0x4321, "a base this machine does not have keeps the local value");
}

static void testDamaged() {
	const Layout lA = layoutAt(0x00100000), lB = layoutAt(0x00A40000);
	generate(g_a, lA, 4242, 0xAAAA0001);
	uint32_t ulLen = 0;
	netWorldEncode(g_a, g_blob, NET_SNAP_MAX, ulLen);
	static uint8_t bad[NET_SNAP_MAX + 64];
	generate(g_b, lB, 4242, 0xBBBB0002);
	check(netWorldDecode(g_blob, ulLen, g_b), "baseline decodes");
	check(!netWorldEncode(g_a, bad, ulLen - 1, ulLen) && ulLen > 0, "encode into a buffer one byte short fails");
	netWorldEncode(g_a, g_blob, NET_SNAP_MAX, ulLen);
	generate(g_b, lB, 4242, 0xBBBB0002);
	check(!netWorldDecode(g_blob, ulLen - 1, g_b), "one byte short");
	memcpy(bad, g_blob, ulLen);
	bad[ulLen] = 0;
	check(!netWorldDecode(bad, ulLen + 1, g_b), "a trailing byte");
	memcpy(bad, g_blob, ulLen);
	bad[0] ^= 1;
	check(!netWorldDecode(bad, ulLen, g_b), "wrong magic");
	memcpy(bad, g_blob, ulLen);
	bad[4] ^= 1;
	check(!netWorldDecode(bad, ulLen, g_b), "wrong version");
	memcpy(bad, g_blob, ulLen);
	bad[5] ^= 1;
	check(!netWorldDecode(bad, ulLen, g_b), "wrong extra count (another build's layout)");
	check(!netWorldDecode(g_blob, 0, g_b) && !netWorldDecode(g_blob, 3, g_b), "empty / tiny input");
	// a party size above the maximum is refused
	generate(g_a, lA, 4242, 0xAAAA0001);
	g_a.st.party.n = PARTY_MAX + 3;
	netWorldEncode(g_a, g_blob, NET_SNAP_MAX, ulLen);
	check(!netWorldDecode(g_blob, ulLen, g_b), "party size above PARTY_MAX");
}

// The creature pool at the map turn: slots left active by earlier fights travel with the world (docs/NETPLAY.md 4.5).
static void testCreaturePool() {
	const Layout lA = layoutAt(0x00100000), lB = layoutAt(0x00A40000);
	check(g_nActive > 100, "the generated worlds have active creatures");
	static uint8_t blob[NET_SNAP_MAX + 64];
	uint32_t ulLen = 0;
	// the worst case: every slot active still fits the limit (and a full map-turn world is well inside it)
	generate(g_a, lA, 31337, 0xAAAA0001);
	for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) g_a.st.aCreatures[i].ulActive = 1;
	check(netWorldEncode(g_a, blob, NET_SNAP_MAX, ulLen), "20 active creatures: encode fits NET_SNAP_MAX");
	checkf(ulLen <= NET_SNAP_MAX, "20 active creatures: %d bytes (limit %d)", static_cast<int>(ulLen), NET_SNAP_MAX);
	printf("world image with 20 active creatures: %u bytes of %u\n", static_cast<unsigned>(ulLen), static_cast<unsigned>(NET_SNAP_MAX));
	generate(g_b, lB, 5, 0xBBBB0002);
	check(netWorldDecode(blob, ulLen, g_b) && netWorldHash(g_a) == netWorldHash(g_b) && netStateHash(g_a.st) == netStateHash(g_b.st), "all slots active: round trip");
	// no creature at all: the receiver's own are freed
	generate(g_a, lA, 31337, 0xAAAA0001);
	for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) g_a.st.aCreatures[i].ulActive = 0;
	netWorldEncode(g_a, blob, NET_SNAP_MAX, ulLen);
	generate(g_b, lB, 5, 0xBBBB0002);
	for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) g_b.st.aCreatures[i].ulActive = 1;
	check(netWorldDecode(blob, ulLen, g_b) && netWorldHash(g_a) == netWorldHash(g_b), "empty pool: the receiver's creatures are freed");
	// the creatures change the hash, and an inactive slot's content does not
	generate(g_a, lA, 31337, 0xAAAA0001);
	const uint32_t ulHash = netWorldHash(g_a);
	uint8_t ubActive = 0, ubIdle = 0;
	while(g_a.st.aCreatures[ubActive].ulActive == 0) ++ubActive;
	while(g_a.st.aCreatures[ubIdle].ulActive != 0) ++ubIdle;
	g_a.st.aCreatures[ubActive].swHp ^= 1;
	check(netWorldHash(g_a) != ulHash, "an active creature's hit points are in the world hash");
	g_a.st.aCreatures[ubActive].swHp ^= 1;
	g_a.st.aCreatures[ubIdle].swHp ^= 1;
	check(netWorldHash(g_a) == ulHash, "an inactive slot's content is not");
	g_a.st.aCreatures[ubIdle].ulActive = 1;
	check(netWorldHash(g_a) != ulHash, "a slot becoming active is");
	// damaged pool section: count above 20, slot index above 19.  The count byte is the first byte where a world with no creature differs from one with
	// exactly one (slot 3); the index byte follows it.
	static uint8_t blobNone[NET_SNAP_MAX + 64], bad[NET_SNAP_MAX + 64];
	uint32_t ulLenNone = 0;
	generate(g_a, lA, 31337, 0xAAAA0001);
	for(uint8_t i = 0; i < NETHASH_CREATURES; ++i) g_a.st.aCreatures[i].ulActive = i == 3 ? 1 : 0;
	netWorldEncode(g_a, blob, NET_SNAP_MAX, ulLen);
	g_a.st.aCreatures[3].ulActive = 0;
	netWorldEncode(g_a, blobNone, NET_SNAP_MAX, ulLenNone);
	uint32_t ulPos = 0;
	while(ulPos < ulLenNone && blob[ulPos] == blobNone[ulPos]) ++ulPos;
	check(ulPos < ulLenNone && blob[ulPos] == 1 && blob[ulPos + 1] == 3, "the creature section is (count, slot, record)");
	generate(g_b, lB, 5, 0xBBBB0002);
	check(netWorldDecode(blob, ulLen, g_b) && g_b.st.aCreatures[3].ulActive != 0, "baseline: one creature in slot 3");
	memcpy(bad, blob, ulLen);
	bad[ulPos] = NETHASH_CREATURES + 1;
	check(!netWorldDecode(bad, ulLen, g_b), "a creature count above 20 is refused");
	memcpy(bad, blob, ulLen);
	bad[ulPos + 1] = NETHASH_CREATURES;
	check(!netWorldDecode(bad, ulLen, g_b), "a creature slot above 19 is refused");
}

// Every field of the image matters to the hash and survives the round trip (one change at a time).
static void testFieldsMatter() {
	const Layout lA = layoutAt(0x00100000), lB = layoutAt(0x00A40000);
	generate(g_a, lA, 777, 0xAAAA0001);
	const uint32_t ulBase = netWorldHash(g_a);
	uint32_t ulLen = 0;
	netWorldEncode(g_a, g_blob, NET_SNAP_MAX, ulLen);
	// walk the encoded bytes: flipping any single byte of the blob changes what the receiver ends up with (or is refused)
	int nSame = 0, nTried = 0;
	static uint8_t flip[NET_SNAP_MAX + 64];
	for(uint32_t i = 0; i < ulLen; i += 7) {
		memcpy(flip, g_blob, ulLen);
		flip[i] ^= 0x10;
		generate(g_b, lB, 777, 0xBBBB0002);
		++nTried;
		if(netWorldDecode(flip, ulLen, g_b) && netWorldHash(g_b) == ulBase) {
			++nSame;   // a flipped tag byte of a pointer makes it "other": the receiver keeps its own, which hashes alike
		}
	}
	checkf(nSame <= nTried / 3, "a flipped byte of the blob changed nothing in %d of %d tries (only a pointer tag or a creature field the hash leaves out may)", nSame, nTried);

	// the extras: every id has its own place in the hash
	for(uint8_t x = 0; x < NX_COUNT; ++x) {
		generate(g_b, lA, 777, 0xAAAA0001);
		g_b.aulX[x] ^= netExtraIsPtr(x) ? 0 : 1;
		if(netExtraIsPtr(x)) g_b.aulX[x] = g_b.aulX[x] == 0 ? lA.ulKnights : 0;
		checkf(netWorldHash(g_b) != ulBase, "extra %d is part of the hash");
	}
	// the pieces outside net_hash's state part
	generate(g_b, lA, 777, 0xAAAA0001); g_b.aubNames[2][5] ^= 1;
	check(netWorldHash(g_b) != ulBase && netStateHash(g_b.st) == netStateHash(g_a.st), "a name buffer byte is in the world hash only");
	generate(g_b, lA, 777, 0xAAAA0001); g_b.aubPartyMap[2] ^= 1;
	check(netWorldHash(g_b) != ulBase, "the party map is in the hash");
	generate(g_b, lA, 777, 0xAAAA0001); g_b.aPool[3].swHp ^= 1;
	check(netWorldHash(g_b) != ulBase, "a pool record is in the hash");
	generate(g_b, lA, 777, 0xAAAA0001); g_b.aPoolInv[0].ubKeys ^= 1;
	check(netWorldHash(g_b) != ulBase, "a pool inventory is in the hash");
	generate(g_b, lA, 777, 0xAAAA0001); g_b.st.aKnights[4].ubSpare112[2] ^= 1;
	check(netWorldHash(g_b) != ulBase, "the spare bytes of a record are in the hash");
	generate(g_b, lA, 777, 0xAAAA0001); g_b.st.aKnights[1].ulIdleScript = 0;
	check(netWorldHash(g_b) == ulBase, "a script pointer of a record is NOT in the hash (a build constant)");
	generate(g_b, lA, 777, 0xAAAA0001); g_b.st.aKnights[1].ulEngagedWith = g_b.st.aKnights[1].ulEngagedWith == 0 ? lA.ulKnights : 0;
	check(netWorldHash(g_b) != ulBase, "a record reference IS in the hash");

	generate(g_b, lA, 777, 0xAAAA0001); g_b.st.aLoot[7].ubKeys ^= 1;
	check(netWorldHash(g_b) != ulBase && netStateHash(g_b.st) != netStateHash(g_a.st), "lair loot");
	// the same world at other addresses hashes equal (the whole point)
	generate(g_b, lB, 777, 0xBBBB0002);
	check(netWorldHash(g_b) == ulBase, "the same world at other addresses has the same hash");
}

// The rejoin at a map turn (found in the first two-window run: `net: desync at tick 3000`, knight 3, a black knight, one tile off).  The first frame of
// the next turn (mapScreenEnter: setup turn, then MS_MOVE) moves the knight whose turn begins by LAB_0656, the move word the previous turn's last frame
// left behind, and the black knight's AI reads its walk cells, roam lair, opponent scratch and shop wish.  All of that is game state that the 50 tick
// hash does not walk: the host holds the previous turn's values, a rejoiner holding its own (0) steps differently.  The world must carry it.
static void testMapTurnInputs() {
	const Layout lA = layoutAt(0x00100000), lB = layoutAt(0x00A40000);
	static const uint8_t aubInputs[] = {NX_AI_MOVE, NX_AI_WALK0, NX_AI_WALK1, NX_AI_WALK2, NX_AI_WALK3, NX_AI_WALK4, NX_AI_WALK5, NX_AI_WALK6, NX_ROAM_LAIR,
	                                    NX_ROAM_HIDDEN, NX_SHOP_COST, NX_SHOP_KIND, NX_SHOP_ITEM, NX_STEP_GATE, NX_TOWN_DIST1, NX_TOWN_DIST2, NX_OPP_DIST0,
	                                    NX_OPP_DIST1, NX_OPP_DIST2, NX_OPP_DIST3, NX_OPP_PTR0, NX_OPP_PTR1, NX_OPP_PTR2, NX_OPP_PTR3};
	generate(g_a, lA, 4242, 0xAAAA0001);
	for(uint8_t i = 0; i < sizeof aubInputs; ++i) {
		g_a.aulX[aubInputs[i]] = netExtraIsPtr(aubInputs[i]) ? 0 : 0x11 + i;
	}
	g_a.aulX[NX_AI_MOVE] = 0x0009;                                      // the previous turn ended moving right and up (LAB_0656)
	g_a.aulX[NX_ROAM_LAIR] = lA.ulLairs + 3 * sizeof(Lair);             // the roam lair is a pointer into the host's lair array
	g_a.aulX[NX_OPP_PTR2] = lA.ulKnights + 2 * sizeof(Knight);
	uint32_t ulLen = 0;
	check(netWorldEncode(g_a, g_blob, NET_SNAP_MAX, ulLen), "turn inputs: encode");
	generate(g_b, lB, 4243, 0xBBBB0002);                                // the rejoiner: a fresh local game, its own cells (stale move word 0)
	g_b.aulX[NX_AI_MOVE] = 0;
	check(netWorldDecode(g_blob, ulLen, g_b), "turn inputs: decode");
	check(g_b.aulX[NX_AI_MOVE] == 0x0009, "the stale move word of the previous turn (the first MS_MOVE of the next turn) reaches the rejoiner");
	bool isSame = true;
	for(uint8_t i = 0; i < sizeof aubInputs; ++i) {
		if(!netExtraIsPtr(aubInputs[i])) {
			isSame = isSame && g_b.aulX[aubInputs[i]] == g_a.aulX[aubInputs[i]];
		}
	}
	check(isSame, "every turn input cell of the map AI (walk, opponent scratch, shop wish, town distances ...) is the host's");
	check(g_b.aulX[NX_ROAM_LAIR] == lB.ulLairs + 3 * sizeof(Lair), "the roam lair pointer is rebased onto the rejoiner's lair array");
	check(g_b.aulX[NX_OPP_PTR2] == lB.ulKnights + 2 * sizeof(Knight), "the opponent record pointer is rebased onto the rejoiner's records");
	check(netWorldHash(g_a) == netWorldHash(g_b), "the worlds hash equal");
	// and each of them is part of the world hash, so a rejoin that missed one is refused instead of desyncing later
	const uint32_t ulBase = netWorldHash(g_a);
	for(uint8_t i = 0; i < sizeof aubInputs; ++i) {
		generate(g_c, lA, 4242, 0xAAAA0001);
		memcpy(g_c.aulX, g_a.aulX, sizeof g_c.aulX);
		g_c.aulX[aubInputs[i]] = netExtraIsPtr(aubInputs[i]) ? (g_c.aulX[aubInputs[i]] == 0 ? lA.ulKnights : 0) : g_c.aulX[aubInputs[i]] ^ 1;
		checkf(netWorldHash(g_c) != ulBase, "turn input %d is in the world hash", static_cast<int>(aubInputs[i]));
	}
}

// ---- the game side ----------------------------------------------------------------------------------------------------------------------
static void testRejoinEffects() {
	// classic: node 2's knight (record 2) is an AI knight after its drop; the rejoin gives it the portrait of its node back
	NetBinding aOrig[NET_KNIGHTS_MAX], aBind[NET_KNIGHTS_MAX];
	for(uint8_t k = 0; k < 4; ++k) { aOrig[k].ubNode = k; aOrig[k].ubPad = PAD_JOY1; aOrig[k].ubSlot = k; aBind[k] = aOrig[k]; }
	aBind[1].ubNode = NET_NODE_DROPPED;
	aBind[2].ubNode = NET_NODE_DROPPED;
	Knight aK[5];
	memset(aK, 0, sizeof aK);
	for(uint8_t i = 0; i < 5; ++i) aK[i].ulKind = i;
	aK[1].ulKind = KIND_AI;
	aK[2].ulKind = KIND_AI;
	aK[2].swHp = 7;
	const uint8_t aPortrait[NET_NODE_MAX] = {3, 0, 1, 2};
	NetDropGame g;
	g.isCoop = false;
	g.ubPlayers = 3;
	g.isFightActive = false;
	check(netRejoinApply(aBind, aOrig, aK, g, aPortrait, 2, nullptr) == 1, "classic: one knight given back");
	check(aK[2].ulKind == 1 && aBind[2].ubNode == 2 && aBind[2].ubPad == PAD_JOY1, "classic: the knight of node 2 is human again (its portrait) and bound to it");
	check(aK[2].swHp == 7, "classic: nothing else of the record changed (the host's state wins)");
	check(aK[1].ulKind == KIND_AI && aBind[1].ubNode == NET_NODE_DROPPED, "classic: the other dropped player is still away");
	check(netRejoinApply(aBind, aOrig, aK, g, aPortrait, 2, nullptr) == 0, "classic: applying twice changes nothing");
	aK[3].ulKind = KIND_AI;   // a black knight beyond the players: never given a human kind
	netRejoinApply(aBind, aOrig, aK, g, aPortrait, 3, nullptr);
	check(aK[3].ulKind == KIND_AI, "classic: a record beyond the players stays a black knight");

	// co-op: after the host played on alone the party is local; node 1's members go back to node 1 on their own controllers
	PartyConfig p;
	partyStart(p, 4);
	for(uint8_t m = 0; m < 4; ++m) { aOrig[m].ubNode = static_cast<uint8_t>(m % 2); aOrig[m].ubPad = static_cast<uint8_t>(m == 1 ? PAD_KEYS_WASD : PAD_JOY1); }
	p.isNet = false;
	for(uint8_t m = 0; m < 4; ++m) { p.aubNode[m] = 0; p.aubPad[m] = static_cast<uint8_t>(m); }
	g.isCoop = true;
	g.ubPlayers = 4;
	check(netRejoinApply(aBind, aOrig, aK, g, aPortrait, 1, &p) == 2, "co-op: two members go back");
	check(p.isNet && p.aubNode[1] == 1 && p.aubNode[3] == 1 && p.aubNode[0] == 0 && p.aubNode[2] == 0, "co-op: the party is bound to the nodes again, node 1's members are on node 1");
	check(p.aubPad[1] == PAD_KEYS_WASD && p.aubPad[3] == PAD_JOY1, "co-op: with the controllers their player had picked");
	check(p.aubPad[0] == 0 && p.aubPad[2] == 2, "co-op: the host's members keep their controllers");
	PartyConfig off;
	partyReset(off);
	check(partyNetRejoin(off, aOrig, 1) == 0 && !off.isNet, "a classic party is not touched");

	NetDropQueue q;
	netDropQueueInit(q);
	NetDropEvent ev = {10, 2};
	netDropQueueAdd(q, ev);
	NetBinding aB2[NET_KNIGHTS_MAX] = {aOrig[0], aOrig[1], aOrig[2], aOrig[3]};
	Knight aK2[5];
	memset(aK2, 0, sizeof aK2);
	for(uint8_t i = 0; i < 5; ++i) aK2[i].ulKind = i;
	g.isCoop = false;
	netDropQueueDue(q, aB2, aK2, g, 10, 0);
	check((q.ubApplied & 4) != 0, "the drop of node 2 is applied");
	netDropQueueForget(q, 2);
	netDropQueueAdd(q, ev);
	check(q.ubCount == 1, "after the rejoin the node can be dropped again");
}

int main() {
	testSnap();

	testWorld();
	testDamaged();
	testCreaturePool();
	testFieldsMatter();
	testMapTurnInputs();
	testRejoinEffects();
	if(g_fail) {
		printf("%d of %d checks FAILED\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}
