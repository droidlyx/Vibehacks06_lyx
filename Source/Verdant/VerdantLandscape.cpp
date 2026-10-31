// Deterministic multiscale height field for Verdant. All dimensions are centimetres.
#include "VerdantWorld.h"
#include "VerdantEcology.h"
#include "VerdantTerrainBasis.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace VerdantLand
{
	constexpr float Extent = 20000.f;
	// Five bands cover 1, 3, 9, 27 and 81 complete eroded terrain tiles per axis.
	// Their nominal wavelengths are 400 / 3^band metres; the finest is 4.94 m.
	// Temporarily sample full-resolution gullies, even below the mesh spacing.
	// This is finite terrain, not sub-centimetre/infinite-scale geometry.
	constexpr float Spacing = 50.f;
	constexpr int32 Cells = 800;
	constexpr int32 FractalOctaves = 5;
	constexpr float FractalLacunarity = 3.f;
	constexpr float FractalGain = .4f;
	constexpr float FractalWavelength = Extent * 2.f;
	constexpr float FractalAmplitude = 9000.f;
	constexpr bool bFilterSubGridGullies = false;
	const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");

	float Ease(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	float SegmentDistance(FVector2D P, FVector2D A, FVector2D B)
	{
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(float(FVector2D::DotProduct(P - A, AB) / AB.SizeSquared()), 0.f, 1.f);
		return FVector2D::Distance(P, A + T * AB);
	}

	float RoadDistance(float X, float Y)
	{
		const FVector2D P(X, Y);
		const FVector2D Hub(0, 1000);
		float D = SegmentDistance(P, FVector2D(-1000, -6200), Hub);
		D = FMath::Min(D, SegmentDistance(P, Hub, FVector2D(-4500, -1500)));
		D = FMath::Min(D, SegmentDistance(P, Hub, FVector2D(4200, -1000)));
		D = FMath::Min(D, SegmentDistance(P, Hub, FVector2D(0, 3600)));
		D = FMath::Min(D, SegmentDistance(P, FVector2D(0, 3600), FVector2D(500, 6500)));
		D = FMath::Min(D, SegmentDistance(P, FVector2D(-4500, -1500), FVector2D(-7400, -2100)));
		D = FMath::Min(D, SegmentDistance(P, FVector2D(4200, -1000), FVector2D(7400, -2100)));
		D = FMath::Min(D, SegmentDistance(P, FVector2D(500, 6500), FVector2D(500, 9700)));
		return FMath::Min(D, float(FMath::Abs((P - FVector2D(0, 1600)).Size() - 8200.f)));
	}

	FVector2D WarpedDomain(float X, float Y)
	{
		// Fixed offsets are the seed. No global RNG or world-state-dependent cache,
		// so mesh generation, prop placement and NPC surface queries agree.
		const FVector2D P(X / 19000.f, Y / 19000.f);
		return FVector2D(X, Y) + 2100.f * FVector2D(
			FMath::PerlinNoise2D(P + FVector2D(17.31f, -41.73f)),
			FMath::PerlinNoise2D(P + FVector2D(-63.19f, 29.47f)));
	}

	float BroadHeight(float X, float Y)
	{
		// Only an offset and the finite map boundary live here. Interior hills
		// come from five scaled copies of the internally detailed terrain basis.
		const float R = FMath::Sqrt(X * X + Y * Y);
		const float A = FMath::Atan2(Y, X);
		float H = 650.f;
		// Keep the existing finite valley boundary, now with a broken ridge rim.
		H += Ease(11500.f, 19500.f, R)
			* (4200.f + 850.f * FMath::Sin(A * 5.f + .6f) + 420.f * FMath::Cos(A * 9.f));
		return H;
	}

	FVector2D TerrainBandUV(const FVector2D& Domain, int32 Octave)
	{
		const float TilesPerAxis = FMath::Pow(FractalLacunarity, static_cast<float>(Octave));
		FVector2D UV = Domain * TilesPerAxis;
		// Change the orientation per scale, never per tile: this preserves seams.
		switch (Octave % 4)
		{
		case 1: UV = FVector2D(-UV.Y, UV.X);
			break;
		case 2: UV = -UV;
			break;
		case 3: UV = FVector2D(UV.Y, -UV.X);
			break;
		default: break;
		}
		UV += FVector2D(.173f * Octave, .317f * Octave);
		return UV;
	}

	float TerrainBand(const FVector2D& Domain, int32 Octave, float OffRoute)
	{
		const float TilesPerAxis = FMath::Pow(FractalLacunarity, static_cast<float>(Octave));
		const FVector2D UV = TerrainBandUV(Domain, Octave);
		const float FootprintUV = bFilterSubGridGullies
			                          ? 1.35f * Spacing * TilesPerAxis / FractalWavelength
			                          : 0.f;
		const float Shape =
			VerdantTerrainBasis::Sample(static_cast<float>(UV.X), static_cast<float>(UV.Y), FootprintUV);
		const float Amplitude = FractalAmplitude * FMath::Pow(FractalGain, static_cast<float>(Octave));
		const float RouteRetention = Octave == 2 ? .7f : 1.f;
		return Shape * Amplitude * FMath::Lerp(RouteRetention, 1.f, OffRoute);
	}

	float InheritedDetailStrength(const FVector2D& Domain, int32 Octave, float CurrentStrength)
	{
		const FVector2D UV = TerrainBandUV(Domain, Octave);
		const float TilesPerAxis = FMath::Pow(FractalLacunarity, static_cast<float>(Octave));
		const float FootprintUV = bFilterSubGridGullies ? 1.35f * Spacing * TilesPerAxis / FractalWavelength : 0.f;
		const float Plain = VerdantTerrainBasis::PlainWeight(static_cast<float>(UV.X), static_cast<float>(UV.Y),
		                                                     FootprintUV);
		// A plain at ANY enclosing scale keeps its child worlds low-relief.
		// Keep subtle detail instead of flattening the final geometry to a plane.
		return FMath::Min(CurrentStrength, FMath::Lerp(1.f, .12f, Plain));
	}

	float FractalRelief(float X, float Y)
	{
		const FVector2D Domain = (WarpedDomain(X, Y) + FVector2D(Extent, Extent)) / FractalWavelength;
		const float OffRoute = Ease(140.f, 800.f, RoadDistance(X, Y));
		float Height = 0.f;
		float DetailStrength = 1.f;
		for (int32 Octave = 0; Octave < FractalOctaves; ++Octave)
		{
			Height += DetailStrength * TerrainBand(Domain, Octave, OffRoute);
			DetailStrength = InheritedDetailStrength(Domain, Octave, DetailStrength);
		}
		return Height;
	}

	float FineRelief(float X, float Y)
	{
		const FVector2D Domain = (WarpedDomain(X, Y) + FVector2D(Extent, Extent)) / FractalWavelength;
		float Height = 0.f;
		float DetailStrength = 1.f;
		for (int32 Octave = 0; Octave < FractalOctaves; ++Octave)
		{
			if (Octave >= FractalOctaves - 2)
				Height += DetailStrength * TerrainBand(Domain, Octave, 1.f);
			DetailStrength = InheritedDetailStrength(Domain, Octave, DetailStrength);
		}
		return Height;
	}
}

float AVerdantWorld::HeightAt(float X, float Y)
{
	using namespace VerdantLand;
	// Clearings soften the large hills, not the last two terrain worlds.
	// Otherwise even a detailed eroded basis would become flat underfoot again.
	const float Fine = FineRelief(X, Y);
	float FineWeight = 1.f;
	float H = BroadHeight(X, Y) + FractalRelief(X, Y) - Fine;
	const FVector2D Clearings[] = {
		FVector2D(-4500, -1500), FVector2D(4200, -1000), FVector2D(500, 6500), FVector2D(0, 3600),
		FVector2D(-1000, -5800)
	};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Clearings); ++I)
	{
		const FVector2D& C = Clearings[I];
		const float D = FVector2D::Distance(FVector2D(X, Y), C);
		const bool IsArrival = I == UE_ARRAY_COUNT(Clearings) - 1;
		const float InnerRadius = IsArrival ? 75.f : 450.f;
		const float OuterRadius = IsArrival ? 250.f : 1200.f;
		if (D >= OuterRadius) continue;
		// The arrival pad alone suppresses fine detail for a safe spawn.
		if (IsArrival) FineWeight = Ease(InnerRadius, OuterRadius, D);
		const float W = (IsArrival ? 1.f : .55f) * (1.f - Ease(InnerRadius, OuterRadius, D));
		const float AnchorHeight = BroadHeight(C.X, C.Y) + FractalRelief(C.X, C.Y) - FineRelief(C.X, C.Y);
		H = FMath::Lerp(H, AnchorHeight, W);
	}
	return H + Fine * FineWeight;
}

