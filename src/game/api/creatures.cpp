// game/api/creatures - see include/game/api/creatures.hpp.
#include "game/api/creatures.hpp"

namespace ms { namespace game {

void creatureApply(const CreatureDef &d, Knight &k, const CreatureEnv &env) {
	if(d.ubAi != CREATURE_AI_ORIGINAL) {
		k.ubType = d.ubAi;
	}
	if(d.swHp >= 0) {
		k.swHp = d.swHp;
		k.swHpMax = d.swHpMax >= 0 ? d.swHpMax : d.swHp;
	} else if(d.swHpMax >= 0) {
		k.swHpMax = d.swHpMax;
	}
	if(d.swReach >= 0) k.uwReachX = (uint16_t)d.swReach;
	if(d.swTooClose >= 0) k.uwTooCloseX = (uint16_t)d.swTooClose;
	if(d.swDepth >= 0) k.uwDepthReach = (uint16_t)d.swDepth;
	if(d.uwIdle) {
		const uint32_t ulIdle = env.pfnAddr(SCRIPT_IDLE, d.uwIdle);
		k.ulIdleScript = ulIdle;
		k.ulScript26 = d.uwAlt ? env.pfnAddr(SCRIPT_IDLE, d.uwAlt) : ulIdle;
	} else if(d.uwAlt) {
		k.ulScript26 = env.pfnAddr(SCRIPT_IDLE, d.uwAlt);
	}
	if(d.uwHurtTable) k.ulHurtScripts = env.pfnAddr(SCRIPT_HURT_TABLE, d.uwHurtTable);
	if(d.uwWalkTable) k.ulWalkScripts = env.pfnAddr(SCRIPT_WALK_TABLE, d.uwWalkTable);
	if(d.uwActionTable) k.ulActionScripts = env.pfnAddr(SCRIPT_ACTION_TABLE, d.uwActionTable);
	if(d.uwDamageTable) k.ulDamageTable = env.pfnAddr(SCRIPT_DAMAGE_TABLE, d.uwDamageTable);
}

void creatureDamageApply(const CreatureDef &d, const CreatureEnv &env, uint32_t ulBuiltinTable) {
	const uint32_t ulTable = d.uwDamageTable ? env.pfnAddr(SCRIPT_DAMAGE_TABLE, d.uwDamageTable) : ulBuiltinTable;
	if(!ulTable) return;
	for(uint8_t i = 0; i < d.ubDamageCount && i < CREATURE_DAMAGE_SLOTS; ++i) {
		if(d.aDamage[i] >= 0) env.pfnStore32(ulTable + 4u * i, (uint32_t)d.aDamage[i]);
	}
}

}}  // namespace ms::game
