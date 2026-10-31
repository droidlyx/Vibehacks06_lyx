#pragma once
#include <algorithm>
#include <cmath>

// Pure, shared rules: no actor state, rendering, network, or hidden resource grants.
namespace VerdantEconomy
{
	constexpr float GrowthInterval = 5.f;
	constexpr float GrowthFraction = .20f;
	constexpr float FoodValueMultiplier = 1.f / 5.f;
	constexpr float FoodRefillInterval = 30.f;
	constexpr float RespawnDelay = 2.5f;
	constexpr float RespawnProtection = 3.f;
	constexpr float DeathHeightMultiplier = .85f;
	constexpr float MaximumHeightCm = 450.f;
	constexpr float FoodEpsilon = .001f;

	inline int Tier(float HeightCm)
	{
		if (!std::isfinite(HeightCm)) return 1;
		return HeightCm >= 320.f ? 4 : HeightCm >= 200.f ? 3 : HeightCm >= 120.f ? 2 : 1;
	}

	inline float Amount(int FoodTier)
	{
		return (FoodTier == 4 ? 100.f : FoodTier == 3 ? 40.f : FoodTier == 2 ? 15.f : 5.f) * FoodValueMultiplier;
	}

	inline float MinimumHeight(int FoodTier)
	{
		return FoodTier == 4 ? 320.f : FoodTier == 3 ? 200.f : FoodTier == 2 ? 120.f : 0.f;
	}

	inline float Capacity(float HeightCm)
	{
		if (!std::isfinite(HeightCm)) return 0.f;
		const float Ratio = std::max(1.f, HeightCm / 81.f);
		return 20.f * Ratio * Ratio;
	}

	inline float GrownHeight(float HeightCm, float Consumed)
	{
		if (!std::isfinite(HeightCm) || !std::isfinite(Consumed) || Consumed <= 0.f) return HeightCm;
		return std::min(MaximumHeightCm,
		                std::cbrt(HeightCm * HeightCm * HeightCm + Consumed * (81.f * 81.f * 81.f / 20.f)));
	}

	inline float RespawnHeight(float HeightCm, float StartingHeightCm)
	{
		return std::max(StartingHeightCm, HeightCm * DeathHeightMultiplier);
	}

	inline float TransferAmount(float Stock, float FreeCapacity, float Requested, int ReceiverTier, int SourceTier)
	{
		if (!std::isfinite(Stock) || !std::isfinite(FreeCapacity) || !std::isfinite(Requested)
			|| SourceTier < 1 || SourceTier > 4 || ReceiverTier < SourceTier)
			return 0.f;
		return std::max(0.f, std::min(Stock, std::min(FreeCapacity, Requested)));
	}
}
