// game/flow/check - the table checks of the scene manager (ROADMAP 9.2a), run by tests/test_flow.py on the host.  A modder's
// new scene or row is checked by the same function (a host build of it is all the test needs).
#include "game/flow/flow.hpp"

namespace ms { namespace game { namespace flow {

namespace {

struct Report {
	char *pcErr;
	uint32_t ulCap;
	int iCount;
};

void put(Report &r, const char *a, const char *b, const char *c) {
	if (r.iCount++ != 0 || !r.pcErr || r.ulCap == 0) return;          // keep the first problem
	uint32_t n = 0;
	const char *aParts[3] = {a, b, c};
	for (const char *p : aParts) {
		for (; p && *p && n + 1 < r.ulCap; ++p) r.pcErr[n++] = *p;
	}
	r.pcErr[n] = 0;
}

const char *nm(const SceneDef *const *pp, SceneId e) {
	const uint8_t i = static_cast<uint8_t>(e);
	if (e == SCENE_ANY) return "<any>";
	return (i < SCENE_COUNT && pp[i] && pp[i]->szName) ? pp[i]->szName : "<unregistered>";
}

bool declares(const SceneDef *s, Event ev) {
	for (uint8_t i = 0; s && i < s->ubEventCount; ++i) {
		if (s->pEvents[i] == ev) return true;
	}
	return false;
}

const Transition *find(const Transition *pRows, uint16_t n, SceneId eFrom, Event ev) {
	for (uint16_t i = 0; i < n; ++i) if (pRows[i].eFrom == eFrom && pRows[i].eEvent == ev) return &pRows[i];
	for (uint16_t i = 0; i < n; ++i) if (pRows[i].eFrom == SCENE_ANY && pRows[i].eEvent == ev) return &pRows[i];
	return 0;
}

bool registered(const SceneDef *const *pp, SceneId e) {
	const uint8_t i = static_cast<uint8_t>(e);
	return i < SCENE_COUNT && pp[i] != 0;
}

}  // namespace

int flowCheck(const SceneDef *const *pp, const Transition *pRows, uint16_t uwRows, char *pcErr, uint32_t ulCap) {
	Report r = {pcErr, ulCap, 0};
	if (pcErr && ulCap) pcErr[0] = 0;
	// 1. the registry: every slot that holds a scene holds it under its own id, with a name and a run hook
	for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
		const SceneDef *s = pp[i];
		if (!s) continue;
		if (static_cast<uint8_t>(s->eId) != i) put(r, "registry: scene ", s->szName, " is registered under another id");
		if (!s->szName) put(r, "registry: a scene has no name", 0, 0);
		if (!s->pfnRun) put(r, "registry: scene ", s->szName, " has no run hook");
		for (uint8_t a = 0; a < s->ubAssetCount; ++a) {
			const AssetRow &row = s->pAssets[a];
			if (row.eKind != AssetKind::Buffer && !row.szFile) put(r, "assets: scene ", s->szName, " has a file row without a name");
			if (static_cast<uint8_t>(row.eSlot) >= SLOT_COUNT) put(r, "assets: scene ", s->szName, " names an unknown slot");
		}
	}
	if (!registered(pp, SceneId::Title)) put(r, "registry: no title scene", 0, 0);
	// 2. the rows: valid from / to, each row can fire (some scene declares its event), no duplicate {from, event}
	for (uint16_t i = 0; i < uwRows; ++i) {
		const Transition &t = pRows[i];
		if (t.eFrom != SCENE_ANY && !registered(pp, t.eFrom)) put(r, "rows: a row starts at an unregistered scene", 0, 0);
		if ((t.eOp == FlowOp::Switch || t.eOp == FlowOp::Push) && !registered(pp, t.eTo))
			put(r, "rows: a row from ", nm(pp, t.eFrom), " goes to an unregistered scene");
		bool bCanFire = false;
		if (t.eFrom == SCENE_ANY) {
			for (uint8_t s = 0; s < SCENE_COUNT; ++s) if (declares(pp[s], t.eEvent)) bCanFire = true;
		} else {
			bCanFire = declares(pp[static_cast<uint8_t>(t.eFrom)], t.eEvent);
		}
		if (!bCanFire) put(r, "rows: a row of ", nm(pp, t.eFrom), " is for an event the scene never returns");
		for (uint16_t j = 0; j < i; ++j) {
			if (pRows[j].eFrom == t.eFrom && pRows[j].eEvent == t.eEvent) put(r, "rows: duplicate row for ", nm(pp, t.eFrom), "");
		}
	}
	// 3. every event a scene declares has a row (its own or an <any> row)
	for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
		const SceneDef *s = pp[i];
		if (!s) continue;
		for (uint8_t e = 0; e < s->ubEventCount; ++e) {
			if (!find(pRows, uwRows, s->eId, s->pEvents[e])) put(r, "rows: scene ", s->szName, " returns an event that has no row");
		}
	}
	// 4. reachability from the title (switch and push rows; <any> rows apply to every reached scene that declares the event)
	bool abSeen[SCENE_COUNT] = {};
	abSeen[static_cast<uint8_t>(SceneId::Title)] = registered(pp, SceneId::Title);
	for (bool bGrew = true; bGrew;) {
		bGrew = false;
		for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
			if (!abSeen[i] || !pp[i]) continue;
			for (uint8_t e = 0; e < pp[i]->ubEventCount; ++e) {
				const Transition *t = find(pRows, uwRows, pp[i]->eId, pp[i]->pEvents[e]);
				if (!t || (t->eOp != FlowOp::Switch && t->eOp != FlowOp::Push) || !registered(pp, t->eTo)) continue;
				const uint8_t to = static_cast<uint8_t>(t->eTo);
				if (!abSeen[to]) { abSeen[to] = true; bGrew = true; }
			}
		}
	}
	for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
		if (pp[i] && !abSeen[i]) put(r, "reach: scene ", pp[i]->szName, " cannot be reached from the title");
	}
	// 5. push / pop balance: a scene entered by PUSH (or switched to from such a scene) can end in a POP
	bool abPushed[SCENE_COUNT] = {};
	for (uint16_t i = 0; i < uwRows; ++i) {
		if (pRows[i].eOp == FlowOp::Push && registered(pp, pRows[i].eTo)) abPushed[static_cast<uint8_t>(pRows[i].eTo)] = true;
	}
	for (bool bGrew = true; bGrew;) {
		bGrew = false;
		for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
			if (!abPushed[i] || !pp[i]) continue;
			for (uint8_t e = 0; e < pp[i]->ubEventCount; ++e) {
				const Transition *t = find(pRows, uwRows, pp[i]->eId, pp[i]->pEvents[e]);
				if (t && t->eOp == FlowOp::Switch && registered(pp, t->eTo) && !abPushed[static_cast<uint8_t>(t->eTo)]) {
					abPushed[static_cast<uint8_t>(t->eTo)] = true;
					bGrew = true;
				}
			}
		}
	}
	for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
		if (!abPushed[i] || !pp[i]) continue;
		bool bPops = false;
		for (uint8_t e = 0; e < pp[i]->ubEventCount; ++e) {
			const Transition *t = find(pRows, uwRows, pp[i]->eId, pp[i]->pEvents[e]);
			if (t && (t->eOp == FlowOp::Pop || (t->eOp == FlowOp::Switch && abPushed[static_cast<uint8_t>(t->eTo)]))) bPops = true;
		}
		if (!bPops) put(r, "balance: pushed scene ", pp[i]->szName, " never pops");
	}
	return r.iCount;
}

}}}  // namespace ms::game::flow
