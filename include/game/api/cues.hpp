// game/api/cues - the Command pattern for side effects of rules (docs/ARCHITECTURE.md 2; ROADMAP 9.3a).
// A rule never plays a sound or draws: it queues a cue, and the scene drains the queue after the rule returns and does
// the real work.  The queue is a fixed 16-entry ring (Object Pool rule: no heap).  Pure.
#pragma once
#include <stdint.h>

namespace ms { namespace game {

enum class CueKind : uint8_t {
	Sound,     // uwArg = sound id
	Text,      // uwArg = text id
	Redraw,    // redraw the current screen
	Screen,    // uwArg = SceneId to switch to
	Fade       // fade the palette
};

struct Cue {
	CueKind eKind;
	uint16_t uwArg;
};

constexpr uint8_t CUE_QUEUE_SIZE = 16;

struct CueQueue {
	Cue aCues[CUE_QUEUE_SIZE];
	uint8_t ubHead;    // next entry to pop
	uint8_t ubCount;   // entries queued
};

void cueQueueClear(CueQueue &q);
bool cuePush(CueQueue &q, CueKind eKind, uint16_t uwArg);   // false (cue dropped) when the ring is full
bool cuePop(CueQueue &q, Cue &out);                         // false when empty; oldest first

inline bool cueSound(CueQueue &q, uint16_t uwSoundId) { return cuePush(q, CueKind::Sound, uwSoundId); }
inline bool cueText(CueQueue &q, uint16_t uwTextId) { return cuePush(q, CueKind::Text, uwTextId); }
inline bool cueRedraw(CueQueue &q) { return cuePush(q, CueKind::Redraw, 0); }

}}  // namespace ms::game
