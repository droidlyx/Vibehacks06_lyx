#include "VerdantGroundCoverComponent.h"
#include "VerdantWorld.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"

namespace VerdantGroundCover
{
	constexpr float CellSize = 600.f;
	constexpr int32 WindowRadius = 2;
	constexpr int32 PoolSize = 25;
	constexpr int32 GrassGrid = 27;
	constexpr int32 CloverGrid = 12;
	constexpr float MaximumSlopeCos = .5735764f; // 55 degrees.
	constexpr float MaximumPlantTilt = 25.f;

	uint32 CellSeed(const FIntPoint& Cell, uint32 Layer)
	{
		uint32 Hash = uint32(Cell.X) * 0x8da6b343u ^ uint32(Cell.Y) * 0xd8163841u ^ Layer;
		Hash ^= Hash >> 16;
		Hash *= 0x7feb352du;
		Hash ^= Hash >> 15;
		return Hash;
	}

	// This adapter reads the triangulated mesh that is already used for collision.
	// It does not evaluate the fractal terrain once per blade or perform ray traces.
	struct FGroundGrid
	{
		const FProcMeshSection* Section = nullptr;
		FTransform GroundTransform;
		int32 Row = 0;
		FVector Origin = FVector::ZeroVector;
		float StepX = 0.f;
		float StepY = 0.f;

		bool Initialize(UProceduralMeshComponent* Ground)
		{
			if (!IsValid(Ground))return false;
			Section = Ground->GetProcMeshSection(0);
			if (!Section || Section->ProcVertexBuffer.Num() < 4)return false;
			Row = FMath::RoundToInt(FMath::Sqrt(float(Section->ProcVertexBuffer.Num())));
			if (Row < 2 || Row * Row != Section->ProcVertexBuffer.Num())return false;
			Origin = Section->ProcVertexBuffer[0].Position;
			StepX = Section->ProcVertexBuffer[1].Position.X - Origin.X;
			StepY = Section->ProcVertexBuffer[Row].Position.Y - Origin.Y;
			GroundTransform = Ground->GetComponentTransform();
			return StepX > SMALL_NUMBER && StepY > SMALL_NUMBER
				&& !GroundTransform.GetScale3D().IsNearlyZero();
		}

		bool Sample(const FVector2D& WorldXY, FVector& Surface, FVector& Normal) const
		{
			const FVector LocalRayOrigin = GroundTransform.InverseTransformPosition(
				FVector(WorldXY.X, WorldXY.Y, GroundTransform.GetLocation().Z));
			const FVector LocalRayDirection = GroundTransform.InverseTransformVector(FVector::UpVector);
			FVector LocalPoint = LocalRayOrigin;
			FVector LocalNormal = FVector::UpVector;
			bool Settled = false;
			for (int32 Iteration = 0; Iteration < 5; ++Iteration)
			{
				const float GX = float((LocalPoint.X - Origin.X) / StepX);
				const float GY = float((LocalPoint.Y - Origin.Y) / StepY);
				if (GX < 0.f || GY < 0.f || GX >= Row - 1 || GY >= Row - 1)return false;
				const int32 X = FMath::FloorToInt(GX), Y = FMath::FloorToInt(GY);
				const float U = GX - X, V = GY - Y;
				const int32 A = Y * Row + X, B = A + 1, C = A + Row, D = C + 1;
				const auto& VA = Section->ProcVertexBuffer[A];
				const auto& VB = Section->ProcVertexBuffer[U >= V ? B : C];
				const auto& VD = Section->ProcVertexBuffer[D];
				const FVector PlaneNormal = FVector::CrossProduct(VB.Position - VA.Position, VD.Position - VA.Position);
				const float Denominator = FVector::DotProduct(PlaneNormal, LocalRayDirection);
				if (FMath::Abs(Denominator) < SMALL_NUMBER)return false;
				const float RayT = FVector::DotProduct(PlaneNormal, VA.Position - LocalRayOrigin) / Denominator;
				const FVector Hit = LocalRayOrigin + LocalRayDirection * RayT;
				if (FVector2D::Distance(FVector2D(Hit), FVector2D(LocalPoint)) < .01f)
				{
					LocalNormal = U >= V
						              ? VA.Normal * (1.f - U) + VB.Normal * (U - V) + VD.Normal * V
						              : VA.Normal * (1.f - V) + VB.Normal * (V - U) + VD.Normal * U;
					LocalPoint = Hit;
					Settled = true;
					break;
				}
				LocalPoint = Hit;
			}
			if (!Settled)return false;
			Surface = GroundTransform.TransformPosition(LocalPoint);
			const FVector Scale = GroundTransform.GetScale3D();
			if (FMath::Abs(Scale.X) < SMALL_NUMBER || FMath::Abs(Scale.Y) < SMALL_NUMBER
				|| FMath::Abs(Scale.Z) < SMALL_NUMBER)
				return false;
			Normal = GroundTransform.TransformVectorNoScale(LocalNormal / Scale).GetSafeNormal();
			return !Surface.ContainsNaN() && Normal.Z >= MaximumSlopeCos;
		}
	};

