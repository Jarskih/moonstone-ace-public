// engine/artcheck - pure checks for the art/ override loader (ROADMAP 5.2a, docs/ART.md "Trying your art in the game").
// No ACE, no OS, no STL, no globals: builds for the host tests as well as the Amiga. rt/files.cpp feeds it the head of
// a candidate file; it says whether the name may be overridden and whether the file is 6-plane (tools/artconv.py
// --planes 6), which the 5-plane game must not be given before MS_ENHANCED (ROADMAP 4.8a).
#pragma once
#include <stdint.h>

namespace ms {

// A name the override loader accepts: non-empty, at most 30 characters (an AmigaDOS file name), and no path part
// ('/' or ':'), so only art/<name> can ever be opened. "." and ".." are refused.
bool artNameIsPlain(const char *name);

// ASCII case-insensitive name comparison (AmigaDOS folds case); true when equal.
bool artNameEqual(const char *a, const char *b);

// Case-folded 32-bit FNV-1a hash of a name, for the "log once per file" table.
uint32_t artNameHash(const char *name);

enum ArtKind : uint8_t {
	ART_OTHER = 0,    // not a picture/cel as artconv writes them (audio, tables, ...): no plane check possible
	ART_PICTURE = 1,  // BE16 planes (4..6), BE32 packed size, 1<<planes palette words, LZSS body; or a pack of them
	ART_CEL = 2,      // BE16 frames, BE32 packed size, BE32 bits, frames x 10-byte records (b9 = plane bits), LZSS body
};

struct ArtVerdict {
	ArtKind kind;
	bool sixPlane;    // picture with plane count 6, or any cel frame with plane bit $20
	bool truncated;   // cel frame table did not fit in the head given: unknown, caller should fall back
	uint32_t packed;  // BE32 packed size of the (first) picture or of the cel, 0 for ART_OTHER
};

// The game reads message.piv and ch.piv raw into fixed regions (program LAB_0051 / mog LAB_012C, LAB_0129: counts
// 4326, 3694 and 9718 originally, 24576 in enhanced mode) and decodes them from there; a larger file would run over
// the region behind it. Returns the largest size a replacement may have, 0 for any other file (no limit known here).
// message.piv is read by both overlays: the smaller of the two counts (mog's 3694) applies to the original game.
uint32_t artMaxSize(const char *name, bool isEnhanced);

// The game reads the packed body of a .cel / .ob / .f file into a fixed buffer before it unpacks it (program LAB_052A,
// mog LAB_0D4F: 10311 longwords). A redrawn cel whose packed size is larger would overwrite what follows it.
// Enhanced mode reads into a bigger carved buffer instead (kEnhCelReadBufferBytes, enhcarve.hpp, ROADMAP 4.8b).
constexpr uint32_t kCelReadBufferBytes = 41244;
// The cap in force: kCelReadBufferBytes, or the enhanced carve's buffer.
uint32_t celReadBufferBytes(bool isEnhanced);

// 'head' = the first headLen bytes of the file (the whole file if it is small), fileSize = its total size.
ArtVerdict artClassify(const uint8_t *head, uint32_t headLen, uint32_t fileSize);

}  // namespace ms
