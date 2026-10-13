// engine/tickpoke - see include/engine/tickpoke.hpp.
#include "engine/tickpoke.hpp"

namespace ms {

void tickPokeClear(TickPokeQueue &q) {
	for(unsigned i = 0; i < kTickPokeSlots; ++i) {
		q.aubUsed[i] = 0;
	}
	q.ulSeq = 0;
}

bool tickPokeResolve(uint8_t ubMode, uint32_t ulArg, uint32_t ulNow, uint32_t ulHashTick, uint32_t &ulTick) {
	uint32_t ulBase = 0;
	switch(ubMode) {
		case TPM_ABSOLUTE:
			ulTick = ulArg;
			return true;
		case TPM_RELATIVE:
			if(ulArg == 0) {
				return false;
			}
			ulBase = ulNow;
			break;
		case TPM_HASH:
			if(ulHashTick == kTickPokeNone) {
				return false;
			}
			ulBase = ulHashTick;
			break;
		default:
			return false;
	}
	if(ulBase + ulArg < ulBase) {
		return false;
	}
	ulTick = ulBase + ulArg;
	return true;
}

TickPokePush tickPokePush(TickPokeQueue &q, uint32_t ulTick, uint32_t ulNow, const char *szName, uint32_t ulValue) {
	if(ulTick < ulNow) {
		return TPP_LATE;
	}
	for(unsigned i = 0; i < kTickPokeSlots; ++i) {
		if(q.aubUsed[i]) {
			continue;
		}
		TickPoke &p = q.aPoke[i];
		p.ulTick = ulTick;
		p.ulSeq = q.ulSeq++;
		p.ulValue = ulValue;
		unsigned n = 0;
		for(; szName[n] && n + 1 < kApTextLen; ++n) {
			p.szName[n] = szName[n];
		}
		p.szName[n] = 0;
		q.aubUsed[i] = 1;   // last: the slot is complete
		return TPP_OK;
	}
	return TPP_FULL;
}

TickPokeTake tickPokeTake(TickPokeQueue &q, uint32_t ulNow, TickPoke &sOut) {
	int iBest = -1;
	for(unsigned i = 0; i < kTickPokeSlots; ++i) {
		if(!q.aubUsed[i] || q.aPoke[i].ulTick > ulNow) {
			continue;
		}
		if(iBest < 0 || q.aPoke[i].ulTick < q.aPoke[iBest].ulTick
			|| (q.aPoke[i].ulTick == q.aPoke[iBest].ulTick && q.aPoke[i].ulSeq < q.aPoke[iBest].ulSeq)) {
			iBest = static_cast<int>(i);
		}
	}
	if(iBest < 0) {
		return TPT_NONE;
	}
	sOut = q.aPoke[iBest];
	q.aubUsed[iBest] = 0;
	return sOut.ulTick < ulNow ? TPT_LATE : TPT_DUE;
}

uint32_t tickPokeHashFloor(uint32_t ulVarTick, uint32_t ulInterval) {
	return (ulVarTick / ulInterval + 1) * ulInterval;
}

bool tickPokeHashLine(const char *szLine, uint32_t &ulTick) {
	static const char kKey[] = "hash tick ";
	for(const char *p = szLine; *p; ++p) {
		unsigned k = 0;
		while(kKey[k] && p[k] == kKey[k]) {
			++k;
		}
		if(kKey[k]) {
			continue;
		}
		const char *d = p + k;
		uint32_t ulV = 0;
		unsigned n = 0;
		for(; d[n] >= '0' && d[n] <= '9' && n < 9; ++n) {
			ulV = ulV * 10 + static_cast<uint32_t>(d[n] - '0');
		}
		if(n == 0) {
			return false;
		}
		ulTick = ulV;
		return true;
	}
	return false;
}

// ---- the hits the barriers consume ----------------------------------------------------------------------------------------------------------------------

namespace {

bool isHashPat(const ApHitLog &l, uint8_t ubPat) {
	return (l.aubHashPat[ubPat >> 3] & (1U << (ubPat & 7))) != 0;
}

}  // namespace

void apHitsClear(ApHitLog &l) {
	uint8_t *p = reinterpret_cast<uint8_t *>(&l);
	for(unsigned i = 0; i < sizeof l; ++i) {
		p[i] = 0;
	}
	l.ulNewestHash = kTickPokeNone;
	l.ulVarHeld = kTickPokeNone;
	l.ulBarHash = kTickPokeNone;
}

void apHitsNote(ApHitLog &l, uint8_t ubPat, uint32_t ulHashTick) {
	const uint32_t ulSeq = l.ulSeq + 1;
	ApHit &h = l.aHit[ulSeq % kApHitRing];
	h.ulSeq = ulSeq;
	h.ulHashTick = ulHashTick;
	h.ubPat = ubPat;
	if(ulHashTick != kTickPokeNone) {
		l.aubHashPat[ubPat >> 3] = static_cast<uint8_t>(l.aubHashPat[ubPat >> 3] | (1U << (ubPat & 7)));
		l.ulNewestHash = ulHashTick;
	}
	l.ulSeq = ulSeq;   // last: the hit is complete
}

bool apHitsTake(ApHitLog &l, uint8_t ubPat) {
	const uint32_t ulNewest = l.ulSeq;
	uint32_t ulFrom = l.ulPtr + 1;
	if(ulNewest >= kApHitRing && ulFrom + kApHitRing <= ulNewest) {
		ulFrom = ulNewest - kApHitRing + 1;   // older hits were overwritten
	}
	if(l.ulFloor != 0 && isHashPat(l, ubPat)) {
		// The floor line, exactly: released once a hash line at or after it has been printed, and it counts as the floor line itself.  The ring only decides how
		// much is forgotten (the lines of this pattern up to the floor), never WHICH line the barrier took: a window whose ring lost the line, or that is late
		// by a bar, takes the same one as its peer.
		if(l.ulNewestHash == kTickPokeNone || l.ulNewestHash < l.ulFloor) {
			return false;
		}
		for(uint32_t ulSeq = ulFrom; ulSeq <= ulNewest; ++ulSeq) {
			const ApHit &h = l.aHit[ulSeq % kApHitRing];
			if(h.ulSeq == ulSeq && h.ubPat == ubPat && h.ulHashTick != kTickPokeNone && h.ulHashTick <= l.ulFloor) {
				l.ulPtr = ulSeq;
			}
		}
		l.ulBarHash = l.ulFloor;
		l.ulFloor = 0;
		return true;
	}
	for(uint32_t ulSeq = ulFrom; ulSeq <= ulNewest; ++ulSeq) {
		const ApHit &h = l.aHit[ulSeq % kApHitRing];
		if(h.ulSeq == ulSeq && h.ubPat == ubPat) {
			l.ulPtr = ulSeq;
			l.ulBarHash = h.ulHashTick;   // kTickPokeNone for any other line: no hash base
			return true;
		}
	}
	return false;
}

void apHitsTimedOut(ApHitLog &l) {
	l.ulBarHash = kTickPokeNone;
	l.ulFloor = 0;
	l.ulVarHeld = kTickPokeNone;   // a timed-out wait var (or any wait) leaves no tick for the sync behind it
}

void apHitsVarPassed(ApHitLog &l, uint32_t ulVarTick) {
	l.ulBarHash = kTickPokeNone;
	l.ulFloor = 0;
	l.ulVarHeld = ulVarTick;
}

void apHitsOtherBarrier(ApHitLog &l) {
	l.ulVarHeld = kTickPokeNone;   // only a `sync` RIGHT behind the `wait var` uses its tick (a later sync took a stale floor: classic run, lair pokes anchored to tick 1350)
}

void apHitsSync(ApHitLog &l, uint32_t ulInterval) {
	if(l.ulVarHeld != kTickPokeNone) {   // right behind a `wait var`: keep the hits, the next hash wait takes the first line after that tick
		l.ulFloor = tickPokeHashFloor(l.ulVarHeld, ulInterval);
		l.ulVarHeld = kTickPokeNone;
	}
	else {
		l.ulFloor = 0;
		l.ulPtr = l.ulSeq;
	}
}

}  // namespace ms
