// engine/paulablep - see include/engine/paulablep.hpp.  PC only.
#include "engine/paulablep.hpp"

#include <math.h>

namespace ms {
namespace paula {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBeta = 8.0;
constexpr int kHalf = 16;                                                       // kernel half-width in output samples
constexpr uint32_t kGrid = kBlepPhases * 2 * kHalf + 1;                         // fine grid points over [-16, 16]

int32_t s_alTable[kBlepTaps * kBlepColumns];
double s_adStep[kGrid];                                                         // S on the fine grid
bool s_isReady;

double besselI0(double dX) {
	double dSum = 1.0;
	double dTerm = 1.0;
	for(int k = 1; k < 60; ++k) {
		dTerm *= (dX / (2.0 * k)) * (dX / (2.0 * k));
		dSum += dTerm;
	}
	return dSum;
}

double kernel(double dX) {
	const double dU = dX / kHalf;
	if(dU <= -1.0 || dU >= 1.0) {
		return 0.0;
	}
	const double dSinc = dX == 0.0 ? 1.0 : sin(kPi * dX) / (kPi * dX);
	return dSinc * besselI0(kBeta * sqrt(1.0 - dU * dU)) / besselI0(kBeta);
}

void build() {
	// cumulative trapezoid integral of the kernel on the fine grid (spacing 1 / kBlepPhases)
	const double dDx = 1.0 / kBlepPhases;
	double dPrev = kernel(-kHalf);
	s_adStep[0] = 0.0;
	for(uint32_t i = 1; i < kGrid; ++i) {
		const double dCur = kernel(-kHalf + i * dDx);
		s_adStep[i] = s_adStep[i - 1] + 0.5 * (dPrev + dCur) * dDx;
		dPrev = dCur;
	}
	const double dTotal = s_adStep[kGrid - 1];
	for(uint32_t t = 0; t < kBlepTaps; ++t) {
		const int lJ = static_cast<int>(t) - kHalf;                              // the step is in frame f, this tap is output frame f + lJ
		for(uint32_t p = 0; p < kBlepColumns; ++p) {
			const uint32_t ulIndex = (t + 1) * kBlepPhases - p;                  // x = (lJ + 1) - p / kBlepPhases, as a grid index from -16
			const double dResidual = s_adStep[ulIndex] / dTotal - (lJ >= 0 ? 1.0 : 0.0);
			s_alTable[t * kBlepColumns + p] = static_cast<int32_t>(lround(dResidual * (1 << kBlepShift)));
		}
	}
	s_isReady = true;
}

}  // namespace

const int32_t *blepKernel() {
	if(!s_isReady) {
		build();
	}
	return s_alTable;
}

}  // namespace paula
}  // namespace ms
