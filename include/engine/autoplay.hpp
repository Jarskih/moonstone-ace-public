// engine/autoplay - the pure parser of the headless boot-test input script (uaeshot.ps1 -Autoplay, src/rt/autoplay.cpp).
// No ACE, no hardware: text in, a frame-sorted event list out (tests/test_autoplay.py).
//
// Script format, one command per line, case-insensitive, '#' or ';' starts a comment, blank lines ignored:
//   frame <N> key <NAME> down|up        raw Amiga key (A-Z 0-9 SPACE RETURN ENTER ESC TAB BACKSPACE DEL UP DOWN LEFT RIGHT
//                                       F1-F10 LSHIFT RSHIFT CTRL LALT RALT LAMIGA RAMIGA NUM0-NUM9 HELP COMMA PERIOD MINUS EQUALS)
//   frame <N> key <NAME> tap            shorthand: down at N, up at N + kApTapFrames
//   frame <N> joy0|joy1 <dirs>          port state := the directions, '+' joined: up down left right fire; "none" releases all
//   frame <N> joy0|joy1 <dirs> pulse [<F>]  like that, but the harness releases it itself after F frames (default 3) or at the next
//                                       file open; logs "AUTOPLAY pulse joyN done, R reads" (R = 0: "MISSED", the game never looked).
//                                       A press that cannot outlive its screen: use it for menu steps and fire.
//   frame <N> type <TEXT>               one tap per character (A-Z 0-9, '_' = space), kApTypeStep frames apart
//   frame <N> shot <NAME>               log "AUTOPLAY shot <NAME>" (the host script captures the window when it sees it)
//   frame <N> log <TEXT>                log "AUTOPLAY log <TEXT>"
//   frame <N> quit                      log "AUTOPLAY quit" (the host script ends the emulator)
//   wait file <NAME> [max <F>]          barrier: block the script until the game has opened the data file NAME ("files: open ..NAME")
//   wait log <TEXT> [max <F>]           barrier: block until a serial-log line contains TEXT ('_' matches a space)
//   wait input [<K>] [gap <G>] [max <F>] barrier: wait until the game has polled the joystick on K (default 5) frames in a row AFTER a break
//                                       (a load, or no poll for more than G ticks, default 0): a new input loop started after a screen change
//   frame <N> mash joy0|joy1 <F>|off    autopilot for fights: from now on the port gets a new pseudo-random stick+fire pattern (the
//                                       eight directions with and without fire, deterministic) every F frames; `off` stops it
//   wait var <NAME> <VALUE> [max <F>]   barrier: until the game variable NAME (`scene` `turn` `day` `spent` `players` or a knight field, see
//                                       rt/autoplay_game.cpp autoplayPeek) equals VALUE (decimal or 0xHEX)
//   sync                                barrier that never blocks: forget the hits seen so far (the next `wait` needs a fresh one)
//   frame <N> poke <SYM> <VALUE>        MS_AUTOPLAY test hook: write VALUE (decimal or 0xHEX) to the named game variable, see rt/autoplay_game.cpp
// `wait` / `sync` take an optional `frame <N>` prefix (minimum delay after the previous barrier, default 0) and an optional trailing
// `max <F>` (give up after F frames and go on, logging "AUTOPLAY wait-timeout"). A barrier ends the segment of the lines before it:
// events never move across it when the parser sorts by frame, and in the segment after a barrier `frame <N>` means N frames after
// the barrier was released (so it is relative to the event, not to the boot). Scripts without a barrier are unchanged: absolute frames.
// A hit is remembered when it happens (a ring of 64), so a wait that is reached after the event already occurred does not hang; each
// wait consumes one hit and the next wait of the same text needs a later one.
// Frames count level-3 VBL interrupts since the autoplay was armed (PAL: 50 per second).
#pragma once
#include <stdint.h>

namespace ms {

constexpr uint16_t kApMaxEvents = 480;
constexpr uint32_t kApTypeStep = 30;   // frames between the key-downs of `type`
constexpr uint32_t kApTapFrames = 15;  // frames a typed / tapped key is held
constexpr unsigned kApTextLen = 28;   // shot / log / wait text: at most 27 characters
constexpr unsigned kApTypeLen = 20;   // `type` text: at most 19 characters

enum ApKind : uint8_t { AP_KEY = 0, AP_JOY = 1, AP_SHOT = 2, AP_LOG = 3, AP_QUIT = 4, AP_WAIT_FILE = 5, AP_WAIT_LOG = 6, AP_WAIT_INPUT = 7, AP_SYNC = 8, AP_POKE = 9, AP_MASH = 10, AP_WAIT_VAR = 11 };

// Barriers (see "wait" above): the script stops at them until their condition holds.
inline bool apIsBarrier(uint8_t ubKind) {
	return (ubKind >= AP_WAIT_FILE && ubKind <= AP_SYNC) || ubKind == AP_WAIT_VAR;
}

struct ApEvent {
	uint32_t ulFrame;
	uint8_t ubKind;
	uint8_t ubCode;   // AP_KEY: raw Amiga key code; AP_JOY: port (0 or 1)
	uint8_t isDown;   // AP_KEY: 1 = press, 0 = release; AP_JOY: 1 = pulse
	uint8_t ubBits;   // AP_JOY: the port's new state (ms::kJoy* bits); AP_WAIT_INPUT: G; AP_MASH: frames per pattern (0 = off)
	char szText[kApTextLen];  // AP_SHOT / AP_LOG / AP_WAIT_* (the pattern) / AP_POKE (the symbol)
	uint16_t uwMax;           // AP_WAIT_*: give up after this many frames (0 = never); AP_JOY pulse: its length in frames (0 = default)
	uint32_t ulValue;         // AP_POKE: the value; AP_WAIT_INPUT: K; AP_WAIT_VAR: the value
};

struct ApParse {
	uint16_t uwCount;         // events written (sorted by frame, file order kept within a frame)
	uint16_t uwErrors;        // lines rejected (they are skipped, the rest still parses)
	uint16_t uwFirstErrLine;  // 1-based, 0 = none
	uint8_t isOverflow;       // more than uwMax events
};

ApParse apParse(const char *pText, uint32_t ulLen, ApEvent *pOut, uint16_t uwMax);

// Raw Amiga key code for a name (case-insensitive); -1 = unknown.
int apKeyCode(const char *szName);

}  // namespace ms
