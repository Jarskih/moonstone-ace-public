// rt/sdl/imgdump - write the converted frame to a file (ROADMAP 11.6a): `moonstone.exe --dump-frame out.png|out.ppm`.
// PNG is 8-bit RGB with stored (uncompressed) deflate blocks, PPM is binary P6; no library needed.  Used by the PC smoke test and later
// by the PC counterpart of build/shots (ROADMAP 11.10).
#pragma once
#include <stdint.h>

namespace rt {
namespace sdl {

// True if the file name ends in .png or .ppm (case-insensitive).
bool imgDumpNameIsSupported(const char *szPath);
// Writes pulRgb (0x00RRGGBB, wWidth x wHeight, row-major, at most 1024 x 768) to szPath in the format of its extension.
bool imgDumpWrite(const char *szPath, const uint32_t *pulRgb, uint32_t ulWidth, uint32_t ulHeight);

}  // namespace sdl
}  // namespace rt
