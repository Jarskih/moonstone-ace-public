// rt/autoplay - scripted input for headless boot tests (-DMS_AUTOPLAY). Format: include/engine/autoplay.hpp, guide:
// docs/AUTOPLAY.md. PROGDIR:autoplay.txt is read once at startup; from then on the level-3 VBL tick injects keys through
// the keyboard ISR's own delivery path (rt::inputInjectKey) and overrides the joystick values the game reads.
// Everything here is a no-op (and the hooks compile away) without MS_AUTOPLAY.
#pragma once
#include <ace/types.h>
#include "engine/input.hpp"

namespace rt {

#if defined(MS_AUTOPLAY) && MS_AUTOPLAY
// Open the serial log, read + parse PROGDIR:autoplay.txt (dos.library inside systemUse). Call from rt::systemCreate.
void autoplayInit();

// One level-3 VERTB service (interrupt context): advance the frame, run the events that are due.
void autoplayTick();

// Joystick override of rt/input's joyPoll: replaces uwPort0/uwPort1 for every port a `joyN` event has touched.
void autoplayJoy(ms::JoyBits &sBits);

// True while an autoplay joystick holds fire (port 0 or 1): the intro skip poll (rt/introskip) treats it as the fire button.
bool autoplayFireHeld();

// The `quit` event has run.
bool autoplayQuit();

// Offered every serial-log line (rt/serlog's serLogf, i.e. the game's own logWrite too): feeds `wait file|log` and lets go of held
// input when a data file is opened (a load).
void autoplayNoteLine(const char *szLine);

// `poke <name> <value>` (rt/autoplay_game.cpp): test hooks on the game state: `who N` picks the knight (default 0, the human of a one-player game), then fields of it (mapx mapy gold hp hpmax frog
// lives progress strength constitution endurance daggers keys moonstones), warp_node N / warp_lair N / warp_knight N (put
// that knight on that map object), cursorx / cursory (the shop cursor, 0..314), dump (log the map: knights, nodes, lairs). False = unknown name / bad value.
bool autoplayPoke(const char *szName, ULONG ulValue);

// `wait var <name> <value>`: read a game variable by name (the knight fields of autoplayPoke, plus scene turn day spent players); false = unknown.
bool autoplayPeek(const char *szName, ULONG &ulValue);
#else
inline void autoplayInit() {}
inline void autoplayTick() {}
inline void autoplayJoy(ms::JoyBits &) {}
inline bool autoplayFireHeld() { return false; }
inline bool autoplayQuit() { return false; }
inline void autoplayNoteLine(const char *) {}
inline bool autoplayPoke(const char *, ULONG) { return false; }
inline bool autoplayPeek(const char *, ULONG &) { return false; }
#endif

}  // namespace rt
