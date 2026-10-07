// game/flow - the scene manager of the game (ROADMAP 9.2a, docs/GAME_FLOW.md): the State pattern over the whole game.
//
// Every screen of the game is a SCENE: one constant SceneDef (enter / run / exit / resume + the list of the assets it
// loads) in its own file under src/game/scenes/.  One manager (src/game/flow/machine.cpp) owns the scene stack, and ONE
// transition table (src/game/flow/registry.cpp) says where every scene goes: a scene's run() only returns an Event
// ("done", "quit", "status key", "a place was chosen" ...); the row {from scene, event} -> {op, to scene} decides what the
// manager does with it:
//
//   SWITCH  exit the current scene, enter the target in its place (the title -> the campaign set-up -> the map)
//   PUSH    keep the current scene underneath (suspended), run the target until it POPs, then resume the current one
//   POP     exit the current scene and resume the one underneath
//   HALT    leave the manager (only the host tests' legacy entry: the game never halts)
//
// Nested screens.  The original game is nested calls (the map frame calls the node menu, which calls the place visit,
// which calls the fight ...).  Where such a call is the screen change, the calling code raises an event with flowRaise():
// the manager looks the row up exactly like for an event a run() returned (it must be a PUSH), runs the target scene to
// its POP and returns, so the original's call order (and so its timing) is kept.  A modder redirects or chains screens by
// editing table rows, never the scene that raised the event.
//
// Memory (docs/GAME_FLOW.md section 5): each entered or pushed scene takes a mark on the scene MemStack and its exit
// releases to it (a scene allocates in enter()); the original's fixed shared buffers are SLOTS a scene acquires in enter
// and releases in exit / while suspended (the asset rows say which), so no scene relies on another scene's leftovers.
//
// Pure: no ACE, no hardware, no game symbols; the rt glue fills the Flow with the environments (MainEnv/MainOps,
// MapLoopCells/MapLoopOps, FlowOps) and the host services.  Constant-initialised tables only (no global constructors run).
#pragma once
#include <stdint.h>

#include "engine/memstack.hpp"

