#include "VerdantEcology.h"
#include "VerdantWorld.h"
#include "Components/CapsuleComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace VerdantLand
{
	float RoadDistance(float X, float Y);
}

namespace
{
	constexpr float Limit = 15500.f;
	const FName EcologyTag(TEXT("VerdantEcology"));

	enum EAsset { Grass, Clover, Flower, PebbleA, PebbleB, Bush, Fern, RockA, RockB, RockC, Tree, Ancient, AssetCount };

	struct FSpecies
	{
		const TCHAR* Name;
		float Height;
		int32 Tier;
		bool bCollision;
		UStaticMesh* Mesh = nullptr;
		FBox Bounds = FBox(ForceInit);
		TArray<FTransform> Transforms;
		float MinimumHeight = TNumericLimits<float>::Max();
		float MaximumHeight = 0.f;
	};

	struct FAnchor
	{
		FVector Position;
		float Radius;
		bool bTree;
		bool bAncient = false;
	};

	struct FPatch
	{
		FVector Center;
		double Yaw;
		double Radius;
		double Aspect;
	};

	FVector PatchPoint(const FPatch& Patch, FRandomStream& Random)
	{
		// Overlapping lobes, an off-centre core and a ragged edge avoid circles/rows.
		const float Angle = Random.FRandRange(-PI, PI);
		const float Radius = FMath::Sqrt(Random.FRand());
		const float Lobe = Random.FRand() < .7f ? -.17f : .38f;
		const FVector Offset((FMath::Cos(Angle) * Radius + Lobe) * Patch.Radius,
		                     FMath::Sin(Angle) * Radius * Patch.Radius * Patch.Aspect, 0.f);
		return Patch.Center + Offset.RotateAngleAxis(Patch.Yaw, FVector::UpVector);
	}

	float DensityAt(const FVector& P, float Scale, float Offset)
	{
		return .5f + .5f * FMath::PerlinNoise2D(FVector2D(P.X / Scale + Offset, P.Y / Scale - Offset * .7f));
	}

	bool ClearOfActivity(const AVerdantWorld& World, const FVector& P, float Road, float Grove, float Mother)
	{
		if (P.SizeSquared2D() > FMath::Square(Limit)
			|| VerdantLand::RoadDistance(P.X, P.Y) < Road
			|| FVector::DistSquared2D(P, World.MotherPosition) < FMath::Square(Mother))
			return false;
		for (const auto& G : World.Groves)
			if (FVector::DistSquared2D(P, G.Position) < FMath::Square(Grove))return false;
		return true;
	}

	bool GroundAt(const AVerdantWorld& World, const FVector& P, float Probe, float MaxSlopeDegrees,
	              FVector& Ground, FVector& Normal, float& MinSupportZ, float& SupportRelief)
	{
		Ground = World.SurfacePoint(P);
		const FVector X1 = World.SurfacePoint(P + FVector(Probe, 0, 0));
		const FVector X0 = World.SurfacePoint(P - FVector(Probe, 0, 0));
		const FVector Y1 = World.SurfacePoint(P + FVector(0, Probe, 0));
		const FVector Y0 = World.SurfacePoint(P - FVector(0, Probe, 0));
		Normal = FVector(-(X1.Z - X0.Z) / (2.f * Probe), -(Y1.Z - Y0.Z) / (2.f * Probe), 1.f).GetSafeNormal();
		MinSupportZ = FMath::Min(Ground.Z, FMath::Min(FMath::Min(X0.Z, X1.Z), FMath::Min(Y0.Z, Y1.Z)));
		const float MaxZ = FMath::Max(Ground.Z, FMath::Max(FMath::Max(X0.Z, X1.Z), FMath::Max(Y0.Z, Y1.Z)));
		SupportRelief = MaxZ - MinSupportZ;
		return Normal.Z >= FMath::Cos(FMath::DegreesToRadians(MaxSlopeDegrees));
	}

	bool Separated(const TArray<FAnchor>& Anchors, const FVector& P, float Radius, float Minimum)
	{
		for (const FAnchor& Other : Anchors)
			if (FVector::DistSquared2D(P, Other.Position) < FMath::Square(FMath::Max(Minimum, Radius + Other.Radius)))
				return false;
		return true;
	}

