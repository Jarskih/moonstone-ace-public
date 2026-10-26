// Host test driver of engine/netstats (ROADMAP M12, tests/test_netstats.py): the ping and loss numbers of the on-screen link line.
// Plain C++ for clang++ (stdio allowed).  Prints "FAIL <what>" per failed check and "OK <n> checks" at the end; exit code 1 on failure.
//   ping     PING / PONG tokens: a sample per matching PONG, an old or unknown token ignored, the EWMA (first sample taken whole, then 1/4 of the difference), rounding
//   loss     the one-second window: percent of covered ticks, too few ticks = 0, old seconds fall out, a long silence starts afresh, a stale clock is harmless
//   worst    the worst link of several: highest ping, highest loss, TCP as soon as one link is not on UDP, nothing in the session / no ping yet = invalid
//   format   the text: "PING 38MS  LOSS 2 PCT", "PING 38MS  TCP", the clamp, empty when invalid, only characters the small font has
#include <stdio.h>
#include <string.h>

#include "engine/netstats.hpp"

using namespace ms;

static int g_fail, g_checks;
static void check(bool ok, const char *what) {
	++g_checks;
	if(!ok) {
		++g_fail;
		printf("FAIL %s\n", what);
	}
}

static uint32_t rttOf(const NetLinkStats &s) {
	uint32_t ulMs = 0;
	netStatsRttMs(s, ulMs);
	return ulMs;
}

static void sampleRttFirst(NetLinkStats &s, uint32_t ulMs) {
	static uint32_t ulToken = 100;
	netStatsPingSent(s, ++ulToken, 0);
	netStatsPong(s, ulToken, ulMs);
}

static void pingTests() {
	NetLinkStats s;
	netStatsInit(s);
	uint32_t ulMs = 99;
	check(!netStatsRttMs(s, ulMs) && ulMs == 0, "no sample yet: no round trip");
	check(!netStatsPong(s, 1, 100), "a PONG without a PING is ignored");
	netStatsPingSent(s, 7, 1000);
	check(!netStatsPong(s, 6, 1040), "a PONG of another token is ignored");
	check(!netStatsRttMs(s, ulMs), "... and takes no sample");
	check(netStatsPong(s, 7, 1040), "the PONG of the PING in flight gives a sample");
	check(rttOf(s) == 40, "the first sample is taken whole (40 ms)");
	check(!netStatsPong(s, 7, 1050), "the same PONG twice counts once");
	netStatsPingSent(s, 8, 2000);
	check(netStatsPong(s, 8, 2080), "second sample");
	check(rttOf(s) == 50, "EWMA 1/4: 40 + (80 - 40) / 4 = 50");
	netStatsPingSent(s, 9, 3000);
	netStatsPong(s, 9, 3010);
	check(rttOf(s) == 40, "EWMA falls again: 50 + (10 - 50) / 4 = 40");
	// a newer PING replaces the one in flight: the late PONG of the old one is not a sample
	netStatsPingSent(s, 10, 4000);
	netStatsPingSent(s, 11, 4500);
	check(!netStatsPong(s, 10, 4600), "the PONG of a replaced PING is ignored");
	check(netStatsPong(s, 11, 4620), "the PONG of the newest PING counts");
	check(rttOf(s) == 60, "EWMA 40 + (120 - 40) / 4 = 60");
	// steady line: converges to the value
	NetLinkStats t;
	netStatsInit(t);
	uint32_t ulClock = 0;
	for(uint32_t i = 0; i < 40; ++i) {
		netStatsPingSent(t, i + 1, ulClock);
		netStatsPong(t, i + 1, ulClock + 123);
		ulClock += 1000;
	}
	check(rttOf(t) == 123, "a steady 123 ms link reads 123 ms");
	// the shown ms are rounded, not cut: 10 ms then 13 ms smooths to 10 + 3/4 = 10.75
	NetLinkStats r;
	netStatsInit(r);
	sampleRttFirst(r, 10);
	sampleRttFirst(r, 13);
	check(rttOf(r) == 11, "10 then 13 ms: 10.75, shown as 11");
	// a clock that went back is no sample
	NetLinkStats u;
	netStatsInit(u);
	netStatsPingSent(u, 1, 5000);
	check(!netStatsPong(u, 1, 4000), "a PONG before its PING (clock went back) is no sample");
}