namespace ms { namespace game {

struct MainEnv;
struct MainOps;
struct PlaceCells;
struct PlaceOps;
struct MapLoopCells;
struct MapLoopOps;

namespace flow {

// The scene ids.  The values of the original's scenes are FIXED (save files, logs and mods name them); a new scene is
// appended before COUNT (docs/GAME_FLOW.md "How to add a scene").  Note: ms::game::SceneId (constants.hpp) is something
// else, the screen id mogSceneId (LAB_068F) of the loot / shop screen dispatcher; this one is flow::SceneId.
enum class SceneId : uint8_t {
	Title = 0,        // LAB_0001 head: tables, title picture + main menu (LAB_0152, LAB_0156, LAB_00B4)
	Practice = 1,     // LAB_0002: the two-knight practice fight
	Campaign = 2,     // LAB_0001 tail: scheduler reset, knight select (LAB_00D3), map set-up (SECSTRT_36)
	Quit = 3,         // LAB_0064: the "game over" / farewell text, then back to the title
	Map = 4,          // LAB_0DAB: the overworld map screen and its frame loop
	Status = 5,       // MOVEQ #9,D0 ; JSR LAB_04CF: the knights' status sheet (space on the map)
	TurnEnd = 6,      // LAB_0DB9 .. LAB_0DBB: the turn scheduler once a knight's move budget is used up (no screen of its own)
	NewDay = 7,       // the round is over: LAB_0DC8, LAB_012B (moon picture), wait for fire, LAB_03EB
	Example = 8,      // MS_EXAMPLE_SCENE only: the "How to add a scene" example (docs/GAME_FLOW.md)
	// the places of the map's node menu (mog.asm LAB_007B; src/game/placevisit.cpp)
	Village = 9,      // LAB_00B0: a home village / castle (+1 life, the temple sheet)
	TownA = 10,       // LAB_0093: HighWood ($19): smith, dice, healer, market, temple
	TownB = 11,       // LAB_008A: WaterDeep ($1A): smith, dice, healer, Mythral the mystic
	Stonehenge = 12,  // LAB_00A1: Danu's offer, or the moonstone of the phase: the ending
	Valley = 13,      // LAB_009D: the four keys, the guardian (demon) fight, a moonstone
	Wizard = 14,      // LAB_007C: Math the wizard (LAB_0456)
	OtherPlace = 15,  // a node that is no place: the visit's prologue only (the CMP.W chain falls through)
	// the town / place screens with their own loader (src/rt/screens.cpp)
	Dice = 16,        // LAB_04A6: the dice house (tav.piv, dice.piv, dice.cel)
	Healer = 17,      // LAB_048E: the healer (HEA.piv, mys.cel)
	Mystic = 18,      // LAB_047C: Mythral the mystic (MYS.piv, mys.cel)
	Ritual = 19,      // LAB_04BF: Danu's ritual at Stonehenge (Hen1.p, Hen1.c, He.a)
	// the sheets of the screen loop LAB_04CF (src/game/combat.cpp screenRun; the scene number in D0)
	ScreenMeet = 20,       // 1: two knights met: loot / exchange
	ScreenLoot = 21,       // 2: a lair's loot
	ScreenOffer = 22,      // 3: Danu's item offer at Stonehenge
	ScreenSmith = 23,      // 5: the smith
	ScreenMarket = 24,     // 6: the market
	ScreenExchange = 25,   // 8: exchange between two knights (nested in a loot sheet)
	ScreenTemple = 26,     // 9: the temple / status sheet / "you lost" / the avoid-the-fight scroll
	ScreenDragonLoot = 27, // 10: the dragon's hoard
	ScreenHandOver = 28,   // 11: exchange with hand-over (nested in a loot sheet)
	ScreenOther = 29,      // any other number (town B's space-key quirk: the number left in D0)
	// the fights (src/game/combat.cpp; each runs the fight loop LAB_0036 and its result sheets)
	Lair = 30,        // LAB_005B: a lair's creatures (arena set-up per creature, fight, loot sheet)
	Duel = 31,        // LAB_004F: two knights meet (avoid scroll, arena, fight, meeting sheet)
	Dragon = 32,      // LAB_0083: the dragon attacks on the map
	Fight = 33,       // LAB_0036 on its own: the practice fight and the valley guardian
	// leaving mog
	Ending = 34,      // SECSTRT_5: the program overlay's ending (rt_run_program; never returns)
	COUNT
};
constexpr uint8_t SCENE_COUNT = static_cast<uint8_t>(SceneId::COUNT);
constexpr SceneId SCENE_ANY = SceneId::COUNT;   // a transition row's "from" that matches every scene (after the scene's own rows)

// What a scene's run() ends with, or what running code raises (flowRaise).  Append only.
enum class Event : uint8_t {
	Done = 0,         // the scene is finished (its normal exit)
	Practice,         // title: the menu cursor is on Practice
	Campaign,         // title: start the campaign
	Quit,             // the map's Q key
	GameOver,         // the turn scheduler found no knight left to play (the same quit screen as Q)
	Restart,          // the map screen starts again (LAB_0DAB re-entered: after a menu / place visit, a new turn)
	Status,           // the map's space key
	TurnOver,         // the current knight's move budget is used up
	NewDay,           // raised by the turn scheduler when a round ends
	Halt,             // leave the manager (host tests)
	// raised by the node menu / the place code / the fights / the screen loop: one per scene they push (same names)
	Village, TownA, TownB, Stonehenge, Valley, Wizard, OtherPlace,
	Dice, Healer, Mystic, Ritual,
	ScreenMeet, ScreenLoot, ScreenOffer, ScreenSmith, ScreenMarket, ScreenExchange, ScreenTemple, ScreenDragonLoot, ScreenHandOver,
	ScreenOther,
	Lair, Duel, Dragon, Fight,
	Ending,
	COUNT
};
constexpr uint8_t EVENT_COUNT = static_cast<uint8_t>(Event::COUNT);

enum class FlowOp : uint8_t { Switch = 0, Push, Pop, Halt };

// The original's fixed buffers that more than one scene writes (carved once at boot by LAB_0004; docs/GAME_FLOW.md section 5).
enum class Slot : uint8_t {
	None = 0,
	PicScreen,        // LAB_05C2 (= LAB_05B9[0] = [11]): the 50,000-byte fast spare buffer: picture staging (map backdrop, place
	                  // pictures, arena pictures), and the Kn5 / Mudmen2 / Demon cel sets
	Background,       // LAB_05C0: the background planes the frame restores from (map, arena compose, screen panels)
	Foreground,       // LAB_05C1: the second picture planes (title Sel.cel, town pictures, the arena tile sheet)
	Screens,          // LAB_0D92 / SECSTRT_35: the drawn and the shown screen (also message.piv staging)
	CreatureCels,     // LAB_05B8[2]: 80,000 chip bytes of creature / place cels (slot pointers LAB_05E0); LAB_05DF caches its kind
	CreatureBank,     // LAB_05C8: the creature / place sound bank (Ratmen and Wizard banks start inside CreatureCels)
	FightHeap,        // LAB_05C3 (= LAB_0A83): creature records / backdrop blob scratch
	Palette,          // the live palette and the map's colour-cycle jobs (LAB_0DC5 / LAB_0DC8)
	COUNT
};
constexpr uint8_t SLOT_COUNT = static_cast<uint8_t>(Slot::COUNT);

enum class AssetKind : uint8_t {
	File = 0,         // read from disk (rt/files) when the scene is entered; szFile is the data file name
	Packed,           // unpacked from a game-lifetime copy in memory (loaded at boot); szFile names the original file
	Buffer            // no data: the scene uses the slot as work space
};

// One line of a scene's asset list: what it loads, into which shared buffer.  The list is in load order.
struct AssetRow {
	AssetKind eKind;
	Slot eSlot;           // the buffer it goes to (acquired by the manager for the scene's lifetime); None = scene memory
	const char *szFile;   // data file name as on the original disks (case as rt/files opens it); 0 for a Buffer row
	uint32_t ulBytes;     // bytes the scene allocates on the scene MemStack for it (0 = it lives in the slot)
};

struct Flow;

// A scene: constant, one per file (src/game/scenes/<name>.cpp).  Every hook may be 0.
struct SceneDef {
	SceneId eId;
	const char *szName;
	const AssetRow *pAssets;          // what enter() loads (docs + tests; the manager acquires the slots)
	uint8_t ubAssetCount;
	const Event *pEvents;             // every event run() can return or the scene's code can raise (the table test checks a row exists)
	uint8_t ubEventCount;
	void (*pfnEnter)(Flow &f);        // load / set up the screen
	Event (*pfnRun)(Flow &f);         // the scene's loop (frame loops keep the original's timing); returns why it ended
	void (*pfnExit)(Flow &f);         // tear down / release
	void (*pfnResume)(Flow &f);       // back on top after a PUSHed scene popped (0 = nothing; the scene's run() is called again)
};

struct Transition {
	SceneId eFrom;        // SCENE_ANY = every scene
	Event eEvent;
	FlowOp eOp;
	SceneId eTo;          // unused for Pop / Halt
};

// Ops of the later areas that the pure scenes call (the rt glue fills them).  Every entry may be 0 on a host.
struct FlowOps {
	void *pCtx;
	// NewDay (the turn scheduler's pfnNewDay port, mog.asm LAB_0DBA)
	void (*pfnNewDayScreen)(void *pCtx);              // LAB_0DC8 (the map's colour jobs off), LAB_012B ("Next Day" + the moon)
	void (*pfnWaitFire)(void *pCtx);                  // LAB_00EC: wait for a fire press and release
	void (*pfnPaletteClear)(void *pCtx);              // LAB_03EB: the palette to black
	// Example (MS_EXAMPLE_SCENE only; docs/GAME_FLOW.md "How to add a scene")
	void (*pfnShowPicture)(void *pCtx, const char *szFile);   // load a .piv from disk into the shown screen, with its palette
};

// rt services of the manager (optional).
struct FlowHost {
	void *pCtx;
	void (*pfnLog)(void *pCtx, const char *szScene, const char *szWhat, uint32_t ulValue);   // MS_AUTOPLAY: enter / exit / high water
	void (*pfnFatal)(void *pCtx, const char *szScene, const char *szWhy, uint32_t ulValue);  // never returns in the game
};

constexpr uint8_t FLOW_DEPTH = 8;   // scene stack depth (map -> status -> example is 3)

// The manager and the blackboard the scenes share.  One instance (the rt glue's), zero-initialised BSS + flowInit.
struct Flow {
	// environments of the areas (filled by the glue; 0 where a host test does not need them)
	const MainEnv *pMain;
	const MainOps *pMainOps;
	const MapLoopCells *pMapCells;
	const MapLoopOps *pMapOps;
	const FlowOps *pOps;
	FlowHost host;
	// tables (the registry by default; a test can swap in a mutated copy)
	const SceneDef *const *ppScenes;   // indexed by SceneId
	const Transition *pRows;
	uint16_t uwRowCount;
	// manager state
	SceneId aStack[FLOW_DEPTH];
	uint32_t aulMark[FLOW_DEPTH];      // MemStack mark taken when the scene was entered
	uint8_t ubDepth;
	uint8_t aubSlotOwner[SLOT_COUNT];  // 0 = free, else 1 + the stack level that holds it
	MemStack mem;                      // the scene memory (rt: the arena tail above the boot carve)
	uint32_t ulGameMark;               // the game-lifetime mark (bottom of every scene)
	uint16_t uwErrors;                 // balance errors seen (a host test checks 0)
	bool bHalt;                        // a HALT row or a hard error: every level returns, flowRun leaves every scene
	uint32_t aulHighSaved[FLOW_DEPTH]; // the MemStack high water of the level below while a level runs
	// nested calls (flowCall): the body the next entered scene runs, and the body of each level
	void (*pfnNextBody)(void *pCtx);
	void *pNextBodyCtx;
	void (*apfnBody[FLOW_DEPTH])(void *pCtx);
	void *apBodyCtx[FLOW_DEPTH];
	// the place being visited (the node menu's row: src/rt/overworld.cpp opPlace)
	const PlaceCells *pPlaceCells;
	const PlaceOps *pPlaceOps;
	uint32_t ulArg;                    // the node id
	uint32_t ulResult;                 // the D0 the visit leaves for the node menu (non-zero restarts the map screen)
};

// Set up the manager: the registry tables, an empty stack, the scene MemStack over [ulMemBase, +ulMemSize).
void flowInit(Flow &f, uint32_t ulMemBase, uint32_t ulMemSize);
// Run scenes from eFirst until a HALT row (never in the game).
void flowRun(Flow &f, SceneId eFirst);
// From inside a running scene's code: look up {current scene, ev}; it must be a PUSH.  Runs the target to its POP and
// returns the event the target ended with.  An event without a row is a table bug: reported (host.pfnFatal) and ignored.
Event flowRaise(Flow &f, Event ev);

// Nested call with a body: like flowRaise, and the pushed scene's run() runs pfnBody(pCtx) (flowBody) where the original
// called the screen.  The rt glue wraps the original's call of a screen this way (the screen's code stays where it is).
Event flowCall(Flow &f, Event ev, void (*pfnBody)(void *pCtx), void *pCtx);
// In a scene's run(): the body its flowCall handed over (nothing when the scene was entered another way).
void flowBody(Flow &f);

// The tables.
const SceneDef *flowScene(const Flow &f, SceneId e);
const Transition *flowFind(const Flow &f, SceneId eFrom, Event ev);
inline SceneId flowCurrent(const Flow &f) { return f.ubDepth ? f.aStack[f.ubDepth - 1] : SceneId::COUNT; }

// The registry (src/game/flow/registry.cpp).
extern const SceneDef *const g_flowScenes[SCENE_COUNT];
extern const Transition g_flowRows[];
extern const uint16_t g_flowRowCount;

// Table checks for the host test (tests/test_flow.py): every scene registered under its own id; every event a scene
// declares has a row; every row's target is a registered scene; every scene is reachable from the title; push / pop
// balance (a PUSHed-only scene has a POP row).  Returns the number of problems, the first one in pcErr.
int flowCheck(const SceneDef *const *ppScenes, const Transition *pRows, uint16_t uwRows, char *pcErr, uint32_t ulCap);

}  // namespace flow
}}  // namespace ms::game
