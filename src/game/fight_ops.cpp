// game/fight_ops - the routines the combat scripts call through opcode $B0, the handler table fill and the knight defaults
// of mog.asm in C++ (ROADMAP 7.1h).  Transcribed from mog.asm; the labels in the comments are mog's.
//
// Script-called routines (fightOpRun): in the script data the operand of $B0 was the address of one of these asm routines;
// asm/patches/mog.fight_ops.json turns each of those operands into FIGHT_OP_TAG | label (fighters.hpp), so nothing jumps to
// the asm bodies, which are dead.  The registers the original read are the arguments: A1 = the owner record, A2 = the frame
// table list, D0..D2 = x / y / z, D3 = facing.  The sound pickers LAB_02DC..02E9 (soundPick), the dagger throw LAB_02CA
// (daggerRelease), the dragon's walk LAB_028E (dragonStep), the hop LAB_0210 / 0211, the counter LAB_01C8, LAB_02DB and the
// S_40 ones: five sound routines and the demon's job / record plumbing LAB_0EEB..LAB_0EF7.
//
// Deliberate differences: a job lookup that misses where the asm then reads or writes through address 0 does nothing; the
// demon's messages (LAB_0EF6) went through LAB_0BB3, a bare RTS, and are left out.
#include "game/api/party.hpp"
#include "engine/util.hpp"
#include "game/fighters.hpp"
#include "game/fighters_int.hpp"

