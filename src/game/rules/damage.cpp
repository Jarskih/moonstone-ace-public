// game/rules/damage - see include/game/rules/damage.hpp.  Every function cites the mog.asm labels it transcribes; word and byte
// arithmetic follows the asm (16-bit wrap, signed compares where the asm branches with BGT / BLE).  The ordering of the checks is the
// original's: a blow is parried before it hurts, and the facts that depend on the hit points AFTER the blow are computed from the
// flat amount the rule itself names.
#include "game/rules/damage.hpp"

namespace ms { namespace game {

namespace {

inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t *p) { return ((uint32_t)rd16(p) << 16) | rd16(p + 2); }
inline uint32_t setW(uint32_t reg, uint16_t v) { return (reg & 0xFFFF0000u) | v; }

// The hit points after a flat loss (SUBI.W #n,80(A1)): the wrapped word, read as the signed number the next BGT / BLE tests.
inline int16_t hpAfter(int16_t swHp, uint16_t uwAmount) { return (int16_t)((uint16_t)swHp - uwAmount); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Contact damage and the shield

// LAB_021B (mog.asm).  Step by step: damage = table[action] (low word) + strength + sword bonus; x2 for the heavy blow; x2 once more for
// a moonstone that matches the moon phase.
uint32_t contactDamage(const Knight &k, const uint8_t *pDamageTable, const Inventory &inv, uint16_t uwMoonFrame) {
	const int16_t swAction = (int16_t)k.uwAction;                           // D2 = 64(A0), a word index
	const uint32_t d0 = rd32(pDamageTable + swAction);                      // MOVE.L 0(A1,D2.W),D0
	uint16_t lo = (uint16_t)d0;
	lo = (uint16_t)(lo + k.ubStrength);                                     // ADD.W D1,D0 (D1 = byte 70)
	lo = (uint16_t)(lo + weaponDamageBonus(k.ulSword));                     // the sword's WeaponDef bonus (original +2 / +3 / +5)
	if(k.uwAction == raw(Action::Heavy)) {                                  // CMP.W #$20,D2 / LSL.W #1,D0
		lo = (uint16_t)(lo << 1);
	}
	const uint8_t ubMoon = inv.ubMoonstones;                                // MOVE.B 22(A2),D1
	if(ubMoon) {
		// One extra doubling at most (LAB_0223 falls into LAB_0224): the first matching flag wins.
		bool bDouble = false;
		if((ubMoon & 1u) && uwMoonFrame == MOON_FRAME_QUARTER) {
			bDouble = true;
		} else if((ubMoon & 8u) && uwMoonFrame == MOON_FRAME_QUARTER) {
			bDouble = true;
		} else if((ubMoon & 2u) && uwMoonFrame == MOON_FRAME_NEW) {
			bDouble = true;
		} else if((ubMoon & 4u) && uwMoonFrame == MOON_FRAME_FULL) {
			bDouble = true;
		}
		if(bDouble) {
			lo = (uint16_t)(lo << 1);
		}
	}
	return setW(d0, lo);
}

// LAB_0204 (mog.asm)
uint16_t protectedDamage(uint16_t uwDamage, const Inventory &inv) {
	const unsigned uCount = inv.ubDamageShift & 63u;                        // LSR.W D1,D0: count mod 64
	return uCount >= 16 ? (uint16_t)0 : (uint16_t)(uwDamage >> uCount);
}

// ---------------------------------------------------------------------------------------------------------
// Blocking

// LAB_01E6 (the decision; the latch and the sound are the caller's)
Parry blockDecide(uint16_t uwRequired, uint16_t uwDefenderAction, bool bStanceLatched, uint8_t ubDefenderFacing, uint8_t ubAttackerFacing) {
	if(uwRequired != uwDefenderAction) {                                    // the defender is not in the action this blow needs
		return Parry::None;
	}
	if(uwDefenderAction == raw(Action::Block)) {                            // LAB_01E9: the stance blocks once until the latch is released
		return bStanceLatched ? Parry::None : Parry::Stance;
	}
	if(ubDefenderFacing != ubAttackerFacing) {                              // facing each other: parried with a clang
		return Parry::Clash;
	}
	return Parry::None;
}

// ---------------------------------------------------------------------------------------------------------
// A knight is hit by a creature: one function per row of kHurtRules.  Each fills the outcome (zeroed by the caller: no damage, Idle).

namespace {

// LAB_020B (Mudmen, Dagger, and the tail of the knight rows): alive, the knight takes the contact damage and plays the hurt script of
// the blow; out already, it is knocked down (a strike $8 knocks it down harder).
void hurtByContact(const HurtFacts &f, HurtOutcome &o) {
	if(f.swHp > 0) {
		o.damage = HitDamage::Contact;
		o.script = FightScript::HurtByAction;
		return;
	}
	o.script = f.uwAttAction == raw(Action::Strike8) ? FightScript::KnightDown07F9 : FightScript::KnightDown07F8;
}

// type 0, the flyer: LAB_0206.  A flat 5.  The blow that kills makes the flyer finish the knight off (its own script restarts) unless the
// knight is itself hitting something or it is the demo; otherwise the knight is hurt (same facing as the flyer or opposite).
void hurtByFlyer(const HurtFacts &f, HurtOutcome &o) {
	o.damage = HitDamage::Fixed;
	o.uwAmount = DAMAGE_FLYER;
	const bool bSame = f.ubFacing == f.ubAttFacing;
	if(!f.bHitting && hpAfter(f.swHp, DAMAGE_FLYER) <= 0 && !f.bDemo) {    // LAB_0206: the kill
		o.ubEffects |= HURT_RESTART_ATTACKER;
		o.attackerScript = bSame ? FightScript::FlyerKillSame0849 : FightScript::FlyerKillOpposite084E;
		o.script = FightScript::Free;
		return;
	}
	o.script = bSame ? FightScript::KnightByBeSame084A : FightScript::KnightByBeOpposite084B;   // LAB_0209
}

// type 8, the demon: LAB_01F9.  10, or 8 for its action 4; a heavy or action 4 blow also turns the knight away.
void hurtByDemon(const HurtFacts &f, HurtOutcome &o) {
	o.damage = HitDamage::Fixed;
	o.uwAmount = DAMAGE_DEMON;
	if(f.uwAttAction == raw(Action::Heavy)) {
		o.ubEffects |= HURT_FACE_AWAY;                                      // LAB_01FC
	} else if(f.uwAttAction == raw(Action::Strike4)) {
		o.uwAmount = DAMAGE_DEMON_STRIKE4;
		o.ubEffects |= HURT_FACE_AWAY;
	}
	o.script = FightScript::HurtByAction;
}

// type 12 / 16, a knight against a knight: LAB_0205.  Out already: knocked down; blocked: the guard animation, no damage; else contact.
void hurtByKnight(const HurtFacts &f, HurtOutcome &o) {
	if(f.swHp <= 0) {
		o.script = FightScript::KnightDown07F9;                             // LAB_01F5
		return;
	}
	if(f.bBlocked) {
		o.script = FightScript::GuardOfOwnAction;
		return;
	}
	hurtByContact(f, o);
}

// type 20, the dragon: LAB_0200.  The knight is drawn to the dragon's depth.  Action 4: 20 through the shield; the rest is the flight blow.
void hurtByDragonFlight(const HurtFacts &f, HurtOutcome &o);   // LAB_0201
void hurtByDragon(const HurtFacts &f, HurtOutcome &o) {
	o.ubEffects |= HURT_MATCH_DEPTH;
	if(f.uwAttAction == raw(Action::Strike4)) {
		o.damage = HitDamage::FixedProtected;                               // LAB_0202
		o.uwAmount = DAMAGE_DRAGON_STRIKE4;
		o.script = FightScript::HurtByAction;
		return;
	}
	hurtByDragonFlight(f, o);
}

// type 24 / 28, the axe troggs: LAB_01F2.  Alive: a parried blow plays the guard animation; else the raw table value at the blow's action
// (no strength, no sword: LAB_01F3).  Out already: the knight falls (harder for the first axe).
void hurtByAxe(const HurtFacts &f, HurtOutcome &o) {
	if(f.swHp > 0) {
		if(f.bBlocked) {
			o.script = FightScript::GuardOfOwnAction;
			return;
		}
		o.damage = HitDamage::TableByAction;
		o.script = FightScript::HurtByAction;
		return;
	}
	o.script = f.ubAttType == raw(ActorType::TroggAxe) ? FightScript::KnightDown07F9 : FightScript::KnightDown07F8;   // LAB_01F4 / LAB_01F5
}

// type 32, the spearman: LAB_01F6.  Parried: the parry script.  Else a flat 3; the blow that kills makes the spearman finish with its
// thrust (its job restarts) unless the knight is hitting something or it is the demo.
void hurtBySpear(const HurtFacts &f, HurtOutcome &o) {
	if(f.bBlocked) {
		o.script = FightScript::KnightParry07F3;
		return;
	}
	o.damage = HitDamage::Fixed;
	o.uwAmount = DAMAGE_SPEARMAN;
	o.script = FightScript::HurtByAction;
	if(!f.bHitting && !(hpAfter(f.swHp, DAMAGE_SPEARMAN) > 0 || f.bDemo)) {
		o.ubEffects |= HURT_RESTART_ATTACKER;
		o.attackerScript = FightScript::SpearKill081C;
		o.script = FightScript::Free;
	}
}

// type 36, the ratmen: LAB_01EF.  Their action 4 and 8 take the raw table entries 1 and 2 (the init rewrites them for the moon phases);
// action 4 also flags a life loss.  Any other blow is the contact damage.
void hurtByRatmen(const HurtFacts &f, HurtOutcome &o) {
	if(f.uwAttAction == raw(Action::Strike4)) {
		o.damage = HitDamage::TableSlot4;
		o.script = FightScript::KnightCursed085F;
		o.ubEffects |= HURT_LIFE_LOSS;
		return;
	}
	if(f.uwAttAction == raw(Action::Strike8)) {
		o.damage = HitDamage::TableSlot8;
		o.script = FightScript::KnightStruck085E;
		return;
	}
	hurtByContact(f, o);
}

// type 40, the dragon in flight: LAB_0201.  QUIRK: the ATTACKER's action becomes $20, so the hurt script is the heavy blow's.  30 through
// the shield.
void hurtByDragonFlight(const HurtFacts &, HurtOutcome &o) {
	o.ubEffects |= HURT_ATTACKER_HEAVY;
	o.damage = HitDamage::FixedProtected;                                   // LAB_0202
	o.uwAmount = DAMAGE_DRAGON_FLIGHT;
	o.script = FightScript::HurtByAction;
}

// type 44, the dragon's object: LAB_0203.  QUIRK: the damage is the object's TYPE byte minus 10 (what the original's register still
// holds), 34, through the shield.  The knight is thrown and faces left.
void hurtByDragonObject(const HurtFacts &, HurtOutcome &o) {
	o.damage = HitDamage::FixedProtected;
	o.uwAmount = DAMAGE_DRAGON_OBJECT;
	o.ubEffects |= HURT_HOP | HURT_FACE_LEFT;
	o.script = FightScript::KnightHop07FB;
}

// type 48, Balok: LAB_01ED.  A flat 5; action 8 turns the knight away.
void hurtByDrake(const HurtFacts &f, HurtOutcome &o) {
	if(f.uwAttAction == raw(Action::Strike8)) {
		o.ubEffects |= HURT_FACE_AWAY;
	}
	o.damage = HitDamage::Fixed;
	o.uwAmount = DAMAGE_DRAKE;
	o.script = FightScript::HurtByAction;
}

// type 64, the troll: LAB_01FD.  A flat 7; a heavy blow that kills flings the knight.
void hurtByStalker(const HurtFacts &f, HurtOutcome &o) {
	o.damage = HitDamage::Fixed;
	o.uwAmount = DAMAGE_STALKER;
	if(f.uwAttAction == raw(Action::Heavy) && hpAfter(f.swHp, DAMAGE_STALKER) <= 0) {
		o.script = FightScript::KnightFlung07FD;
		return;
	}
	o.script = FightScript::HurtByAction;
}

}  // namespace

// The Type Object table of the knight's reactions, one row per attacker ActorType (original: the handler table LAB_0621, filled by
// LAB_020F).  Snatcher (Mudmen) and a thrown dagger take the plain contact path.
const HurtRule kHurtRules[] = {
	{ActorType::Be, BlockRule::Never, hurtByFlyer, "flyer"},
	{ActorType::Mudmen, BlockRule::Never, hurtByContact, "snatcher"},
	{ActorType::Demon, BlockRule::Never, hurtByDemon, "demon"},
	{ActorType::KnightFight, BlockRule::WhileAlive, hurtByKnight, "knight"},
	{ActorType::KnightMap, BlockRule::WhileAlive, hurtByKnight, "ai_knight"},
	{ActorType::Dragon, BlockRule::Never, hurtByDragon, "dragon"},
	{ActorType::TroggAxe, BlockRule::WhileAlive, hurtByAxe, "brawler"},
	{ActorType::TroggAxeB, BlockRule::WhileAlive, hurtByAxe, "brawler_b"},
	{ActorType::TroggSpear, BlockRule::Always, hurtBySpear, "spearman"},
	{ActorType::Ratmen, BlockRule::Never, hurtByRatmen, "caster"},
	{ActorType::DragonFlight, BlockRule::Never, hurtByDragonFlight, "dragon_flight"},
	{ActorType::DragonPart, BlockRule::Never, hurtByDragonObject, "dragon_part"},
	{ActorType::Balok, BlockRule::Never, hurtByDrake, "drake"},
	{ActorType::Dagger, BlockRule::Never, hurtByContact, "dagger"},
	{ActorType::Troll, BlockRule::Never, hurtByStalker, "stalker"},
};
const uint8_t kHurtRuleCount = (uint8_t)(sizeof kHurtRules / sizeof kHurtRules[0]);

const HurtRule *hurtRuleFor(uint8_t ubType) {
	for(uint8_t i = 0; i < kHurtRuleCount; ++i) {
		if(raw(kHurtRules[i].type) == ubType) {
			return &kHurtRules[i];
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------------------------------------
// The knight hit something

// LAB_01E0 / LAB_01E1: the record the knight hit decides whether its attack carries on.  The dragon in flight hurts back (LAB_0201);
// the mapped creature types share LAB_01E1; a type the original has no slot for does nothing.
HitOutcome knightHitOutcome(const HitFacts &f) {
	HitOutcome o;
	o.script = FightScript::Idle;
	o.bHurtByTarget = false;
	if(f.ubTargetType == raw(ActorType::DragonFlight)) {
		o.bHurtByTarget = true;
		return o;
	}
	const HurtRule *pRule = hurtRuleFor(f.ubTargetType);
	if(pRule == 0) {
		return o;
	}
	// LAB_01E1
	if(f.ubTargetType == raw(ActorType::KnightFight) || f.ubTargetType == raw(ActorType::KnightMap)) {   // LAB_01E4
		if(f.swTargetHp > 0) {
			if(f.uwTargetAction == raw(Action::Block)) {                    // a blocking knight stops the blow
				o.script = FightScript::Stop;
				return o;
			}
		} else if(f.uwAction == raw(Action::Strike8) && !f.bDemo) {         // a strike on a knight that is down
			o.script = FightScript::Stop;
			return o;
		}
	}
	if(f.uwTargetAction == raw(Action::Strike24)) {                         // LAB_01E2
		o.script = FightScript::Stop;
		return o;
	}
	o.script = FightScript::Alternate;                                      // LAB_01E3
	return o;
}

}}  // namespace ms::game
