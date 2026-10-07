// game/placevisit - the visit of a map location in C++ (ROADMAP 7.1l): LAB_007B of mog.asm and everything behind it up to LAB_00B3.
// Split from game/overworld.hpp (the map screen) because it uses the decisions of game/scene_places.hpp (which node is which place, which
// town button is which action).  Pure: no ACE, no OS, no globals; the asm routines are PlaceCalls behind PlaceOps, the cells come in as
// pointers.  src/rt/overworld.cpp wires it (rtOwPlaceVisit); tests/test_places_emu.py runs it against the original LAB_007B in unicorn.
#pragma once
#include <stdint.h>

#include "game/api/data.hpp"
#include "game/state.hpp"

namespace ms { namespace game {

#pragma pack(push, 2)

// The visit of a map location: LAB_007B and everything behind it up to LAB_00B3 - the screen preparation, the two
// town menus (LAB_008A..LAB_0092, LAB_0093..LAB_009A with their button tables LAB_009C / LAB_009B), the wizard's tail (LAB_007C), the
// valley guardian (LAB_009D..LAB_00A0, LAB_0DCA), Stonehenge (LAB_00A1..LAB_00AF) and the shared exit (LAB_00B0..LAB_00B3).  The
// decisions that have C++ homes (game/scene_places.hpp, game/scene_town.hpp) come in as ops; every asm routine is a PlaceCall.
// Returns what the original left in D0 for the node menu (non-zero restarts the map screen).

// The asm routines a visit calls (value = label; SECSTRT_n = 0x8000 + n, decimal digits as hex like MainStep).
enum PlaceCall : uint16_t {
	PCALL_CEL_INIT5 = 0x8028,          // SECSTRT_28, the cel renderer init, with D7 = 5 planes
	PCALL_SETUP_TURN = 0x0DBD,         // turn set-up (LAB_0113 is a JMP to it)
	PCALL_JOBS_RESET = 0x0305,
	PCALL_CREATURE_CLEAR = 0x02CE,
	PCALL_FIGHT_TABLES = 0x0155,
	PCALL_FADE_OUT = 0x03F0,
	PCALL_KEY_RESET = 0x0B82,
	PCALL_MATH_SHOW = 0x0456,          // Math the wizard's screen
	PCALL_SCREEN = 0x04CF,             // the scene state machine, D0 = the scene
	PCALL_LOAD_TOWN_A = 0x012E,
	PCALL_LOAD_TOWN_B = 0x012F,
	PCALL_CURSOR_ON = 0x0575,
	PCALL_BLIT_SCREEN = 0x0419,        // A0 -> A1
	PCALL_BLIT_BOTH = 0x0418,
	PCALL_FADE_TO = 0x03F2,            // A0 = the palette table
	PCALL_HIT_TEST = 0x0451,           // D0 / D1 = the cursor position; out D0 = hit (word), A0 = the button record
	PCALL_JOYSTICK = 0x00EE,           // out D1 = the move word (bit 4 = fire)
	PCALL_KEY_XLAT = 0x0D8D,           // D0 = the key cell; out D0.w = the translated key
	PCALL_CURSOR_OFF = 0x057B,
	PCALL_MYSTIC = 0x047C,
	PCALL_DICE = 0x04A6,
	PCALL_HEALER = 0x048E,
	PCALL_RITUAL = 0x04BF,
	PCALL_TEXT = 0x0136,               // a message text record in A0
	PCALL_TEXT_RECOLOURED = 0x0137,    // the same with the other colours
	PCALL_WAIT_FIRE = 0x00EC,
	PCALL_WAIT = 0x0D74,               // D0 = frames
	PCALL_DISK_PROMPT = 0x0100,        // D0 = the disk
	PCALL_GUARDIAN_SETUP = 0x01A0,     // the valley guardian's arena
	PCALL_FIGHT_RUN = 0x0036,
	PCALL_FIGHT_MEET = 0x004F,
	PCALL_BUTTONS_CLEAR = 0x044E,
	PCALL_BUTTON_ADD = 0x0448          // A0 = the button record
};
struct PlaceRet {
	uint32_t ulD0, ulD1, ulA0;
};
// LAB_0A58: the button record the town screens fill and hand to LAB_0448 (24 bytes; the first long is never written).
struct TownButtonRec {
	uint32_t ulSpare0;           // +0
	uint16_t uwW;              // +4   $40
	uint16_t uwH;              // +6   $10 / $1A / $1F / $0C
	uint32_t ul8;              // +8   0
	uint16_t uwX;              // +12
	uint16_t uwY;              // +14
	uint32_t ulId;             // +16  1..5: the long TownButton / townButton() decides on
	uint16_t uw20;             // +20  1
	uint16_t uw22;             // +22  $4A
};
static_assert(sizeof(TownButtonRec) == 24, "LAB_0A58 is DS.L 6");

struct PlaceCells {
	uint16_t *puwPictureFlag;  // LAB_0D4C: cleared on entry
	uint32_t *pulSpriteBank;        // ActiveKnights +10 (LAB_05E4 +10) := LAB_05E3
	const uint32_t *pulE3;     // the long at LAB_05E3
	uint32_t *pulFightLink;    // LAB_05F4: cleared by the arena reset
	uint16_t *puwDragonActive; // LAB_0667: cleared by the arena reset
	uint16_t *puwMenuX;        // LAB_097F: the cursor start of the town menus
	uint16_t *puwMenuY;        // LAB_0980
	const uint32_t *pulPicture;// LAB_0704: the town picture buffer (copied to the screen)
	const uint32_t *pulScreen; // LAB_05C0
	uint32_t ulPalette;        // address of LAB_05B7 (the palette table of the town screens)
	TownButtonRec *pButton;    // LAB_0A58
	uint32_t ulButtonAddr;     // its address
	uint32_t *pulLocked;       // LAB_0662: cleared after the valley fight
	const uint8_t *pubDefeat;  // LAB_05DC: bit 0 = the first fighter lost
	uint16_t *puwStoneAnswer;  // LAB_053B: $FFFF before the wizard's item screen (changed when an item was taken)
	uint16_t *puwSpent;        // LAB_0655
	const uint16_t *puwBudget; // LAB_0665
	uint16_t *puwBootFlags;    // rt_boot_flags ($3E0)
	const volatile uint16_t *puwKeyCell;   // SECSTRT_21
	// the text records and the knight records the calls take as A0
	uint32_t ulTextValleyKeys; // LAB_06E8
	uint32_t ulTextValleyWin;  // LAB_06E1
	uint32_t ulTextStoneNo;    // LAB_06CD
	uint32_t ulTextStoneCode;  // LAB_06D1
	uint32_t ulProgramName;    // LAB_06D9 ("program"; the overlay switch reads it)
	uint32_t ulActiveAddr;     // address of LAB_05E4
	uint32_t ulE3Addr;         // address of LAB_05E3
};

struct PlaceOps {
	void *pCtx;
	PlaceRet (*pfnCall)(void *pCtx, PlaceCall eCall, uint32_t ulD0, uint32_t ulD1, uint32_t ulA0, uint32_t ulA1);
	uint32_t (*pfnButtonId)(void *pCtx, uint32_t ulRecord);   // the long at +16 of the button record the hit test returned
	void (*pfnCastle)(void *pCtx);                 // rt_town_castle_blessing (LAB_00B0): a castle visit gives a life
	bool (*pfnStoneMatches)(void *pCtx);           // rt_places_stone_check: the knight holds the stone of this moon phase
	uint32_t (*pfnStoneCode)(void *pCtx);          // rt_places_stone_code: the ending code D7
	void (*pfnDanu)(void *pCtx);                   // rt_places_danu
	bool (*pfnValleyKeys)(void *pCtx);             // rt_places_valley_keys: all four keys
	void (*pfnValleyDefeat)(void *pCtx);           // rt_places_valley_loss
	void (*pfnValleyVictory)(void *pCtx);          // rt_places_valley_win
	void (*pfnValleyMoonstone)(void *pCtx);        // rt_places_valley_stone
	void (*pfnRunProgram)(void *pCtx);             // JMP rt_run_program (never returns in the game)
	void (*pfnSceneSetup)(void *pCtx);             // SECSTRT_36 (rtOwSceneSetup)
	void (*pfnColourStop)(void *pCtx);             // LAB_0DC8
	const GameData *pData;                         // the place rows of places.ini (nullptr: the original node numbers)
};

// The two town screens' button layouts (LAB_009B: town A / node $19, LAB_009C: town B / node $1A).
struct TownLayout {
	uint16_t uwX;
	uint16_t auwY[5];
	uint16_t uwH4;             // the height of the fourth button
};
extern const TownLayout kTownA, kTownB;
// LAB_009B / LAB_009C: clear the list and add the five buttons (the record is rewritten between the calls like the asm does).
void townButtonsBuild(const TownLayout &l, const PlaceCells &c, const PlaceOps &ops);
// LAB_007B (without its asm entry): the whole visit of the node ulId (the node menu's row kind; only the low word classifies).  Returns the
// D0 the original left: $FFFF after the shared exit (LAB_00B3), the id for a node that is no place, the fight's D0 for a duel.
// QUIRKS kept: town B's space key opens the scene number left in D0 (the translated key); the duel's registers are the prologue's
// (A0 = LAB_05E4, A1 = LAB_05E3: the id $21 never gets here, the node menu sends the duels straight to LAB_004F).
uint32_t placeVisit(uint32_t ulId, const PlaceCells &c, const PlaceOps &ops);
// The pieces of placeVisit, one per scene of the game (ROADMAP 9.2a, src/game/scenes/): the prologue of every visit, then the
// place of the kind.  placeVisit == placeVisitBegin + placeVisitKind(placeClassify(id)); each returns the D0 placeVisit returns.
void placeVisitBegin(const PlaceCells &c, const PlaceOps &ops);
uint32_t placeVillage(const PlaceCells &c, const PlaceOps &ops);
void placeTownLoad(bool bA, const PlaceOps &ops);                              // LAB_012E / LAB_012F
uint32_t placeTownRun(bool bA, const PlaceCells &c, const PlaceOps &ops);      // the town's draw / poll / act loop
uint32_t placeStonehenge(const PlaceCells &c, const PlaceOps &ops);            // the ending branch does not return in the game
uint32_t placeValley(const PlaceCells &c, const PlaceOps &ops);
uint32_t placeWizard(const PlaceCells &c, const PlaceOps &ops);
uint32_t placeVisitKind(uint8_t ubKind, uint32_t ulId, const PlaceCells &c, const PlaceOps &ops);   // ubKind: a PlaceKind
// LAB_0DCA: the valley victory's tail (key reset, turn set-up, the random moonstone).  D0 passes through the calls like the registers of
// the original (its D0 was the rng draw & 3: a zero left the map screen unredrawn; here the value of the wait before it, non-zero).
uint32_t valleyTail(const PlaceOps &ops, uint32_t ulD0);

#pragma pack(pop)

}}  // namespace ms::game
