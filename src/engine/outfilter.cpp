// engine/outfilter - see include/engine/outfilter.hpp.  PC only.
#include "engine/outfilter.hpp"

#include <math.h>

namespace ms {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHighPassHz = 5.2;
constexpr double kA500LowPassHz = 4420.97;
constexpr double kA1200LowPassHz = 34400.0;
constexpr double kLedHz = 3090.53;
constexpr double kLedQ = 0.660225;

int16_t toS16(double dV) {
	const double dR = floor(dV + 0.5);
	return dR > 32767.0 ? 32767 : (dR < -32768.0 ? -32768 : static_cast<int16_t>(dR));
}

}  // namespace

void filterInit(OutFilter &f, uint8_t ubModel, uint32_t ulRate) {
	f = OutFilter();
	f.ubModel = ubModel > FILTER_A1200_LED ? static_cast<uint8_t>(FILTER_OFF) : ubModel;
	if(f.ubModel == FILTER_OFF) {
		return;
	}
	const double dFs = static_cast<double>(ulRate);
	const bool isA500 = f.ubModel == FILTER_A500 || f.ubModel == FILTER_A500_LED;
	f.isLedOn = f.ubModel == FILTER_A500_LED || f.ubModel == FILTER_A1200_LED;
	f.dLpA = 1.0 - exp(-2.0 * kPi * (isA500 ? kA500LowPassHz : kA1200LowPassHz) / dFs);   // matched pole: right at 34 kHz, where a bilinear one warps
	f.dHpA = 1.0 / (1.0 + 2.0 * kPi * kHighPassHz / dFs);
	if(f.isLedOn) {
		const double dW0 = 2.0 * kPi * kLedHz / dFs;
		const double dAlpha = sin(dW0) / (2.0 * kLedQ);
		const double dA0 = 1.0 + dAlpha;
		f.dB0 = (1.0 - cos(dW0)) / 2.0 / dA0;
		f.dB1 = (1.0 - cos(dW0)) / dA0;
		f.dB2 = f.dB0;
		f.dA1 = -2.0 * cos(dW0) / dA0;
		f.dA2 = (1.0 - dAlpha) / dA0;
	}
}

void filterRun(OutFilter &f, int16_t *pFrames, uint32_t ulFrames) {
	if(f.ubModel == FILTER_OFF) {
		return;
	}
	for(uint32_t i = 0; i < ulFrames; ++i) {
		for(uint32_t s = 0; s < 2; ++s) {
			double dX = static_cast<double>(pFrames[2 * i + s]);
			f.adLp[s] += (dX - f.adLp[s]) * f.dLpA;
			dX = f.adLp[s];
			const double dY = f.dHpA * (f.adHpY[s] + dX - f.adHpX[s]);
			f.adHpX[s] = dX;
			f.adHpY[s] = dY;
			dX = dY;
			if(f.isLedOn) {
				const double dOut = f.dB0 * dX + f.adZ1[s];
				f.adZ1[s] = f.dB1 * dX - f.dA1 * dOut + f.adZ2[s];
				f.adZ2[s] = f.dB2 * dX - f.dA2 * dOut;
				dX = dOut;
			}
			pFrames[2 * i + s] = toS16(dX);
		}
	}
}

}  // namespace ms