	FTransform PlantTransform(const FVector& Surface, const FVector& Normal, UStaticMesh* Mesh,
	                          float NominalHeight, float SizeFactor, float Yaw)
	{
		const FBox Box = Mesh->GetBoundingBox();
		const float Scale = NominalHeight * SizeFactor / FMath::Max(1.f, float(Box.GetSize().Z));
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		const float Angle = FMath::Min(FMath::Acos(FMath::Clamp(float(Normal.Z), -1.f, 1.f)),
		                               FMath::DegreesToRadians(MaximumPlantTilt));
		const FQuat Tilt = Axis.IsNearlyZero() ? FQuat::Identity : FQuat(Axis, Angle);
		const FQuat Rotation = Tilt * FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		const FVector Base(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z);
		return FTransform(Rotation, Surface + Normal * 1.f - Rotation.RotateVector(Base * Scale), FVector(Scale));
	}
}

UVerdantGroundCoverComponent::UVerdantGroundCoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .2f;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

UHierarchicalInstancedStaticMeshComponent* UVerdantGroundCoverComponent::MakeLayer(UStaticMesh* Mesh, FName Name)
{
	if (!Mesh || !GetOwner())return nullptr;
	auto* Layer = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner(), Name);
	GetOwner()->AddInstanceComponent(Layer);
	Layer->SetupAttachment(GetOwner()->GetRootComponent());
	Layer->SetMobility(EComponentMobility::Movable);
	Layer->SetStaticMesh(Mesh);
	Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Layer->SetGenerateOverlapEvents(false);
	Layer->SetCanEverAffectNavigation(false);
	Layer->SetCastShadow(false);
	Layer->bEnableDensityScaling = false;
	Layer->bAutoRebuildTreeOnInstanceChanges = false;
	// At a window shift its departing strip is at least 12m from the player.
	Layer->SetCullDistances(1000, 1200);
	Layer->RegisterComponent();
	return Layer;
}

void UVerdantGroundCoverComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld() || !GetWorld()->IsGameWorld() || GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}
	GrassMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Nature/SM_Grass01.SM_Grass01"));
	CloverMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Nature/SM_Clover01.SM_Clover01"));
	if (!GrassMesh && !CloverMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("VERDANT_GROUNDCOVER disabled: no low-cover meshes"));
		SetComponentTickEnabled(false);
		return;
	}
	Cells.SetNum(VerdantGroundCover::PoolSize);
	for (int32 I = 0; I < VerdantGroundCover::PoolSize; ++I)
	{
		GrassSlots.Add(MakeLayer(GrassMesh, FName(*FString::Printf(TEXT("NearGrass_%02d"), I))));
		CloverSlots.Add(MakeLayer(CloverMesh, FName(*FString::Printf(TEXT("NearClover_%02d"), I))));
	}
	UE_LOG(LogTemp, Display,
	       TEXT("VERDANT_GROUNDCOVER pooled_cells=25 tile_m=6 candidate_cap=21825 detail_radius_m=12 scale=0.8..1.2"));
}

