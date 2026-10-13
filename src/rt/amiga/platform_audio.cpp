// rt/amiga/platform_audio - the Paula registers of rt::platform::audio for the Amiga (ROADMAP 11.1b / 11.10): volatile accesses to $DFF000 + reg,
// each one a compiler barrier (the level-4 handler reads the state these writes publish).  The module player (musicPlay ...) and the channel
// handler of the interface are not routed through the platform on the Amiga yet: rt/audio.cpp drives ptplayer, rt/irq.cpp owns the level-4 trampoline.
#include "rt/platform/audio.hpp"

namespace {
constexpr uintptr_t kCustom = 0xDFF000;
}

namespace rt {
namespace platform {

void paulaWrite16(uint16_t uwReg, uint16_t uwValue) {
	*reinterpret_cast<volatile uint16_t *>(kCustom + uwReg) = uwValue;
	asm volatile("" ::: "memory");
}

void paulaWrite32(uint16_t uwReg, uint32_t ulValue) {
	*reinterpret_cast<volatile uint32_t *>(kCustom + uwReg) = ulValue;
	asm volatile("" ::: "memory");
}

uint16_t paulaRead16(uint16_t uwReg) {
	asm volatile("" ::: "memory");
	return *reinterpret_cast<volatile uint16_t *>(kCustom + uwReg);
}

}  // namespace platform
}  // namespace rt
