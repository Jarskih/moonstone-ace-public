// game/flow/machine - the scene manager (ROADMAP 9.2a): see include/game/flow/flow.hpp and docs/GAME_FLOW.md.
//
// One scene stack, one transition table.  The manager never calls a scene's code except through the SceneDef hooks, and
// it is the only place that enters, leaves, suspends or resumes a scene, takes / releases memory marks and shared slots.
#include "game/flow/flow.hpp"

namespace ms { namespace game { namespace flow {

namespace {

// ---- reporting ---------------------------------------------------------------------------------------------------------

const char *nameOf(const Flow &f, SceneId e) {
	const SceneDef *s = flowScene(f, e);
	return s && s->szName ? s->szName : "?";
}

void log(Flow &f, SceneId e, const char *szWhat, uint32_t ulValue) {
	if (f.host.pfnLog) f.host.pfnLog(f.host.pCtx, nameOf(f, e), szWhat, ulValue);
}

// A table / memory error: the game cannot go on (host.pfnFatal names the scene and the value; the manager halts, so the
// overlay's entry returns and rtGameRun leaves the game cleanly).
void problem(Flow &f, SceneId e, const char *szWhy, uint32_t ulValue) {
	++f.uwErrors;
	f.bHalt = true;
	if (f.host.pfnFatal) f.host.pfnFatal(f.host.pCtx, nameOf(f, e), szWhy, ulValue);
}

// A bookkeeping error (a shared slot acquired twice): counted and logged, the game goes on (the buffers are the original's).
void warn(Flow &f, SceneId e, const char *szWhy, uint32_t ulValue) {
	++f.uwErrors;
	log(f, e, szWhy, ulValue);
}

// ---- shared slots ------------------------------------------------------------------------------------------------------
// aubSlotOwner[slot] = 1 + the stack level of the scene that holds it.  A scene acquires its slots when it is entered or
// resumed and releases them when it exits or is suspended under a pushed scene (the original reuses these buffers
// across screens: whatever a scene left in them is gone when it gets them back, so every scene reloads on (re)entry).

void slotsAcquire(Flow &f, uint8_t ubLevel) {
	const SceneDef *s = flowScene(f, f.aStack[ubLevel]);
	if (!s) return;
	for (uint8_t i = 0; i < s->ubAssetCount; ++i) {
		const uint8_t ubSlot = static_cast<uint8_t>(s->pAssets[i].eSlot);
		if (ubSlot == 0 || ubSlot >= SLOT_COUNT) continue;
		const uint8_t ubOwner = f.aubSlotOwner[ubSlot];
		if (ubOwner != 0 && ubOwner != ubLevel + 1) warn(f, s->eId, "WARN slot held by another scene", ubSlot);
		f.aubSlotOwner[ubSlot] = static_cast<uint8_t>(ubLevel + 1);
	}
}

void slotsRelease(Flow &f, uint8_t ubLevel) {
	for (uint8_t i = 1; i < SLOT_COUNT; ++i) {
		if (f.aubSlotOwner[i] == ubLevel + 1) f.aubSlotOwner[i] = 0;
	}
}

// ---- enter / leave -----------------------------------------------------------------------------------------------------

void enter(Flow &f, SceneId e) {
	if (f.ubDepth >= FLOW_DEPTH) {
		problem(f, e, "scene stack full", f.ubDepth);
		return;
	}
	const uint8_t ubLevel = f.ubDepth++;
	f.aStack[ubLevel] = e;
	f.aulMark[ubLevel] = memStackMark(f.mem);
	f.apfnBody[ubLevel] = f.pfnNextBody;                      // a flowCall's body belongs to the scene it enters
	f.apBodyCtx[ubLevel] = f.pNextBodyCtx;
	f.pfnNextBody = 0;
	f.pNextBodyCtx = 0;
	f.aulHighSaved[ubLevel] = f.mem.ulHigh;
	memStackHighReset(f.mem);
	const SceneDef *s = flowScene(f, e);
	if (!s) {
		problem(f, e, "scene not registered", static_cast<uint32_t>(e));
		return;
	}
	slotsAcquire(f, ubLevel);
	// Scene memory for the asset rows that need some (a scene that does not fit fails here, at its entry, never mid-play).
	for (uint8_t i = 0; i < s->ubAssetCount; ++i) {
		uint32_t ulAddr = 0;
		if (s->pAssets[i].ulBytes && !memStackAlloc(f.mem, s->pAssets[i].ulBytes, ulAddr)) {
			problem(f, e, "scene memory does not fit, bytes needed", s->pAssets[i].ulBytes);
		}
	}
	if (f.bHalt) return;                                      // it did not fit: the scene is not entered
	if (s->pfnEnter) s->pfnEnter(f);
}

void leave(Flow &f) {
	if (f.ubDepth == 0) return;
	const uint8_t ubLevel = static_cast<uint8_t>(f.ubDepth - 1);
	const SceneId e = f.aStack[ubLevel];
	const SceneDef *s = flowScene(f, e);
	if (s && s->pfnExit) s->pfnExit(f);
	log(f, e, "exit, scene memory high water", f.mem.ulHigh - f.aulMark[ubLevel]);
	slotsRelease(f, ubLevel);
	if (!memStackRelease(f.mem, f.aulMark[ubLevel])) warn(f, e, "WARN memory release above the top", f.aulMark[ubLevel]);
	if (f.mem.ulHigh < f.aulHighSaved[ubLevel]) f.mem.ulHigh = f.aulHighSaved[ubLevel];
	f.ubDepth = ubLevel;
}

// Runs the scene on top of the stack (and whatever replaces it at this level) until this level POPs; returns the event
// that popped it (Event::Halt when the manager halts).
Event runLevel(Flow &f) {
	const uint8_t ubLevel = f.ubDepth;
	for (;;) {
		if (f.bHalt || f.ubDepth != ubLevel) return Event::Halt;
		const SceneId eCur = f.aStack[ubLevel - 1];
		const SceneDef *s = flowScene(f, eCur);
		const Event ev = (s && s->pfnRun) ? s->pfnRun(f) : Event::Done;
		const Transition *t = flowFind(f, eCur, ev);
		if (!t) {
			problem(f, eCur, "no transition row for event", static_cast<uint32_t>(ev));
			f.bHalt = true;
			return Event::Halt;
		}
		switch (t->eOp) {
			case FlowOp::Switch:
				leave(f);
				enter(f, t->eTo);
				break;
			case FlowOp::Push: {
				slotsRelease(f, static_cast<uint8_t>(ubLevel - 1));   // suspended: its shared buffers are free for the pushed scene
				enter(f, t->eTo);
				runLevel(f);
				if (f.bHalt) return Event::Halt;
				slotsAcquire(f, static_cast<uint8_t>(ubLevel - 1));
				if (s && s->pfnResume) s->pfnResume(f);
				break;
			}
			case FlowOp::Pop:
				leave(f);
				return ev;
			case FlowOp::Halt:
				f.bHalt = true;
				return Event::Halt;
		}
	}
}

}  // namespace

const SceneDef *flowScene(const Flow &f, SceneId e) {
	const uint8_t i = static_cast<uint8_t>(e);
	if (i >= SCENE_COUNT || !f.ppScenes) return 0;
	return f.ppScenes[i];
}

const Transition *flowFind(const Flow &f, SceneId eFrom, Event ev) {
	for (uint16_t i = 0; i < f.uwRowCount; ++i) {           // the scene's own rows first ...
		const Transition &t = f.pRows[i];
		if (t.eFrom == eFrom && t.eEvent == ev) return &t;
	}
	for (uint16_t i = 0; i < f.uwRowCount; ++i) {           // ... then the rows for every scene
		const Transition &t = f.pRows[i];
		if (t.eFrom == SCENE_ANY && t.eEvent == ev) return &t;
	}
	return 0;
}

void flowInit(Flow &f, uint32_t ulMemBase, uint32_t ulMemSize) {
	f.ppScenes = g_flowScenes;
	f.pRows = g_flowRows;
	f.uwRowCount = g_flowRowCount;
	f.ubDepth = 0;
	for (uint8_t i = 0; i < SLOT_COUNT; ++i) f.aubSlotOwner[i] = 0;
	memStackInit(f.mem, ulMemBase, ulMemSize);
	f.ulGameMark = memStackMark(f.mem);
	f.uwErrors = 0;
	f.bHalt = false;
	f.pfnNextBody = 0;
	f.pNextBodyCtx = 0;
}

Event flowCall(Flow &f, Event ev, void (*pfnBody)(void *pCtx), void *pCtx) {
	f.pfnNextBody = pfnBody;
	f.pNextBodyCtx = pCtx;
	const Event r = flowRaise(f, ev);
	f.pfnNextBody = 0;                                        // (not entered: a table error, already reported)
	f.pNextBodyCtx = 0;
	return r;
}

void flowBody(Flow &f) {
	if (f.ubDepth == 0) return;
	const uint8_t ubLevel = static_cast<uint8_t>(f.ubDepth - 1);
	void (*pfn)(void *) = f.apfnBody[ubLevel];
	f.apfnBody[ubLevel] = 0;                                  // once
	if (pfn) pfn(f.apBodyCtx[ubLevel]);
}

void flowRun(Flow &f, SceneId eFirst) {
	f.bHalt = false;
	enter(f, eFirst);
	runLevel(f);
	while (f.ubDepth) leave(f);
	f.bHalt = false;
}

Event flowRaise(Flow &f, Event ev) {
	const SceneId eCur = flowCurrent(f);
	const Transition *t = flowFind(f, eCur, ev);
	if (!t || t->eOp != FlowOp::Push) {
		problem(f, eCur, "raised event has no PUSH row", static_cast<uint32_t>(ev));
		return ev;
	}
	const uint8_t ubLevel = static_cast<uint8_t>(f.ubDepth - 1);
	slotsRelease(f, ubLevel);
	enter(f, t->eTo);
	const Event r = runLevel(f);
	slotsAcquire(f, ubLevel);                                 // the raising code continues where it was: no resume hook
	return r;
}

}}}  // namespace ms::game::flow
