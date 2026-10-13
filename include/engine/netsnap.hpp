// engine/netsnap - the byte container of a world snapshot (ROADMAP M12, rejoin, docs/NETPLAY.md 4.5).  Pure and header-only: no ACE, no hardware,
// no globals, no heap.  A snapshot is a flat byte string written field by field in big-endian order (so it is the same on every CPU); the sender
// cuts it into STATE_CHUNKs, the receiver puts the chunks back together (NetSnapRx) and checks the CRC-32 of the whole before it reads a field.
//
//   NetSnapWriter   put8 / put16 / put32 / putBytes into a caller's buffer; a write past the end sets isOk false (nothing is written past it)
//   NetSnapReader   get8 / get16 / get32 / getBytes; a read past the end sets isOk false and returns 0
//   NetSnapRx       the chunks of a snapshot in order: begin(bytes, chunks, crc), put(index, data, len), done() -> true when whole and the CRC matches
//
// What goes into a snapshot is game/net_world (the world image and its codec); this file knows nothing about the game.
#pragma once
#include <stdint.h>

#include "engine/netproto.hpp"
#include "engine/statehash.hpp"

namespace ms {

struct NetSnapWriter {
	uint8_t *pBuf;
	uint32_t ulCap, ulLen;
	bool isOk;
};

inline void snapWriterInit(NetSnapWriter &w, uint8_t *pBuf, uint32_t ulCap) {
	w.pBuf = pBuf;
	w.ulCap = ulCap;
	w.ulLen = 0;
	w.isOk = true;
}
inline void snapPut8(NetSnapWriter &w, uint8_t ub) {
	if(w.isOk && w.ulLen < w.ulCap) {
		w.pBuf[w.ulLen++] = ub;
	} else {
		w.isOk = false;
	}
}
inline void snapPut16(NetSnapWriter &w, uint16_t uw) {
	snapPut8(w, static_cast<uint8_t>(uw >> 8));
	snapPut8(w, static_cast<uint8_t>(uw));
}
inline void snapPut32(NetSnapWriter &w, uint32_t ul) {
	snapPut16(w, static_cast<uint16_t>(ul >> 16));
	snapPut16(w, static_cast<uint16_t>(ul));
}
inline void snapPutBytes(NetSnapWriter &w, const uint8_t *p, uint32_t ulLen) {
	for(uint32_t i = 0; i < ulLen; ++i) {
		snapPut8(w, p[i]);
	}
}

struct NetSnapReader {
	const uint8_t *pBuf;
	uint32_t ulLen, ulPos;
	bool isOk;
};

inline void snapReaderInit(NetSnapReader &r, const uint8_t *pBuf, uint32_t ulLen) {
	r.pBuf = pBuf;
	r.ulLen = ulLen;
	r.ulPos = 0;
	r.isOk = true;
}
inline uint8_t snapGet8(NetSnapReader &r) {
	if(r.isOk && r.ulPos < r.ulLen) {
		return r.pBuf[r.ulPos++];
	}
	r.isOk = false;
	return 0;
}
inline uint16_t snapGet16(NetSnapReader &r) {
	const uint16_t uwHi = snapGet8(r);
	return static_cast<uint16_t>((uwHi << 8) | snapGet8(r));
}
inline uint32_t snapGet32(NetSnapReader &r) {
	const uint32_t ulHi = snapGet16(r);
	return (ulHi << 16) | snapGet16(r);
}
inline void snapGetBytes(NetSnapReader &r, uint8_t *p, uint32_t ulLen) {
	for(uint32_t i = 0; i < ulLen; ++i) {
		p[i] = snapGet8(r);
	}
}

// ---- chunking --------------------------------------------------------------------------------------------------------------

inline uint16_t snapChunkCount(uint32_t ulBytes) {
	return static_cast<uint16_t>((ulBytes + NET_CHUNK_BYTES - 1) / NET_CHUNK_BYTES);
}
inline uint32_t snapChunkOffset(uint16_t uwIndex) {
	return static_cast<uint32_t>(uwIndex) * NET_CHUNK_BYTES;
}
inline uint16_t snapChunkLen(uint32_t ulBytes, uint16_t uwIndex) {
	const uint32_t ulOff = snapChunkOffset(uwIndex);
	if(ulOff >= ulBytes) {
		return 0;
	}
	const uint32_t ulLeft = ulBytes - ulOff;
	return static_cast<uint16_t>(ulLeft < NET_CHUNK_BYTES ? ulLeft : NET_CHUNK_BYTES);
}

// The receiving side.  Chunks must arrive in order (TCP keeps them so); anything else is a protocol error.
struct NetSnapRx {
	uint8_t aubBuf[NET_SNAP_MAX];
	uint32_t ulBytes, ulCrc;
	uint16_t uwChunks, uwNext;
	bool isActive;       // begin() was called
	bool isFailed;       // a chunk was out of order, too long, or the sum does not fit
};

inline void snapRxReset(NetSnapRx &rx) {
	rx.ulBytes = 0;
	rx.ulCrc = 0;
	rx.uwChunks = 0;
	rx.uwNext = 0;
	rx.isActive = false;
	rx.isFailed = false;
}
inline bool snapRxBegin(NetSnapRx &rx, uint32_t ulBytes, uint16_t uwChunks, uint32_t ulCrc) {
	snapRxReset(rx);
	if(ulBytes == 0 || ulBytes > NET_SNAP_MAX || uwChunks != snapChunkCount(ulBytes)) {
		rx.isFailed = true;
		return false;
	}
	rx.ulBytes = ulBytes;
	rx.uwChunks = uwChunks;
	rx.ulCrc = ulCrc;
	rx.isActive = true;
	return true;
}
inline bool snapRxPut(NetSnapRx &rx, uint16_t uwIndex, const uint8_t *pData, uint16_t uwLen) {
	if(!rx.isActive || rx.isFailed || uwIndex != rx.uwNext || uwLen != snapChunkLen(rx.ulBytes, uwIndex)) {
		rx.isFailed = true;
		return false;
	}
	const uint32_t ulOff = snapChunkOffset(uwIndex);
	for(uint16_t i = 0; i < uwLen; ++i) {
		rx.aubBuf[ulOff + i] = pData[i];
	}
	++rx.uwNext;
	return true;
}
// All chunks in and the CRC of the whole equals the one announced.
inline bool snapRxDone(const NetSnapRx &rx, uint16_t uwChunksAtEnd, uint32_t ulCrcAtEnd) {
	return rx.isActive && !rx.isFailed && rx.uwNext == rx.uwChunks && uwChunksAtEnd == rx.uwChunks && ulCrcAtEnd == rx.ulCrc &&
		crc32(rx.aubBuf, rx.ulBytes) == rx.ulCrc;
}

}  // namespace ms
