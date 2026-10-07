// game/rules/ai_fight - the dispatch table: which fight AI plays which creature type (ROADMAP 9.6h; the handler table LAB_08C7 that LAB_01AE
// filled, now one table of rows).  See include/game/rules/ai_fight.hpp for the AIs themselves, one file each (ai_fight_<name>.cpp).
#include "game/rules/ai_fight.hpp"

namespace ms { namespace game {

// One row per ActorType the original writes into LAB_08C7.  The name is the monster kit's AI name (tools/monsterkit/ai_catalog.json);
// brawler, brawler_b and spearman are three types of one AI (the type byte tells the axe from the spear).
const FightAi kFightAis[] = {
	{ActorType::Be, AiKind::Flyer, "flyer"},
	{ActorType::Mudmen, AiKind::Snatcher, "snatcher"},
	{ActorType::Demon, AiKind::Demon, "demon"},
	{ActorType::KnightFight, AiKind::Knight, "knight"},
	{ActorType::KnightMap, AiKind::AiKnight, "ai_knight"},
	{ActorType::Dragon, AiKind::Dragon, "dragon"},
	{ActorType::TroggAxe, AiKind::Brawler, "brawler"},
	{ActorType::TroggAxeB, AiKind::Brawler, "brawler_b"},
	{ActorType::TroggSpear, AiKind::Brawler, "spearman"},
	{ActorType::Ratmen, AiKind::Caster, "caster"},
	{ActorType::DragonFlight, AiKind::Idle, "effect"},
	{ActorType::DragonPart, AiKind::DragonPart, "dragon_part"},
	{ActorType::Balok, AiKind::Drake, "drake"},
	{ActorType::Dagger, AiKind::Dagger, "dagger"},
	{ActorType::KnightAlt, AiKind::Knight, "knight_alt"},
	{ActorType::Troll, AiKind::Stalker, "stalker"},
};
const uint8_t kFightAiCount = (uint8_t)(sizeof kFightAis / sizeof kFightAis[0]);

const FightAi *fightAiByType(uint8_t ubType) {
	for(uint8_t i = 0; i < kFightAiCount; ++i) {
		if(raw(kFightAis[i].type) == ubType) {
			return &kFightAis[i];
		}
	}
	return 0;
}

const FightAi *fightAiByName(const char *pName) {
	for(uint8_t i = 0; i < kFightAiCount; ++i) {
		const char *a = kFightAis[i].pName;
		const char *b = pName;
		while(*a != 0 && *a == *b) {
			++a;
			++b;
		}
		if(*a == 0 && *b == 0) {
			return &kFightAis[i];
		}
	}
	return 0;
}

const char *fightAiName(AiKind kind) {
	for(uint8_t i = 0; i < kFightAiCount; ++i) {
		if(kFightAis[i].kind == kind) {
			return kFightAis[i].pName;
		}
	}
	return "";
}

}}  // namespace ms::game
