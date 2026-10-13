// rt/sdl/system - rt::platform::system for the PC (ROADMAP 11.4, 11.6a): the life cycle is main.cpp's guards (SdlRuntime, window),
// "chip" memory is the emulated 2 MB chip RAM of rt/sdl/chip (the planar screens must live in it), "fast" is the host heap.
#include "rt/platform/system.hpp"

#include <stdlib.h>

#include "rt/sdl/chip.hpp"
#include "rt/sdl/log.hpp"

namespace rt {
namespace platform {

bool systemStart() {
	sdl::chipReset();
	return true;   // SDL is brought up by the SdlRuntime guard of main.cpp
}

void systemStop() {
}

bool systemIsPal() {
	return true;
}

void *allocChip(uint32_t ulBytes) {
	return sdl::chipAlloc(ulBytes);
}

void *allocFast(uint32_t ulBytes) {
	return calloc(1, ulBytes);
}

void freeMem(void *pBlock, uint32_t ulBytes) {
	if(pBlock == nullptr) {
		return;
	}
	const ms::softblit::ChipView sChip = sdl::chipView();
	const uint32_t ulAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pBlock));
	if(ulAddr - sChip.origin < sChip.size) {   // wraps below the origin
		sdl::chipFree(pBlock, ulBytes);
	}
	else {
		free(pBlock);
	}
}

uint32_t memFreeChip() {
	return sdl::chipFreeBytes();
}

uint32_t memFreeFast() {
	return 0;   // the host heap has no meaningful "free" figure
}

uint32_t memLargestChip() {
	return sdl::chipLargestFree();
}

}  // namespace platform
}  // namespace rt
