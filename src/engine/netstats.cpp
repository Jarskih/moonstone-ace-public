// engine/netstats - see include/engine/netstats.hpp (ROADMAP M12).  New code, no original label.
#include "engine/netstats.hpp"

namespace ms {

namespace {

void clearBucket(NetLinkStats &s, uint8_t ubIndex) {
	s.auwFresh[ubIndex] = 0;
	s.auwCovered[ubIndex] = 0;
}

// Moves the newest bucket forward to the one that holds ulNow; the buckets it passes are empty.  A gap of the whole window or more starts afresh.
void rotate(NetLinkStats &s, uint32_t ulNow) {
	if(!s.isBucketed) {
		s.isBucketed = true;
		s.ulBucketMs = ulNow;
		s.ubHead = 0;
		clearBucket(s, 0);
		return;
	}
	uint32_t ulElapsed = ulNow - s.ulBucketMs;
	if(static_cast<int32_t>(ulElapsed) < 0) {
		return;   // the clock went back (a caller with a stale time): keep counting in the newest bucket
	}
	if(ulElapsed >= NS_LOSS_BUCKET_MS * NS_LOSS_BUCKETS) {
		for(uint8_t i = 0; i < NS_LOSS_BUCKETS; ++i) {
			clearBucket(s, i);
		}
		s.ulBucketMs = ulNow;
		return;
	}
	while(ulElapsed >= NS_LOSS_BUCKET_MS) {
		s.ubHead = static_cast<uint8_t>((s.ubHead + 1) % NS_LOSS_BUCKETS);
		clearBucket(s, s.ubHead);
		s.ulBucketMs += NS_LOSS_BUCKET_MS;
		ulElapsed -= NS_LOSS_BUCKET_MS;
	}
}

void appendText(char (&acOut)[NS_TEXT_MAX], uint8_t &ubPos, const char *sz) {
	for(; *sz && ubPos + 1 < NS_TEXT_MAX; ++sz) {
		acOut[ubPos++] = *sz;
	}
	acOut[ubPos] = 0;
}

void appendNumber(char (&acOut)[NS_TEXT_MAX], uint8_t &ubPos, uint32_t ulValue) {
	char ac[11];
	uint8_t ubLen = 0;
	do {
		ac[ubLen++] = static_cast<char>('0' + ulValue % 10);
		ulValue /= 10;
	} while(ulValue != 0 && ubLen < sizeof ac);
	while(ubLen != 0 && ubPos + 1 < NS_TEXT_MAX) {
		acOut[ubPos++] = ac[--ubLen];
	}
	acOut[ubPos] = 0;
}

}  // namespace

void netStatsInit(NetLinkStats &s) {
	uint8_t *p = reinterpret_cast<uint8_t *>(&s);
	for(uint32_t i = 0; i < sizeof s; ++i) {
		p[i] = 0;
	}
}

void netStatsPingSent(NetLinkStats &s, uint32_t ulToken, uint32_t ulNow) {
	s.ulToken = ulToken;
	s.ulSentMs = ulNow;
	s.isPingOut = true;
}

bool netStatsPong(NetLinkStats &s, uint32_t ulToken, uint32_t ulNow) {
	if(!s.isPingOut || ulToken != s.ulToken) {
		return false;
	}
	s.isPingOut = false;
	const int32_t lSample16 = static_cast<int32_t>(ulNow - s.ulSentMs) * 16;
	if(lSample16 < 0) {
		return false;
	}
	if(!s.hasRtt) {
		s.hasRtt = true;
		s.ulRttX16 = static_cast<uint32_t>(lSample16);
	} else {
		const int32_t lDelta = (lSample16 - static_cast<int32_t>(s.ulRttX16)) / (1 << NS_RTT_SHIFT);
		s.ulRttX16 = static_cast<uint32_t>(static_cast<int32_t>(s.ulRttX16) + lDelta);
	}
	return true;
}

bool netStatsRttMs(const NetLinkStats &s, uint32_t &ulMs) {
	ulMs = s.hasRtt ? (s.ulRttX16 + 8) / 16 : 0;
	return s.hasRtt;
}

void netStatsTickIn(NetLinkStats &s, uint32_t ulNow, bool isCovered) {
	rotate(s, ulNow);
	uint16_t &uwCount = isCovered ? s.auwCovered[s.ubHead] : s.auwFresh[s.ubHead];
	if(uwCount != 0xFFFF) {
		++uwCount;
	}
}

uint8_t netStatsLossPct(NetLinkStats &s, uint32_t ulNow) {
	rotate(s, ulNow);
	uint32_t ulFresh = 0, ulCovered = 0;
	for(uint8_t i = 0; i < NS_LOSS_BUCKETS; ++i) {
		ulFresh += s.auwFresh[i];
		ulCovered += s.auwCovered[i];
	}
	const uint32_t ulTotal = ulFresh + ulCovered;
	if(ulTotal < NS_LOSS_MIN_TICKS) {
		return 0;
	}
	return static_cast<uint8_t>((ulCovered * 100 + ulTotal / 2) / ulTotal);
}

void netStatsWorst(const NetStatsLink *pLinks, uint8_t ubCount, uint32_t ulNow, NetStatsView &out) {
	out.isValid = false;
	out.uwPingMs = 0;
	out.isUdp = true;
	out.ubLossPct = 0;
	bool isAny = false;
	for(uint8_t k = 0; k < ubCount; ++k) {
		if(pLinks[k].pStats == nullptr) {
			continue;
		}
		isAny = true;
		NetLinkStats &s = *pLinks[k].pStats;
		uint32_t ulMs = 0;
		if(netStatsRttMs(s, ulMs)) {
			out.isValid = true;
			const uint16_t uwMs = ulMs > NS_PING_MAX_MS ? NS_PING_MAX_MS : static_cast<uint16_t>(ulMs);
			if(uwMs > out.uwPingMs) {
				out.uwPingMs = uwMs;
			}
		}
		if(pLinks[k].isUdpActive) {
			const uint8_t ubLoss = netStatsLossPct(s, ulNow);
			if(ubLoss > out.ubLossPct) {
				out.ubLossPct = ubLoss;
			}
		} else {
			out.isUdp = false;
		}
	}
	if(!isAny) {
		out.isValid = false;
		out.isUdp = false;
	}
}

uint8_t netStatsFormat(const NetStatsView &v, char (&acOut)[NS_TEXT_MAX]) {
	uint8_t ubPos = 0;
	acOut[0] = 0;
	if(!v.isValid) {
		return 0;
	}
	appendText(acOut, ubPos, "PING ");
	appendNumber(acOut, ubPos, v.uwPingMs);
	appendText(acOut, ubPos, "MS  ");
	if(v.isUdp) {
		appendText(acOut, ubPos, "LOSS ");
		appendNumber(acOut, ubPos, v.ubLossPct);
		appendText(acOut, ubPos, " PCT");
	} else {
		appendText(acOut, ubPos, "TCP");
	}
	appendText(acOut, ubPos, "  F10");   // the key that hides the line
	return ubPos;
}

}  // namespace ms
