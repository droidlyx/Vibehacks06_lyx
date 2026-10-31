#pragma once

#include "CoreMinimal.h"

// Shared player spawn/reset/HUD values; no reflected class layout changes.
namespace VerdantPlayerScale
{
	constexpr float StartingBodySize = .1f;
	constexpr float StartingHeightCm = StartingBodySize * 180.f;
	constexpr float ReferenceBodySize = .45f;
	constexpr float SpeedMultiplier = .5f;

	// Half the old speed at the former 0.81 m body, then linear in body size.
	// The legacy exponent only calibrates that fixed reference, not the player.
	inline float MovementSpeed(float BodySize, float LegacyBaseSpeed, float LegacyExponent = .32f)
	{
		return SpeedMultiplier * LegacyBaseSpeed * FMath::Pow(ReferenceBodySize, LegacyExponent)
			* FMath::Max(0.f, BodySize) / ReferenceBodySize;
	}
}