FVector AVerdantWorld::SurfacePoint(FVector P, float Lift) const
{
	// Interpolate the same grid triangles as collision, avoiding floating props
	// and vertical oscillation when NPCs follow the analytic surface.
	using namespace VerdantLand;
	const float GX = FMath::Clamp(float((P.X + Extent) / Spacing), 0.f, float(Cells) - .0001f);
	const float GY = FMath::Clamp(float((P.Y + Extent) / Spacing), 0.f, float(Cells) - .0001f);
	const int32 IX = FMath::FloorToInt(GX), IY = FMath::FloorToInt(GY);
	const float U = GX - IX, V = GY - IY;
	const float X = -Extent + IX * Spacing, Y = -Extent + IY * Spacing;
	const float H00 = HeightAt(X, Y), H10 = HeightAt(X + Spacing, Y);
	const float H01 = HeightAt(X, Y + Spacing), H11 = HeightAt(X + Spacing, Y + Spacing);
	const float Z = U >= V
		                ? H00 * (1.f - U) + H10 * (U - V) + H11 * V
		                : H00 * (1.f - V) + H11 * U + H01 * (V - U);
	P.Z = Z + Lift;
	return P;
}

UMaterialInterface* AVerdantWorld::ColoredMaterial(FLinearColor Color, float Glow)
{
	static TMap<uint64, TWeakObjectPtr<UMaterialInstanceDynamic>> Cache;
	const uint64 Key = (uint64(Color.ToFColor(false).ToPackedARGB()) << 16)
		| uint64(FMath::Clamp(FMath::RoundToInt(Glow * 100.f), 0, 65535));
	if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Existing = Cache.Find(Key))
		if (Existing->IsValid()) return Existing->Get();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Verdant.M_Verdant"));
	if (!Base)
		Base = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Base) return UMaterial::GetDefaultMaterial(MD_Surface);
	UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(Base, this);
	MI->SetVectorParameterValue(TEXT("Tint"), Color);
	MI->SetScalarParameterValue(TEXT("Roughness"), .88f);
	MI->SetScalarParameterValue(TEXT("Glow"), Glow);
	Cache.Add(Key, MI);
	return MI;
}

