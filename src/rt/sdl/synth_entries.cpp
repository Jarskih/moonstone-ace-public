// rt/sdl/synth_entries - the PC twins of the register entries of mog's synth (src/rt/amiga/synth_entries.cpp, ROADMAP 11.10): the same names with the C
// convention, because the VBL hook list (rt_synth_tick), the INT4 handler cell (rt_synth_int4) and rt/soundbank.cpp name them.  The synth itself is
// src/rt/synth.cpp over the Paula model of rt/sdl/audio.
#include <stdint.h>

extern "C" {
void rtSynthInitC(void);
void rtSynthRelocC(void);
void rtSynthTickC(void);
void rtSynthFadeC(void);
void rtSynthInt4C(void);

void rt_synth_init(void) { rtSynthInitC(); }
void rt_synth_reloc(void) { rtSynthRelocC(); }
void rt_synth_tick(void) { rtSynthTickC(); }
void rt_synth_fade(void) { rtSynthFadeC(); }
void rt_synth_int4(void) { rtSynthInt4C(); }
}
