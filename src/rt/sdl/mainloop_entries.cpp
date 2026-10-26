// rt/sdl/mainloop_entries - the PC side of src/rt/mainloop.cpp (ROADMAP 11.1d part 2): the two entries the map loop and the main loop reach by name (the Amiga ones are in
// src/rt/amiga/mainloop_entries.cpp, which also set the stack pointer to the loop's base).
#include <stdint.h>

extern "C" {

void rtOwLoop(void);     // src/rt/overworld.cpp: LAB_0DAB, the map loop in one piece
void rtMogQuit(void);    // src/rt/mainloop.cpp: the quit scene

// MainOps::enterMap: the Amiga entry resets SP to the loop's base and JMPs rtOwLoop (the original chain never returns: the exits go through rt_mog_quit);
// the PC runs the map loop in place, as a call.
void rtMogEnterMap(void) { rtOwLoop(); }

// rt_mog_quit, LAB_0064 (the map's quit key): the Amiga entry sets SP to the loop's base, runs the quit scene and starts the main loop again, so it never returns
// to the map loop.  The PC runs the quit scene as a call; unwinding the scene stack is the flow's job (ROADMAP 11.4).
void rt_mog_quit(void) { rtMogQuit(); }

}  // extern "C"
