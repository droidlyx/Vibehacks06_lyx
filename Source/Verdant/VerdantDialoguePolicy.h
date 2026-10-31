#pragma once
#include <algorithm>
#include <cmath>

// Cheap deterministic prefilter; no model, network or actor side effects.
namespace VerdantDialoguePolicy
{
	constexpr float DefaultRadiusCm = 4000.f;
	constexpr float DefaultMaxHeightRatio = 5.f;

	inline bool Eligible(double DistanceSquared, double HeightA, double HeightB,
	                     double RadiusCm = DefaultRadiusCm, double MaxHeightRatio = DefaultMaxHeightRatio)
	{
		if (!std::isfinite(DistanceSquared) || DistanceSquared < 0 || !std::isfinite(HeightA)
			|| !std::isfinite(HeightB) || HeightA <= 0 || HeightB <= 0
			|| !std::isfinite(RadiusCm) || RadiusCm <= 0
			|| !std::isfinite(MaxHeightRatio) || MaxHeightRatio < 1)
			return false;
		return DistanceSquared <= RadiusCm * RadiusCm
			&& std::max(HeightA, HeightB) <= std::min(HeightA, HeightB) * MaxHeightRatio;
	}
}