void UVerdantGroundCoverComponent::RefreshWindow(const FVector& PlayerPosition)
{
	using namespace VerdantGroundCover;
	const FIntPoint Center(FMath::FloorToInt(PlayerPosition.X / CellSize),
	                       FMath::FloorToInt(PlayerPosition.Y / CellSize));
	if (bHasWindow && Center == WindowCenter)return;
	bHasWindow = true;
	WindowCenter = Center;
	TSet<FIntPoint> Desired;
	for (int32 Y = -WindowRadius; Y <= WindowRadius; ++Y)
		for (int32 X = -WindowRadius; X <= WindowRadius; ++X)Desired.Add(Center + FIntPoint(X, Y));
	for (int32 I = 0; I < Cells.Num(); ++I)
	{
		if (!Cells[I].bAssigned || Desired.Contains(Cells[I].Key))continue;
		if (GrassSlots[I])GrassSlots[I]->ClearInstances();
		if (CloverSlots[I])CloverSlots[I]->ClearInstances();
		Cells[I].bAssigned = false;
		Cells[I].bBuilt = false;
	}
	for (const FIntPoint& Key : Desired)
	{
		if (Cells.ContainsByPredicate([&](const FCellSlot& C) { return C.bAssigned && C.Key == Key; }))continue;
		const int32 Free = Cells.IndexOfByPredicate([](const FCellSlot& C) { return !C.bAssigned; });
		if (Free == INDEX_NONE)break;
		Cells[Free].Key = Key;
		Cells[Free].bAssigned = true;
		Cells[Free].bBuilt = false;
	}
	PendingSlots.Reset();
	for (int32 I = 0; I < Cells.Num(); ++I)
		if (Cells[I].bAssigned && !Cells[I].bBuilt)PendingSlots.Add(I);
	PendingSlots.Sort([this, PlayerPosition](int32 A, int32 B)
	{
		const FVector2D PA((Cells[A].Key.X + .5f) * CellSize, (Cells[A].Key.Y + .5f) * CellSize);
		const FVector2D PB((Cells[B].Key.X + .5f) * CellSize, (Cells[B].Key.Y + .5f) * CellSize);
		return FVector2D::DistSquared(PA, FVector2D(PlayerPosition)) < FVector2D::DistSquared(
			PB, FVector2D(PlayerPosition));
	});
}

bool UVerdantGroundCoverComponent::BuildCell(int32 SlotIndex)
{
	using namespace VerdantGroundCover;
	auto* Valley = Cast<AVerdantWorld>(GetOwner());
	FGroundGrid Grid;
	if (!Valley || !Grid.Initialize(Valley->Ground))
	{
		if (!bReportedTerrain)
		{
			UE_LOG(LogTemp, Warning, TEXT("VERDANT_GROUNDCOVER waiting for a regular triangulated terrain section"));
			bReportedTerrain = true;
		}
		return false;
	}
	const FIntPoint Cell = Cells[SlotIndex].Key;
	auto Populate = [&](UHierarchicalInstancedStaticMeshComponent* Layer, UStaticMesh* Mesh,
	                    int32 Strata, float Height, uint32 Seed)
	{
		if (!Layer || !Mesh)return;
		TArray<FTransform> Instances;
		Instances.Reserve(Strata * Strata);
		FRandomStream Random(int32(CellSeed(Cell, Seed)));
		for (int32 Y = 0; Y < Strata; ++Y)
			for (int32 X = 0; X < Strata; ++X)
			{
				const FVector2D P((Cell.X + (X + Random.FRandRange(.05f, .95f)) / Strata) * CellSize,
				                  (Cell.Y + (Y + Random.FRandRange(.05f, .95f)) / Strata) * CellSize);
				// Sample every random parameter before filters: slope changes never shift later seeds.
				const float Retain = Random.FRand();
				const float Size = Random.FRandRange(.8f, 1.2f);
				const float Yaw = Random.FRandRange(0.f, 360.f);
				const float Noise = .5f + .5f * FMath::PerlinNoise2D(P / 420.f + FVector2D(17, 31));
				if (Retain > .78f + .18f * FMath::Clamp(Noise, 0.f, 1.f))continue;
				FVector Surface, Normal;
				if (!Grid.Sample(P, Surface, Normal))continue;
				Instances.Add(PlantTransform(Surface, Normal, Mesh, Height, Size, Yaw));
			}
		Layer->ClearInstances();
		Layer->AddInstances(Instances, false, true, false);
		Layer->BuildTreeIfOutdated(false, true);
	};
	Populate(GrassSlots[SlotIndex], GrassMesh, GrassGrid, 25.f, 0x1634a27bu);
	Populate(CloverSlots[SlotIndex], CloverMesh, CloverGrid, 8.f, 0x9a715c31u);
	Cells[SlotIndex].bBuilt = true;
	return true;
}

void UVerdantGroundCoverComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                 FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bEnding || !GetWorld() || GetWorld()->bIsTearingDown)return;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)return;
	RefreshWindow(Player->GetActorLocation());
	for (int32 Built = 0; Built < 2 && !PendingSlots.IsEmpty(); ++Built)
	{
		const int32 Slot = PendingSlots[0];
		if (!BuildCell(Slot))break;
		PendingSlots.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

void UVerdantGroundCoverComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	for (const auto& Layer : GrassSlots)if (IsValid(Layer.Get()))Layer->DestroyComponent();
	for (const auto& Layer : CloverSlots)if (IsValid(Layer.Get()))Layer->DestroyComponent();
	GrassSlots.Reset();
	CloverSlots.Reset();
	Cells.Reset();
	PendingSlots.Reset();
	Super::EndPlay(Reason);
}
