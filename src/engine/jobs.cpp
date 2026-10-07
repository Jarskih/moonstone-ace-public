// engine/jobs - see jobs.hpp. Transcribed from program.asm LAB_01E4 / LAB_01DA / LAB_01E8 / LAB_01EC.
#include "engine/jobs.hpp"

namespace ms {

namespace {

// Byte loops on purpose: no libc in the engine, and GCC must not turn them into a memset/memcpy call.
#if defined(__GNUC__) && !defined(__clang__)
#define MS_NO_LIBCALLS __attribute__((optimize("no-tree-loop-distribute-patterns")))
#else
#define MS_NO_LIBCALLS
#endif

MS_NO_LIBCALLS void zero(void *pDst, uint32_t n) {
	uint8_t *p = (uint8_t *)pDst;
	while(n--) {
		*p++ = 0;
	}
}

MS_NO_LIBCALLS void copy(void *pDst, const void *pSrc, uint32_t n) {
	uint8_t *d = (uint8_t *)pDst;
	const uint8_t *s = (const uint8_t *)pSrc;
	while(n--) {
		*d++ = *s++;
	}
}

}  // namespace

void jobsSort(const JobEnv &env) {
	bool isSwapped;
	do {
		isSwapped = false;
		for(uint32_t i = 0; i + 1 < JOB_COUNT; ++i) {
			Job *pJob = &env.pPool[i];
			if(pJob[1].z >= pJob->z) {
				continue;
			}
			copy(env.pTempJob, pJob, sizeof(Job));
			copy(pJob, pJob + 1, sizeof(Job));
			copy(pJob + 1, env.pTempJob, sizeof(Job));
			isSwapped = true;
		}
	} while(isSwapped);
}

MS_NO_LIBCALLS void jobsClearDrawLists(uint8_t *pLists) {
	for(uint32_t i = 0; i < DRAW_LISTS_BYTES; ++i) {
		pLists[i] = 0xFF;
	}
}

void jobsReset(const JobEnv &env) {
	zero(env.pPool, sizeof(Job) * JOB_COUNT);
	zero(env.pStates, sizeof(JobState));  // only block 0, as in the original; alloc clears a block when it takes it
	for(uint32_t i = 0; i < JOB_COUNT; ++i) {
		env.pPool[i].pState = jobAddr(&env.pStates[i]);
	}
}

Job *jobAlloc(const JobEnv &env, const JobInit &init) {
	Job *pJob = env.pPool;
	uint32_t i = 0;
	while(pJob->active) {
		if(++i == JOB_COUNT) {
			*env.pFullFlag = 2;
			return nullptr;
		}
		++pJob;
	}
	zero(jobPtr<JobState>(pJob->pState), sizeof(JobState));
	// Fields 12..21, 23, 33..35 and 40 keep whatever the slot held; the original never touched them.
	pJob->pScript = init.pScript;
	pJob->id = init.id;
	pJob->pFrames = init.pFrames;
	pJob->x = init.x;
	pJob->y = init.y;
	pJob->z = init.z;
	pJob->flags = init.flags;
	pJob->handler = init.handler;
	pJob->active = 1;
	pJob->hasScript = 1;
	return pJob;
}

void jobsSpawnQueued(const JobEnv &env) {
	for(uint32_t i = 0; i < JOB_COUNT; ++i) {
		Job *pJob = &env.pPool[i];
		if(!pJob->active || pJob->hasScript) {
			continue;
		}
		// The handler index is a byte offset into the table of longs, not an element number.
		const uint32_t handler = *(const uint32_t *)(env.pHandlers + pJob->handler);
		JobHandlerResult res;
		env.callHandler(handler, pJob, &res);
		if(res.pScript == JOB_HANDLER_KEEP) {
			continue;
		}
		if(res.pScript == 0) {
			pJob->active = 0;
			pJob->hasScript = 0;  // the original clears the pair with one word write
			continue;
		}
		pJob->pScript = res.pScript;
		pJob->x = res.x;
		pJob->y = res.y;
		pJob->z = res.z;
		pJob->flags = res.flags;
		pJob->hasScript = 1;
	}
}

void jobsTick(const JobEnv &env) {
	jobsSort(env);
	for(uint32_t i = 0; i < JOB_COUNT; ++i) {
		Job *pJob = &env.pPool[i];
		if(!pJob->active || pJob->paused) {
			continue;
		}
		JobState *pState = jobPtr<JobState>(pJob->pState);
		*env.pScriptFlag = 0;
		if(!pState->twin) {
			env.runScript(pJob, pState);
			continue;
		}
		// Twin pass: run a copy of the job (y = 0, script from the state block, scratch state) and then the job itself.
		copy(env.pTempJob, pJob, sizeof(Job));
		env.pTempJob->y = 0;
		env.pTempJob->pScript = pState->pTwinScript;
		env.pTempJob->pState = jobAddr(env.pTempState);
		env.runScript(env.pTempJob, pState);
		env.runScript(pJob, pState);
	}
}

}  // namespace ms