UStaticMeshComponent* AVerdantWorld::AddShape(const FString& Name, const TCHAR* MeshPath, FVector Position,
                                              FVector Scale, FLinearColor Color, bool Collision)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	if (!Mesh) return nullptr;
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(
		this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(*Name)));
	AddInstanceComponent(Part);
	if (GetRootComponent()) Part->SetupAttachment(GetRootComponent());
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetStaticMesh(Mesh);
	Part->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Part->SetCollisionObjectType(ECC_WorldStatic);
	Part->SetCollisionResponseToAllChannels(ECR_Block);
	Part->SetCanEverAffectNavigation(false);
	// Alpha zero is the internal sentinel for retaining CC0 atlas materials.
	if (Color.A > 0.f)
		for (int32 Slot = 0; Slot < FMath::Max(1, Mesh->GetStaticMaterials().Num()); ++Slot)
			Part->SetMaterial(Slot, ColoredMaterial(Color));
	Part->RegisterComponent();
	Part->SetWorldLocation(Position);
	Part->SetWorldScale3D(Scale);
	return Part;
}

void AVerdantWorld::BuildLandscape()
{
	using namespace VerdantLand;
	const double StartedAt = FPlatformTime::Seconds();
	if (!Ground)
	{
		Ground = NewObject<UProceduralMeshComponent>(this, TEXT("ValleyGround"));
		AddInstanceComponent(Ground);
		if (GetRootComponent()) Ground->SetupAttachment(GetRootComponent());
		else SetRootComponent(Ground);
		Ground->RegisterComponent();
	}
	Ground->bUseAsyncCooking = false; // Collision must be ready before residents spawn.
	Ground->bUseComplexAsSimpleCollision = true;
	Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Ground->SetCollisionObjectType(ECC_WorldStatic);
	Ground->SetCollisionResponseToAllChannels(ECR_Block);
	Ground->SetCanEverAffectNavigation(false);
	TArray<FVector> Verts, Normals;
	TArray<int32> Indices;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	const int32 Row = Cells + 1;
	// Sample once. Normals use neighbours from the very same height grid as the
	// collision mesh, rather than evaluating all noise octaves five times.
	TArray<float> Heightmap;
	Heightmap.SetNumUninitialized(Row * Row);
	float MinHeight = TNumericLimits<float>::Max();
	float MaxHeight = TNumericLimits<float>::Lowest();
	for (int32 Y = 0; Y <= Cells; ++Y)
		for (int32 X = 0; X <= Cells; ++X)
		{
			const float H = HeightAt(-Extent + X * Spacing, -Extent + Y * Spacing);
			Heightmap[Y * Row + X] = H;
			MinHeight = FMath::Min(MinHeight, H);
			MaxHeight = FMath::Max(MaxHeight, H);
		}
	Verts.Reserve(Row * Row);
	Normals.Reserve(Row * Row);
	UV.Reserve(Row * Row);
	Colors.Reserve(Row * Row);
	Tangents.Reserve(Row * Row);
	Indices.Reserve(Cells * Cells * 6);
	for (int32 Y = 0; Y <= Cells; ++Y)
		for (int32 X = 0; X <= Cells; ++X)
		{
			const float PX = -Extent + X * Spacing, PY = -Extent + Y * Spacing;
			const float H = Heightmap[Y * Row + X];
			const int32 Left = FMath::Max(0, X - 1), Right = FMath::Min(Cells, X + 1);
			const int32 Bottom = FMath::Max(0, Y - 1), Top = FMath::Min(Cells, Y + 1);
			const float DX = (Heightmap[Y * Row + Right] - Heightmap[Y * Row + Left]) / ((Right - Left) * Spacing);
			const float DY = (Heightmap[Top * Row + X] - Heightmap[Bottom * Row + X]) / ((Top - Bottom) * Spacing);
			Verts.Emplace(PX, PY, H);
			Normals.Add(FVector(-DX, -DY, 1).GetSafeNormal());
			UV.Emplace(PX / 1800.f, PY / 1800.f);
			Tangents.Emplace(FVector(1, 0, DX).GetSafeNormal(), false);
			const float Variation = .5f + .5f * FMath::PerlinNoise2D(FVector2D(PX / 1700.f + 17, PY / 1700.f - 31));
			FLinearColor C = FMath::Lerp(FLinearColor(.12f, .23f, .075f), FLinearColor(.29f, .37f, .13f), Variation);
			// Painted paths follow the softened corridors of the fractal terrain.
			C = FMath::Lerp(C, FLinearColor(.47f, .38f, .25f), 1.f - Ease(150.f, 380.f, RoadDistance(PX, PY)));
			C = FMath::Lerp(C, FLinearColor(.43f, .40f, .30f), Ease(.3f, 1.1f, FVector2D(DX, DY).Size()) * .8f);
			C = FMath::Lerp(C, FLinearColor(.48f, .51f, .43f), Ease(2400.f, 4300.f, H));
			Colors.Add(C);
		}
	for (int32 Y = 0; Y < Cells; ++Y)
		for (int32 X = 0; X < Cells; ++X)
		{
			const int32 A = Y * Row + X, B = A + 1, C = A + Row, D = C + 1;
			Indices.Append({A, D, B, A, C, D});
		}
	Ground->CreateMeshSection_LinearColor(0, Verts, Indices, Normals, UV, Colors, Tangents, true);
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Art/Materials/M_Terrain.M_Terrain")))
		Ground->SetMaterial(0, Mat);
	// Audit the intended route centres against the character's 75 degree floor
	// limit. This is a slope check, not a substitute for collision/movement tests.
	float MaxRouteSlope = 0.f;
	auto SampleRouteSlope = [&](const FVector2D& P)
	{
		const float DX = (HeightAt(P.X + Spacing, P.Y) - HeightAt(P.X - Spacing, P.Y)) / (2.f * Spacing);
		const float DY = (HeightAt(P.X, P.Y + Spacing) - HeightAt(P.X, P.Y - Spacing)) / (2.f * Spacing);
		MaxRouteSlope = FMath::Max(MaxRouteSlope, FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY))));
	};
	const FVector2D Routes[][2] = {
		{FVector2D(-1000, -6200), FVector2D(0, 1000)},
		{FVector2D(0, 1000), FVector2D(-4500, -1500)},
		{FVector2D(0, 1000), FVector2D(4200, -1000)},
		{FVector2D(0, 1000), FVector2D(0, 3600)},
		{FVector2D(0, 3600), FVector2D(500, 6500)},
		{FVector2D(-4500, -1500), FVector2D(-7400, -2100)},
		{FVector2D(4200, -1000), FVector2D(7400, -2100)},
		{FVector2D(500, 6500), FVector2D(500, 9700)}
	};
	for (const auto& Route : Routes)
	{
		const int32 Steps = FMath::CeilToInt(FVector2D::Distance(Route[0], Route[1]) / 100.f);
		for (int32 I = 0; I <= Steps; ++I)
			SampleRouteSlope(FMath::Lerp(Route[0], Route[1], float(I) / Steps));
	}
	for (int32 I = 0; I < 512; ++I)
	{
		const float A = 2.f * PI * I / 512.f;
		SampleRouteSlope(FVector2D(0, 1600) + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 8200.f);
	}
	UE_LOG(LogTemp, Display,
	       TEXT(
		       "VERDANT_LANDSCAPE profile=plains_basis_v5 fractal=eroded_heightmap_tiles amplitude_m=%.1f octaves=%d gain=%.2f lacunarity=%.1f vertices=%d triangles=%d width_m=400 spacing_m=%.2f min_m=%.2f max_m=%.2f route_max_slope_deg=%.2f build_ms=%.1f"
	       ),
	       FractalAmplitude / 100.f, FractalOctaves, FractalGain, FractalLacunarity, Verts.Num(), Indices.Num() / 3,
	       Spacing / 100.f, MinHeight / 100.f, MaxHeight / 100.f, MaxRouteSlope,
	       (FPlatformTime::Seconds() - StartedAt) * 1000.0);
	UE_LOG(LogTemp, Display,
	       TEXT("VERDANT_TERRAIN_BASIS resolution=%d seed=%d droplets=%d lifetime=%d mip_filtered=%d"),
	       VerdantTerrainBasis::Resolution, VerdantTerrainBasis::Seed,
	       VerdantTerrainBasis::DropletCount, VerdantTerrainBasis::DropletLifetime, bFilterSubGridGullies ? 1 : 0);
	for (int32 Octave = 0; Octave < FractalOctaves; ++Octave)
	{
		const float CellsPerAxis = FMath::Pow(FractalLacunarity, static_cast<float>(Octave));
		UE_LOG(LogTemp, Display,
		       TEXT(
			       "VERDANT_TERRAIN_BAND band=%d tiles_per_axis=%.0f nominal_tiles_2d=%.0f wavelength_m=%.4f amplitude_m=%.4f"
		       ),
		       Octave, CellsPerAxis, CellsPerAxis * CellsPerAxis,
		       FractalWavelength / (CellsPerAxis * 100.f),
		       FractalAmplitude * FMath::Pow(FractalGain, static_cast<float>(Octave)) / 100.f);
	}
}

