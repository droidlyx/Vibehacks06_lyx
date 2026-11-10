#pragma once
#include <algorithm>
#include <cmath>

namespace VerdantManualScale
{
	constexpr double MultiplierPerSecond = 1.5;

	// Integrate from the actual size each frame; a blocked expansion accrues no debt.
	inline double NextSize(double Current, int Direction, double Seconds, double Minimum, double Maximum)
	{
		if (!std::isfinite(Current) || Current <= 0 || !std::isfinite(Seconds) || Seconds <= 0
			|| !std::isfinite(Minimum) || !std::isfinite(Maximum) || Minimum <= 0 || Maximum < Minimum
			|| Direction == 0)
			return Current;
		const double Factor = std::pow(MultiplierPerSecond, (Direction > 0 ? 1. : -1.) * Seconds);
		return std::clamp(Current * Factor, Minimum, Maximum);
	}
}
