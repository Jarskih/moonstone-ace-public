// engine/paulablep - the band-limited step table of the Paula model's RENDER_BLEP renderer (PC only; builds with <math.h>: the table is
// computed in double once and stored as integers, so every use is integer arithmetic).
//
// The kernel: h(x) = sinc(x) * Kaiser(x / 16, beta 8) for |x| <= 16 output samples (a half-band low-pass: -6 dB at the Nyquist frequency,
// flat to ~20 kHz and far down above ~28 kHz at 48 kHz, so what folds into the audible band is negligible), normalised to unit sum.  S(x) is
// its running integral: the band-limited unit step.  The table holds the RESIDUAL R(x) = S(x) - U(x) (U the ideal step, 1 for x > 0), which is
// non-zero only for |x| < 16: a step is added to the output as "the held level" (U, the naive sample) plus delta * R.
//
// Layout: kBlepTaps rows (tap t is the output frame f + t - kBlepDelay for a step in frame f) of kBlepColumns columns (column p = the step's
// position in the frame, p / kBlepPhases of a frame from its start; the last column is the frame end and the position is interpolated between
// two columns).  Value = R((t - kBlepDelay + 1) - p / kBlepPhases), Q24 (1.0 = 1 << 24).
#pragma once
#include <stdint.h>

namespace ms {
namespace paula {

constexpr uint32_t kBlepTaps = 32;
constexpr uint32_t kBlepPhases = 1024;
constexpr uint32_t kBlepColumns = kBlepPhases + 1;
constexpr int kBlepShift = 24;

// The table, kBlepTaps * kBlepColumns values, built on first use.
const int32_t *blepKernel();

}  // namespace paula
}  // namespace ms