namespace ms { namespace game {

using namespace fi;

namespace {

// LAB_0EC0 / LAB_0ECF: the sound ids of LAB_0EBD / LAB_0ED0 (bytes of the code hunk).
const uint8_t kIds0EC0[4] = {0x53, 0x54, 0x55, 0x55};
const uint8_t kIds0ECF[8] = {0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x89};

// SUBI.B #1,13(A1) ; BGE: the branch reads the true signed result (N xor V), a byte that was $80 does not count as "not
// negative" after the wrap.
bool countDownGe(uint8_t &ub) {
	const int r = (int)(int8_t)ub - 1;
	ub = (uint8_t)r;
	return r >= 0;
}

// LAB_0211 (and LAB_0213): one hop step.  The facing comes from LAB_08D0; facing 1 walks right (BSET #0), anything else
// left (BSET #1); the arena edge clip LAB_0215 may drop the bit, and the job moves by the hop table's entry for the step.
void hopStep(const FighterEnv &e, Knight &k) {
	++*e.pHopStep;                                                 // ADDI.W #1,LAB_08CF
	*e.pSelf = addr(&k);
	k.uwInput = 0;
	k.ubFacing = *e.pHopDir;
	const bool right = *e.pHopDir == 1;
	k.uwInput = (uint16_t)(k.uwInput | (right ? 1u : 2u));
	clipToArena(k);
	if(!(k.uwInput & (right ? 1u : 2u))) {
		return;
	}
	CombatJob *j = jobOfOwner(e.pJobs, *e.pSelf);                  // JSR LAB_0315
	if(j == 0) {
		return;
	}
	const uint16_t d1 = rd16(jobPtr<const uint8_t>(*e.pHopTable + (uint32_t)(int32_t)(int16_t)(uint16_t)(*e.pHopStep << 1)));
	j->uwX = (uint16_t)(right ? j->uwX + d1 : j->uwX - d1);        // ADD.W D1,6(A0) / SUB.W D1,6(A0)
}

// The two plays of LAB_0EB9 / LAB_0EEF: sequences by the draw's low two bits.
void soundPair(const FighterEnv &e, uint16_t a, uint16_t b) {
	e.sound(a);
	e.sound(b);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// LAB_01C6: the knight defaults

void knightDefaults(Knight &k) {
	k.ubStrength = 1;
	k.ubConstitution = 1;
	k.ubEndurance = 1;
	k.ubLives = 5;
	k.swHp = 0x14;
	k.ulArmour = raw(ArmourItem::Padded);
	k.ulSword = raw(SwordItem::Long);
	k.uwProgress = 0;
	k.ubDaggers = 0x0A;
	k.uwGold = 0x0A;
	k.ubRecency = 0xFF;
	k.ubLifeLoss = 0;
	knightDisengage(k);
	uint8_t *inv = jobPtr<uint8_t>(knightInventoryAddr(k));                 // MOVEA.L 96(A1),A0 ; CLR.B (A0)+ x 24
	for(unsigned i = 0; i < 24; ++i) {
		inv[i] = 0;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// The handler table

void fightTablesInit(const FighterEnv &e) {
	// One slot per row of the type table (rules/ai_fight.cpp kFightAis): the handler of the AI that type runs.
	for(uint8_t i = 0; i < kFightAiCount; ++i) {
		wr32(e.pHandlerTable + raw(kFightAis[i].type), fighterHandlerAddress(e.scripts, kFightAis[i].kind));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// $B0 targets

bool fightOpRun(const FighterEnv &e, uint32_t ulFn, uint32_t ulOwner, uint32_t ulFrames, uint16_t uwX, uint16_t uwY,
                uint16_t uwZ, uint8_t ubFacing) {
	if((ulFn & 0xFFFF0000u) != FIGHT_OP_TAG) {
		return false;
	}
	Knight &o = *rec(ulOwner);
	switch(ulFn & 0xFFFFu) {
	case 0x01C8:                                                   // the owner's timer: reloaded to $1E after a draw
		if(!countDownGe(o.ubAnimTimer)) {
			rngNext(*e.pSeed);
			o.ubAnimTimer = 0x1E;
		}
		return true;
	case 0x0210:
		*e.pHopStep = 0xFFFF;
		return true;
	case 0x0211:
		hopStep(e, o);
		return true;
	case 0x028E:
		dragonStep(e);
		return true;
	case 0x02AC:
		e.sfxFixed(0, 0x1D);
		return true;
	case 0x02AD:
		e.sfxFixed(1, 0x1B);
		return true;
	case 0x02AE:
		soundPair(e, 0x30, 0x12);
		return true;
	case 0x02CA:                                                   // the dagger throw (the result is not used by a script)
		daggerRelease(e.pJobs, e.pCreatures, o, e.scripts.sDaggerScript, e.scripts.tDaggerDamage, ulFrames, uwX, uwY, uwZ,
		              ubFacing);
		return true;
	case 0x02DB:
		e.pVars->uwLairFlag = 1;
		return true;
	case 0x02DC: soundPick(SOUND_02DC, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02DE: soundPick(SOUND_02DE, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E0: soundPick(SOUND_02E0, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E1: soundPick(SOUND_02E1, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E2: soundPick(SOUND_02E2, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E3: soundPick(SOUND_02E3, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E4: soundPick(SOUND_02E4, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E6: soundPick(SOUND_02E6, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E8: soundPick(SOUND_02E8, *e.pSeed, *e.pPickCycle, e.sound); return true;
	case 0x02E9: soundPick(SOUND_02E9, *e.pSeed, *e.pPickCycle, e.sound); return true;
	// ---- S_40 ----
	case 0x0EB8:
		e.sfxFixed(0, 0x50);
		return true;
	case 0x0EB9:
		switch(rngNext(*e.pSeed) & 3) {
		case 0: soundPair(e, 0x57, 0x94); break;
		case 1: soundPair(e, 0x58, 0x95); break;
		case 2: soundPair(e, 0x59, 0x96); break;
		default: soundPair(e, 0x97, 0x98); break;
		}
		return true;
	case 0x0EBD:
		e.sound(kIds0EC0[rngNext(*e.pSeed) & 3]);
		return true;
	case 0x0EBE:
		*e.pBatCounter = (uint16_t)((*e.pBatCounter + 1) & 3);
		if(*e.pBatCounter == 0) {
			e.sound(0x5A);
		}
		return true;
	case 0x0ED0:                                                   // two draws, two sounds
		for(int i = 0; i < 2; ++i) {
			e.sound(kIds0ECF[rngNext(*e.pSeed) & 7]);
		}
		return true;
	case 0x0EEB: {                                                 // the body record gets an object job at the body's job
		CombatJob *j = jobOfOwner(e.pJobs, *e.pBodyA);
		if(j == 0) {
			return true;
		}
		Knight &b = *rec(*e.pBodyB);
		const uint16_t d0 = j->uwX, d1 = j->uwY, d2 = j->uwX;      // MOVE.W 6(A0),D2: the x again, as in the asm
		const uint8_t d3 = j->ubFlags;
		b.uwX = d0;
		b.uwHeight = d1;
		b.uwY = d2;
		b.ubFacing = d3;
		jobCreate(e.pJobs, e.scripts.s08AF, addr(&b), ulFrames, d0, d1, d2, d3, 8);
		return true;
	}
	case 0x0EEC: {                                                 // the body's job takes the facing of the demon's
		CombatJob *jb = jobOfOwner(e.pJobs, *e.pBodyB);
		CombatJob *ja = jobOfOwner(e.pJobs, *e.pBodyA);
		if(ja != 0 && jb != 0) {
			jb->ubFlags = ja->ubFlags;
		}
		return true;
	}
	case 0x0EED:
		jobKill(e.pJobs, *e.pBodyB);
		return true;
	case 0x0EEE:
		e.sfxFixed(0, 0x4A);
		e.sfxFixed(1, 0x4B);
		e.sfxFixed(2, 0x4C);
		e.sfxFixed(3, 0x4D);
		return true;
	case 0x0EEF:
		switch(rngNext(*e.pSeed) & 3) {
		case 1: soundPair(e, 0x42, 0x43); break;
		case 2: soundPair(e, 0x44, 0x45); break;
		default: soundPair(e, 0x40, 0x41); break;
		}
		return true;
	case 0x0EF6: {                                                 // "DEMON HIT" (no text player), 10 damage, frozen
		subHp(*rec(*e.pFirst), 10);
		jobTogglePause(e.pJobs, *e.pFirst);
		return true;
	}
	case 0x0EF7: {                                                 // the demon lets go of the first fighter
		jobTogglePause(e.pJobs, *e.pFirst);
		CombatJob *j = jobOfOwner(e.pJobs, *e.pFirst);
		const Knight &body = *rec(*e.pBodyA);
		if(j != 0) {
			const uint8_t d0 = (uint8_t)(body.ubFacing ^ 2);
			j->ubFlags = d0;
			j->uwZ = body.uwY;
			int16_t d1 = (int16_t)body.uwX;
			d1 = (int16_t)(d1 + (d0 == 1 ? -0x89 : 0x89));
			if(!(d1 < 0x140)) {
				d1 = 0x13F;
			}
			if(d1 < 0) {
				d1 = 1;
			}
			j->uwX = (uint16_t)d1;
		}
		const Knight &first = *rec(*e.pFirst);
		uint32_t script = first.ulIdleScript;
		if(!((int16_t)first.swHp > 0)) {                           // dead: the screen fades out first
			for(unsigned i = 2; i < 8; ++i) {
				e.pFade[i] = 0;
			}
			e.fadeA();
			e.fadeB();
			script = e.scripts.s07F7;
		}
		jobRestart(e.pJobs, *e.pFirst, script);
		return true;
	}
	default:
		return false;
	}
}

}}  // namespace ms::game
