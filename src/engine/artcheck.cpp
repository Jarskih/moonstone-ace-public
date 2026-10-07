// engine/artcheck - see include/engine/artcheck.hpp. Format knowledge: docs/ART.md, tools/artconv.py (parse_piv/parse_cel).
#include "engine/artcheck.hpp"

#include "engine/enhcarve.hpp"

namespace ms {

namespace {

inline char fold(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
inline uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
inline uint32_t be32(const uint8_t *p) { return (be16(p) << 16) | be16(p + 2); }

// Picture header at 'at'; returns the end offset of that picture (header + palette + packed) or 0 if it is not one.
uint32_t pictureEnd(const uint8_t *head, uint32_t headLen, uint32_t at, uint32_t fileSize, uint32_t &planes) {
	if(at + 6 > headLen) return 0;
	planes = be16(head + at);
	if(planes < 4 || planes > 6) return 0;
	uint32_t end = at + 6 + 2 * (1u << planes) + be32(head + at + 2);
	if(end < at || end > fileSize) return 0;
	return end;
}

}  // namespace

bool artNameIsPlain(const char *name) {
	if(!name || !*name) return false;
	unsigned n = 0;
	for(const char *p = name; *p; ++p, ++n) {
		if(*p == '/' || *p == ':' || n >= 30) return false;
	}
	if(name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) return false;
	return true;
}

uint32_t celReadBufferBytes(bool isEnhanced) {
	return isEnhanced ? kEnhCelReadBufferBytes : kCelReadBufferBytes;
}

uint32_t artMaxSize(const char *name, bool isEnhanced) {
	if(artNameEqual(name, "message.piv")) {
		return isEnhanced ? kEnhRawPictureBytes : 3694u;
	}
	if(artNameEqual(name, "test")) {  // the arena pack, see enhcarve.hpp
		return isEnhanced ? kEnhPackBytes : 198745u;
	}
	if(artNameEqual(name, "ch.piv")) {
		return isEnhanced ? kEnhRawPictureBytes : 9718u;
	}
	return 0;
}

bool artNameEqual(const char *a, const char *b) {
	while(*a && *b) {
		if(fold(*a) != fold(*b)) return false;
		++a; ++b;
	}
	return !*a && !*b;
}

uint32_t artNameHash(const char *name) {
	uint32_t h = 2166136261u;
	for(; *name; ++name) {
		h ^= (uint8_t)fold(*name);
		h *= 16777619u;
	}
	return h;
}

ArtVerdict artClassify(const uint8_t *head, uint32_t headLen, uint32_t fileSize) {
	ArtVerdict v = {ART_OTHER, false, false, 0};
	// Picture (exact size), then cel (exact size), then a pack of pictures back to back.
	uint32_t planes = 0;
	uint32_t end = pictureEnd(head, headLen, 0, fileSize, planes);
	if(end && end == fileSize) {
		v.kind = ART_PICTURE;
		v.sixPlane = planes == 6;
		v.packed = be32(head + 2);
		return v;
	}
	if(headLen >= 10) {
		uint32_t count = be16(head);
		uint32_t packed = be32(head + 2);
		if(count && fileSize == 10 + 10 * count + packed) {
			v.kind = ART_CEL;
			v.packed = packed;
			if(10 + 10 * count > headLen) {
				v.truncated = true;
				return v;
			}
			for(uint32_t i = 0; i < count; ++i) {
				if(head[10 + 10 * i + 9] & 0x20) v.sixPlane = true;
			}
			return v;
		}
	}
	if(end) {  // first picture ends before the file does: a pack; every picture header we can see is checked
		v.kind = ART_PICTURE;
		v.sixPlane = planes == 6;
		v.packed = be32(head + 2);
		uint32_t at = end;
		while(at < fileSize) {
			if(at + 6 > headLen) break;  // later headers are beyond the head: not seen (artconv writes one plane count per pack)
			uint32_t e = pictureEnd(head, headLen, at, fileSize, planes);
			if(!e) break;
			if(planes == 6) v.sixPlane = true;
			at = e;
		}
	}
	return v;
}

}  // namespace ms