	void Record(FSpecies& Species, const FVector& Ground, const FQuat& Rotation, float Height, float Sink)
	{
		const float Scale = Height / FMath::Max(1.f, float(Species.Bounds.GetSize().Z));
		// Preserve the authored pivot in XY (tree trunks are not canopy-bounds centres).
		const FVector At = Ground - Rotation.RotateVector(FVector(0, 0, Species.Bounds.Min.Z * Scale))
			- FVector(0, 0, Sink);
		Species.Transforms.Emplace(Rotation, At, FVector(Scale));
		Species.MinimumHeight = FMath::Min(Species.MinimumHeight, Height);
		Species.MaximumHeight = FMath::Max(Species.MaximumHeight, Height);
	}
}

void VerdantEcology::Build(AVerdantWorld& World)
{
	// Owned components make rebuilds idempotent without touching gameplay/terrain components.
	TArray<UActorComponent*> Previous = World.GetComponentsByTag(UActorComponent::StaticClass(), EcologyTag);
	for (UActorComponent* Component : Previous)if (IsValid(Component))Component->DestroyComponent();

	FSpecies Species[AssetCount] = {
		{TEXT("SM_Grass01"), 25.f, 1, false}, {TEXT("SM_Clover01"), 8.f, 1, false},
		{TEXT("SM_Flowers01"), 28.f, 1, false}, {TEXT("SM_Pebble01"), 12.f, 1, false},
		{TEXT("SM_Pebble02"), 14.f, 1, false}, {TEXT("SM_Bush01"), 110.f, 2, false},
		{TEXT("SM_Fern01"), 70.f, 2, false}, {TEXT("SM_Rock01"), 120.f, 2, true},
		{TEXT("SM_Rock02"), 150.f, 2, true}, {TEXT("SM_Rock03"), 180.f, 2, true},
		{TEXT("SM_Tree01"), 650.f, 3, false}, {TEXT("SM_Tree02"), 1400.f, 4, false}
	};
	for (auto& S : Species)
	{
		const FString Path = FString::Printf(TEXT("/Game/Art/Nature/%s.%s"), S.Name, S.Name);
		S.Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (S.Mesh) S.Bounds = S.Mesh->GetBoundingBox();
		else UE_LOG(LogTemp, Warning, TEXT("VERDANT_ECOLOGY missing %s"), *Path);
	}

	FRandomStream Trees(603171), Rocks(603172), Understory(603173), Details(603174);
	TArray<FAnchor> TreeAnchors, RockAnchors;
	TArray<FPatch> ForestPatches;
	const FVector ForestCenters[] = {
		{-10500, -7200, 0}, {-4300, -5900, 0}, {4300, -6500, 0}, {11100, -3000, 0}, {10500, 5700, 0},
		{6500, 10500, 0}, {-1700, 11900, 0}, {-8500, 9000, 0}, {-11600, 1300, 0}, {-6100, 2300, 0}
	};
	int32 TallCount = 0;
	for (const FVector& Center : ForestCenters)
	{
		FPatch Patch{
			Center + FVector(Trees.FRandRange(-700.f, 700.f), Trees.FRandRange(-700.f, 700.f), 0),
			Trees.FRandRange(0.f, 360.f), Trees.FRandRange(1700.f, 2600.f), Trees.FRandRange(.5f, .85f)
		};
		ForestPatches.Add(Patch);
		const int32 Desired = Trees.RandRange(7, 13);
		int32 Added = 0;
		for (int32 Attempt = 0; Attempt < Desired * 32 && Added < Desired; ++Attempt)
		{
			const FVector P = PatchPoint(Patch, Trees);
			if (!ClearOfActivity(World, P, 850.f, 1250.f, 1700.f)
				|| FVector::DistSquared2D(P, FVector(-1000, -5800, 0)) < FMath::Square(1200.f)
				|| DensityAt(P, 900.f, 21.f) < .36f)
				continue;
			const int32 Kind = TallCount < 12 && Trees.FRand() < .13f ? Ancient : Tree;
			FSpecies& S = Species[Kind];
			if (!S.Mesh)continue;
			const float Height = S.Height * Trees.FRandRange(.8f, 1.2f);
			const float Radius = Height * (Kind == Ancient ? .39f : .34f);
			if (!Separated(TreeAnchors, P, Radius, 430.f))continue;
			FVector Ground, Normal;
			float Support, Relief;
			if (!GroundAt(World, P, 90.f, 40.f, Ground, Normal, Support, Relief) || Relief > 160.f)continue;
			Record(S, Ground, FRotator(0, Trees.FRandRange(0.f, 360.f), 0).Quaternion(), Height, 5.f);
			TreeAnchors.Add({Ground, Radius, true, Kind == Ancient});
			if (Kind == Ancient)++TallCount;
			++Added;
		}
	}

	auto AddRock = [&](FVector P, int32 Kind)-> bool
	{
		FSpecies& S = Species[Kind];
		if (!S.Mesh)return false;
		if (!ClearOfActivity(World, P, 850.f, 1250.f, 1700.f)
			|| FVector::DistSquared2D(P, FVector(-1000, -5800, 0)) < FMath::Square(1200.f))
			return false;
		const float Height = S.Height * Rocks.FRandRange(.8f, 1.2f);
		const float Radius = Height * float(
			FMath::Max(S.Bounds.GetSize().X, S.Bounds.GetSize().Y) / S.Bounds.GetSize().Z) * .45f;
		if (!Separated(RockAnchors, P, Radius + 45.f, 180.f))return false;
		for (const FAnchor& A : TreeAnchors)if (FVector::DistSquared2D(P, A.Position) < FMath::Square(Radius + 120.f))
			return false;
		FVector Ground, Normal;
		float Support, Relief;
		if (!GroundAt(World, P, FMath::Clamp(Radius * .7f, 45.f, 110.f), 38.f, Ground, Normal, Support, Relief)
			|| Relief > Height * .65f)
			return false;
		// Partially seat the boulder into the lowest sampled support; don't float it over gullies.
		Ground.Z = Support;
		Record(S, Ground, FRotator(0, Rocks.FRandRange(0.f, 360.f), 0).Quaternion(), Height, Height * .035f);
		RockAnchors.Add({Ground, Radius, false, false});
		return true;
	};
	for (int32 I = 0; I < ForestPatches.Num(); ++I)
	{
		FPatch Patch = ForestPatches[I];
		Patch.Center += FVector(400, -300, 0).RotateAngleAxis(Patch.Yaw, FVector::UpVector);
		Patch.Radius = Rocks.FRandRange(450.f, 1000.f);
		const int32 Desired = Rocks.RandRange(2, 5);
		int32 Added = 0;
		for (int32 Attempt = 0; Attempt < Desired * 18 && Added < Desired; ++Attempt)
			if (AddRock(PatchPoint(Patch, Rocks), RockA + Rocks.RandRange(0, 2)))++Added;
	}
	// Keep a remembered pair on the approach, but move a route-intersecting reference outward.
	const FVector References[] = {FVector(-1750, -4300, 0), FVector(-350, -4250, 0), FVector(2100, 6600, 0)};
	for (int32 I = 0; I < UE_ARRAY_COUNT(References); ++I)
	{
		FVector Reference = References[I];
		for (int32 Push = 0; Push < 8 && VerdantLand::RoadDistance(Reference.X, Reference.Y) < 1000.f; ++Push)
		{
			FVector Away(
				VerdantLand::RoadDistance(Reference.X + 50, Reference.Y) - VerdantLand::RoadDistance(
					Reference.X - 50, Reference.Y),
				VerdantLand::RoadDistance(Reference.X, Reference.Y + 50) - VerdantLand::RoadDistance(
					Reference.X, Reference.Y - 50), 0);
			if (Away.IsNearlyZero())Away = FVector(I == 0 ? -1 : 1, 0, 0);
			Reference += Away.GetSafeNormal() * 160.f;
		}
		for (int32 Attempt = 0; Attempt < 16; ++Attempt)
		{
			const FVector P = Reference + (Attempt
				                               ? FVector(Rocks.FRandRange(-180.f, 180.f),
				                                         Rocks.FRandRange(-180.f, 180.f), 0)
				                               : FVector::ZeroVector);
			if (AddRock(P, RockA + I))break;
		}
	}

	TArray<FAnchor> Anchors = TreeAnchors;
	Anchors.Append(RockAnchors);
	TArray<FVector> BushPositions;
	for (const FAnchor& Anchor : Anchors)
	{
		// Some anchors have no understory; others carry several lobes.
		if (Understory.FRand() < .29f)continue;
		const int32 Desired = Understory.RandRange(1, 4);
		const float Facing = Understory.FRandRange(0.f, 360.f);
		for (int32 Attempt = 0, Added = 0; Attempt < Desired * 7 && Added < Desired; ++Attempt)
		{
			const float Angle = Facing + Understory.FRandRange(-90.f, 90.f);
			const float Distance = Anchor.bTree
				                       ? Understory.FRandRange(190.f, 590.f)
				                       : Anchor.Radius + Understory.FRandRange(90.f, 260.f);
			const FVector P = Anchor.Position + FVector(Distance, 0, 0).RotateAngleAxis(Angle, FVector::UpVector);
			if (!ClearOfActivity(World, P, 350.f, 850.f, 1100.f) || DensityAt(P, 430.f, 71.f) < .38f)continue;
			bool Crowded = false;
			for (const FVector& Other : BushPositions)
				if (FVector::DistSquared2D(P, Other) < FMath::Square(115.f))
				{
					Crowded = true;
					break;
				}
			if (Crowded)continue;
			const int32 Kind = Anchor.bTree && Understory.FRand() < .58f ? Fern : Bush;
			FSpecies& S = Species[Kind];
			if (!S.Mesh)continue;
			FVector Ground, Normal;
			float Support, Relief;
			if (!GroundAt(World, P, 45.f, 40.f, Ground, Normal, Support, Relief) || Relief > 70.f)continue;
			const float Height = S.Height * Understory.FRandRange(.8f, 1.2f);
			Record(S, Ground, FRotator(0, Understory.FRandRange(0.f, 360.f), 0).Quaternion(), Height, 3.f);
			BushPositions.Add(Ground);
			++Added;
		}
	}

	TArray<FPatch> DetailPatches;
	for (const FAnchor& Anchor : Anchors)
	{
		if (Details.FRand() < .23f)continue;
		const float Angle = Details.FRandRange(0.f, 360.f);
		const float Offset = Anchor.bTree ? Details.FRandRange(160.f, 330.f) : Anchor.Radius + 100.f;
		DetailPatches.Add({
			Anchor.Position + FVector(Offset, 0, 0).RotateAngleAxis(Angle, FVector::UpVector),
			Angle, Details.FRandRange(150.f, 350.f), Details.FRandRange(.4f, .85f)
		});
	}
	// Uneven roadside islands: no paired rows or equal intervals.
	const FVector ApproachStart(-1000, -6200, 0), ApproachEnd(0, 1000, 0);
	const FVector Along = (ApproachEnd - ApproachStart).GetSafeNormal2D(), Across(-Along.Y, Along.X, 0);
	for (int32 I = 0; I < 13; ++I)
	{
		const float T = Details.FRandRange(.07f, .92f), Side = Details.FRand() < .58f ? -1.f : 1.f;
		const FVector P = FMath::Lerp(ApproachStart, ApproachEnd, T) + Across * Side * Details.FRandRange(340.f, 670.f);
		DetailPatches.Add({
			P, Details.FRandRange(0.f, 360.f), Details.FRandRange(130.f, 290.f), Details.FRandRange(.4f, .8f)
		});
	}
	for (const auto& Grove : World.Groves)
		for (int32 I = 0; I < 3; ++I)
		{
			const float Angle = Details.FRandRange(0.f, 360.f);
			DetailPatches.Add({
				Grove.Position + FVector(Details.FRandRange(850.f, 1200.f), 0, 0).RotateAngleAxis(
					Angle, FVector::UpVector),
				Angle, Details.FRandRange(170.f, 330.f), Details.FRandRange(.45f, .8f)
			});
		}
	int32 DetailCount = 0;
	for (const FPatch& Patch : DetailPatches)
	{
		const bool FlowerPocket = Details.FRand() < .16f;
		const bool CloverPocket = Details.FRand() < .35f;
		const int32 Desired = Details.RandRange(18, 44);
		for (int32 Attempt = 0, Added = 0; Attempt < Desired * 4 && Added < Desired && DetailCount < 4400; ++Attempt)
		{
			const FVector P = PatchPoint(Patch, Details);
			if (!ClearOfActivity(World, P, 240.f, 360.f, 1000.f) || DensityAt(P, 170.f, 117.f) < .37f)continue;
			bool InsideAnchor = false;
			for (const FAnchor& A : Anchors)
				if (FVector::DistSquared2D(P, A.Position) < FMath::Square(A.bTree ? 100.f : A.Radius * .8f))
				{
					InsideAnchor = true;
					break;
				}
			if (InsideAnchor)continue;
			const float Choice = Details.FRand();
			const int32 Kind = Choice < .08f
				                   ? PebbleA
				                   : Choice < .14f
				                   ? PebbleB
				                   : FlowerPocket && Choice < .42f
				                   ? Flower
				                   : CloverPocket && Choice < .72f
				                   ? Clover
				                   : Grass;
			FSpecies& S = Species[Kind];
			if (!S.Mesh)continue;
			FVector Ground, Normal;
			float Support, Relief;
			if (!GroundAt(World, P, 20.f, 50.f, Ground, Normal, Support, Relief))continue;
			const float Height = S.Height * Details.FRandRange(.8f, 1.2f);
			const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector,
			                                             FMath::Lerp(FVector::UpVector, Normal, .45f).GetSafeNormal());
			const FQuat Rotation = Tilt * FRotator(0, Details.FRandRange(0.f, 360.f), 0).Quaternion();
			Record(S, Ground, Rotation, Height, FMath::Min(1.5f, Height * .025f));
			++Added;
			++DetailCount;
		}
	}

	int32 TierCounts[4] = {0, 0, 0, 0};
	for (auto& S : Species)
	{
		if (!S.Mesh || S.Transforms.IsEmpty())continue;
		auto* Batch = NewObject<UHierarchicalInstancedStaticMeshComponent>(&World,
		                                                                   MakeUniqueObjectName(
			                                                                   &World,
			                                                                   UHierarchicalInstancedStaticMeshComponent::StaticClass(),
			                                                                   FName(*(FString(TEXT("Ecology_")) + S.
				                                                                   Name))));
		World.AddInstanceComponent(Batch);
		Batch->SetupAttachment(World.GetRootComponent());
		Batch->ComponentTags.Add(EcologyTag);
		Batch->ComponentTags.Add(FName(*FString::Printf(TEXT("EcologyTier.%d"), S.Tier)));
		Batch->SetMobility(EComponentMobility::Movable);
		Batch->SetStaticMesh(S.Mesh);
		Batch->SetCollisionEnabled(S.bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Batch->SetCollisionObjectType(ECC_WorldStatic);
		Batch->SetCollisionResponseToAllChannels(ECR_Block);
		Batch->SetGenerateOverlapEvents(false);
		Batch->SetCanEverAffectNavigation(false);
		Batch->SetCastShadow(S.Tier > 1);
		Batch->bAutoRebuildTreeOnInstanceChanges = false;
		if (S.bCollision)Batch->ComponentTags.Add(TEXT("AudioSurface.Stone"));
		if (S.Tier == 1)Batch->SetCullDistances(6000, 9000);
		else if (S.Tier == 2 && !S.bCollision)Batch->SetCullDistances(11000, 15000);
		Batch->RegisterComponent();
		Batch->AddInstances(S.Transforms, false, true, false);
		Batch->BuildTreeIfOutdated(true, true);
		TierCounts[S.Tier - 1] += S.Transforms.Num();
		UE_LOG(LogTemp, Display,
		       TEXT(
			       "VERDANT_ECOLOGY species=%s tier=%d count=%d height_m=%.3f..%.3f nominal_m=%.3f variation=continuous_0.8_1.2"
		       ),
		       S.Name, S.Tier, S.Transforms.Num(), S.MinimumHeight/100.f, S.MaximumHeight/100.f, S.Height/100.f);
	}
	for (const FAnchor& TreeAnchor : TreeAnchors)
	{
		const float Height = TreeAnchor.Radius / (TreeAnchor.bAncient ? .39f : .34f);
		const float Radius = FMath::Clamp(Height * .035f, 25.f, 60.f), HalfHeight = FMath::Max(Radius, Height * .28f);
		auto* Trunk = NewObject<UCapsuleComponent>(
			&World, MakeUniqueObjectName(&World, UCapsuleComponent::StaticClass(),TEXT("EcologyTrunkCollision")));
		World.AddInstanceComponent(Trunk);
		Trunk->SetupAttachment(World.GetRootComponent());
		Trunk->ComponentTags.Add(EcologyTag);
		Trunk->SetMobility(EComponentMobility::Movable);
		Trunk->SetCapsuleSize(Radius, HalfHeight);
		Trunk->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Trunk->SetCollisionObjectType(ECC_WorldStatic);
		Trunk->SetCollisionResponseToAllChannels(ECR_Block);
		Trunk->SetGenerateOverlapEvents(false);
		Trunk->SetCanEverAffectNavigation(false);
		Trunk->SetHiddenInGame(true);
		Trunk->RegisterComponent();
		Trunk->SetWorldLocation(TreeAnchor.Position + FVector(0, 0, HalfHeight));
	}
	UE_LOG(LogTemp, Display,
	       TEXT(
		       "VERDANT_ECOLOGY patches=%d detail_patches=%d tiers=%d/%d/%d/%d trunk_colliders=%d ground_base=separate_near_player_layer"
	       ),
	       ForestPatches.Num(), DetailPatches.Num(), TierCounts[0], TierCounts[1], TierCounts[2], TierCounts[3],
	       TreeAnchors.Num());
}
