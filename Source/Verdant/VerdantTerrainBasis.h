#pragma once

#include "CoreMinimal.h"

// Original procedural implementation; no Ember heightmap pixels are included.
// Method references (not a claim to reconstruct the premade reference asset):
// https://web.mit.edu/cesium/Public/terrain.pdf
// https://github.com/SebLague/Hydraulic-Erosion
namespace VerdantTerrainBasis
{
	constexpr int32 Resolution = 256;
	constexpr int32 Seed = 603107;
	constexpr int32 DropletCount = 20000;
	constexpr int32 DropletLifetime = 48;

	inline int32 WrapIndex(int32 I, int32 Size)
	{
		return (I % Size + Size) % Size;
	}

	inline float WrapCoordinate(float X, int32 Size)
	{
		return X - FMath::FloorToFloat(X / static_cast<float>(Size)) * Size;
	}

	inline float UnitWrap(float X)
	{
		return X - FMath::FloorToFloat(X);
	}

	// Blend opposite samples with a smoothstep so value and first derivative
	// agree across the tile seam. The underlying noise remains UE's Perlin.
	inline float PeriodicNoise(float U, float V, float Frequency, const FVector2D& Offset)
	{
		U = UnitWrap(U);
		V = UnitWrap(V);
		const float SX = U * U * (3.f - 2.f * U);
		const float SY = V * V * (3.f - 2.f * V);
		auto Noise = [&](float X, float Y)
		{
			return FMath::PerlinNoise2D(FVector2D(X * Frequency, Y * Frequency) + Offset);
		};
		return FMath::Lerp(FMath::Lerp(Noise(U, V), Noise(U - 1.f, V), SX),
		                   FMath::Lerp(Noise(U, V - 1.f), Noise(U - 1.f, V - 1.f), SX), SY);
	}

	struct FHeightGradient
	{
		float Height;
		float DX;
		float DY;
	};

	inline FHeightGradient ReadGrid(const TArray<float>& Map, int32 Size, float X, float Y)
	{
		const int32 IX = FMath::FloorToInt(X), IY = FMath::FloorToInt(Y);
		const float U = X - IX, V = Y - IY;
		const int32 X0 = WrapIndex(IX, Size), X1 = WrapIndex(IX + 1, Size);
		const int32 Y0 = WrapIndex(IY, Size), Y1 = WrapIndex(IY + 1, Size);
		const float A = Map[Y0 * Size + X0], B = Map[Y0 * Size + X1];
		const float C = Map[Y1 * Size + X0], D = Map[Y1 * Size + X1];
		return {
			FMath::Lerp(FMath::Lerp(A, B, U), FMath::Lerp(C, D, U), V),
			FMath::Lerp(B - A, D - C, V), FMath::Lerp(C - A, D - B, U)
		};
	}

	inline void Deposit(TArray<float>& Map, float X, float Y, float Amount)
	{
		const int32 IX = FMath::FloorToInt(X), IY = FMath::FloorToInt(Y);
		const float U = X - IX, V = Y - IY;
		for (int32 DY = 0; DY <= 1; ++DY)
			for (int32 DX = 0; DX <= 1; ++DX)
				Map[WrapIndex(IY + DY, Resolution) * Resolution + WrapIndex(IX + DX, Resolution)]
					+= Amount * (DX ? U : 1.f - U) * (DY ? V : 1.f - V);
	}

