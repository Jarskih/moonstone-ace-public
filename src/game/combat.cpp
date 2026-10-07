// game/combat - see include/game/combat.hpp.  Every function cites the mog.asm labels it transcribes.  Word and byte
// arithmetic follows the asm (8/16-bit wrap, signed compares where the asm branches with BGT/BLE/BPL/BMI).
#include "game/api/data.hpp"
#include "game/api/party.hpp"
#include "game/combat.hpp"

#include "game/rules.hpp"
#include "game/rules/waves.hpp"

namespace ms { namespace game {

namespace {

// ActiveKnights +16 values: the delay after the fight is decided before the loop ends (LAB_0006, LAB_004B).
enum { END_DELAY_WON = 0x23, END_DELAY_DEAD = 0x32 };
// LAB_004B: the key code (after LAB_0D8D) that pauses the fight until the next key.
enum { KEY_PAUSE = 0x20 };
// LAB_003E: the first fighter's warning shows at or below this hp.
enum { LOW_HP = 10 };

inline Knight &K(const FightEnv &e, uint32_t ulAddr) { return *e.m.pfnKnight(ulAddr); }
inline Inventory &I(const FightEnv &e, uint32_t ulAddr) { return *e.m.pfnInventory(ulAddr); }

// LAB_001C with the pointer lookups of the asm (the winner A0, the loser A1, their inventories 96(A0) / 96(A1)).
void lootTransfer(const FightEnv &e, Knight &winner, Knight &loser) {
	settleFight(winner, loser, I(e, knightInventoryAddr(winner)), I(e, knightInventoryAddr(loser)), e.aKnights, e.aInv);
}

// ---------------------------------------------------------------------------------------------------------
// LAB_0042: the three colour words of a low-hp warning, by the knight's kind.  The asm tests the kinds in the order
// 0, 1, 3, 2, anything else (the dragon and AI knights).
void warningColours(const FightEnv &e, const Knight &k) {
	uint16_t *p = e.c.pWarnColours;
	switch (k.ulKind) {
	case raw(KnightKind::Knight0): p[0] = 0x000C; p[1] = 0x0009; p[2] = 0x0006; break;
	case raw(KnightKind::Knight1): p[0] = 0x0FA0; p[1] = 0x0E70; p[2] = 0x0C50; break;
	case raw(KnightKind::Knight3): p[0] = 0x0D00; p[1] = 0x0900; p[2] = 0x0500; break;
	case raw(KnightKind::Knight2): p[0] = 0x0AE8; p[1] = 0x06B5; p[2] = 0x0473; break;
	default: p[0] = 0x0408; p[1] = 0x0305; p[2] = 0x0003; break;
	}
}

// LAB_003E: spawn the three warning jobs of a fighter whose hp has dropped to 10 or less.  The first fighter always
// gets them (job types 6, 7, 8), the second only on the arena kinds $0C and $10 (types 9, 10, 11).
void lowHpWarnings(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	if (c.pWarn[0] == 0) {                                          // TST.L LAB_05A5 ; BNE LAB_003F
		const Knight &k = K(e, c.pAct->ulCurrent);
		if (!((int16_t)k.swHp > LOW_HP)) {                          // CMP.W #$0A,D0 ; BGT LAB_003F
			warningColours(e, k);
			c.pWarn[0] = o.spawnWarning(6, c.pWarnColours[0]);      // SECSTRT_1
			c.pWarn[1] = o.spawnWarning(7, c.pWarnColours[1]);      // LAB_05A3
			c.pWarn[2] = o.spawnWarning(8, c.pWarnColours[2]);      // LAB_05A4
		}
	}
	if (*c.pArenaKind == 0x0C || *c.pArenaKind == 0x10) {           // LAB_003F
		if (c.pWarn[3] == 0) {                                      // TST.L LAB_05A8 ; BNE LAB_0041
			const Knight &k = K(e, c.pAct->ulOpponent);
			if (!((int16_t)k.swHp > LOW_HP)) {
				warningColours(e, k);
				c.pWarn[3] = o.spawnWarning(9, c.pWarnColours[0]);
				c.pWarn[4] = o.spawnWarning(10, c.pWarnColours[1]);
				c.pWarn[5] = o.spawnWarning(11, c.pWarnColours[2]);
			}
		}
	}
}

// LAB_0048: end the warning jobs by clearing the first long of each job record.
void clearWarnings(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	if (c.pWarn[0] != 0) {
		o.killJob(c.pWarn[0]);
		o.killJob(c.pWarn[1]);
		o.killJob(c.pWarn[2]);
	}
	if (c.pWarn[3] != 0) {
		o.killJob(c.pWarn[3]);
		o.killJob(c.pWarn[4]);
		o.killJob(c.pWarn[5]);
	}
}

// LAB_004B: a first fighter with negative hp ($FFFF marks the dead, LAB_000D) ends the fight after a delay; the key
// $20 pauses the fight until the next key press.  The key buffer is cleared at the end of every frame.
void endAndPause(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	ActiveKnights &act = *c.pAct;
	if (act.ubFightActive != 0) {                                   // TST.B 8(A2) ; BEQ LAB_004C
		if ((int16_t)K(e, act.ulCurrent).swHp < 0) {                // MOVE.W 80(A0),D0 ; BPL LAB_004C
			act.ubEndTimer = END_DELAY_DEAD;
			act.ubFightActive = 0;
		}
	}
	const uint16_t uwKey = o.translateKey(*c.pKey);                 // MOVE.W SECSTRT_21,D0 ; JSR LAB_0D8D
	if (uwKey == KEY_PAUSE) {
		o.keyReset();                                               // LAB_0B82
		while (*c.pKey == 0) {                                      // LAB_004D: TST.W SECSTRT_21 ; BEQ LAB_004D
		}
	}
	o.keyReset();                                                   // LAB_004E
}

// LAB_000A: toggle the pause word of every creature job except the one in LAB_05F4, then the dragon's.
void toggleCreaturePauses(const FightEnv &e, const FightOps &o) {
	uint32_t ulRec = *e.c.pCreatureBase;
	for (int i = 0; i < 20; ++i) {                                  // MOVE.L #$13,D1 ; DBF
		if (ulRec != *e.c.pSkipJob) o.togglePause(ulRec);           // CMP.L LAB_05F4,D0 ; BEQ LAB_000C
		ulRec += sizeof(Knight);                                    // ADDI.L #$84,D0
	}
	o.togglePause(e.a.ulDragonRecord);                              // MOVE.L #LAB_0617,D0 ; JSR LAB_0319
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// LAB_0036 (mog.asm 588)
void fightRun(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	ActiveKnights &act = *c.pAct;
	*c.pActionLatch = 0;
	*c.pSoundStep = 0;
	c.pWarn[0] = 0;                                                 // CLR.L LAB_05A5
	c.pWarn[3] = 0;                                                 // CLR.L LAB_05A8
	o.keyReset();
	act.ubFightActive = 1;
	*c.pFighter = act.ulCurrent;                                    // MOVE.L 0(A2),LAB_05F2
	o.clearScriptSlots();
	*c.pFrameFlag = 1;
	*c.pFrameStart = *c.pTick;                                      // MOVE.L LAB_0B9D,LAB_05AC
	o.flip();
	toggleCreaturePauses(e, o);
	if (c.pMode->ub == 4) o.initFightScreen();                      // CMPI.B #4,LAB_05DF

	do {
		do {                                                        // LAB_0037
			o.frameStart();
			o.jobPass();
			o.tick();
			o.flip();
			o.contactPass();
			o.drawPass();
			lowHpWarnings(e, o);
			if (*c.pExtraPassFlag != 0) o.extraPass();
			endAndPause(e, o);
			o.frameWait();
		} while (act.ubFightActive != 0);                           // TST.B 8(A2) ; BNE LAB_0037
		act.ubEndTimer = (uint8_t)(act.ubEndTimer - 1);             // SUBI.B #1,16(A2) ; BNE LAB_0037
	} while (act.ubEndTimer != 0);

	o.soundTick();
	// LAB_000E: the defeat word is cleared, then one bit per fighter whose hp ran out.
	const uint8_t ubBits = settleDefeats(K(e, act.ulCurrent), K(e, act.ulOpponent));
	c.pDefeat->ub = ubBits;
	c.pDefeat->ubSpare = 0;
	if (c.pMode->ub == 4) o.afterFight();
	clearWarnings(e, o);
	*c.pTurnSpent = *c.pTurnBudget;                                 // MOVE.W LAB_0665,LAB_0655
	*c.pBadLuck = 0;
	o.fadeIn();
	o.clearJobs();
	if (*c.pEncounterKind == raw(EncounterKind::Lair)) e.m.pfnLair(*c.pLair)->uwCreaturesLeft = c.pRules ? waveWriteback(*c.pRules, c.pRules->ubCoopEnabled != 0, *c.pFightTotal) : *c.pFightTotal;
}

// LAB_0006 (mog.asm 307)
void fightEnd(const FightEnv &e) {
	ActiveKnights &act = *e.c.pAct;
	if (act.ubFightActive != 0) {
		act.ubEndTimer = END_DELAY_WON;
		act.ubFightActive = 0;
	}
}

// LAB_0005 (mog.asm 298)
void fightCreatureDied(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	if ((int16_t)K(e, *c.pFighter).swHp <= 0) {                     // MOVE.W 80(A0),D0 ; BLE LAB_0006
		fightEnd(e);
		return;
	}
	*c.pAliveNow = (uint16_t)(*c.pAliveNow - 1);
	const int32_t slLeft = (int32_t)(int16_t)*c.pFightTotal - 1;     // SUBI.W #1 ; BGT: true signed result > 0
	*c.pFightTotal = (uint16_t)slLeft;
	if (!(slLeft > 0) && *c.pAliveNow == 0) {                      // BGT LAB_0008 ; TST.W LAB_05EE ; BNE LAB_0008
		fightEnd(e);
		return;
	}
	for (;;) {                                                      // LAB_0008
		if (*c.pMaxAlive == *c.pAliveNow) return;                 // CMP.W LAB_05EE,D0 ; BEQ LAB_0009
		const uint32_t ulFn = *c.pSpawnFn;
		if ((int16_t)*c.pFightTotal <= 0) return;                    // TST.W LAB_05EC ; BLE LAB_0009
		o.callSpawn(ulFn);                                          // JSR (A0)
	}
}

// ---------------------------------------------------------------------------------------------------------
// LAB_0058 (mog.asm 878)
bool fightAvoid(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	ActiveKnights &act = *c.pAct;
	const Knight &def = K(e, act.ulOpponent);
	if (def.ulKind == KIND_AI) return false;                        // CMPI.L #4,54(A1) ; BEQ LAB_005A
	*c.pAvoidWho = act.ulOpponent;                                  // MOVE.L A1,LAB_05D2
	if (I(e, knightInventoryAddr(def)).ubBattleAvoid == 0) return false;     // TST.B 18(A0) ; BEQ LAB_005A
	o.copyName(def.ulName);                                         // LAB_0059: LAB_06C8 := the name
	o.resetScreen();
	o.showAvoidText();
	o.waitFire();
	o.op03EB();
	*c.pLastSlot = 0xFFFF;
	const uint32_t ulFirst = act.ulCurrent;                         // the defender becomes the current knight for the screen
	act.ulCurrent = act.ulOpponent;
	o.screen(SCENE_TEMPLE);
	act.ulCurrent = ulFirst;
	bool bAvoided = false;
	if (*c.pLastSlot == 0x12) {                                     // CMPI.W #$12,LAB_053B
		bAvoided = true;
		if (*c.pBadLuck != 0) {                                     // TST.W LAB_05D3: the gamble failed
			bAvoided = false;
			*c.pAvoidBy = *c.pAvoidWho;                             // MOVE.L LAB_05D2,LAB_05D1
		}
	}
	return bAvoided;
}

// LAB_0065 (mog.asm 976)
static void arenaReset(const FightEnv &e, const FightOps &o) {
	o.clearJobs();
	o.clearObjects();
	o.op0155();
	o.fadeOut();
	o.keyReset();
	*e.c.pSkipJob = 0;                                              // CLR.L LAB_05F4
	*e.c.pDragonActive = 0;
}

// LAB_004F (mog.asm 800)
void fightMeet(const FightEnv &e, const FightOps &o, uint32_t ulFirst, uint32_t ulSecond) {
	const FightCells &c = e.c;
	ActiveKnights &act = *c.pAct;
	uint32_t ulA0 = ulFirst, ulA1 = ulSecond;
	o.resetScreen();                                                // A0/A1 are saved around LAB_0DC8
	act.ulCurrent = ulA0;
	act.ulOpponent = ulA1;

	if (K(e, ulA1).ubFrogDays == 0 && K(e, ulA1).ubLives != 0) {    // TST.B 82(A1) ; BNE LAB_0053 ; TST.B 73(A1) ; BEQ LAB_0053
		if (fightAvoid(e, o)) {                                     // LAB_0058 ; TST.W D0 ; BNE LAB_0054
			o.returnToMap();
			return;
		}
		ulA0 = act.ulCurrent;
		ulA1 = act.ulOpponent;
		Knight &a = K(e, ulA0);
		Knight &b = K(e, ulA1);
		if (a.ulKind == KIND_AI && b.ulKind == KIND_AI) {           // two AI knights never fight
			o.returnToMap();
			return;
		}
		uint8_t ubPort = raw(InputPort::Joy1);                                         // MOVEQ #2,D0
		if (a.ulKind != KIND_AI) {
			a.ubType = raw(ActorType::KnightFight);
			a.ubInputPort = ubPort;
			ubPort = raw(InputPort::Joy0);
		}
		if (b.ulKind != KIND_AI) {
			b.ubType = raw(ActorType::KnightFight);
			b.ubInputPort = ubPort;
		}
		arenaReset(e, o);                                           // BSR.W LAB_0065
		arenaMeet(e, o);                                            // JSR LAB_0164
		fightRun(e, o);                                             // JSR LAB_0036
		*c.pSwapped = 0;
		ulA0 = act.ulCurrent;
		ulA1 = act.ulOpponent;
		if (c.pDefeat->ub == (FIGHT_LOST_FIRST | FIGHT_LOST_SECOND)) {   // CMPI.B #3,LAB_05DC ; BEQ LAB_0055
			// LAB_0055: both lost: a human first knight sees the temple screen
			if (K(e, act.ulCurrent).ulKind != KIND_AI) o.screen(SCENE_TEMPLE);
			o.returnToMap();
			return;
		}
		if (c.pDefeat->ub & FIGHT_LOST_FIRST) {                     // BTST #0,LAB_05DC ; BEQ LAB_0053
			*c.pSwapped = 1;
			act.ulOpponent = ulA0;                                  // the loser is the second knight of the screen
			act.ulCurrent = ulA1;
			ulA0 = act.ulCurrent;
			ulA1 = act.ulOpponent;
		}
	}
	// LAB_0053: ulA0 is the winner (or the first knight of a meeting without a fight), ulA1 the other one.
	if (K(e, ulA0).ulKind == KIND_AI) {                             // CMPI.L #4,54(A0) ; BEQ LAB_0057
		Knight &w = K(e, ulA0);
		progressAward(w, PROGRESS_AI_KNIGHT_WIN);                    // ADDI.W #1,78(A0)
		lootTransfer(e, w, K(e, ulA1));                             // BSR.W LAB_001C
		o.returnToMap();
		return;
	}
	o.screen(SCENE_MEET);
	if (*c.pSwapped != 0) {                                         // TST.W LAB_05AD: swap the pair back (stale when no fight)
		const uint32_t ulT = act.ulCurrent;
		act.ulCurrent = act.ulOpponent;
		act.ulOpponent = ulT;
	}
	o.returnToMap();                                                // LAB_0054
}

// LAB_005F (mog.asm 948)
void lairTidy(const FightEnv &e) {
	const FightCells &c = e.c;
	if (*c.pTravel != 0) return;                                    // TST.W LAB_065E ; BNE LAB_0063
	Lair &lair = *e.m.pfnLair(*c.pLair);
	bool bUsed = lair.uwFlag8 != 0;                                 // TST.W 8(A0) ; BEQ
	const uint8_t *pLoot = reinterpret_cast<const uint8_t *>(&I(e, lair.ulLoot));
	for (unsigned i = 0; i < sizeof(Inventory); ++i)                // MOVE.L #$17,D7 ; TST.B (A1)+
		if (pLoot[i] != 0) bUsed = true;
	if (!bUsed) {
		lair.swMapX = -1;                                           // MOVE.L #$ffffffff,10(A0)
		lair.swMapY = -1;
	}
}

// LAB_005B (mog.asm 917)
void fightCreature(const FightEnv &e, const FightOps &o, uint32_t ulLair) {
	const FightCells &c = e.c;
	*c.pLair = ulLair;                                              // MOVE.L A1,LAB_08C6
	o.resetScreen();
	if (*c.pTravel == 0) {                                          // TST.W LAB_065E ; BNE LAB_005D
		arenaReset(e, o);
		arenaCreature(e, o);                                        // JSR LAB_01A3
		fightRun(e, o);
		*c.pCurrent = c.pAct->ulCurrent;                            // MOVE.L 0(A0),LAB_0633
		if (c.pDefeat->ub & FIGHT_LOST_FIRST) {                     // BTST #0,LAB_05DC ; BEQ LAB_005C
			o.screen(SCENE_TEMPLE);
			o.returnToMap();
			return;
		}
		Knight &k = K(e, *c.pCurrent);                              // LAB_005C
		progressAward(k, PROGRESS_CREATURE_FIGHT);
	}
	o.screen(SCENE_CREATURE);                                       // LAB_005D
	lairTidy(e);
	o.returnToMap();
	if (*c.pTravel != 0) o.mapStep();                               // JSR LAB_0E03
}

// LAB_0083 (mog.asm 1267)
void fightDragon(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	*c.pFighter = *c.pCurrent;                                      // MOVE.L LAB_0633,LAB_05F2
	Knight &me = K(e, *c.pFighter);
	bool bLost;
	if (me.ulKind == KIND_AI) {                                     // an AI knight loses a life and the dragon wins at once
		me.ubLives = (uint8_t)(me.ubLives - 1);
		bLost = true;
	} else if (me.ubFrogDays != 0 || me.ubLives == 0) {             // LAB_0084
		bLost = true;
	} else {
		arenaDragon(e, o);                                          // JSR LAB_0192
		fightRun(e, o);
		bLost = (c.pDefeat->ub & FIGHT_LOST_FIRST) != 0;
	}
	if (bLost) {                                                    // LAB_0085
		lootTransfer(e, K(e, e.a.ulDragonRecord), K(e, *c.pFighter));
		c.pDefeat->ub |= FIGHT_LOST_FIRST;                          // ORI.B #1,LAB_05DC
	} else {                                                        // LAB_0086
		K(e, e.a.ulDragonRecord).ubLives = 0xFF;                    // the dragon is marked
		Knight &k = K(e, *c.pFighter);
		progressAward(k, PROGRESS_DRAGON_SLAIN);
		o.screen(SCENE_DRAGON);
	}
	*c.pTurnSpent = *c.pTurnBudget;                                 // LAB_00B2
	o.returnToMap();                                                // LAB_00B3
}

// ---------------------------------------------------------------------------------------------------------
// LAB_0167 (mog.asm 3568)
void arenaKnightTables(const FightEnv &e, Knight &k) {
	k.ulActionScripts = e.a.ulActionScripts;
	k.ulHurtScripts = e.a.ulHurtScripts;
	k.ulDamageTable = e.a.ulDamageTable;
	knightSetWalkScripts(k, e.a.ulKnightWalkTab);
	k.ulDefenseTable = e.a.ulDefenseTable;
	k.ulIdleScript = e.a.ulIdleScript;
	k.ulScript26 = e.a.ulAltScript;
	k.ulJobParam = e.a.ulJobParamKnight;
	k.uwReachX = 0x64;
	k.uwDepthReach = 4;
	k.uwTooCloseX = 0x50;
}

namespace {

// The counters every knight arena ends with: one fighter's worth of creatures, no spawn routine.
void arenaCounters(const FightEnv &e, bool bSecondSpawn) {
	const FightCells &c = e.c;
	*c.pFrameDelay = 6;
	*c.pMaxAlive = 1;
	*c.pFightTotal = 1;
	*c.pAliveNow = 0;
	*c.pSpawnFn = e.a.ulNoSpawn;
	if (bSecondSpawn) *c.pSpawnFn2 = e.a.ulNoSpawn;
}

// LAB_0195: the dragon's and the bats' shared record values.
void dragonCommon(const FightEnv &e, Knight &k) {
	// All of LAB_0195 (mog.asm 4028).  The map flight (LAB_0DCB, overworld.cpp dragonSpawn) leaves the dragon record with type $28, the flight
	// walk scripts LAB_08FC and its own cel work area, so the arena has to rewrite every one of these fields, the type included:
	// without it the arena dragon ran the map-flight handler and drew the map-flight cels (garbage, never attacked).
	k.ulDamageTable = e.a.ulDragonDamage;                           // MOVE.L #LAB_0603,42(A1)
	k.ulJobParam = e.a.ulJobParamFight;                             // MOVE.L #LAB_05E0,38(A1)
	k.ulHurtScripts = e.a.ulDragonHurtTab;                          // MOVE.L #LAB_0604,30(A1)
	knightSetWalkScripts(k, e.a.ulDragonWalkTab);                         // MOVE.L #LAB_060F,46(A1)
	k.ulIdleScript = e.a.ulDragonIdle;                              // MOVE.L #LAB_0882,22(A1)
	k.ulScript26 = e.a.ulDragonIdle;                                // MOVE.L #LAB_0882,26(A1)
	k.uwReachX = 0x3C;
	k.uwTooCloseX = 0x14;
	k.uwDepthReach = 5;
	k.swHp = 0x78;                                                  // the bats too: their $32 is overwritten by this call (asm order)
	k.swHpMax = 0x78;
	k.ubType = raw(ActorType::Dragon);
	k.ubFacing = 1;
	k.ubInputPort = raw(InputPort::Ai);
}

// creatures.ini row `dragon` over the numbers LAB_0195 and the damage pokes of LAB_0192 just wrote (ROADMAP 9.7): hit points, the three
// distances and the damage slots that are set (-1 = keep).  The bats and the scripts/tables keep the original.
void dragonRowApply(const FightEnv &e, const FightOps &o, const CreatureDef &d, Knight &dr) {
	if (d.swHp >= 0) { dr.swHp = d.swHp; dr.swHpMax = d.swHpMax >= 0 ? d.swHpMax : d.swHp; }
	else if (d.swHpMax >= 0) dr.swHpMax = d.swHpMax;
	if (d.swReach >= 0) dr.uwReachX = (uint16_t)d.swReach;
	if (d.swTooClose >= 0) dr.uwTooCloseX = (uint16_t)d.swTooClose;
	if (d.swDepth >= 0) dr.uwDepthReach = (uint16_t)d.swDepth;
	for (uint8_t i = 0; i < d.ubDamageCount && i < CREATURE_DAMAGE_SLOTS; ++i)
		if (d.aDamage[i] >= 0) o.poke32(e.a.ulDragonDamage + 4u * i, (uint32_t)d.aDamage[i]);
}

// One bat of the dragon's arena (LAB_0192 +37..): a creature record at x 5, hp $32, bat scripts.
void spawnBat(const FightEnv &e, const FightOps &o, uint32_t *pHandle, uint16_t uwY) {
	const uint32_t ulRec = o.allocCreature();                       // JSR LAB_0171
	*pHandle = ulRec;
	Knight &b = K(e, ulRec);
	b.uwX = 5;
	b.uwHeight = 0;
	b.uwY = uwY;
	b.swHp = 0x32;
	b.swHpMax = 0x32;
	dragonCommon(e, b);
	b.ubType = raw(ActorType::DragonPart);
	b.ulIdleScript = e.a.ulBatScript;
	b.ulScript26 = e.a.ulBatScript;
	b.uwDepthReach = 0x0A;
	o.spawn(ulRec, b.ulIdleScript);                                 // JSR LAB_01A8 (A0 = 22(A1))
}

}  // namespace

// LAB_0164 (mog.asm 3517)
void arenaMeet(const FightEnv &e, const FightOps &o) {
	o.commonSetup(2);                                               // MOVEQ #2,D0 ; BSR.W LAB_016F
	o.op0116();
	const uint32_t ulRec = e.c.pAct->ulOpponent;
	*e.c.pTarget = ulRec;                                           // MOVE.L A1,LAB_0634
	Knight &k = K(e, ulRec);
	k.uwX = 0x1E;
	k.uwHeight = 0;
	k.uwY = 0x4B;
	k.ubFacing = 1;
	arenaKnightTables(e, k);
	k.ulJobParam = e.a.ulJobParamFight;                             // MOVE.L #LAB_05E0,38(A1)
	o.spawn(ulRec, e.a.ulSpawnScript);                              // JSR LAB_01A9 with A0 = LAB_07FC
	arenaCounters(e, false);
	o.paletteMode(12);                                              // MOVEQ #12,D0 ; JMP LAB_03F3
}

// LAB_0165 (mog.asm 3539)
void arenaPractice(const FightEnv &e, const FightOps &o) {
	o.op0100(2);
	o.op015F();
	o.clearObjects();
	o.clearJobs();
	o.op0116();
	*e.c.pCurrent = e.a.ulKnight0;                                  // MOVE.L #LAB_0613,LAB_0633
	o.placeAttacker();                                              // JSR LAB_01A4
	Knight &k = K(e, e.a.ulKnight1);
	*e.c.pTarget = e.a.ulKnight1;
	k.uwX = 0x1E;
	k.uwHeight = 0;
	k.ubFacing = 1;
	arenaKnightTables(e, k);
	k.ulJobParam = e.a.ulJobParamFight;
	k.ubInputPort = raw(InputPort::Joy0);
	o.spawn(e.a.ulKnight1, e.a.ulSpawnScript);
	arenaCounters(e, false);
	*e.c.pArenaParam = 0x0C;                                        // MOVE.L #$c,LAB_08C4
	o.paletteMode(12);
}

// LAB_0192 (mog.asm 3966)
void arenaDragon(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	o.commonSetup(3);
	o.op0121();
	const uint32_t ulHurt = K(e, *c.pCurrent).ulHurtScripts;        // MOVEA.L LAB_0633,A1 ; MOVEA.L 30(A1),A0
	o.poke32(ulHurt + 4, e.a.ulDragonHurtA);
	o.poke32(ulHurt + 8, e.a.ulDragonHurtB);
	o.poke32(ulHurt + 32, e.a.ulDragonHurtB);
	o.poke32(ulHurt + 20, e.a.ulDragonHurtC);
	o.poke32(e.a.ulDragonDamage + 8, 0x1E);                         // LEA LAB_0603,A0
	o.poke32(e.a.ulDragonDamage + 32, 0x1E);
	o.poke32(e.a.ulDragonDamage + 20, 0x0A);
	o.poke32(e.a.ulDragonDamage + 4, 0x0A);
	Knight &dr = K(e, e.a.ulDragonRecord);
	dr.uwX = 0x50;
	dr.uwHeight = 0xFFD8;
	dr.uwY = 0x64;
	dr.ubFacing = 1;
	dragonCommon(e, dr);
	if (e.pDragonRow) dragonRowApply(e, o, *e.pDragonRow, dr);      // creatures.ini row `dragon` (ROADMAP 9.7): the set-up kept the original numbers
	*c.pDragonFlags = 0;
	o.spawn(e.a.ulDragonRecord, dr.ulIdleScript);                   // JSR LAB_01A8
	spawnBat(e, o, c.pHandleBat1, 0x50);
	spawnBat(e, o, c.pHandleBat2, 0x78);
	arenaCounters(e, true);
	o.paletteMode(0x14);
}

// LAB_01A3 (mog.asm 4195)
void arenaCreature(const FightEnv &e, const FightOps &o) {
	const FightCells &c = e.c;
	const Lair &lair = *e.m.pfnLair(*c.pLair);
	*c.pEncounterKind = 2;
	*c.pLairParam = lair.ulParam16;
	*c.pArenaParam = lair.uwParam14;                                // MOVEQ #0,D0 ; MOVE.W 14(A0),D0
	o.creatureArena((int32_t)(int16_t)lair.uwHandlerOfs);           // MOVE.W 4(A0),D0 ; EXT.L D0 ; MOVEA.L 0(A1,D0.L),A1 ; JSR (A1)
}

// ---------------------------------------------------------------------------------------------------------
// LAB_04E1 (mog.asm 10635)
void screenPalette(const ScreenEnv &e) {
	const ScreenCells &c = e.c;
	uint32_t ulRec = *c.pKnightA;
	uint16_t *p = c.pPalette;
	for (int pass = 0; pass < 2; ++pass) {                          // MOVEQ #1,D0 ; DBF D0,LAB_04E2
		switch (e.pfnKnight(ulRec)->ulKind) {
		case raw(KnightKind::Knight0): *p++ = 0x003F; *p++ = 0x0028; break;
		case raw(KnightKind::Knight1): *p++ = 0x0FB0; *p++ = 0x0B60; break;
		case raw(KnightKind::Knight3): *p++ = 0x0F00; *p++ = 0x0800; break;
		case raw(KnightKind::Knight2): *p++ = 0x04C3; *p++ = 0x0160; break;
		default: *p++ = 0x0027; *p++ = 0x0003; break;
		}
		const uint32_t s = *c.pScene;
		if (s != SCENE_MEET && s != SCENE_EXCHANGE_8 && s != SCENE_EXCHANGE_11) return;   // LAB_04E7
		ulRec = *c.pKnightB;                                        // MOVEA.L LAB_068D,A1
	}
}

// LAB_04D4 (mog.asm 10586)
void screenRedraw(const ScreenEnv &e, const ScreenOps &o) {
	const ScreenCells &c = e.c;
	for (;;) {
		*c.pTextFlag = 1;
		o.op044E();
		o.op04EA();
		o.blitBackground();
		o.setTarget();
		o.lootSetup();
		o.op03A7();
		*c.pButtons = e.a.ulUseButtons;
		*c.pStatKnight = *c.pKnightA;                               // MOVE.L LAB_068B,LAB_0632
		*c.pXShift = 0;
		*c.pLineBase = 0;
		const uint32_t ulScene = *c.pScene;
		for (const uint16_t *p = c.pWideScenes; (int16_t)*p >= 0; ++p) {
			if ((uint16_t)ulScene == *p) {                          // CMP.W D1,D0 (the low word of the scene)
				*c.pXShift = 0x4A;
				break;
			}
		}
		o.drawStats();                                              // LAB_04D6: BSR.W LAB_04F8
		*c.pButtons = e.a.ulTakeButtons;
		*c.pXShift = 0x96;
		if (ulScene == SCENE_CREATURE) {
			o.op051B();
		} else if (ulScene == SCENE_MEET) {
			if (!((int8_t)e.pfnKnight(*c.pKnightB)->ubLives > 0)) *c.pChanged = 0;   // TST.B 73(A0) ; BGT LAB_04D8
			if (*c.pChanged != 0) {                                 // the opponent was looted: on to the temple screen
				*c.pScene = SCENE_TEMPLE;
				continue;
			}
			*c.pStatKnight = *c.pKnightB;
			*c.pLineBase = 1;
			o.drawStats();
		} else if (ulScene == SCENE_EXCHANGE_11 || (ulScene == SCENE_EXCHANGE_8 && *c.pChanged == 0)) {
			*c.pStatKnight = *c.pKnightB;
			*c.pLineBase = 1;
			o.drawStats();
			o.op0524();
		} else if (ulScene == SCENE_EXCHANGE_8) {                   // LAB_04DC: changed: back to the previous scene
			*c.pChanged = 0;
			*c.pScene = *c.pPrevScene;
			continue;
		} else {
			if (ulScene == SCENE_SMITH) o.op0522();                 // LAB_04DD
			if (ulScene == SCENE_MARKET) {                          // LAB_04DE
				*c.pStatKnight = e.a.ulMarketRecord;
				o.op04FE();
				o.op051F(*c.pStatKnight);
			}
			if (ulScene == SCENE_DRAGON) o.op051D();                // LAB_04DF
		}
		break;
	}
	screenPalette(e);                                               // LAB_04E0
	o.flip();
	o.blitScreen();
	o.waitBlitter();
	o.op0D8A();
	o.op03EE();
}

// LAB_04CF (mog.asm 10547)
void screenRun(const ScreenEnv &e, const ScreenOps &o, uint32_t ulScene) {
	const ScreenCells &c = e.c;
	*c.pTextFlag = 1;
	*c.pSprites = *c.pSpriteSrc;                                    // LEA LAB_05E2,A0 ; MOVE.L 4(A0),LAB_0986
	*c.pChanged = 0;
	*c.pScene = ulScene;
	*c.pDone = 0;
	o.fadeOut();
	o.op0575();
	o.op0588();
	screenRedraw(e, o);                                             // JSR LAB_04D4
	for (;;) {                                                      // LAB_04D0
		const uint32_t ulRegion = o.hitTest(*c.pCursorX, *c.pCursorY);
		if (ulRegion != 0 && *c.pCursorLock == 0) {                 // TST.L D0 ; BEQ ; TST.W LAB_0981 ; BNE
			o.click(ulRegion);
			if (*c.pDone != 0) break;                               // TST.W LAB_0984 ; BNE LAB_04D2
		}
		o.flip();
		o.drawPass();
	}
	o.fadeOut();                                                    // LAB_04D2
	o.op057B();
	if (*c.pHandOver != 0) {                                        // TST.W LAB_053C
		knightEngage(*c.pDragon, c.pAct->ulOpponent);                        // MOVE.L 4(A1),100(A0) with A0 = LAB_0617
		*c.pHandOver = 0;
	}
	*c.pTextFlag = 0;
}

}}  // namespace ms::game