void AVerdantWorld::BuildScenery()
{
	using namespace VerdantLand;
	const FLinearColor Bark(.22f, .12f, .065f), Moss(.23f, .38f, .12f);
	// Separate seeded ecology owns the stable scale bands and clustered dressing.
	VerdantEcology::Build(*this);
	FRandomStream FruitRandom(603207);
	MotherPosition = SurfacePoint(MotherPosition);
	AddShape(TEXT("MotherTrunk"), Cylinder, MotherPosition + FVector(0, 0, 690), FVector(2.7f, 2.7f, 13.8f), Bark);
	// Explicit primitive collision keeps the trunk solid even if its visual mesh
	// lacks cooked simple collision. The cylindrical section spans z=0..1380;
	// rounded caps extend below the soil and into the canopy, never across paths.
	auto* TrunkCollision = NewObject<UCapsuleComponent>(this,
	                                                    MakeUniqueObjectName(
		                                                    this, UCapsuleComponent::StaticClass(),
		                                                    TEXT("MotherTrunkCollision")));
	AddInstanceComponent(TrunkCollision);
	TrunkCollision->SetupAttachment(GetRootComponent());
	TrunkCollision->SetMobility(EComponentMobility::Movable);
	TrunkCollision->SetCapsuleSize(135.f, 825.f);
	TrunkCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TrunkCollision->SetCollisionObjectType(ECC_WorldStatic);
	TrunkCollision->SetCollisionResponseToAllChannels(ECR_Block);
	TrunkCollision->SetGenerateOverlapEvents(false);
	TrunkCollision->SetCanEverAffectNavigation(false);
	TrunkCollision->SetHiddenInGame(true);
	TrunkCollision->RegisterComponent();
	TrunkCollision->SetWorldLocation(MotherPosition + FVector(0, 0, 690));
	for (int32 I = 0; I < 5; ++I)
	{
		const float A = I * 2.f * PI / 5.f;
		const FVector Side(FMath::Cos(A), FMath::Sin(A), 0);
		auto* Branch = AddShape(TEXT("MotherBranch"), Cylinder, MotherPosition + Side * 250.f + FVector(0, 0, 1150),
		                        FVector(.95f, .95f, 8), Bark);
		if (Branch) Branch->SetWorldRotation(FRotator(38, FMath::RadiansToDegrees(A), 0));
		AddShape(TEXT("MotherOuterCrown"), Sphere, MotherPosition + Side * 520.f + FVector(0, 0, 1600 + (I % 2) * 100),
		         FVector(9, 8, 6), FLinearColor(.35f, .43f, .16f));
		auto* Root = AddShape(TEXT("MotherRoot"), Cone, MotherPosition + Side * 195.f + FVector(0, 0, 50),
		                      FVector(2, 2, 7), Bark);
		if (Root) Root->SetWorldRotation(FRotator(77, FMath::RadiansToDegrees(A), 0));
	}
	MotherCrown = AddShape(TEXT("MotherHeart"), Sphere, MotherPosition + FVector(0, 0, 1760), FVector(9, 9, 7),
	                       FLinearColor(.33f, .36f, .16f));
	// A raised golden heart makes the shared objective legible from the basin.
	auto* Heart = AddShape(TEXT("MotherSap"), Sphere, MotherPosition + FVector(0, -150, 420), FVector(1.05f),
	                       FLinearColor(1.f, .58f, .12f));
	if (Heart) Heart->SetMaterial(0, ColoredMaterial(FLinearColor(1.f, .58f, .12f), .4f));
	for (int32 Index = 0; Index < Groves.Num(); ++Index)
	{
		auto& G = Groves[Index];
		G.Position = SurfacePoint(G.Position);
		G.Marker = AddShape(FString::Printf(TEXT("Grove_%d_Marker"), Index), Cylinder, G.Position + FVector(0, 0, 9),
		                    FVector(7.5f, 7.5f, .12f), G.Color);
		G.FruitVisuals.Empty();
		// Twelve resource visuals, in three unequal growing patches instead of a ring.
		// Cluster geometry changes presentation only, never resource accounting/radius.
		FVector PatchCentres[3];
		const float Phase = FruitRandom.FRandRange(0.f, 2.f * PI);
		for (int32 K = 0; K < 3; ++K)
		{
			const float Angle = Phase + K * 2.07f + FruitRandom.FRandRange(-.22f, .22f);
			PatchCentres[K] = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * FruitRandom.FRandRange(300.f, 440.f);
		}
		TArray<FVector> PlantOffsets;
		for (int32 J = 0; J < 12; ++J)
		{
			const int32 Patch = J < 5 ? 0 : J < 9 ? 1 : 2;
			FVector Offset;
			for (int32 Attempt = 0; Attempt < 20; ++Attempt)
			{
				const float Angle = FruitRandom.FRandRange(0.f, 2.f * PI);
				const float Radius = FMath::Sqrt(FruitRandom.FRand()) * 170.f;
				Offset = PatchCentres[Patch] +
					FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius * .72f, 0);
				if (!PlantOffsets.ContainsByPredicate([&](const FVector& Other)
				{
					return FVector::DistSquared2D(Offset, Other) < FMath::Square(85.f);
				}))break;
			}
			PlantOffsets.Add(Offset);
			const FVector P = SurfacePoint(G.Position + Offset);
			// Original composite height is 163.5 cm. Every part shares one 80-120% scale.
			const float PlantScale = (120.f / 163.5f) * FruitRandom.FRandRange(.8f, 1.2f);
			AddShape(TEXT("FruitStem"), Cylinder, P + FVector(0, 0, 48) * PlantScale,
			         FVector(.12f, .12f, .96f) * PlantScale, Bark);
			AddShape(TEXT("FruitLeaves"), Sphere, P + FVector(0, 0, 62) * PlantScale,
			         FVector(1.45f, 1.1f, .45f) * PlantScale, Moss);
			auto* Fruit = AddShape(TEXT("GroveFruit"), Sphere, P + FVector(0, 0, 116) * PlantScale,
			                       FVector(.85f, .85f, .95f) * PlantScale, G.Color);
			if (Fruit) G.FruitVisuals.Add(Fruit);
		}
	}
	// Fallback lighting also makes an Entry-map launch usable before map creation.
	if (!TActorIterator<ASkyAtmosphere>(GetWorld()))
	{
		auto* Sky = GetWorld()->SpawnActor<ASkyAtmosphere>();
		if (Sky) Sky->GetComponent()->SetRayleighScatteringScale(.75f);
	}
	if (!TActorIterator<ADirectionalLight>(GetWorld()))
	{
		auto* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 10000), FRotator(-38, -38, 0));
		if (Sun)
			if (auto* Light = Sun->FindComponentByClass<UDirectionalLightComponent>())
			{
				Light->SetMobility(EComponentMobility::Movable);
				Light->SetIntensity(4.2f);
				Light->SetLightColor(FLinearColor(1.f, .93f, .82f));
				Light->SetAtmosphereSunLight(true);
			}
	}
	if (!TActorIterator<ASkyLight>(GetWorld()))
	{
		auto* Sky = GetWorld()->SpawnActor<ASkyLight>();
		if (Sky)
		{
			auto* Light = Sky->GetLightComponent();
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensity(1.25f);
			Light->SetRealTimeCapture(true);
		}
	}
	if (!TActorIterator<AExponentialHeightFog>(GetWorld()))
	{
		auto* Fog = GetWorld()->SpawnActor<AExponentialHeightFog>();
		if (Fog)
		{
			Fog->GetComponent()->SetFogDensity(.0045f);
			Fog->GetComponent()->SetFogHeightFalloff(.18f);
			Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.51f, .62f, .68f));
		}
	}
	UpdateGroveVisuals();
}