	inline float ErodeBrush(TArray<float>& Map, float X, float Y, float Amount)
	{
		const int32 IX = FMath::RoundToInt(X), IY = FMath::RoundToInt(Y);
		float Removed = 0.f;
		// A compact normalized brush avoids single-sample pits; never invent
		// sediment when the bedrock floor prevents the requested removal.
		for (int32 DY = -1; DY <= 1; ++DY)
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const float Weight = static_cast<float>((DX == 0 ? 2 : 1) * (DY == 0 ? 2 : 1)) / 16.f;
				float& H = Map[WrapIndex(IY + DY, Resolution) * Resolution + WrapIndex(IX + DX, Resolution)];
				const float Taken = FMath::Min(FMath::Max(0.f, H), Amount * Weight);
				H -= Taken;
				Removed += Taken;
			}
		return Removed;
	}

	inline void HydraulicErosion(TArray<float>& Map)
	{
		FRandomStream Random(Seed + 811);
		for (int32 Drop = 0; Drop < DropletCount; ++Drop)
		{
			float X = Random.FRand() * Resolution, Y = Random.FRand() * Resolution;
			float DX = 0.f, DY = 0.f, Speed = 1.f, Water = 1.f, Sediment = 0.f;
			for (int32 Step = 0; Step < DropletLifetime; ++Step)
			{
				const FHeightGradient Here = ReadGrid(Map, Resolution, X, Y);
				DX = DX * .15f - Here.DX * .85f;
				DY = DY * .15f - Here.DY * .85f;
				const float Length = FMath::Sqrt(DX * DX + DY * DY);
				if (Length < 1.e-6f) break;
				DX /= Length;
				DY /= Length;
				const float NextX = WrapCoordinate(X + DX, Resolution);
				const float NextY = WrapCoordinate(Y + DY, Resolution);
				const float Delta = ReadGrid(Map, Resolution, NextX, NextY).Height - Here.Height;
				const float Capacity = 6.f * FMath::Max(-Delta, .001f) * Speed * Water;
				if (Delta > 0.f || Sediment > Capacity)
				{
					const float Amount = Delta > 0.f ? FMath::Min(Delta, Sediment) : (Sediment - Capacity) * .25f;
					Deposit(Map, X, Y, Amount);
					Sediment -= Amount;
				}
				else if (Delta < 0.f)
				{
					const float Amount = FMath::Min((Capacity - Sediment) * .22f, -Delta);
					Sediment += ErodeBrush(Map, X, Y, Amount);
				}
				Speed = FMath::Sqrt(FMath::Max(0.f, Speed * Speed - Delta * 4.f));
				Water *= .97f;
				X = NextX;
				Y = NextY;
			}
			// Including stalled/expired particles makes this redistribution of
			// terrain material, rather than silent loss at the end of a droplet.
			Deposit(Map, X, Y, Sediment);
		}
	}

	inline void RelaxTalus(TArray<float>& Map)
	{
		TArray<float> Change;
		for (int32 Pass = 0; Pass < 8; ++Pass)
		{
			Change.Init(0.f, Map.Num());
			for (int32 Y = 0; Y < Resolution; ++Y)
				for (int32 X = 0; X < Resolution; ++X)
				{
					const int32 A = Y * Resolution + X;
					for (const FIntPoint Direction : {FIntPoint(1, 0), FIntPoint(0, 1)})
					{
						const int32 B = WrapIndex(Y + Direction.Y, Resolution) * Resolution + WrapIndex(
							X + Direction.X, Resolution);
						const float Difference = Map[A] - Map[B];
						const float Amount = FMath::Sign(Difference) * FMath::Max(0.f, FMath::Abs(Difference) - .012f) *
							.08f;
						Change[A] -= Amount;
						Change[B] += Amount;
					}
				}
			for (int32 I = 0; I < Map.Num(); ++I) Map[I] += Change[I];
		}
	}

	// New type and accessor keep the old process-local V4 cache layout untouched.
	struct FPlainsBasisV5
	{
		TArray<TArray<float>> Mips;
		TArray<TArray<float>> PlainMips;

		FPlainsBasisV5()
		{
			TArray<float> Map;
			Map.SetNumUninitialized(Resolution * Resolution);
			FRandomStream Random(Seed);
			struct FMassif
			{
				float U, V, Angle, Length, Width, Height;
			};
			TArray<FMassif> Massifs;
			for (int32 I = 0; I < 7; ++I)
				Massifs.Add(FMassif{
					Random.FRand(), Random.FRand(), static_cast<float>(Random.FRand() * 2.f * PI),
					static_cast<float>(Random.FRandRange(.13f, .24f)),
					static_cast<float>(Random.FRandRange(.04f, .09f)),
					static_cast<float>(Random.FRandRange(.28f, .52f))
				});
			for (int32 Y = 0; Y < Resolution; ++Y)
				for (int32 X = 0; X < Resolution; ++X)
				{
					const float U = (X + .5f) / Resolution, V = (Y + .5f) / Resolution;
					const float WU = UnitWrap(U + .085f * PeriodicNoise(U, V, 3.f, FVector2D(23.7f, 61.4f)));
					const float WV = UnitWrap(V + .085f * PeriodicNoise(U, V, 3.f, FVector2D(-35.2f, 12.9f)));
					float MacroHeight = 0.f;
					for (const FMassif& Massif : Massifs)
					{
						const float C = FMath::Cos(Massif.Angle), S = FMath::Sin(Massif.Angle);
						// Sum nearby periodic images; merely wrapping each distance
						// independently would crease a rotated anisotropic massif.
						for (int32 TileY = -1; TileY <= 1; ++TileY)
							for (int32 TileX = -1; TileX <= 1; ++TileX)
							{
								const float DU = WU - Massif.U + TileX, DV = WV - Massif.V + TileY;
								const float Along = (DU * C + DV * S) / Massif.Length;
								const float Across = (-DU * S + DV * C) / Massif.Width;
								MacroHeight += Massif.Height * FMath::Exp(-Along * Along - Across * Across);
							}
					}
					Map[Y * Resolution + X] = MacroHeight;
				}
			// Select lowlands from the complete macro field, rather than hoping
			// erosion happens to leave flat ground. These are classification targets,
			// not a measured final percentage of walkable or perfectly flat terrain.
			TArray<float> RankedMacro = Map;
			RankedMacro.Sort();
			const float LowlandCeiling = RankedMacro[(RankedMacro.Num() - 1) * 30 / 100];
			const float UplandStart = RankedMacro[(RankedMacro.Num() - 1) * 62 / 100];
			const float TransitionRange = FMath::Max(UplandStart - LowlandCeiling, 1.e-5f);
			TArray<float> Plains;
			Plains.SetNumUninitialized(Resolution * Resolution);
			for (int32 Y = 0; Y < Resolution; ++Y)
				for (int32 X = 0; X < Resolution; ++X)
				{
					const int32 Index = Y * Resolution + X;
					const float MacroHeight = Map[Index];
					const float T = FMath::Clamp((MacroHeight - LowlandCeiling) / TransitionRange, 0.f, 1.f);
					const float Upland = T * T * (3.f - 2.f * T);
					Plains[Index] = 1.f - Upland;
					// Compress only pre-erosion lowland variation. A gentle slope
					// remains for drainage; upper mountain shapes retain their height.
					float H = .12f + FMath::Lerp(MacroHeight * .1f, MacroHeight, Upland);
					const float U = (X + .5f) / Resolution, V = (Y + .5f) / Resolution;
					const float WU = UnitWrap(U + .085f * PeriodicNoise(U, V, 3.f, FVector2D(23.7f, 61.4f)));
					const float WV = UnitWrap(V + .085f * PeriodicNoise(U, V, 3.f, FVector2D(-35.2f, 12.9f)));
					float Frequency = 3.f, Amplitude = .16f * FMath::Lerp(.03f, 1.f, Upland);
					for (int32 Layer = 0; Layer < 5; ++Layer)
					{
						const float N = PeriodicNoise(WU, WV, Frequency,
						                              FVector2D(7.31f + Layer * 13.17f, -25.47f + Layer * 9.23f));
						const float Ridge = 1.f - FMath::Abs(N);
						H += Amplitude * (.65f * (Ridge * Ridge - .65f) + .35f * N);
						Frequency *= 2.f;
						Amplitude *= .48f;
					}
					Map[Y * Resolution + X] = FMath::Max(.01f, H);
				}
			HydraulicErosion(Map);
			RelaxTalus(Map);
			float Minimum = TNumericLimits<float>::Max(), Maximum = TNumericLimits<float>::Lowest();
			double Sum = 0.0;
			for (const float H : Map)
			{
				Minimum = FMath::Min(Minimum, H);
				Maximum = FMath::Max(Maximum, H);
				Sum += H;
			}
			const float Mean = static_cast<float>(Sum / Map.Num());
			const float Range = FMath::Max(Maximum - Minimum, 1.e-6f);
			for (float& H : Map) H = (H - Mean) / Range;
			// Mean zero, peak-to-trough range one. Never normalize mips separately:
			// unresolved gullies must average out instead of aliasing into spikes.
			Mips.Add(MoveTemp(Map));
			// Keep the original broad landform mask: deposition/erosion can shape
			// channels through plains without redefining them as new mountain zones.
			PlainMips.Add(MoveTemp(Plains));
			for (int32 Size = Resolution; Size > 1; Size /= 2)
			{
				const TArray<float>& Previous = Mips.Last();
				const TArray<float>& PreviousPlains = PlainMips.Last();
				const int32 NextSize = Size / 2;
				TArray<float> Next;
				TArray<float> NextPlains;
				Next.SetNumUninitialized(NextSize * NextSize);
				NextPlains.SetNumUninitialized(NextSize * NextSize);
				for (int32 Y = 0; Y < NextSize; ++Y)
					for (int32 X = 0; X < NextSize; ++X)
					{
						const int32 A = (Y * 2) * Size + X * 2;
						Next[Y * NextSize + X] = .25f * (Previous[A] + Previous[A + 1] + Previous[A + Size] + Previous[A
							+ Size + 1]);
						NextPlains[Y * NextSize + X] = .25f * (PreviousPlains[A] + PreviousPlains[A + 1]
							+ PreviousPlains[A + Size] + PreviousPlains[A + Size + 1]);
					}
				Mips.Add(MoveTemp(Next));
				PlainMips.Add(MoveTemp(NextPlains));
			}
		}

		static float ReadLevels(const TArray<TArray<float>>& Levels, float U, float V, float FootprintUV)
		{
			// Zero footprint explicitly bypasses mip averaging. Bilinear sampling
			// still interpolates the original grid without selecting a coarser mip.
			if (FootprintUV <= 0.f)
				return ReadGrid(Levels[0], Resolution, UnitWrap(U) * Resolution - .5f,
				                UnitWrap(V) * Resolution - .5f).Height;
			const float LOD = FMath::Clamp(FMath::Log2(FMath::Max(1.f, Resolution * FootprintUV)), 0.f,
			                               static_cast<float>(Levels.Num() - 1));
			const int32 A = FMath::FloorToInt(LOD), B = FMath::Min(A + 1, Levels.Num() - 1);
			auto AtLevel = [&](int32 Level)
			{
				const int32 Size = Resolution >> Level;
				return ReadGrid(Levels[Level], Size, UnitWrap(U) * Size - .5f, UnitWrap(V) * Size - .5f).Height;
			};
			return FMath::Lerp(AtLevel(A), AtLevel(B), LOD - A);
		}

		float Read(float U, float V, float FootprintUV) const
		{
			return ReadLevels(Mips, U, V, FootprintUV);
		}

		float ReadPlainWeight(float U, float V, float FootprintUV) const
		{
			return FMath::Clamp(ReadLevels(PlainMips, U, V, FootprintUV), 0.f, 1.f);
		}
	};

	inline const FPlainsBasisV5& ErodedBasisV5()
	{
		static const FPlainsBasisV5 Basis;
		return Basis;
	}

	inline float Sample(float U, float V, float FootprintUV)
	{
		return ErodedBasisV5().Read(U, V, FootprintUV);
	}

	inline float PlainWeight(float U, float V, float FootprintUV)
	{
		return ErodedBasisV5().ReadPlainWeight(U, V, FootprintUV);
	}
}