static void feed(NetLinkStats &s, uint32_t ulNow, uint32_t ulFresh, uint32_t ulCovered) {
	for(uint32_t i = 0; i < ulFresh; ++i) {
		netStatsTickIn(s, ulNow, false);
	}
	for(uint32_t i = 0; i < ulCovered; ++i) {
		netStatsTickIn(s, ulNow, true);
	}
}

static void lossTests() {
	NetLinkStats s;
	netStatsInit(s);
	check(netStatsLossPct(s, 0) == 0, "nothing counted: 0");
	feed(s, 1000, 9, 1);
	check(netStatsLossPct(s, 1000) == 0, "10 ticks are too few to judge: 0");
	feed(s, 1100, 85, 5);
	check(netStatsLossPct(s, 1100) == 6, "6 of 100 ticks covered: 6 percent");
	NetLinkStats a;
	netStatsInit(a);
	feed(a, 0, 50, 0);
	check(netStatsLossPct(a, 0) == 0, "clean link: 0");
	feed(a, 0, 0, 25);
	check(netStatsLossPct(a, 0) == 33, "25 of 75: 33 (rounded)");
	NetLinkStats b;
	netStatsInit(b);
	feed(b, 0, 0, 40);
	check(netStatsLossPct(b, 0) == 100, "everything covered: 100");
	// the window: five one-second buckets
	NetLinkStats w;
	netStatsInit(w);
	feed(w, 0, 0, 50);          // second 0: heavy loss
	feed(w, 1000, 50, 0);       // second 1: clean
	check(netStatsLossPct(w, 1000) == 50, "two seconds in the window: 50 of 100");
	feed(w, 2000, 50, 0);
	feed(w, 3000, 50, 0);
	feed(w, 4000, 50, 0);
	check(netStatsLossPct(w, 4000) == 20, "five seconds in the window: 50 of 250 = 20");
	feed(w, 5000, 50, 0);
	check(netStatsLossPct(w, 5000) == 0, "second 0 fell out of the window: clean again");
	// a long silence: afresh
	NetLinkStats z;
	netStatsInit(z);
	feed(z, 0, 0, 60);
	check(netStatsLossPct(z, 0) == 100, "before the silence");
	check(netStatsLossPct(z, 60000) == 0, "a minute later nothing is left of it");
	feed(z, 60000, 100, 0);
	check(netStatsLossPct(z, 60500) == 0, "and counting goes on cleanly");
	// an uneven gap: only whole buckets rotate
	NetLinkStats g;
	netStatsInit(g);
	feed(g, 0, 0, 30);
	feed(g, 2500, 30, 0);
	check(netStatsLossPct(g, 2500) == 50, "a gap of 2.5 s keeps the first second in the window");
	check(netStatsLossPct(g, 4999) == 50, "... until 5 s after it began (4999 ms)");
	check(netStatsLossPct(g, 5000) == 0, "... and drops it at 5000 ms (bucket 0 gone, bucket 2 holds the clean ticks)");
	// a stale clock does not wreck the window
	NetLinkStats c;
	netStatsInit(c);
	feed(c, 5000, 40, 10);
	feed(c, 4000, 10, 0);
	check(netStatsLossPct(c, 5000) == 17, "a clock that went back keeps counting in the newest second (10 of 60)");
	// the counters saturate instead of wrapping
	NetLinkStats m;
	netStatsInit(m);
	for(uint32_t i = 0; i < 70000; ++i) {
		netStatsTickIn(m, 0, true);
	}
	for(uint32_t i = 0; i < 60000; ++i) {
		netStatsTickIn(m, 0, false);
	}
	check(netStatsLossPct(m, 0) == 52, "70000 covered and 60000 fresh ticks in a second: the covered counter saturates (65535 of 125535), it does not wrap");
}

static NetStatsLink linkOf(NetLinkStats *p, bool isUdp) {
	NetStatsLink l;
	l.pStats = p;
	l.isUdpActive = isUdp;
	return l;
}

static void sampleRtt(NetLinkStats &s, uint32_t ulMs) {
	netStatsPingSent(s, 1, 0);
	netStatsPong(s, 1, ulMs);
}

