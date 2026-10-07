// game/api/cues - the cue ring (include/game/api/cues.hpp, ROADMAP 9.3a).  Pure.
#include "game/api/cues.hpp"

namespace ms { namespace game {

void cueQueueClear(CueQueue &q) {
	q.ubHead = 0;
	q.ubCount = 0;
}

bool cuePush(CueQueue &q, CueKind eKind, uint16_t uwArg) {
	if(q.ubCount >= CUE_QUEUE_SIZE) {
		return false;
	}
	Cue &cue = q.aCues[(q.ubHead + q.ubCount) % CUE_QUEUE_SIZE];
	cue.eKind = eKind;
	cue.uwArg = uwArg;
	++q.ubCount;
	return true;
}

bool cuePop(CueQueue &q, Cue &out) {
	if(q.ubCount == 0) {
		return false;
	}
	out = q.aCues[q.ubHead];
	q.ubHead = (q.ubHead + 1) % CUE_QUEUE_SIZE;
	--q.ubCount;
	return true;
}

}}  // namespace ms::game
