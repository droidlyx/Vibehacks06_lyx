#pragma once
#include <algorithm>
#include <cmath>

// NPC population limits are separate from player growth and manual scale preview.
namespace VerdantNPCScale
{
	constexpr float MinimumHeightCm = 30.f;
	constexpr float MaximumHeightCm = 1000.f;

	inline float HeightAtLogQuantile(float Quantile)
	{
		const float Q = std::isfinite(Quantile) ? std::clamp(Quantile, 0.f, 1.f) : 0.f;
		if (Q <= 0.f) return MinimumHeightCm;
		if (Q >= 1.f) return MaximumHeightCm;
		return MinimumHeightCm * std::pow(MaximumHeightCm / MinimumHeightCm, Q);
	}
}
