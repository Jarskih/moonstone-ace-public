// rt/sdl/imgdump - see imgdump.hpp.
#include "rt/sdl/imgdump.hpp"

#include <stdio.h>
#include <string.h>

#include "engine/endian.hpp"
#include "rt/platform/log.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr uint32_t kMaxWidth = 1024;
constexpr uint32_t kMaxHeight = 768;
constexpr uint32_t kStoredBlock = 65535;

uint8_t s_aubRaw[(kMaxWidth * 3 + 1) * kMaxHeight];   // filtered scanlines, input of the zlib stream

// Closes the file on every exit path (docs/RAII.md).
class FileGuard {
public:
	FileGuard(const char *szPath, const char *szMode) : m_pFile(fopen(szPath, szMode)) {}
	~FileGuard() {
		if(m_pFile != nullptr) {
			fclose(m_pFile);
		}
	}
	FileGuard(const FileGuard &) = delete;
	FileGuard &operator=(const FileGuard &) = delete;
	FileGuard(FileGuard &&) = delete;
	FileGuard &operator=(FileGuard &&) = delete;
	FILE *get() const { return m_pFile; }
private:
	FILE *m_pFile;
};

uint32_t crcUpdate(uint32_t ulCrc, const uint8_t *pubData, uint32_t ulLen) {
	ulCrc = ~ulCrc;
	for(uint32_t i = 0; i < ulLen; ++i) {
		ulCrc ^= pubData[i];
		for(int k = 0; k < 8; ++k) {
			ulCrc = (ulCrc >> 1) ^ (0xEDB88320u & (0u - (ulCrc & 1u)));
		}
	}
	return ~ulCrc;
}

// Writes one PNG chunk.  The body is given in up to two pieces so the IDAT stream need not be copied.
bool writeChunk(FILE *pFile, const char *szType, const uint8_t *pubA, uint32_t ulLenA, const uint8_t *pubB, uint32_t ulLenB) {
	uint8_t aubHead[8];
	ms::be::st32(aubHead, ulLenA + ulLenB);
	memcpy(aubHead + 4, szType, 4);
	uint32_t ulCrc = crcUpdate(0, aubHead + 4, 4);
	ulCrc = crcUpdate(ulCrc, pubA, ulLenA);
	ulCrc = crcUpdate(ulCrc, pubB, ulLenB);
	uint8_t aubCrc[4];
	ms::be::st32(aubCrc, ulCrc);
	return fwrite(aubHead, 1, 8, pFile) == 8 && (ulLenA == 0 || fwrite(pubA, 1, ulLenA, pFile) == ulLenA)
		&& (ulLenB == 0 || fwrite(pubB, 1, ulLenB, pFile) == ulLenB) && fwrite(aubCrc, 1, 4, pFile) == 4;
}

bool hasExtension(const char *szPath, const char *szExt) {
	const size_t ulPath = strlen(szPath);
	const size_t ulExt = strlen(szExt);
	if(ulPath < ulExt) {
		return false;
	}
	for(size_t i = 0; i < ulExt; ++i) {
		const char c = szPath[ulPath - ulExt + i];
		if((c >= 'A' && c <= 'Z' ? c + 32 : c) != szExt[i]) {
			return false;
		}
	}
	return true;
}

bool writePpm(const char *szPath, const uint32_t *pulRgb, uint32_t ulWidth, uint32_t ulHeight) {
	FileGuard sFile(szPath, "wb");
	if(sFile.get() == nullptr) {
		return false;
	}
	fprintf(sFile.get(), "P6\n%u %u\n255\n", static_cast<unsigned>(ulWidth), static_cast<unsigned>(ulHeight));
	for(uint32_t i = 0; i < ulWidth * ulHeight; ++i) {
		const uint8_t aubPixel[3] = {static_cast<uint8_t>(pulRgb[i] >> 16), static_cast<uint8_t>(pulRgb[i] >> 8), static_cast<uint8_t>(pulRgb[i])};
		if(fwrite(aubPixel, 1, 3, sFile.get()) != 3) {
			return false;
		}
	}
	return true;
}

