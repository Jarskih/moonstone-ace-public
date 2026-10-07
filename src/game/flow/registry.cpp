// game/flow/registry - THE scene table and THE transition table of the game (ROADMAP 9.2a, docs/GAME_FLOW.md).
//
// To add a scene: write src/game/scenes/<name>.cpp (one constant SceneDef), append its id to flow::SceneId, add one line to
// g_flowScenes and the rows that lead to it and away from it below.  No existing scene's code changes: a scene is reached
// by a row.  tests/test_flow.py checks the tables (every event has a row, every scene is reachable, pushes pop).
#include "game/flow/scenes.hpp"

namespace ms { namespace game { namespace flow {

// Indexed by SceneId (constant-initialised: addresses only, no constructor runs).
const SceneDef *const g_flowScenes[SCENE_COUNT] = {
	&kSceneTitle,        // Title
	&kScenePractice,     // Practice
	&kSceneCampaign,     // Campaign
	&kSceneQuit,         // Quit
	&kSceneMap,          // Map
	&kSceneStatus,       // Status
	&kSceneTurnEnd,      // TurnEnd
	&kSceneNewDay,       // NewDay
#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
	&kSceneExample,      // Example
#else
	0,                   // Example (MS_EXAMPLE_SCENE)
#endif
	&kSceneVillage,      // Village
	&kSceneTownA,        // TownA
	&kSceneTownB,        // TownB
	&kSceneStonehenge,   // Stonehenge
	&kSceneValley,       // Valley
	&kSceneWizard,       // Wizard
	&kSceneOtherPlace,   // OtherPlace
	&kSceneDice,         // Dice
	&kSceneHealer,       // Healer
	&kSceneMystic,       // Mystic
	&kSceneRitual,       // Ritual
	&kSceneScreenMeet,   // ScreenMeet
	&kSceneScreenLoot,   // ScreenLoot
	&kSceneScreenOffer,  // ScreenOffer
	&kSceneScreenSmith,  // ScreenSmith
	&kSceneScreenMarket, // ScreenMarket
	&kSceneScreenExchange,     // ScreenExchange
	&kSceneScreenTemple,       // ScreenTemple
	&kSceneScreenDragonLoot,   // ScreenDragonLoot
	&kSceneScreenHandOver,     // ScreenHandOver
	&kSceneScreenOther,  // ScreenOther
	&kSceneLair,         // Lair
	&kSceneDuel,         // Duel
	&kSceneDragon,       // Dragon
	&kSceneFight,        // Fight
	&kSceneEnding,       // Ending
};

#define ROW(from, ev, op, to) {SceneId::from, Event::ev, FlowOp::op, SceneId::to}
#define ANY(ev, op, to) {SCENE_ANY, Event::ev, FlowOp::op, SceneId::to}           // from every scene (after the scene's own rows)
#define POP(from, ev) {SceneId::from, Event::ev, FlowOp::Pop, SCENE_ANY}          // back to whichever scene pushed it

const Transition g_flowRows[] = {
	// the top level (mog.asm LAB_0001 / LAB_0002 / LAB_0064)
	ROW(Title, Practice, Switch, Practice),      // CMPI.W #2,LAB_06DC ; BEQ LAB_0002
	ROW(Title, Campaign, Switch, Campaign),
	ROW(Practice, Done, Switch, Title),          // BRA.W LAB_0001
	ROW(Campaign, Done, Switch, Map),            // JMP LAB_0DAB
	ROW(Quit, Done, Switch, Title),              // JMP LAB_0001
	// the map (LAB_0DAB .. LAB_0DBC)
	ROW(Map, Restart, Switch, Map),              // BRA.W LAB_0DAB
	ROW(Map, Status, Push, Status),              // space: LAB_04CF (9), then BRA.W LAB_0DAB (the map's resume)
	ROW(Map, TurnOver, Switch, TurnEnd),         // LAB_0DB9
	ROW(Map, Quit, Switch, Quit),                // Q: JMP LAB_0064
	ROW(Map, Halt, Halt, Map),                   // host tests only (no map cells)
#if defined(MS_EXAMPLE_SCENE) && MS_EXAMPLE_SCENE
	ROW(Status, Done, Switch, Example),          // the example: after the status sheet, before the map comes back
	POP(Example, Done),
#else
	POP(Status, Done),
#endif
	// the turn (LAB_0DB9 .. LAB_0DBB)
	ROW(TurnEnd, Done, Switch, Map),             // TURN_PLAY: BEQ.W LAB_0DAB
	ROW(TurnEnd, GameOver, Switch, Quit),        // TURN_GAME_OVER: JMP LAB_0064
	ROW(TurnEnd, NewDay, Push, NewDay),          // the round is over (raised inside advanceTurn)
	POP(NewDay, Done),
	// the node menu (LAB_0E3D / LAB_0E45, raised inside the map frame; each pops back into the menu, which restarts the map
	// screen with the visit's D0): the places of LAB_007B, the lairs (LAB_005B) and the knights (LAB_004F)
	ROW(Map, Village, Push, Village),
	ROW(Map, TownA, Push, TownA),
	ROW(Map, TownB, Push, TownB),
	ROW(Map, Stonehenge, Push, Stonehenge),
	ROW(Map, Valley, Push, Valley),
	ROW(Map, Wizard, Push, Wizard),
	ROW(Map, OtherPlace, Push, OtherPlace),
	ROW(Map, Lair, Push, Lair),
	ROW(Map, Duel, Push, Duel),                  // also the AI knight's arrival (LAB_0E17)
	ROW(Map, Dragon, Push, Dragon),              // LAB_0DB6: the frame goes on after it
	POP(Village, Done),
	POP(TownA, Done),
	POP(TownB, Done),
	POP(Stonehenge, Done),
	POP(Valley, Done),
	POP(Wizard, Done),
	POP(OtherPlace, Done),
	POP(Lair, Done),
	POP(Duel, Done),
	POP(Dragon, Done),
	// inside the places
	ROW(TownA, Dice, Push, Dice),                // LAB_0097
	ROW(TownA, Healer, Push, Healer),            // LAB_009A
	ROW(TownB, Dice, Push, Dice),                // LAB_008F
	ROW(TownB, Healer, Push, Healer),            // LAB_0091
	ROW(TownB, Mystic, Push, Mystic),            // LAB_008E
	ROW(Stonehenge, Ritual, Push, Ritual),       // LAB_00A5: an item was offered
	ROW(Stonehenge, Ending, Push, Ending),       // LAB_00AF: JMP SECSTRT_5 (never comes back)
	ROW(Valley, Fight, Push, Fight),             // LAB_009E: the guardian
	ROW(OtherPlace, Duel, Push, Duel),           // the duel id $21 (unreachable)
	ROW(Practice, Fight, Push, Fight),           // LAB_0002: the practice fight
	POP(Dice, Done),
	POP(Healer, Done),
	POP(Mystic, Done),
	POP(Ritual, Done),
	POP(Fight, Done),
	POP(Ending, Done),          // (never taken: the overlay switch does not return)
	// the sheets of the screen loop LAB_04CF: raised by whatever runs it (map status, places, fights, a sheet itself)
	ANY(ScreenMeet, Push, ScreenMeet),
	ANY(ScreenLoot, Push, ScreenLoot),
	ANY(ScreenOffer, Push, ScreenOffer),
	ANY(ScreenSmith, Push, ScreenSmith),
	ANY(ScreenMarket, Push, ScreenMarket),
	ANY(ScreenExchange, Push, ScreenExchange),
	ANY(ScreenTemple, Push, ScreenTemple),
	ANY(ScreenDragonLoot, Push, ScreenDragonLoot),
	ANY(ScreenHandOver, Push, ScreenHandOver),
	ANY(ScreenOther, Push, ScreenOther),
	POP(ScreenMeet, Done),
	POP(ScreenLoot, Done),
	POP(ScreenOffer, Done),
	POP(ScreenSmith, Done),
	POP(ScreenMarket, Done),
	POP(ScreenExchange, Done),
	POP(ScreenTemple, Done),
	POP(ScreenDragonLoot, Done),
	POP(ScreenHandOver, Done),
	POP(ScreenOther, Done),
};

#undef ROW
#undef ANY
#undef POP

const uint16_t g_flowRowCount = sizeof(g_flowRows) / sizeof(g_flowRows[0]);

}}}  // namespace ms::game::flow
