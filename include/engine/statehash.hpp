// engine/statehash - the checksum of the network desync check (ROADMAP 12.5 / 12.6, docs/NETPLAY.md section 1 "Desync check").
// Pure and header-only: the standard CRC-32 (IEEE 802.3, reflected, polynomial $EDB88320; "123456789" -> $CBF43926), so the
// Amiga, the PC build and tools/netpeer.py agree.  Only the checksum lives here; the walk over the game state (addresses
// normalised to offsets) is ROADMAP 12.5.
//
// Incremental use:   uint32_t c = crc32Begin();  c = crc32Update(c, p, n);  c = crc32U32(c, v);  hash = crc32End(c);
// One shot:          crc32(p, n).
//
// The table is 16 entries (one nibble at a time): small constant data, no start-up code, and two steps per byte instead of
// eight, which matters for a hash over the whole state on a 68020.
#pragma once
#include <stdint.h>

namespace ms {

namespace statehash_detail {
constexpr uint32_t kNibbleTable[16] = {
	0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
	0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};
}  // namespace statehash_detail

// Start value of a running checksum.
inline uint32_t crc32Begin() { return 0xFFFFFFFFu; }

// Feeds ulLen bytes into a running checksum.
inline uint32_t crc32Update(uint32_t ulCrc, const uint8_t *pData, uint32_t ulLen) {
	for(uint32_t i = 0; i < ulLen; ++i) {
		ulCrc ^= pData[i];
		ulCrc = (ulCrc >> 4) ^ statehash_detail::kNibbleTable[ulCrc & 0x0F];
		ulCrc = (ulCrc >> 4) ^ statehash_detail::kNibbleTable[ulCrc & 0x0F];
	}
	return ulCrc;
}

// Feeds a 32-bit value as four bytes, most significant first (so the hash does not depend on the host byte order).
inline uint32_t crc32U32(uint32_t ulCrc, uint32_t ulValue) {
	const uint8_t aub[4] = {
		static_cast<uint8_t>(ulValue >> 24), static_cast<uint8_t>(ulValue >> 16),
		static_cast<uint8_t>(ulValue >> 8), static_cast<uint8_t>(ulValue),
	};
	return crc32Update(ulCrc, aub, 4);
}

// Final value of a running checksum.
inline uint32_t crc32End(uint32_t ulCrc) { return ulCrc ^ 0xFFFFFFFFu; }

// The checksum of one buffer.
inline uint32_t crc32(const uint8_t *pData, uint32_t ulLen) {
	return crc32End(crc32Update(crc32Begin(), pData, ulLen));
}

}  // namespace ms