static void worstTests() {
	NetStatsView v;
	NetStatsLink aNone[3] = {linkOf(nullptr, false), linkOf(nullptr, false), linkOf(nullptr, false)};
	netStatsWorst(aNone, 3, 0, v);
	check(!v.isValid, "no link in the session: nothing to show");
	NetLinkStats a, b, c;
	netStatsInit(a);
	netStatsInit(b);
	netStatsInit(c);
	NetStatsLink aOne[3] = {linkOf(&a, true), linkOf(nullptr, false), linkOf(nullptr, false)};
	netStatsWorst(aOne, 3, 0, v);
	check(!v.isValid, "a link without a PONG yet: nothing to show");
	sampleRtt(a, 38);
	netStatsWorst(aOne, 3, 0, v);
	check(v.isValid && v.uwPingMs == 38 && v.isUdp && v.ubLossPct == 0, "one UDP link: 38 ms, no loss");
	// three links
	sampleRtt(b, 90);
	sampleRtt(c, 55);
	feed(a, 0, 90, 10);
	feed(b, 0, 98, 2);
	feed(c, 0, 0, 0);
	NetStatsLink aAll[3] = {linkOf(&a, true), linkOf(&b, true), linkOf(&c, true)};
	netStatsWorst(aAll, 3, 0, v);
	check(v.isValid && v.uwPingMs == 90, "the worst ping of the links");
	check(v.isUdp && v.ubLossPct == 10, "the worst loss of the links");
	// one link on TCP: the line says TCP
	NetStatsLink aMixed[3] = {linkOf(&a, true), linkOf(&b, false), linkOf(&c, true)};
	netStatsWorst(aMixed, 3, 0, v);
	check(v.isValid && !v.isUdp, "one link on TCP: the node is on TCP");
	check(v.uwPingMs == 90, "the ping still counts every link");
	// only the TCP link has a ping: still shown
	NetLinkStats d, e;
	netStatsInit(d);
	netStatsInit(e);
	sampleRtt(e, 20);
	NetStatsLink aHalf[2] = {linkOf(&d, true), linkOf(&e, true)};
	netStatsWorst(aHalf, 2, 0, v);
	check(v.isValid && v.uwPingMs == 20, "the links that have a ping count, the others wait");
	// the clamp
	NetLinkStats f;
	netStatsInit(f);
	sampleRtt(f, 5000);
	NetStatsLink aSlow[1] = {linkOf(&f, true)};
	netStatsWorst(aSlow, 1, 0, v);
	check(v.uwPingMs == NS_PING_MAX_MS, "a ping beyond three digits is clamped");
}

static bool isSmallFontChar(char ch) {
	return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ';
}

static void formatTests() {
	char ac[NS_TEXT_MAX];
	NetStatsView v;
	v.isValid = true;
	v.uwPingMs = 38;
	v.isUdp = true;
	v.ubLossPct = 2;
	check(netStatsFormat(v, ac) == 26 && strcmp(ac, "PING 38MS  LOSS 2 PCT  F10") == 0, "udp line");
	v.isUdp = false;
	check(netStatsFormat(v, ac) == 19 && strcmp(ac, "PING 38MS  TCP  F10") == 0, "tcp line");
	v.isUdp = true;
	v.uwPingMs = 0;
	v.ubLossPct = 0;
	check(strcmp((netStatsFormat(v, ac), ac), "PING 0MS  LOSS 0 PCT  F10") == 0, "zeros");
	v.uwPingMs = 999;
	v.ubLossPct = 100;
	check(strcmp((netStatsFormat(v, ac), ac), "PING 999MS  LOSS 100 PCT  F10") == 0, "widest line");
	check(strlen(ac) < NS_TEXT_MAX, "the widest line fits");
	v.isValid = false;
	check(netStatsFormat(v, ac) == 0 && ac[0] == 0, "invalid: empty");
	bool isOk = true;
	for(uint32_t ping = 0; ping <= 1000; ping += 37) {
		for(uint32_t loss = 0; loss <= 100; loss += 7) {
			v.isValid = true;
			v.uwPingMs = static_cast<uint16_t>(ping);
			v.ubLossPct = static_cast<uint8_t>(loss);
			for(int udp = 0; udp < 2; ++udp) {
				v.isUdp = udp != 0;
				netStatsFormat(v, ac);
				for(const char *p = ac; *p; ++p) {
					isOk = isOk && isSmallFontChar(*p);
				}
			}
		}
	}
	check(isOk, "only upper case letters, digits and spaces (no '%', ':', '/' or '-')");
}

int main() {
	pingTests();
	lossTests();
	worstTests();
	formatTests();
	if(g_fail) {
		printf("%d of %d checks failed\n", g_fail, g_checks);
		return 1;
	}
	printf("OK %d checks\n", g_checks);
	return 0;
}
