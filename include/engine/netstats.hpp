// engine/netstats - the numbers of the on-screen link line of a network game: ping and packet loss (ROADMAP M12, docs/NETPLAY.md 4.7).  Pure: no sockets, no
// globals, no heap, no clock of its own; the node (engine/netnode) feeds it the clock and the events it already sees.
//
//   ping   the round trip of the keepalive PING / PONG.  The PING carries a token and the PONG echoes it (protocol 6 as it is: no protocol change), so the
//          node remembers when it sent token N to a link and the PONG of N gives the sample.  The samples are smoothed (EWMA, weight 1/4).
//   loss   of the datagrams of the per-tick traffic (engine/netudp), per link.  Every tick a datagram brings is counted once: FRESH when it arrived as the
//          newest tick of its datagram (nothing earlier was missing), COVERED when it arrived new but behind a newer tick of the same datagram (the history
//          repaired an earlier datagram that was lost, or came late).  loss = covered / (fresh + covered) over the last NS_LOSS_BUCKETS seconds.  A tick that
//          never arrives by datagram (the link is stalled: TCP carries it) is not counted here; the link going silent shows in the ping instead.
//   line   the worst link of the node (host: over its clients; client: its link to the host): the highest ping, the highest loss, "TCP" as soon as one link is
//          not on UDP, formatted in the small font's character set (upper case; no '%', ':', '/' or '-': the glyphs are missing or blank).
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint32_t NS_PING_MS = 1000;            // a node PINGs its peers this often while the session runs
constexpr uint8_t NS_RTT_SHIFT = 2;              // the EWMA weight of a new sample is 1 / 2^NS_RTT_SHIFT
constexpr uint32_t NS_LOSS_BUCKET_MS = 1000;     // the loss window is made of one-second buckets ...
constexpr uint8_t NS_LOSS_BUCKETS = 5;           // ... this many of them (the newest, still filling, included)
constexpr uint16_t NS_LOSS_MIN_TICKS = 25;       // fewer ticks than this in the window: no judgement, loss 0
constexpr uint16_t NS_PING_MAX_MS = 999;         // shown ping is clamped to three digits
constexpr uint8_t NS_TEXT_MAX = 32;              // the text with its terminator

struct NetLinkStats {
	// ping
	uint32_t ulToken;                // the token of the PING in flight (valid while isPingOut)
	uint32_t ulSentMs;
	bool isPingOut;
	bool hasRtt;
	uint32_t ulRttX16;               // the smoothed round trip in 1/16 ms
	// loss window
	bool isBucketed;                 // ulBucketMs is valid
	uint32_t ulBucketMs;             // start of the newest bucket
	uint8_t ubHead;                  // index of the newest bucket
	uint16_t auwFresh[NS_LOSS_BUCKETS];
	uint16_t auwCovered[NS_LOSS_BUCKETS];
};

void netStatsInit(NetLinkStats &s);

// A PING with ulToken went to the peer at ulNow.  A newer PING replaces the one in flight (its PONG is the sample that counts).
void netStatsPingSent(NetLinkStats &s, uint32_t ulToken, uint32_t ulNow);
// A PONG echoed ulToken at ulNow.  True when it answered the PING in flight (a sample was taken); an old or unknown token changes nothing.
bool netStatsPong(NetLinkStats &s, uint32_t ulToken, uint32_t ulNow);
// The smoothed round trip in ms (rounded); 0 and false before the first sample.
bool netStatsRttMs(const NetLinkStats &s, uint32_t &ulMs);

// A tick arrived in a datagram for the first time: isCovered = a newer tick of the same datagram was there as well (see the top).
void netStatsTickIn(NetLinkStats &s, uint32_t ulNow, bool isCovered);
// The loss of the window in percent (0..100), rounded; 0 while the window holds fewer than NS_LOSS_MIN_TICKS ticks.
uint8_t netStatsLossPct(NetLinkStats &s, uint32_t ulNow);

// ---- the line --------------------------------------------------------------------------------------------------------------------
struct NetStatsView {
	bool isValid;                    // a ping is known: there is something to show
	uint16_t uwPingMs;               // the worst smoothed ping (clamped to NS_PING_MAX_MS)
	bool isUdp;                      // every link of the session carries its ticks in datagrams
	uint8_t ubLossPct;               // the worst loss (only meaningful with isUdp)
};

// One link as the node sees it.
struct NetStatsLink {
	NetLinkStats *pStats;            // null: the link is not in the session
	bool isUdpActive;
};

// The worst link of up to ubCount.  Nothing in the session, or no ping yet (the first second): isValid false.
void netStatsWorst(const NetStatsLink *pLinks, uint8_t ubCount, uint32_t ulNow, NetStatsView &out);
// "PING 38MS  LOSS 2 PCT" or "PING 38MS  TCP"; empty when !isValid.  Returns the length.
uint8_t netStatsFormat(const NetStatsView &v, char (&acOut)[NS_TEXT_MAX]);

}  // namespace ms