bool writePng(const char *szPath, const uint32_t *pulRgb, uint32_t ulWidth, uint32_t ulHeight) {
	const uint32_t ulRowBytes = 1 + ulWidth * 3;
	const uint32_t ulRawSize = ulRowBytes * ulHeight;
	for(uint32_t y = 0; y < ulHeight; ++y) {
		uint8_t *pubRow = s_aubRaw + y * ulRowBytes;
		pubRow[0] = 0;   // filter: none
		for(uint32_t x = 0; x < ulWidth; ++x) {
			const uint32_t ulRgb = pulRgb[y * ulWidth + x];
			pubRow[1 + 3 * x] = static_cast<uint8_t>(ulRgb >> 16);
			pubRow[2 + 3 * x] = static_cast<uint8_t>(ulRgb >> 8);
			pubRow[3 + 3 * x] = static_cast<uint8_t>(ulRgb);
		}
	}
	// zlib stream: 78 01, stored blocks (BFINAL, LEN, NLEN, data), Adler-32 of the raw bytes; the IDAT chunk is written piecewise with
	// a running CRC, so the stream is never copied.
	const uint32_t ulBlocks = (ulRawSize + kStoredBlock - 1) / kStoredBlock;
	const uint32_t ulIdatLen = 2 + ulBlocks * 5 + ulRawSize + 4;
	FileGuard sFile(szPath, "wb");
	FILE *pFile = sFile.get();
	if(pFile == nullptr) {
		return false;
	}
	static const uint8_t s_aubSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	uint8_t aubIhdr[13];
	ms::be::st32(aubIhdr, ulWidth);
	ms::be::st32(aubIhdr + 4, ulHeight);
	aubIhdr[8] = 8;    // bit depth
	aubIhdr[9] = 2;    // truecolour
	aubIhdr[10] = aubIhdr[11] = aubIhdr[12] = 0;
	bool isOk = fwrite(s_aubSignature, 1, 8, pFile) == 8 && writeChunk(pFile, "IHDR", aubIhdr, 13, nullptr, 0);
	// IDAT, written piecewise with a running CRC
	uint8_t aubHead[8];
	ms::be::st32(aubHead, ulIdatLen);
	memcpy(aubHead + 4, "IDAT", 4);
	uint32_t ulCrc = crcUpdate(0, aubHead + 4, 4);
	isOk = isOk && fwrite(aubHead, 1, 8, pFile) == 8;
	const uint8_t aubZlibHead[2] = {0x78, 0x01};
	ulCrc = crcUpdate(ulCrc, aubZlibHead, 2);
	isOk = isOk && fwrite(aubZlibHead, 1, 2, pFile) == 2;
	uint32_t ulA = 1;   // Adler-32
	uint32_t ulB = 0;
	for(uint32_t i = 0; i < ulRawSize; ++i) {
		ulA = (ulA + s_aubRaw[i]) % 65521u;
		ulB = (ulB + ulA) % 65521u;
	}
	for(uint32_t ulPos = 0; ulPos < ulRawSize && isOk; ulPos += kStoredBlock) {
		const uint32_t ulLen = ulRawSize - ulPos < kStoredBlock ? ulRawSize - ulPos : kStoredBlock;
		const uint8_t aubBlock[5] = {static_cast<uint8_t>(ulPos + ulLen >= ulRawSize ? 1 : 0), static_cast<uint8_t>(ulLen), static_cast<uint8_t>(ulLen >> 8),
		                             static_cast<uint8_t>(~ulLen), static_cast<uint8_t>((~ulLen) >> 8)};
		ulCrc = crcUpdate(ulCrc, aubBlock, 5);
		ulCrc = crcUpdate(ulCrc, s_aubRaw + ulPos, ulLen);
		isOk = fwrite(aubBlock, 1, 5, pFile) == 5 && fwrite(s_aubRaw + ulPos, 1, ulLen, pFile) == ulLen;
	}
	uint8_t aubAdler[4];
	ms::be::st32(aubAdler, (ulB << 16) | ulA);
	ulCrc = crcUpdate(ulCrc, aubAdler, 4);
	uint8_t aubCrc[4];
	ms::be::st32(aubCrc, ulCrc);
	isOk = isOk && fwrite(aubAdler, 1, 4, pFile) == 4 && fwrite(aubCrc, 1, 4, pFile) == 4;
	return isOk && writeChunk(pFile, "IEND", nullptr, 0, nullptr, 0);
}

}  // namespace

bool imgDumpNameIsSupported(const char *szPath) {
	return hasExtension(szPath, ".png") || hasExtension(szPath, ".ppm");
}

bool imgDumpWrite(const char *szPath, const uint32_t *pulRgb, uint32_t ulWidth, uint32_t ulHeight) {
	if(ulWidth == 0 || ulHeight == 0 || ulWidth > kMaxWidth || ulHeight > kMaxHeight) {
		return false;
	}
	bool isOk = false;
	if(hasExtension(szPath, ".png")) {
		isOk = writePng(szPath, pulRgb, ulWidth, ulHeight);
	}
	else if(hasExtension(szPath, ".ppm")) {
		isOk = writePpm(szPath, pulRgb, ulWidth, ulHeight);
	}
	if(!isOk) {
		platform::logFormat("cannot write the frame to %s\n", szPath);
	}
	return isOk;
}

}  // namespace sdl
}  // namespace rt
