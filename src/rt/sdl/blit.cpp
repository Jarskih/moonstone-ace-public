// rt/sdl/blit - rt::platform::blit for the PC (ROADMAP 11.7): the software blitter of engine/softblit over the chip RAM.
// A blit runs to completion inside blitRun, so there is never anything to wait for.
#include "rt/platform/blit.hpp"

#include <stdint.h>
#include <stdlib.h>

#include "engine/softblit.hpp"
#include "rt/platform/log.hpp"
#include "rt/sdl/chip.hpp"
#include "rt/sdl/timer.hpp"

namespace rt {
namespace platform {

namespace {

}  // namespace

void blitWait() {
	// synchronous blitter: nothing is ever in flight
}

bool blitBusy() {
	return false;
}

void blitRun(const ms::BlitOp &sOp) {
	const ms::softblit::Status status = ms::softblit::run(sdl::chipView(), sOp);
	if(status != ms::softblit::BLIT_OK) {
		// A blit the game never programs, or one that leaves chip RAM: fail loudly in debug, log and skip it otherwise.
		logFormat("blitRun: %s (con0 %04x con1 %04x size %04x A %08x B %08x C %08x D %08x)\n", ms::softblit::statusText(status),
		          (unsigned)sOp.con0, (unsigned)sOp.con1, (unsigned)sOp.size, (unsigned)sOp.ptA, (unsigned)sOp.ptB,
		          (unsigned)sOp.ptC, (unsigned)sOp.ptD);
#ifndef NDEBUG
		abort();
#endif
	}
	sdl::timerRaiseBlitDone();   // the BLIT interrupt: the chip finished (synchronously, see rt/sdl/timer.hpp)
}

}  // namespace platform
}  // namespace rt