void AVerdantWorld::UpdateGroveVisuals()
{
	for (auto& G : Groves)
	{
		const float Life = FMath::Clamp(G.Fertility, 0.f, 1.f);
		const FLinearColor Soil = FMath::Lerp(FLinearColor(.31f, .23f, .14f), G.Color * .6f, Life);
		if (G.Marker)
		{
			G.Marker->SetMaterial(0, ColoredMaterial(Soil, G.bOpen ? .09f : 0.f));
			G.Marker->SetWorldScale3D(FVector(7.5f, 7.5f, G.bOpen ? .12f : .32f));
		}
		const int32 VisibleFruit = FMath::Clamp(FMath::CeilToInt(G.Food / 4.f), 0, G.FruitVisuals.Num());
		for (int32 I = 0; I < G.FruitVisuals.Num(); ++I)
			if (G.FruitVisuals[I])
			{
				G.FruitVisuals[I]->SetVisibility(I < VisibleFruit);
				G.FruitVisuals[I]->SetMaterial(
					0, ColoredMaterial(FMath::Lerp(FLinearColor(.36f, .23f, .09f), G.Color, Life), .22f));
			}
	}
	if (MotherCrown)
	{
		const float Progress = FMath::Clamp(TreeNutrition / FMath::Max(1.f, TreeGoal), 0.f, 1.f);
		const FLinearColor Crown = FMath::Lerp(FLinearColor(.33f, .36f, .16f), FLinearColor(.75f, .95f, .38f),
		                                       Progress);
		MotherCrown->SetMaterial(0, ColoredMaterial(Crown, .05f + Progress * .7f));
		MotherCrown->SetWorldScale3D(FVector(9, 9, 7) * (1.f + Progress * .22f));
	}
}
