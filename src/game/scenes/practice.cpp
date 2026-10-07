// scenes/practice - the practice fight of the main menu (mog.asm LAB_0002, 178; ROADMAP 9.2a): knight 0 (joystick port 1)
// against knight 1 (port 0) in arena $C, then back to the title (BRA.W LAB_0001).
//
//   enter  the player count forced to 2 (saved in LAB_05DB), LAB_01AE, the two fighter records, the disk prompt (a no-op),
//          LAB_0134 (message screen), LAB_013C (the arena picture of region $C), LAB_01BE, LAB_0165 (the practice arena), LAB_0011
//   run    LAB_0036: the fight loop
//   exit   the player count restored
#include "game/flow/scenes.hpp"

#include "engine/jobs.hpp"   // jobAddr: game address of a record

namespace ms { namespace game { namespace flow {

namespace {

constexpr uint8_t TYPE_KNIGHT_FIGHT = raw(ActorType::KnightFight);   // Knight::ubType of a knight in a fight
constexpr uint32_t ARENA_PRACTICE = 0x0C;                            // MOVE.L #$C,LAB_08C4 before LAB_013C

const AssetRow kAssets[] = {
	{AssetKind::Packed, Slot::Screens, "message.piv", 0},   // LAB_0134: the message screen (pack[13], loaded at boot)
	{AssetKind::Packed, Slot::PicScreen, "test", 0},        // LAB_013C: the arena pictures of the "Test" pack, staged in LAB_05C2
	{AssetKind::File, Slot::FightHeap, "??*.t", 0},         // LAB_0A6D: the backdrop tile blob of the region (fo/gl/sw/wa*.t)
	{AssetKind::Buffer, Slot::Background, 0, 0},            // the composed backdrop
	{AssetKind::Buffer, Slot::Foreground, 0, 0},            // the tile sheet
	{AssetKind::File, Slot::CreatureCels, "HE?.ob", 0},     // LAB_0165 -> LAB_0116: He1.ob .. He3.ob (skipped while LAB_05DF says they are loaded)
};
const Event kEvents[] = {Event::Done, Event::Fight};

void enter(Flow &f) {
	const MainEnv &env = *f.pMain;
	*env.pPlayersSaved = *env.pPlayers;                       // MOVE.W LAB_05C5,LAB_05DB
	env.pActive->uwPlayerCount = 2;                           // MOVE.W #2,14(LAB_05E4)
	*env.pPlayers = 2;                                        // MOVE.W #2,LAB_05C5
	mainStep(f, STEP_LAB_01AE);                               // (resets LAB_05E4 and more, hence the writes below follow it)
	env.pActive->ulCurrent = jobAddr(&env.pKnights[0]);       // MOVE.L #LAB_0613,0(A0)
	env.pActive->ulOpponent = jobAddr(&env.pKnights[1]);      // MOVE.L #LAB_0614,4(A0)
	env.pKnights[0].ubType = TYPE_KNIGHT_FIGHT;               // MOVE.B #$0C,77(A0)
	env.pKnights[0].ubInputPort = raw(InputPort::Joy1);       // MOVE.B #2,11(A0)
	env.pKnights[0].ulKind = raw(KnightKind::Knight0);        // MOVE.L #0,54(A0)
	env.pKnights[1].ubType = TYPE_KNIGHT_FIGHT;
	env.pKnights[1].ubInputPort = raw(InputPort::Joy0);
	env.pKnights[1].ulKind = raw(KnightKind::Knight2);
	mainStep(f, STEP_LAB_0100_D2);
	mainStep(f, STEP_LAB_0134);
	*env.pArena = ARENA_PRACTICE;
	mainStep(f, STEP_LAB_013C);
	mainStep(f, STEP_LAB_01BE);
	mainStep(f, STEP_LAB_0165);
	mainStep(f, STEP_LAB_0011);
}

Event run(Flow &f) {
	mainStep(f, STEP_LAB_0036);
	return Event::Done;
}

void exit(Flow &f) {
	*f.pMain->pPlayers = *f.pMain->pPlayersSaved;             // MOVE.W LAB_05DB,LAB_05C5
}

}  // namespace

const SceneDef kScenePractice = {SceneId::Practice, "Practice", kAssets, countOf(kAssets), kEvents, countOf(kEvents), enter, run, exit, 0};

}}}  // namespace ms::game::flow
