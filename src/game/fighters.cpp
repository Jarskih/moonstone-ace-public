// game/fighters - see fighters.hpp.  Transcribed from mog.asm LAB_01CA..LAB_0300 and the movement helpers
// LAB_0F1A..LAB_0F34.  Labels in the comments are mog's.  Word operations of the asm are 16-bit here (casts to
// uint16_t / int16_t), branches are the signed / unsigned conditions the 68k evaluates (BGT / BLE / BLT / BGE signed,
// BEQ / BNE on the result of the compare), byte flag tests (BTST #n,63(A1)) are bit tests of the record's input word.
#include "game/api/party.hpp"
#include "game/fighters.hpp"

#include "engine/util.hpp"
#include "game/fighters_int.hpp"
#include "game/rules/ai_fight.hpp"
#include "game/rules/damage.hpp"

namespace ms { namespace game {


namespace {

// ---- constants the asm keeps in its data ------------------------------------------------------------------------

// LAB_08C9 / LAB_08CA / LAB_08CB: x / up / down steps of the walk cycle, indexed by the walk phase 0..3.
const uint16_t kStepX[4] = {0x0019, 0x0003, 0x0017, 0x0004};      // LAB_08C9
const uint16_t kStepUp[4] = {0x0002, 0x0009, 0x0002, 0x0009};     // LAB_08CA (subtracted)
const uint16_t kStepDown[4] = {0x0008, 0x0002, 0x0009, 0x0002};   // LAB_08CB

// LAB_07D9 / LAB_07DA: action offset per input word (bits 0..3 = right, left, down, up; the fire bit is masked out),
// facing right / left.  The asm tables are adjacent (LAB_07D9 has 11 words, then LAB_07DA), so entries 11..15 of the
// right-facing table are the first five of the left-facing one.
const uint16_t kActionLeft[16] = {0x0000, 0x0014, 0x0008, 0x0000, 0x001C, 0x0010, 0x0004, 0x0000,
                                  0x0020, 0x000C, 0x0018, 0x0016, 0x0000, 0x0000, 0x0000, 0x0040};
const uint16_t kActionRight[16] = {0x0000, 0x0008, 0x0014, 0x0000, 0x001C, 0x0004, 0x0010, 0x0000,
                                   0x0020, 0x0018, 0x000C, 0x0000, 0x0014, 0x0008, 0x0000, 0x001C};

// LAB_024E / LAB_024F / LAB_0250: {dx, dy} per walk phase of the brawler (LAB_0F1A..LAB_0F1F).  The three tables are
// adjacent in the asm (6 + 5 + 5 entries); a phase past the end of one reads the next, and past the last reads code,
// which this port reads as 0 (phases are 0..7 and only entries 3.. of the last table could go there).
const int16_t kMoveTab[32] = {0, -1, 7, 1, 0x17, 0, 0, -1, 7, 1, 0x17, 0,                  // LAB_024E
                              0, 10, 0, 3, 1, 7, -1, 3, 0, 10,                              // LAB_024F
                              3, 10, 2, 4, 1, 4, 4, 2, 0, 10};                              // LAB_0250
const uint32_t kTabWalk = 0, kTabAttackUp = 12, kTabAttackDown = 22;                        // word offsets of the three tables

// ---- game memory access ---------------------------------------------------------------------------------------------

inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
inline void wr32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

inline Knight *rec(uint32_t a) { return jobPtr<Knight>(a); }
inline uint32_t addr(const void *p) { return jobAddr(p); }

// MOVE.L 0(A,D.W),X: a long at a base address plus the sign-extended word index.
inline uint32_t tab32(uint32_t ulBase, uint16_t uwIndex) {
	return rd32(jobPtr<const uint8_t>(ulBase + (uint32_t)(int32_t)(int16_t)uwIndex));
}

// The word at +106 (timer byte, then the byte after it), big-endian like the 68k sees it.
inline uint16_t cooldownWord(const Knight &k) { return (uint16_t)((k.ubCooldown << 8) | k.ubCooldownLo); }
inline void setCooldownWord(Knight &k, uint16_t v) {
	k.ubCooldown = (uint8_t)(v >> 8);
	k.ubCooldownLo = (uint8_t)v;
}
// A word write to +104 covers the two flag bytes.
inline void setBehaviourWord(Knight &k, uint16_t v) {
	k.ubBehaviourFlags = (uint8_t)(v >> 8);
	k.ubBehaviourFlags2 = (uint8_t)v;
}

inline bool bit(uint32_t v, unsigned n) { return ((v >> n) & 1u) != 0; }
inline void subHp(Knight &k, uint16_t d) { k.swHp = (int16_t)((uint16_t)k.swHp - d); }

// LAB_02BA: the common exit.  A1 is LAB_0633's record at this point (the dragon's LAB_028E reassigns it).
HandlerResult finish(const FighterEnv &e) {
	const Knight &k = *rec(*e.pSelf);
	HandlerResult r;
	r.ulScript = e.pVars->ulNextScript;
	r.uwX = k.uwX;
	r.uwY = k.uwHeight;
	r.uwZ = k.uwY;
	r.ubFacing = k.ubFacing;
	return r;
}

inline uint16_t damageOf(const FighterEnv &e, const Knight &attacker) {      // JSR LAB_021B -> D0 (the callers use D0.W)
	return (uint16_t)contactDamage(attacker, jobPtr<const uint8_t>(attacker.ulDamageTable),
	                               *jobPtr<const Inventory>(knightInventoryAddr(attacker)), e.pActive->uwMoonFrame);
}

inline Knight &dragonOf(const FighterEnv &e) { return e.pKnights[4]; }       // LAB_0617

// LAB_0006 / LAB_000D: the end-of-fight timer and "the current fighter is dead".
void fightTimer(const FighterEnv &e) {
	ActiveKnights &a = *e.pActive;
	if(a.ubFightActive != 0) {
		a.ubEndTimer = 0x23;
		a.ubFightActive = 0;
	}
}
void killCurrent(const FighterEnv &e) { rec(e.pActive->ulCurrent)->swHp = -1; }

// LAB_02C6: the first fighter's job takes the opposite horizontal facing (BCHG #1) and the record follows.
void flipFirstFacing(const FighterEnv &e) {
	CombatJob *j = jobOfOwner(e.pJobs, *e.pFirst);
	if(j != 0) {
		j->ubFlags ^= 2;
		rec(*e.pFirst)->ubFacing = j->ubFlags;
	}
}

inline void playSound(const FighterEnv &e, uint16_t id) { e.sound(id); }

// LAB_00EA: the joystick that belongs to the record (+11 == 1: port 0, else port 1).
uint16_t inputFor(const FighterEnv &e, const Knight &k) {
	const Joystick j = e.joystick();
	return k.ubInputPort == raw(InputPort::Joy0) ? j.uwPort0 : j.uwPort1;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Job table helpers (LAB_0315 / LAB_0319 / LAB_031B / LAB_030D)

// LAB_0315: the first of the ten job slots (active or not) whose owner is ulOwner.
CombatJob *jobOfOwner(CombatJob *pJobs, uint32_t ulOwner) {
	for(uint32_t i = 0; i < COMBAT_JOB_COUNT; ++i) {
		if(pJobs[i].ulOwner == ulOwner) {
			return &pJobs[i];
		}
	}
	return 0;
}

// LAB_0319: EORI.W #1,48(job).
void jobTogglePause(CombatJob *pJobs, uint32_t ulOwner) {
	CombatJob *j = jobOfOwner(pJobs, ulOwner);
	if(j != 0) {
		j->uwPaused ^= 1;
	}
}

// LAB_031B: MOVE.W #0,0(job) (active and running), CLR.L (owner): the record is free.
void jobKill(CombatJob *pJobs, uint32_t ulOwner) {
	CombatJob *j = jobOfOwner(pJobs, ulOwner);
	if(j != 0) {
		j->ubActive = 0;
		j->ubRunning = 0;
		jobPtr<Knight>(j->ulOwner)->ulActive = 0;
	}
}

// LAB_030D: the job of A1 gets a cleared work block, the script A0 and the running flag.
void jobRestart(CombatJob *pJobs, uint32_t ulOwner, uint32_t ulScript) {
	CombatJob *j = jobOfOwner(pJobs, ulOwner);
	if(j != 0) {
		uint8_t *w = jobPtr<uint8_t>(j->ulWork);
		for(uint32_t i = 0; i < sizeof(CombatWork); ++i) {
			w[i] = 0;
		}
		j->ulScript = ulScript;
		j->ubRunning = 1;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared pieces of the knight

// LAB_0215.  Called with D0 / D1 = the steps, which it ignores.
void clipToArena(Knight &k) {
	int16_t d0 = 0x19;
	if(bit(k.ubFacing, 1)) {
		d0 = (int16_t)-d0;
	}
	d0 = (int16_t)(d0 + (int16_t)k.uwX);
	const int16_t d1 = (int16_t)(9 + (int16_t)k.uwY);
	if(d0 < 10) {
		k.uwInput &= (uint16_t)~2u;
	}
	if(d0 > 0x140) {
		k.uwInput &= (uint16_t)~1u;
	}
	if(d1 > 0x9B) {
		k.uwInput &= (uint16_t)~4u;
	}
	if(d1 < 0x1E) {
		k.uwInput &= (uint16_t)~8u;
	}
}

// LAB_01E6.  The decision is rules/damage.cpp blockDecide; the stance latch (+104 bit 7) and the parry sound are done here.
bool blockCheck(const FighterEnv &e, Knight &self, const Knight &attacker) {
	const uint16_t uwRequired = (uint16_t)tab32(self.ulDefenseTable, attacker.uwAction);
	const Parry parry = blockDecide(uwRequired, self.uwAction, (self.ubBehaviourFlags & 0x80) != 0, self.ubFacing, attacker.ubFacing);
	if(parry == Parry::Stance) {                      // LAB_01E9: the stance blocks once until the latch is released
		self.ubBehaviourFlags |= 0x80;
	} else if(parry == Parry::Clash) {                // facing each other: parried with a clang
		playSound(e, 0x11);
	}
	return parry != Parry::None;
}

namespace {

// The address of a script a rule names (FightScript): the record's own tables, or one of the original's labels.
uint32_t scriptOf(const FighterEnv &e, FightScript s, const Knight &self, const Knight &att) {
	const FightScripts &sc = e.scripts;
	switch(s) {
	case FightScript::Idle: return self.ulIdleScript;
	case FightScript::Stop: return 0xFFFFFFFFu;
	case FightScript::Free: return 0;
	case FightScript::Alternate: return self.ulScript26;
	case FightScript::HurtByAction: return tab32(self.ulHurtScripts, att.uwAction);                // LAB_020E
	case FightScript::GuardOfOwnAction: return tab32(self.ulActionScripts, self.uwAction);
	case FightScript::KnightParry07F3: return sc.s07F3;
	case FightScript::KnightDown07F8: return sc.s07F8;
	case FightScript::KnightDown07F9: return sc.s07F9;
	case FightScript::KnightHop07FB: return sc.s07FB;
	case FightScript::KnightFlung07FD: return sc.s07FD;
	case FightScript::KnightCursed085F: return sc.s085F;
	case FightScript::KnightStruck085E: return sc.s085E;
	case FightScript::KnightByBeSame084A: return sc.s084A;
	case FightScript::KnightByBeOpposite084B: return sc.s084B;
	case FightScript::FlyerKillSame0849: return sc.s0849;
	case FightScript::FlyerKillOpposite084E: return sc.s084E;
	case FightScript::SpearKill081C: return sc.s081C;
	}
	return self.ulIdleScript;
}

// LAB_0204 + SUB.W D0,80(A1).
void protectedHit(Knight &self, uint16_t uwDamage) {
	subHp(self, protectedDamage(uwDamage, *jobPtr<const Inventory>(knightInventoryAddr(self))));
}

// The hit points a HurtOutcome takes (rules/damage.hpp HitDamage).
void takeDamage(const FighterEnv &e, Knight &self, const Knight &att, const HurtOutcome &o) {
	switch(o.damage) {
	case HitDamage::None: break;
	case HitDamage::Contact: subHp(self, damageOf(e, att)); break;
	case HitDamage::TableByAction: subHp(self, (uint16_t)tab32(att.ulDamageTable, att.uwAction)); break;     // LAB_01F3: the table value, not LAB_021B
	case HitDamage::TableSlot4: subHp(self, (uint16_t)rd32(jobPtr<const uint8_t>(att.ulDamageTable + 4))); break;
	case HitDamage::TableSlot8: subHp(self, (uint16_t)rd32(jobPtr<const uint8_t>(att.ulDamageTable + 8))); break;
	case HitDamage::Fixed: subHp(self, o.uwAmount); break;
	case HitDamage::FixedProtected: protectedHit(self, o.uwAmount); break;
	}
}

// One blow of the creature `att` on the knight `self`, by the row of the type table: collect the facts, ask the rule, apply the answer
// (effects, hit points, the next script).  LAB_01EC.. / LAB_0620.
HandlerResult applyHurt(const FighterEnv &e, Knight &self, Knight &att, const HurtRule &rule) {
	FightVars &v = *e.pVars;
	HurtFacts f;
	f.swHp = self.swHp;
	f.ubFacing = self.ubFacing;
	f.bHitting = self.ulHitTarget != 0;
	f.bDemo = *e.pDemoFlag != 0;
	f.ubAttType = att.ubType;
	f.uwAttAction = att.uwAction;
	f.ubAttFacing = att.ubFacing;
	f.bBlocked = (rule.block == BlockRule::Always || (rule.block == BlockRule::WhileAlive && self.swHp > 0)) && blockCheck(e, self, att);
	HurtOutcome o = {};
	rule.fn(f, o);
	if(o.ubEffects & HURT_MATCH_DEPTH) {
		self.uwY = att.uwY;
		self.uwY = (uint16_t)(self.uwY - 1);
	}
	if(o.ubEffects & HURT_ATTACKER_HEAVY) {
		att.uwAction = raw(Action::Heavy);                                       // QUIRK: it rewrites the other record
	}
	if(o.ubEffects & HURT_FACE_AWAY) {
		self.ubFacing = (uint8_t)(att.ubFacing ^ 2);
	}
	if(o.ubEffects & HURT_FACE_LEFT) {
		self.ubFacing = 3;
	}
	if(o.ubEffects & HURT_LIFE_LOSS) {
		self.ubLifeLoss = 1;
	}
	takeDamage(e, self, att, o);
	if(o.ubEffects & HURT_HOP) {
		*e.pHopDir = 1;
		*e.pHopTable = e.scripts.tHop;
	}
	if(o.ubEffects & HURT_RESTART_ATTACKER) {
		jobRestart(e.pJobs, self.ulAttacker, scriptOf(e, o.attackerScript, self, att));
	}
	v.ulNextScript = scriptOf(e, o.script, self, att);
	return finish(e);
}

// LAB_01EC: the record that hit the knight picks the reaction through the type table (rules/damage.cpp kHurtRules); a type the table has
// no row for does nothing (the original jumps to address 0).
HandlerResult knightHurt(const FighterEnv &e, Knight &self) {
	Knight &att = *rec(self.ulAttacker);
	const HurtRule *rule = hurtRuleFor(att.ubType);
	if(rule == 0) {
		return finish(e);
	}
	return applyHurt(e, self, att, *rule);
}

// LAB_01E0: the record the knight hit (rules/damage.cpp knightHitOutcome).
HandlerResult knightHit(const FighterEnv &e, Knight &self) {
	Knight &tgt = *rec(self.ulHitTarget);
	HitFacts f;
	f.ubTargetType = tgt.ubType;
	f.swTargetHp = tgt.swHp;
	f.uwTargetAction = tgt.uwAction;
	f.uwAction = self.uwAction;
	f.bDemo = *e.pDemoFlag != 0;
	const HitOutcome o = knightHitOutcome(f);
	if(o.bHurtByTarget) {                                  // the dragon in flight hurts back: LAB_0201 with the target as the attacker
		return applyHurt(e, self, tgt, *hurtRuleFor(raw(ActorType::DragonFlight)));
	}
	e.pVars->ulNextScript = scriptOf(e, o.script, self, tgt);
	return finish(e);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// LAB_01CA: the knight

HandlerResult fighterKnight(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	*e.pSelf = addr(&k);                                           // MOVE.L A0,LAB_0633
	v.ulNextScript = k.ulIdleScript;
	if(k.ulAttacker != 0) {
		return knightHurt(e, k);
	}
	if(k.ulHitTarget != 0) {
		return knightHit(e, k);
	}
	k.uwAction = raw(Action::Idle);
	uint16_t in = inputFor(e, k);                                  // JSR LAB_00EA
	k.uwInput = in;
	if(in == 0) {
		return finish(e);
	}
	if(addr(&k) == *e.pConfused && *e.pConfusedOn != 0) {         // confused: up / down and left / right swap
		if((in & 0x0C) != 0) {
			in ^= 0x0C;
		}
		if((in & 0x03) != 0) {
			in ^= 0x03;
		}
		k.uwInput = in;
	}
	if(bit(k.uwInput, 4)) {                                        // LAB_01DD: fire -> the action of the direction
		const uint16_t idx = (uint16_t)(k.uwInput & 0xFFEF);
		const uint16_t *tab = k.ubFacing == 3 ? kActionLeft : kActionRight;
		const uint16_t act = idx < 16 ? tab[idx] : 0;
		k.uwAction = act;
		v.ulNextScript = tab32(k.ulActionScripts, act);
		return finish(e);
	}
	v.swStepX = 0;
	v.swStepY = 0;
	const uint8_t savedPhase = k.ubAnimPhase;                      // LAB_01D8
	k.ubAnimPhase = (uint8_t)((k.ubAnimPhase + 1) & 3);
	const unsigned phase = k.ubAnimPhase;
	if(bit(k.uwInput, 3)) {                                        // LAB_01DB: up
		v.swStepY = (int16_t)-(int16_t)kStepUp[phase];
	} else if(bit(k.uwInput, 2)) {                                 // LAB_01DC: down
		v.swStepY = (int16_t)kStepDown[phase];
	}
	if(bit(k.uwInput, 0)) {                                        // LAB_01D9
		k.ubFacing = 1;
		v.swStepX = (int16_t)kStepX[phase];
	} else if(bit(k.uwInput, 1)) {
		k.ubFacing = 3;
		v.swStepX = (int16_t)-(int16_t)kStepX[phase];
	}
	const uint16_t mask = blockedMask(e.pJobs, &k, (uint16_t)v.swStepX, k.ubFacing);   // LAB_03A9
	k.uwInput = (uint16_t)(k.uwInput & mask);
	clipToArena(k);                                                // LAB_0215
	e.obstacles(k, v.swStepX, v.swStepY);                          // LAB_0A71
	const uint16_t d1 = k.uwInput;
	const uint16_t d2 = (uint16_t)v.swStepX, d3 = (uint16_t)v.swStepY;
	bool moved = false;
	uint16_t d0 = 0;
	if(bit(d1, 3)) {
		k.uwY = (uint16_t)(k.uwY + d3);
		moved = true;
		d0 = 0x20;
	}
	if(bit(d1, 2)) {
		k.uwY = (uint16_t)(k.uwY + d3);
		moved = true;
		d0 = 0x40;
	}
	if(bit(d1, 1)) {
		k.uwX = (uint16_t)(k.uwX + d2);
		moved = true;
		d0 = 0;
	}
	if(bit(d1, 0)) {
		k.uwX = (uint16_t)(k.uwX + d2);
		moved = true;
		d0 = 0;
	}
	if(!moved) {                                                   // blocked everywhere: stand still
		v.ulNextScript = k.ulIdleScript;
		k.ubAnimPhase = savedPhase;
		return finish(e);
	}
	k.ubBehaviourFlags &= 0x7F;                                          // LAB_01D7: BCLR #7,104
	d0 = (uint16_t)(d0 + (uint16_t)(k.ubAnimPhase << 2));
	v.ulNextScript = tab32(knightWalkScripts(k), d0);
	return finish(e);
}

// ---------------------------------------------------------------------------------------------------------------------
// LAB_0226: the flyer

HandlerResult fighterFlyer(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	v.ulNextScript = k.ulIdleScript;
	*e.pSelf = addr(&k);
	k.uwInput = 0;
	k.uwAction = raw(Action::Heavy);
	*e.pTarget = *e.pFirst;
	if(k.ulAttacker != 0) {                                        // LAB_0230
		Knight &att = *rec(k.ulAttacker);
		if(flyerHitTaken(att.ubType).bHurt) {                      // everything but another flyer
			subHp(k, damageOf(e, att));
			v.ulNextScript = tab32(k.ulHurtScripts, att.uwAction);
			return finish(e);
		}
	}
	if(k.ulHitTarget != 0) {                                       // LAB_0231
		Knight &t = *rec(k.ulHitTarget);
		if(flyerHitsTarget(t.ubType)) {
			if(flyerFinishesTarget(t.ulHitTarget != 0, t.swHp, *e.pDemoFlag != 0)) {
				jobKill(e.pJobs, *e.pTarget);                      // the first fighter's job and record
				killCurrent(e);                                    // LAB_000D
				fightTimer(e);                                     // LAB_0006
				v.ulNextScript = k.ubFacing == t.ubFacing ? e.scripts.s0849 : e.scripts.s084E;
				return finish(e);
			}
			v.ulNextScript = k.ulScript26;                         // LAB_0233
			return finish(e);
		}
	}
	const Knight &tgt = *rec(*e.pTarget);                          // LAB_0228
	const FlyerTurn turn = flyerEdgeTurn(k.ubFacing, k.uwX);       // LAB_0228
	k.uwX = turn.uwX;
	k.ubFacing = turn.ubFacing;
	if(turn.bTurned) {                                             // LAB_022A
		*e.pToggle ^= 1;
		const bool wobble = *e.pToggle == 0;
		k.uwY = flyerLaneY(wobble, tgt.uwY, wobble ? e.beamPosition() : (uint16_t)0);   // LAB_022B: wobble by the beam position
		k.ubAnimTimer = flyerPauseFrames(*e.pSeed);                // LAB_022C
		k.ubAnimTimer = (uint8_t)(k.ubAnimTimer - 1);
		if(k.ubAnimTimer != 0) {
			return finish(e);
		}
	}
	k.ubAnimPhase = (uint8_t)(k.ubAnimPhase + 1);                  // LAB_022D
	if(k.ubAnimPhase == 4) {
		k.ubAnimPhase = 0;
	}
	v.ulNextScript = tab32(knightWalkScripts(k), (uint16_t)((int16_t)(int8_t)k.ubAnimPhase << 2));
	k.uwX = (uint16_t)(k.uwX + flyerStepX(k.ubAnimPhase, k.ubFacing));   // LAB_022E
	return finish(e);
}

// ---------------------------------------------------------------------------------------------------------------------
// The movement helpers LAB_0F1A..LAB_0F34 the brawler and the dragon use

namespace {

// LAB_0F2E: D7 = +1 when the key in the facing direction is held, else -1.
int32_t dirSign(const Knight &k) {
	if(k.ubFacing == 1) {
		return bit(k.uwInput, 0) ? 1 : -1;
	}
	return bit(k.uwInput, 1) ? 1 : -1;
}

inline int16_t moveTab(uint32_t wordBase, uint8_t phase, unsigned which) {
	const uint32_t i = wordBase + (uint32_t)phase * 2u + which;     // 4 bytes per entry, the asm reads from the table base
	return i < sizeof kMoveTab / sizeof kMoveTab[0] ? kMoveTab[i] : (int16_t)0;
}

// LAB_0F1A / LAB_0F1C: sideways walk step from the table at wordBase (0F1A mirrors dx).
Dir sideStep(const FighterEnv &e, const Knight &k, bool bMirror) {
	MoveVars &m = *e.pMove;
	Dir d;
	d.d7 = dirSign(k);
	m.swDx = moveTab(kTabWalk, k.ubAnimPhase, 0);
	m.swDy = moveTab(kTabWalk, k.ubAnimPhase, 1);
	d.d6 = 0;
	if(bMirror) {
		m.swDx = (int16_t)-m.swDx;
	}
	if(m.swDyOverride != 0) {
		m.swDy = m.swDyOverride;
	}
	return d;
}

// LAB_0F1E (up) / LAB_0F1F (down).
Dir vertStep(const FighterEnv &e, const Knight &k, bool bUp) {
	MoveVars &m = *e.pMove;
	Dir d;
	d.d7 = 1;
	const uint32_t base = bUp ? kTabAttackUp : kTabAttackDown;
	m.swDx = moveTab(base, k.ubAnimPhase, 0);
	m.swDy = moveTab(base, k.ubAnimPhase, 1);
	if(bUp) {
		d.d6 = 32;
		m.swDy = (int16_t)-m.swDy;
		m.swDyOverride = (int16_t)0xFFFB;
	} else {
		m.swDyOverride = 5;
		d.d6 = 64;
	}
	return d;
}

// LAB_0F32: advance the walk phase until the walk-script table has an entry there.  The asm loops forever on an all-null
// table; this gives up after 16 steps (the phase has long wrapped).
void advancePhase(const FighterEnv &e, Knight &k, int32_t d6, int32_t d7) {
	for(int guard = 0; guard < 16; ++guard) {
		k.ubAnimPhase = (uint8_t)((uint8_t)(k.ubAnimPhase + (uint8_t)d7) & 7);
		const uint32_t off = ((uint32_t)k.ubAnimPhase << 2) + (uint32_t)d6;
		if(tab32(knightWalkScripts(k), (uint16_t)off) != 0) {
			break;
		}
	}
	e.pMove->uwTabOffset = (uint16_t)d6;
}

// LAB_0F33: the walk script of the current phase.
HandlerResult walkScript(const FighterEnv &e, Knight &k) {
	e.pVars->ulNextScript = tab32(knightWalkScripts(k), (uint16_t)(((uint16_t)k.ubAnimPhase << 2) + e.pMove->uwTabOffset));
	return finish(e);
}

// LAB_0F20: take the step: advance the phase, drop the blocked directions, move.
HandlerResult moveApply(const FighterEnv &e, Knight &k, const Dir &d) {
	FightVars &v = *e.pVars;
	MoveVars &m = *e.pMove;
	advancePhase(e, k, d.d6, d.d7);
	const uint16_t mask = blockedMask(e.pJobs, &k, (uint16_t)m.swDx, k.ubFacing);
	k.uwInput = (uint16_t)(k.uwInput & mask);
	if(k.uwInput == 0) {
		k.ubAnimPhase = 0;
		v.ulNextScript = k.ulIdleScript;
		return finish(e);
	}
	if((k.uwInput & 3) == 0) {
		m.swDx = 0;
	}
	if((k.uwInput & 0x0C) == 0) {
		m.swDy = 0;
	}
	v.ulNextScript = tab32(knightWalkScripts(k), (uint16_t)(((uint16_t)k.ubAnimPhase << 2) + m.uwTabOffset));
	k.uwX = (uint16_t)(k.uwX + (uint16_t)m.swDx);
	k.uwY = (uint16_t)(k.uwY + (uint16_t)m.swDy);
	return finish(e);
}

// LAB_0F24 result (Approach, fighters_int.hpp): D0 and the word of D1 the callers read, d7 = the tunable the last LAB_02BF
// probe used (+118, or +116): what D7 holds on return.

// LAB_0F24: face the target, set the input bits that walk the fighter towards it (or away when it is inside the tunable
// +118), flag whether the depth is within reach (LAB_0F3D) and whether the depth needed correcting (LAB_0F3F).
Approach approach(const FighterEnv &e) {
	MoveVars &m = *e.pMove;
	m.swDx = 0;
	m.swDy = 0;
	m.uwNear = 0;
	m.swDyOverride = 0;
	Knight &self = *rec(*e.pSelf);
	const Knight &tgt = *rec(*e.pTarget);
	faceTarget(self, tgt);
	m.uwVertical = 0;
	Approach r;
	if(depthNear(self, tgt)) {
		m.uwNear = 1;
	} else if((int16_t)self.uwY <= (int16_t)tgt.uwY) {              // LAB_0F26: above: walk down
		self.uwInput |= 4;
		m.uwVertical = 1;
	} else {                                                       // LAB_0F25
		self.uwInput |= 8;
		m.uwVertical = 1;
	}
	if(xNear(self, tgt, (int16_t)self.uwTooCloseX)) {                 // LAB_0F2B: too close, back away
		const int16_t diff = (int16_t)(tgt.uwX - self.uwX);
		if(diff < 0) {
			self.uwInput |= 1;
		} else {
			self.uwInput |= 2;
		}
		r.d0 = 1;
		r.d1 = (uint16_t)diff;
		r.d7 = self.uwTooCloseX;
		return r;
	}
	if(xNear(self, tgt, (int16_t)self.uwReachX)) {                 // in reach: stay
		r.d0 = m.uwVertical;
		r.d1 = (uint16_t)(tgt.uwX - self.uwX);                      // LAB_02BF leaves D1 = |dx| (word, NEG when N)
		if(r.d1 & 0x8000u) {
			r.d1 = (uint16_t)(0u - r.d1);
		}
		r.d7 = self.uwReachX;
		return r;
	}
	if((int16_t)self.uwX < (int16_t)tgt.uwX) {                     // LAB_02C8 = 1: walk right
		self.uwInput |= 1;
	} else {
		self.uwInput |= 2;
	}
	r.d0 = 1;
	r.d1 = self.uwReachX;
	r.d7 = self.uwReachX;
	return r;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// LAB_0236: the brawler

namespace {

// LAB_0238 / LAB_0239: choose the walk helpers from the input bits, then take the step.
HandlerResult brawlerWalk(const FighterEnv &e, Knight &k, uint16_t d7) {
	if((k.uwInput & 0xFF) == 0) {                                  // nothing to do: keep the idle script
		return finish(e);
	}
	// No helper runs when only stale high input bits are set: D7 is still what LAB_0F24 left (the last probe's tunable),
	// D6 whatever the caller had (taken as 0 here).
	Dir d;
	d.d6 = 0;
	d.d7 = d7;
	if(bit(k.uwInput, 3)) {
		d = vertStep(e, k, true);
	}
	if(bit(k.uwInput, 2)) {
		d = vertStep(e, k, false);
	}
	if(bit(k.uwInput, 0)) {
		d = sideStep(e, k, false);
	}
	if(bit(k.uwInput, 1)) {
		d = sideStep(e, k, true);
	}
	return moveApply(e, k, d);
}

// LAB_023E on: the attack decision (rules/ai_fight_brawler.cpp brawlerDecide) by the distance d1 to the target, applied here.
HandlerResult brawlerAttack(const FighterEnv &e, Knight &k, uint16_t d1, uint16_t d7) {
	FightVars &v = *e.pVars;
	BrawlerFacts f;
	f.swDist = (int16_t)d1;
	f.swTooClose = (int16_t)k.uwTooCloseX;
	f.swReach = (int16_t)k.uwReachX;
	f.ubCooldown = k.ubCooldown;
	f.swTargetHp = rec(*e.pTarget)->swHp;
	f.bSpear = k.ubType == raw(ActorType::TroggSpear);
	f.bDemo = *e.pDemoFlag != 0;
	f.bGloated = v.uwLairFlag != 0;
	f.bFirstDefends = rec(*e.pFirst)->uwAction == raw(Action::Defend);
	const BrawlerPlan plan = brawlerDecide(f, *e.pSeed);
	switch(plan) {
	case BrawlerPlan::Walk: return brawlerWalk(e, k, d7);
	case BrawlerPlan::Stand: return finish(e);
	case BrawlerPlan::CountDown:
		k.ubCooldown = (uint8_t)(k.ubCooldown - 1);
		return finish(e);
	case BrawlerPlan::GloatOnBody: v.uwLairFlag = 1; break;                  // the downed target is gloated over once
	default: break;
	}
	k.ubCooldown = brawlerCooldownOf(plan);
	k.uwAction = raw(brawlerActionOf(plan));
	v.ulNextScript = plan == BrawlerPlan::Thrust ? e.scripts.s081B : tab32(k.ulActionScripts, k.uwAction);   // LAB_081B / action table
	return finish(e);
}

// LAB_0237: no hit pending (A0 = the record whose input / action words are cleared: normally the brawler itself).
HandlerResult brawlerThink(const FighterEnv &e, Knight &k, Knight &a0) {
	a0.uwInput = 0;
	a0.uwAction = raw(Action::Idle);
	const Approach ap = approach(e);                              // JSR LAB_0F24 (reloads A0 = LAB_0633)
	uint16_t d1 = ap.d1;
	if(ap.d0 != 0) {
		if(e.pMove->uwNear == 0) {
			return brawlerWalk(e, k, ap.d7);
		}
		bool left;
		d1 = xDelta(k, *rec(*e.pTarget), left);                    // JSR LAB_02C2; MOVE.W D0,D1
	}
	return brawlerAttack(e, k, d1, ap.d7);
}

}  // namespace

HandlerResult fighterBrawler(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	*e.pSelf = addr(&k);
	v.ulNextScript = k.ulIdleScript;
	*e.pTarget = *e.pFirst;
	if(k.ulAttacker != 0) {                                        // LAB_0246
		Knight &att = *rec(k.ulAttacker);
		k.ubCooldown = 0;
		const HitTaken hit = brawlerHitTaken(att.ubType);
		if(hit.bHurt) {
			subHp(k, hit.bContact ? damageOf(e, att) : hit.uwFixed);
			v.ulNextScript = tab32(k.ulHurtScripts, att.uwAction);
		}
		return finish(e);
	}
	if(k.ulHitTarget != 0) {                                       // LAB_024B
		Knight &t = *rec(k.ulHitTarget);
		v.ulNextScript = k.ulScript26;
		k.ubCooldown = 0x0A;
		switch(brawlerAfterHit(t.ubType == raw(ActorType::KnightFight), t.ulHitTarget != 0, t.swHp, k.ubType == raw(ActorType::TroggSpear), *e.pDemoFlag != 0)) {
		case BrawlerAfterHit::Think: return brawlerThink(e, k, t);  // QUIRK: A0 is the hit record, its input / action are cleared
		case BrawlerAfterHit::FinishOff:
			jobKill(e.pJobs, addr(&t));
			v.ulNextScript = e.scripts.s081C;
			break;
		case BrawlerAfterHit::Stop: v.ulNextScript = 0xFFFFFFFFu; break;
		case BrawlerAfterHit::Idle: v.ulNextScript = k.ulIdleScript; break;     // LAB_024C
		case BrawlerAfterHit::Alternate: break;                                  // the script26 set above
		}
		return finish(e);
	}
	return brawlerThink(e, k, k);
}

// ---------------------------------------------------------------------------------------------------------------------
// LAB_0251: the caster

namespace {

// SUB.W D0,80(A1) / SUBI.W #n,80(A1) followed by BGT: the branch looks at the true signed difference (N xor V), not
// at the wrapped word.
bool subHpGt(Knight &k, uint16_t d) {
	const int32_t r = (int32_t)k.swHp - (int32_t)(int16_t)d;
	k.swHp = (int16_t)r;
	return r > 0;
}

inline void startDagger(const FighterEnv &e) {
	daggerStart(e.pSlots, *e.pBlock, rec(*e.pSelf), e.uwJunkA, e.uwJunkC);
}

// LAB_0259: the caster's dagger is out.
HandlerResult casterThrown(const FighterEnv &e, Knight &k) {
	k.ubBehaviourFlags |= 0x01;
	e.pVars->ulNextScript = e.scripts.s0850;
	return finish(e);
}

// LAB_0256: aim the dagger at the target and throw it (also the end of the hold state).
HandlerResult casterAim(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	const DaggerAim r = daggerAim(*e.pBlock, *rec(*e.pSelf), *rec(*e.pTarget), &k, (int16_t)k.uwReachX);
	*e.pAimDist = r.uwDist;
	v.uwAimSteps = r.uwSteps;
	v.uwAimArc = r.uwArc;
	e.pBlock->uwSteps = casterThrowSteps(v.uwAimSteps);
	if(casterThrowsClose((int16_t)*e.pAimDist)) {                  // close: a quick underhand throw
		k.ubBehaviourFlags |= CASTER_QUICK;
		e.pBlock->uwExtra = 2;
		startDagger(e);
		advancePhase(e, k, 32, 1);
		return walkScript(e, k);
	}
	startDagger(e);                                                // LAB_0258
	return casterThrown(e, k);
}

// LAB_025E: the dagger has landed: count the hold down, then throw again.
HandlerResult casterHold(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	setCooldownWord(k, (uint16_t)(cooldownWord(k) - 1));
	if(cooldownWord(k) == 0) {                                             // LAB_0260
		k.ubBehaviourFlags = 0;
		v.ubCasterFlags[0] &= 0xFB;
		return casterAim(e, k);
	}
	bool left;
	const uint16_t d0 = xDelta(k, *rec(*e.pTarget), left);
	v.ulNextScript = casterTargetNear((int16_t)d0) ? e.scripts.s0864 : e.scripts.s0863;
	return finish(e);
}

// LAB_0255: no attack in reach: start a throw (or carry on with the one in flight).
HandlerResult casterStart(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	k.uwAction = raw(Action::Idle);
	k.ubAnimPhase = 0;
	if(v.ubCasterFlags[0] & 0x04) {
		return casterAim(e, k);
	}
	v.ubCasterFlags[0] |= 0x04;
	k.ubBehaviourFlags |= 0x04;
	const CombatJob *job = jobOfOwner(e.pJobs, *e.pSecond);       // LAB_05F4: the second fighter's job (position to aim at)
	setCooldownWord(k, 0x1E);
	DaggerBlock &b = *e.pBlock;
	b.ulOwner = addr(&k);
	b.uwX = k.uwX;
	b.uwDepth = k.uwY;
	b.uwHeight = 0xFFEC;
	b.uwTargetX = job ? job->uwX : (uint16_t)0;                    // a missing job reads low memory in the asm
	b.uwTargetDepth = (uint16_t)((job ? job->uwZ : (uint16_t)0) + 3);
	b.uwTargetHeight = job ? job->uwY : (uint16_t)0;
	b.uwSteps = 0x0E;
	b.uwExtra = 0;
	startDagger(e);
	return casterThrown(e, k);
}

// LAB_025A: the dagger is in flight: follow its trajectory.
HandlerResult casterFlight(const FighterEnv &e, Knight &k) {
	uint16_t out[3] = {k.uwX, k.uwY, k.uwHeight};                    // no slot: the asm hands the caller's stale registers back
	const int32_t st = daggerStep(e.pSlots, addr(&k), out);
	k.uwX = out[0];
	k.uwY = out[1];
	k.uwHeight = out[2];
	if((uint16_t)st != 0) {
		if(!(k.ubBehaviourFlags & 0x04)) {
			k.uwHeight = 0;
			k.ubBehaviourFlags = 0;
			return finish(e);
		}
		k.ubBehaviourFlags = 0;                                          // LAB_025D
		k.ubBehaviourFlags |= 0x08;
		return casterHold(e, k);
	}
	int32_t d6 = 32;                                               // LAB_025B
	if((int16_t)k.uwHeight > (int16_t)0xFFF6) {
		d6 = 0;
	}
	advancePhase(e, k, d6, 1);
	return walkScript(e, k);
}

// LAB_0264: held by the caster (the target is the first fighter): the second joystick's fire + down breaks the hold.
HandlerResult casterGrabbed(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	const uint16_t d1 = e.joystick().uwPort1;
	if(bit(d1, 4) && bit(d1, 2)) {                                 // LAB_0266
		v.ulNextScript = e.scripts.s085D;
		k.ubBehaviourFlags = 0;
		k.ubBehaviourFlags2 |= 0x01;
		return finish(e);
	}
	setCooldownWord(k, (uint16_t)(cooldownWord(k) - 1));                           // LAB_0265
	if(cooldownWord(k) == 0) {
		k.ubBehaviourFlags = 0;                                          // LAB_0267
		k.ubBehaviourFlags |= 0x10;
		v.ulNextScript = e.scripts.s085C;
		return finish(e);
	}
	v.ulNextScript = e.scripts.s085B;
	return finish(e);
}

// LAB_0268: throw the held fighter: a long arc to the right, the first fighter takes the damage.
HandlerResult casterThrow(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	v.ubCasterFlags[0] &= 0xDF;
	setCooldownWord(k, 0x11);
	DaggerBlock &b = *e.pBlock;
	b.ulOwner = addr(&k);
	b.uwX = k.uwX;
	b.uwDepth = k.uwY;
	b.uwHeight = (uint16_t)(k.uwHeight + 0xFFA6);
	b.uwTargetX = (uint16_t)(k.uwX + 0x96);
	b.uwTargetDepth = k.uwY;
	b.uwTargetHeight = k.uwHeight;
	b.uwSteps = 0x11;
	b.uwExtra = 0x78;
	startDagger(e);
	setBehaviourWord(k, 0);
	k.ubBehaviourFlags |= 0x01;
	k.ubAnimPhase = 0;
	k.uwHeight = 0xFFBA;
	v.ulNextScript = e.scripts.s0851;
	CombatJob *job = jobOfOwner(e.pJobs, *e.pFirst);
	if(job != 0) {
		job->ubFlags = k.ubFacing;
	}
	Knight &first = *rec(*e.pFirst);
	first.ulHitTarget = 0;
	first.ulAttacker = 0;
	uint32_t script = first.ulIdleScript;
	if(!subHpGt(first, 5)) {
		script = e.scripts.s07F7;
	}
	jobRestart(e.pJobs, addr(&first), script);
	jobTogglePause(e.pJobs, *e.pTarget);
	return finish(e);
}

// LAB_026A: the grab was shaken off: free the first fighter.
HandlerResult casterRelease(const FighterEnv &e, Knight &k) {
	(void)k;
	FightVars &v = *e.pVars;
	jobTogglePause(e.pJobs, *e.pFirst);
	Knight &first = *rec(*e.pFirst);
	first.ulHitTarget = 0;
	first.ulAttacker = 0;
	jobRestart(e.pJobs, addr(&first), first.ulScript26);
	v.ubCasterFlags[0] &= 0xDF;
	v.ulNextScript = 0;
	return finish(e);
}

// LAB_0261: the first fighter is held in the air by the caster (105 bit 2): fire + down on the second joystick hits it.
HandlerResult casterSlam(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	const uint16_t d1 = e.joystick().uwPort1;
	if(bit(d1, 4) && bit(d1, 2)) {
		v.ulNextScript = e.scripts.s0867;
		Knight &first = *rec(*e.pTarget);
		if(subHpGt(k, damageOf(e, first))) {                       // LAB_0263
			return finish(e);
		}
		jobTogglePause(e.pJobs, *e.pTarget);
		Knight &t = *rec(*e.pTarget);
		t.ulHitTarget = 0;
		t.ulAttacker = 0;
		jobRestart(e.pJobs, addr(&t), e.scripts.s07FA);
		v.ubCasterFlags[0] &= 0xF7;
		v.ulNextScript = e.scripts.s0868;
		return finish(e);
	}
	v.ulNextScript = e.scripts.s0866;                              // LAB_0262
	Knight &t = *rec(*e.pTarget);
	if(!subHpGt(t, 1)) {
		v.ulNextScript = e.scripts.s0869;
	}
	return finish(e);
}

// LAB_0252: nothing hit the caster this frame (a0: the record whose action word is cleared, normally the caster).
HandlerResult casterThink(const FighterEnv &e, Knight &k, Knight &a0) {
	FightVars &v = *e.pVars;
	a0.uwAction = raw(Action::Idle);
	Knight &tgt = *rec(*e.pTarget);
	switch(casterState(k.ubBehaviourFlags, k.ubBehaviourFlags2)) {
	case CasterState::Flight: return casterFlight(e, k);
	case CasterState::Grabbed: return casterGrabbed(e, k);
	case CasterState::Release: return casterRelease(e, k);
	case CasterState::Throw: return casterThrow(e, k);
	case CasterState::Hold: return casterHold(e, k);
	case CasterState::Slam: return casterSlam(e, k);
	case CasterState::Think: break;
	}
	faceTarget(k, tgt);                                            // JSR LAB_02C4
	bool left;
	const bool near = depthNear(k, tgt);                           // JSR LAB_02BB
	switch(casterMelee(near, v.ubCasterFlags[0], tgt.swHp, v.uwCasterWait, near ? (int16_t)xDelta(k, tgt, left) : (int16_t)0)) {
	case CasterMelee::Stand: return finish(e);
	case CasterMelee::Wait:
		v.uwCasterWait = (uint16_t)(v.uwCasterWait - 1);
		return finish(e);
	case CasterMelee::Strike8:                                     // LAB_0253
		k.uwAction = raw(Action::Strike8);
		v.ulNextScript = e.scripts.s0859;
		return finish(e);
	case CasterMelee::Strike4:                                     // LAB_0254
		k.uwAction = raw(Action::Strike4);
		v.ulNextScript = e.scripts.s0857;
		return finish(e);
	case CasterMelee::StartThrow: break;
	}
	return casterStart(e, k);
}

}  // namespace

HandlerResult fighterCaster(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	v.ulNextScript = k.ulIdleScript;
	*e.pSelf = addr(&k);
	*e.pTarget = *e.pFirst;
	if(k.ulAttacker != 0) {                                        // LAB_026B
		Knight &att = *rec(k.ulAttacker);
		switch(casterHurtBy(att.ubType, k.ubBehaviourFlags, att.uwAction)) {
		case CasterHurt::Think: return casterThink(e, k, att);     // QUIRK: A0 is the attacker, its action word is cleared
		case CasterHurt::FinishedOff:                              // LAB_026D
			v.ulNextScript = e.scripts.s0860;
			k.ubBehaviourFlags = 0;
			k.uwHeight = 0;
			k.swHp = -1;
			return finish(e);
		case CasterHurt::Wound: {                                  // LAB_026E
			const Knight &first = *rec(*e.pTarget);
			subHp(k, damageOf(e, first));
			v.ulNextScript = tab32(k.ulHurtScripts, first.uwAction);
			return finish(e);
		}
		case CasterHurt::Killed:                                   // LAB_026F / LAB_0270
			v.ulNextScript = e.scripts.s086B;
			k.ubBehaviourFlags = 0;
			k.uwHeight = 0;
			k.swHp = -1;
			return finish(e);
		case CasterHurt::InFlight:
			subHp(k, damageOf(e, *rec(*e.pTarget)));
			k.ubBehaviourFlags = 0;
			k.uwHeight = 0;
			v.ulNextScript = e.scripts.s0862;
			return finish(e);
		}
	}
	if(k.ulHitTarget != 0) {                                       // LAB_0273
		Knight &t = *rec(k.ulHitTarget);
		switch(casterHitReply(t.ubType, k.ubBehaviourFlags, v.ubCasterFlags[0])) {
		case CasterHit::Think: return casterThink(e, k, t);        // QUIRK: A0 is the hit record
		case CasterHit::Flight: return casterFlight(e, k);         // LAB_0278
		case CasterHit::Grab:
			k.ubBehaviourFlags = 0;
			k.ubBehaviourFlags |= CASTER_GRABBED;
			v.ubCasterFlags[0] |= CASTER_SHARED_GRABBED;
			setCooldownWord(k, (uint16_t)(int16_t)(int8_t)(uint8_t)(t.ubEndurance + 6));
			v.ulNextScript = e.scripts.s085B;
			k.uwHeight = 0;
			jobTogglePause(e.pJobs, addr(&t));
			return finish(e);
		case CasterHit::SlamStart:                                 // LAB_0279
			setBehaviourWord(k, 0);
			k.ubBehaviourFlags2 |= CASTER2_SLAM;
			v.ubCasterFlags[0] |= CASTER_SHARED_SLAM;
			v.ulNextScript = e.scripts.s0865;
			jobTogglePause(e.pJobs, addr(&t));
			return finish(e);
		case CasterHit::Counter: break;
		}
		v.ulNextScript = 0xFFFFFFFFu;
		setBehaviourWord(k, 0);
		v.uwCasterWait = CASTER_WAIT;
		if(t.ubFacing == k.ubFacing) {
			flipFirstFacing(e);                                    // BSR LAB_02C6
		}
		if(k.uwAction == raw(Action::Strike8)) {                                      // LAB_0274
			v.ulNextScript = e.scripts.s085A;
		}
		if(k.uwAction == raw(Action::Strike4)) {
			v.ulNextScript = e.scripts.s0858;
		}
		return finish(e);
	}
	return casterThink(e, k, k);
}

// ---------------------------------------------------------------------------------------------------------------------
// LAB_027A: the dragon

namespace {

// LAB_0F34: a hit spark at the hit point of the record src (the type $28 slot of the handler table becomes LAB_02D2).
void hitSpark(const FighterEnv &e, const Knight &src) {
	wr32(e.pHandlerTable + 40, e.scripts.hIdle);
	creatureSpawn(e.pJobs, e.pCreatures, e.scripts.s08BF, e.scripts.fFrames0648, src.uwHitX, 0, src.uwHitY,
	              rec(*e.pFirst)->ubFacing, 0x28);
}

// LAB_0297: the dragon's object, 55 to the right of it.
void dragonSpawn(const FighterEnv &e) {
	wr32(e.pHandlerTable + 40, e.scripts.hIdle);
	const Knight &d = dragonOf(e);
	creatureSpawn(e.pJobs, e.pCreatures, e.scripts.s0886, e.scripts.fFrames05E0, (uint16_t)(d.uwX + 0x37), 0,
	              (uint16_t)(d.uwY + 5), 1, 0x28);
}

// LAB_029E: the two records the fight keeps beside the dragon follow its depth (+10 and -20 from it), then the exit.
HandlerResult dragonLinked(const FighterEnv &e) {
	const Knight &d = dragonOf(e);
	uint16_t d0 = (uint16_t)(d.uwY + 10);
	if(*e.pLinkA != 0) {
		rec(*e.pLinkA)->uwY = d0;
	}
	d0 = (uint16_t)(d0 - 30);
	if(*e.pLinkB != 0) {
		rec(*e.pLinkB)->uwY = d0;
	}
	return finish(e);
}

// LAB_028E: the dragon's walk towards (or away from) the first fighter, kept inside x 30..100, mirrored into its job.
void dragonStep(const FighterEnv &e) {
	FightVars &v = *e.pVars;
	Knight &d = dragonOf(e);
	d.uwInput = 0;
	*e.pSelf = addr(&d);
	*e.pTarget = *e.pFirst;
	uint16_t save116 = 0, save118 = 0;
	const bool reduced = (v.ubDragonFlags[0] & DRAGON_REDUCED) != 0;
	if(reduced) {                                                  // tunables forced to 2 / 1 for this step
		save116 = d.uwReachX;
		save118 = d.uwTooCloseX;
		d.uwReachX = DRAGON_REDUCED_REACH;
		d.uwTooCloseX = DRAGON_REDUCED_TOO_CLOSE;
	}
	approach(e);                                                   // JSR LAB_0F24
	d.ubFacing = 1;
	d.uwX = dragonStepX(d.uwX, d.uwInput);
	d.uwY = dragonStepY(d.uwY, d.uwInput);
	CombatJob *job = jobOfOwner(e.pJobs, addr(&d));
	if(job != 0) {                                                 // none: the asm writes to addresses 6 and 10
		job->uwX = d.uwX;
		job->uwZ = d.uwY;
	}
	if(reduced && job != 0) {
		// QUIRK: the tunables are restored through the JOB pointer (job + 116 / +118, inside the following jobs), so the
		// dragon keeps 2 / 1 and another job's width / height words are overwritten.
		CombatJob *j2 = job + 2;                                   // 2 * 50 + 16 = 116: the width and height words
		j2->uwW = save116;
		j2->uwH = save118;
	}
}

// LAB_0283..LAB_0286: choose the next attack of the dragon (a1 = the dragon record); the decision is rules/ai_fight_dragon.cpp.
HandlerResult dragonPick(const FighterEnv &e, Knight &a1) {
	FightVars &v = *e.pVars;
	switch(dragonPickAttack(v.ubDragonFlags[0], (int16_t)v.uwDragonDist, rec(*e.pTarget)->swHp, e.pMove->uwNear != 0)) {
	case DragonAttack::None: break;
	case DragonAttack::Strike8:
		a1.uwAction = raw(Action::Strike8);
		v.ulNextScript = e.scripts.s0872;
		break;
	case DragonAttack::Strike4:
		a1.uwAction = raw(Action::Strike4);
		v.ulNextScript = e.scripts.s0883;
		break;
	case DragonAttack::Breath:
		a1.uwAction = raw(Action::Heavy);
		v.ulNextScript = e.scripts.s087E;
		dragonSpawn(e);
		break;
	}
	return dragonLinked(e);
}

// LAB_027D: a flight is running: the dragon tracks the target's depth and follows the trajectory.
HandlerResult dragonFlight(const FighterEnv &e, Knight *pA1) {
	FightVars &v = *e.pVars;
	uint8_t &fl = v.ubDragonFlags[0];
	Knight &a1 = *pA1;                                             // normally the dragon; a job when LAB_0287 sent it here
	setCooldownWord(a1, (uint16_t)(cooldownWord(a1) - 1));
	if(cooldownWord(a1) == 0) {
		fl &= (uint8_t)~DRAGON_FLIGHT;
	}
	const Knight &tgt = *rec(*e.pTarget);
	if((int16_t)a1.uwY < (int16_t)tgt.uwY) {                       // LAB_027E
		a1.uwY = (uint16_t)(a1.uwY + 5);
	} else {
		a1.uwY = (uint16_t)(a1.uwY - 5);
	}
	Knight &d = dragonOf(e);                                       // LAB_0280
	uint16_t out[3] = {d.uwX, d.uwY, d.uwHeight};
	daggerStep(e.pSlots, addr(&a1), out);
	d.uwX = out[0];
	d.uwHeight = out[2];
	d.ubAnimPhase = (uint8_t)(d.ubAnimPhase + 1);
	uint16_t d0 = d.ubAnimPhase;
	if(!((int8_t)d.ubAnimPhase < 8)) {
		d0 = 7;
	}
	d0 = (uint16_t)(d0 << 2);
	v.ulNextScript = tab32(knightWalkScripts(d) + dragonFlightWalkOffset(fl), d0);
	return dragonLinked(e);
}

// LAB_027B: the dragon thinks (a1 = the handler's record).
HandlerResult dragonThink(const FighterEnv &e, Knight *pA1) {
	FightVars &v = *e.pVars;
	uint8_t &fl = v.ubDragonFlags[0];
	if(fl & DRAGON_FLIGHT) {
		return dragonFlight(e, pA1);
	}
	dragonStep(e);                                                 // BSR LAB_028E: A1 = the dragon from here on
	Knight &d = dragonOf(e);
	bool left;
	const uint16_t dist = xDelta(d, *rec(*e.pTarget), left);       // JSR LAB_02C2
	v.uwDragonDist = dist;
	if(dragonNextPhase(fl, (int16_t)dist) == DragonThink::PickAttack) {
		return dragonPick(e, d);                                   // LAB_0283
	}
	// LAB_027B tail (near: first phase) / LAB_027C (far: second phase): start a flight
	const bool far = (int16_t)dist >= DRAGON_FAR;
	const DragonFlightPlan plan = dragonFlightPlan(far);
	DaggerBlock &b = *e.pBlock;
	const Knight &t = *rec(*e.pTarget);
	fl |= plan.ubFlagsSet;
	fl &= (uint8_t)~plan.ubFlagsClear;
	d.ubAnimPhase = 0;
	v.ulNextScript = rd32(jobPtr<const uint8_t>(knightWalkScripts(d) + plan.uwWalkOffset));
	setCooldownWord(d, plan.uwSteps);
	b.ulOwner = addr(&d);
	b.uwX = d.uwX;
	b.uwDepth = d.uwY;
	b.uwHeight = d.uwHeight;
	b.uwTargetX = plan.uwTargetX;
	b.uwTargetDepth = t.uwY;
	b.uwTargetHeight = plan.uwTargetHeight;
	b.uwSteps = plan.uwSteps;
	b.uwExtra = plan.uwArc;
	startDagger(e);
	return dragonLinked(e);
}

}  // namespace

HandlerResult fighterDragon(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	uint8_t &fl = v.ubDragonFlags[0];
	v.ulNextScript = k.ulIdleScript;
	*e.pSelf = addr(&k);
	*e.pTarget = *e.pFirst;
	Knight *a1 = &k;
	if(k.ulAttacker != 0) {                                        // LAB_0287
		Knight &att = *rec(k.ulAttacker);
		const HitTaken hit = dragonHitTaken(att.ubType);
		if(hit.bHurt) {
			fl |= DRAGON_HIT;                                      // LAB_0289 / LAB_028B
			if(hit.bContact) {
				subHp(k, damageOf(e, att));
				v.ulNextScript = tab32(k.ulHurtScripts, att.uwAction);
			} else {
				v.ulNextScript = rd32(jobPtr<const uint8_t>(k.ulHurtScripts + 12));
				subHp(k, hit.uwFixed);
			}
			hitSpark(e, dragonOf(e));                              // LAB_028A: A0 = the dragon record
			return finish(e);
		}
		CombatJob *job = jobOfOwner(e.pJobs, addr(&dragonOf(e)));
		if(job != 0 && job->ubRunning != 0) {                      // LAB_0288
			v.ulNextScript = 0xFFFFFFFFu;
			return finish(e);
		}
		if(job != 0) {
			a1 = reinterpret_cast<Knight *>(job);                  // QUIRK: A1 is the JOB from here (LAB_027D writes into the job table)
		}
		return dragonThink(e, a1);
	}
	if(k.ulHitTarget != 0) {                                       // LAB_028C
		if(k.uwAction != raw(Action::Strike4)) {
			v.ulNextScript = 0xFFFFFFFFu;
			return finish(e);
		}
		jobKill(e.pJobs, k.ulHitTarget);                           // LAB_028D
		v.ulNextScript = e.scripts.s0884;
		return finish(e);
	}
	k.uwAction = raw(Action::Idle);
	return dragonThink(e, a1);
}

// ---------------------------------------------------------------------------------------------------------------------
// LAB_0298: the dragon's object

HandlerResult fighterDragonPart(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	v.ulNextScript = k.ulIdleScript;
	*e.pSelf = addr(&k);
	*e.pTarget = *e.pFirst;
	const Knight &tgt = *rec(*e.pTarget);
	if(k.ulAttacker != 0) {                                        // LAB_029B
		v.ulNextScript = k.ulIdleScript;
		return finish(e);
	}
	if(k.ulHitTarget != 0) {                                       // LAB_029C
		if(dragonPartHopsOnHit(rec(k.ulHitTarget)->ubType)) {
			*e.pHopDir = 1;
			*e.pHopTable = e.scripts.tHop;
			v.ulNextScript = k.ulIdleScript;
		}
		return finish(e);
	}
	if(dragonOf(e).swHp <= 0) {
		v.ulNextScript = e.scripts.s0887;
		return finish(e);
	}
	if(dragonPartStrikes(tgt.swHp, depthNear(k, tgt), (int16_t)tgt.uwX)) {   // LAB_0299
		k.uwAction = raw(Action::Strike20);
		v.ulNextScript = e.scripts.s0881;
		return finish(e);
	}
	return dragonLinked(e);                                        // LAB_029A -> LAB_029E
}

// ---------------------------------------------------------------------------------------------------------------------
// LAB_029F: the drake

namespace {

// LAB_02AF / LAB_02B0 / the tail of LAB_02AA: the landing is heard.
void drakeLanded(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	k.ubBehaviourFlags2 = 5;
	v.ulNextScript = e.scripts.s088A;
	k.uwHeight = 0;
	v.ubDrakeFlags[0] = 0;
	if(!drakeLandsHard((int16_t)v.uwAimArc)) {
		e.sound(0x2F);                                             // LAB_02B0
	} else {
		e.screenEffect();                                          // JSR LAB_0427
		e.sound(0x2D);                                             // LAB_02AF
		e.sound(0x2E);
	}
}

// LAB_02A9: in flight.
HandlerResult drakeFlight(const FighterEnv &e, Knight &k, Knight &tgt) {
	FightVars &v = *e.pVars;
	k.ubBehaviourFlags2 = 0;
	uint16_t out[3] = {k.uwX, k.uwY, k.uwHeight};
	daggerStep(e.pSlots, addr(&k), out);
	k.uwX = out[0];
	k.uwY = out[1];
	k.uwHeight = out[2];
	v.uwDrakeSteps = (uint16_t)(v.uwDrakeSteps - 1);
	if(v.uwDrakeSteps != 0) {
		v.ulNextScript = e.scripts.s088B;
		bool left;
		if(!drakeLandsOnTarget(v.uwAimSteps, v.uwDrakeSteps, (int16_t)out[2], (int16_t)xDelta(k, tgt, left))) {
			return finish(e);
		}
		k.uwX = tgt.uwX;                                           // landed on the target: it is hurt and the fight ends
		k.uwY = tgt.uwY;
		jobRestart(e.pJobs, addr(&tgt), e.scripts.s07FD);
		killCurrent(e);
	}
	drakeLanded(e, k);                                             // LAB_02AA
	return finish(e);
}

}  // namespace

HandlerResult fighterDrake(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	*e.pSelf = addr(&k);
	v.ulNextScript = k.ulIdleScript;
	*e.pTarget = *e.pFirst;
	Knight &tgt = *rec(*e.pTarget);
	uint8_t &fl = v.ubDrakeFlags[0];
	if(fl & 0x02) {
		return drakeFlight(e, k, tgt);
	}
	if(k.ulAttacker != 0) {                                        // LAB_02B1
		Knight &att = *rec(k.ulAttacker);
		subHp(k, damageOf(e, att));
		hitSpark(e, k);
		v.ulNextScript = e.scripts.s0891;
		return finish(e);
	}
	if(k.ulHitTarget != 0) {                                       // LAB_02B2
		Knight &t = *rec(k.ulHitTarget);
		switch(drakeHitReply(k.uwAction)) {
		case DrakeHit::Grab:                                       // LAB_02B4: grabs it
			jobTogglePause(e.pJobs, addr(&t));
			v.ulNextScript = e.scripts.s0893;
			fl |= DRAKE_GRABBED;
			return finish(e);
		case DrakeHit::Strike:
			v.ulNextScript = e.scripts.s088E;
			return finish(e);
		case DrakeHit::Alternate: break;
		}
		v.ulNextScript = k.ulScript26;
		return finish(e);
	}
	if(fl & 0x01) {                                                // LAB_02B5
		v.ulNextScript = e.scripts.s0894;
		fl &= 0xFE;
		fl |= 0x20;
		return finish(e);
	}
	if(fl & 0x20) {                                                // LAB_02B8
		Knight &first = *rec(*e.pFirst);
		if(first.swHp <= 0) {                                      // LAB_02B6
			v.uwDrakeToggle ^= 1;
			v.ulNextScript = v.uwDrakeToggle != 0 ? e.scripts.s0895 : e.scripts.s0896;
			fl = 0;
			fl |= 0x40;
			return finish(e);
		}
		first.ulHitTarget = 0;
		first.ulAttacker = 0;
		jobTogglePause(e.pJobs, *e.pFirst);
		jobRestart(e.pJobs, addr(&first), first.ulIdleScript);
		CombatJob *job = jobOfOwner(e.pJobs, *e.pFirst);
		if(job != 0) {
			uint16_t d1 = DRAKE_RELEASE_OFFSET;
			if(bit(k.ubFacing, 1)) {
				d1 = (uint16_t)(0u - d1);
			}
			job->uwX = (uint16_t)(k.uwX + d1);
		}
		fl = 0;
		v.ulNextScript = k.ulScript26;
		return finish(e);
	}
	if(tgt.swHp <= 0) {
		return finish(e);
	}
	v.uwDrakeX = tgt.uwX;
	v.uwDrakeY = tgt.uwY;
	k.ubFacing = (int16_t)k.uwX < (int16_t)v.uwDrakeX ? 1 : 3;
	if(depthNear(k, tgt)) {
		fl = 0;
		bool left;
		const uint16_t d0 = xDelta(k, tgt, left);
		switch(drakeMelee((int16_t)d0, k.uwAction, rec(*e.pFirst)->ubDaggers)) {
		case DrakeMelee::JumpClose: k.ubBehaviourFlags2 = 0; break;
		case DrakeMelee::HopStrike:                                // LAB_02A2
			v.ulNextScript = e.scripts.s088D;
			k.uwAction = raw(Action::Strike8);
			*e.pHopDir = k.ubFacing;
			*e.pHopTable = e.scripts.tHop;
			return finish(e);
		case DrakeMelee::Heavy:                                    // LAB_02A3 / LAB_02A4
			k.uwAction = raw(Action::Heavy);
			v.ulNextScript = e.scripts.s0890;
			return finish(e);
		case DrakeMelee::Stand: return finish(e);                  // LAB_02A5
		case DrakeMelee::Jump: break;
		}
	}
	// LAB_02A6: aim and jump
	const DaggerAim r = daggerAim(*e.pBlock, *rec(*e.pSelf), *rec(*e.pTarget), &k, 0x50);
	*e.pAimDist = r.uwDist;
	v.uwAimSteps = r.uwSteps;
	v.uwAimArc = r.uwArc;
	if(!drakeCanJump(v.uwAimSteps)) {
		return finish(e);
	}
	if(k.ubBehaviourFlags2 != 0) {
		k.ubBehaviourFlags2 = (uint8_t)(k.ubBehaviourFlags2 - 1);
		if(k.ubBehaviourFlags2 != 0) {
			return finish(e);
		}
	}
	k.uwAction = raw(Action::Idle);                                                // LAB_02A7
	const uint16_t steps = drakeJumpSteps(v.uwAimSteps);
	e.pBlock->uwSteps = steps;
	v.uwDrakeSteps = steps;
	e.pBlock->uwExtra = v.uwAimArc;
	startDagger(e);
	fl = 0;
	fl |= 0x02;
	v.ulNextScript = e.scripts.s088A;
	return finish(e);
}

// ---------------------------------------------------------------------------------------------------------------------
// LAB_02CB: the thrown dagger, LAB_02D2: the idle object

HandlerResult fighterDagger(const FighterEnv &e, Knight &k) {
	FightVars &v = *e.pVars;
	*e.pSelf = addr(&k);
	v.ulNextScript = e.scripts.s07EC;
	if(k.ulHitTarget == 0) {
		if(k.ubFacing == 1) {
			if((int16_t)k.uwX < 0x14A) {
				return finish(e);
			}
		} else if((int16_t)k.uwX > 0) {                            // LAB_02CC
			return finish(e);
		} else if((int16_t)(0 - (int16_t)k.uwX) > 10) {
			return finish(e);
		}
	}
	v.ulNextScript = 0;                                            // LAB_02D1: it is gone
	return finish(e);
}

HandlerResult fighterIdle(const FighterEnv &e, Knight &k) {
	*e.pSelf = addr(&k);
	k.ulAttacker = 0;
	k.ulHitTarget = 0;
	e.pVars->ulNextScript = 0xFFFFFFFFu;
	return finish(e);
}

// ---------------------------------------------------------------------------------------------------------------------
// dispatch

namespace {

typedef HandlerResult (*FighterFn)(const FighterEnv &, Knight &);

// The handler of each AI kind, indexed by AiKind (rules/ai_fight.hpp); the kind of a creature type is the row of kFightAis.
const FighterFn kHandlers[(unsigned)AiKind::Count] = {
	fighterKnight, fighterFlyer, fighterBrawler, fighterCaster, fighterDragon, fighterDragonPart, fighterDrake,
	fighterDagger, fighterIdle, fighterSnatcher, fighterDemon, fighterAiKnight, fighterStalker,
};

// The order the original compares the handler address in (the S_40 handlers first): only matters when two addresses are equal.
const AiKind kRunOrder[(unsigned)AiKind::Count] = {
	AiKind::Snatcher, AiKind::Demon, AiKind::AiKnight, AiKind::Stalker, AiKind::Knight, AiKind::Flyer, AiKind::Brawler,
	AiKind::Caster, AiKind::Dragon, AiKind::DragonPart, AiKind::Drake, AiKind::Dagger, AiKind::Idle,
};

}  // namespace

// The identity of the handler of a kind: what the handler table LAB_08C7 holds for its creature types.
uint32_t fighterHandlerAddress(const FightScripts &s, AiKind kind) {
	switch(kind) {
	case AiKind::Knight: return s.hKnight;
	case AiKind::Flyer: return s.hFlyer;
	case AiKind::Brawler: return s.hBrawler;
	case AiKind::Caster: return s.hCaster;
	case AiKind::Dragon: return s.hDragon;
	case AiKind::DragonPart: return s.hDragonPart;
	case AiKind::Drake: return s.hDrake;
	case AiKind::Dagger: return s.hDagger;
	case AiKind::Idle: return s.hIdle;
	case AiKind::Snatcher: return s.hSnatcher;
	case AiKind::Demon: return s.hDemon;
	case AiKind::AiKnight: return s.hAiKnight;
	case AiKind::Stalker: return s.hStalker;
	case AiKind::Count: break;
	}
	return 0;
}

bool fighterRun(const FighterEnv &e, uint32_t ulHandler, uint32_t ulOwner, HandlerResult *pOut) {
	Knight &k = *rec(ulOwner);
	for(unsigned i = 0; i < (unsigned)AiKind::Count; ++i) {
		const AiKind kind = kRunOrder[i];
		if(ulHandler == fighterHandlerAddress(e.scripts, kind)) {
			*pOut = kHandlers[(unsigned)kind](e, k);
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// The script-called sound pickers LAB_02DC..LAB_02E9 (each ends in JMP LAB_0AA2)

namespace {

// LAB_02EE / LAB_02EF: the id lists of LAB_02E9 / LAB_02E8, $FFFF terminated.  They are adjacent in the asm (LAB_02EE has
// four terminators, LAB_02EF follows) and share one offset cell, so a list that was left at an offset past its end walks
// into the other one; past the end of both (code in the asm) this port starts over.
const uint16_t kSoundLists[19] = {0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,   // LAB_02EE
                                  0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0xFFFF};            // LAB_02EF
const unsigned kList02EE = 0, kList02EF = 10;

// LAB_02EA: the list picker: step the offset by 2, wrap to the first entry at the terminator.
uint16_t listPick(unsigned base, SoundCycle &c) {
	c.uwList = (uint16_t)(c.uwList + 2);
	unsigned i = base + (c.uwList >> 1);
	if(i >= sizeof kSoundLists / sizeof kSoundLists[0] || kSoundLists[i] == 0xFFFF) {
		c.uwList = 0;
		i = base;
	}
	return kSoundLists[i];
}

}  // namespace

void soundPick(SoundPick which, uint32_t &ulSeed, SoundCycle &cycle, void (*sound)(uint16_t uwId)) {
	switch(which) {
	case SOUND_02DC: {                                             // 0..3 with 0 and 1 folded together, + $1E
		uint32_t d0 = rngNext(ulSeed) & 3;
		if(d0 != 0) {
			d0 -= 1;
		}
		sound((uint16_t)(d0 + 0x1E));
		break;
	}
	case SOUND_02DE: {                                             // two sounds: $18.. (folded) then $14..$17
		uint32_t d0 = rngNext(ulSeed) & 3;
		if(d0 != 0) {
			d0 -= 1;
		}
		sound((uint16_t)(d0 + 0x18));
		sound((uint16_t)((rngNext(ulSeed) & 3) + 0x14));
		break;
	}
	case SOUND_02E0: sound((uint16_t)((rngNext(ulSeed) & 1) + 0x5B)); break;
	case SOUND_02E1: sound((uint16_t)((rngNext(ulSeed) & 1) + 0x6A)); break;
	case SOUND_02E2: sound((uint16_t)((rngNext(ulSeed) & 3) + 0x61)); break;
	case SOUND_02E3: sound((uint16_t)((rngNext(ulSeed) & 1) + 0x65)); break;
	case SOUND_02E4: {
		uint32_t d0 = rngNext(ulSeed) & 3;
		if(d0 != 0) {
			d0 -= 1;
		}
		sound((uint16_t)(d0 + 0x67));
		break;
	}
	case SOUND_02E6:                                               // a cycle 0..4: ids 4..8
		cycle.uwPlain = (uint16_t)(cycle.uwPlain + 1);
		if(!((int16_t)cycle.uwPlain < 5)) {
			cycle.uwPlain = 0;
		}
		sound((uint16_t)(cycle.uwPlain + 4));
		break;
	case SOUND_02E8: sound(listPick(kList02EF, cycle)); break;
	case SOUND_02E9: sound(listPick(kList02EE, cycle)); break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// The exports of fighters_int.hpp (the file-local helpers above, for fight_creatures.cpp / fight_ops.cpp)

namespace fi {

HandlerResult finish(const FighterEnv &e) { return ms::game::finish(e); }
uint16_t damageOf(const FighterEnv &e, const Knight &attacker) { return ms::game::damageOf(e, attacker); }
void killCurrent(const FighterEnv &e) { ms::game::killCurrent(e); }
Approach approach(const FighterEnv &e) { return ms::game::approach(e); }
int32_t dirSign(const Knight &k) { return ms::game::dirSign(k); }
void advancePhase(const FighterEnv &e, Knight &k, int32_t d6, int32_t d7) { ms::game::advancePhase(e, k, d6, d7); }
HandlerResult moveApply(const FighterEnv &e, Knight &k, const Dir &d) { return ms::game::moveApply(e, k, d); }
void hitSpark(const FighterEnv &e, const Knight &src) { ms::game::hitSpark(e, src); }
void dragonStep(const FighterEnv &e) { ms::game::dragonStep(e); }
HandlerResult knightHurtBy(const FighterEnv &e, Knight &self, Knight &att, uint8_t ubRuleType) {
	return ms::game::applyHurt(e, self, att, *hurtRuleFor(ubRuleType));
}

}  // namespace fi

}}  // namespace ms::game
