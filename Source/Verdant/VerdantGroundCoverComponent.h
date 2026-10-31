#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VerdantGroundCoverComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

// A stable world-space carpet near the viewer. Moving the window changes residency,
// never the size, seed or placement of an existing world's grass population.
UCLASS(ClassGroup=(Environment))
class VERDANT_API UVerdantGroundCoverComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVerdantGroundCoverComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FCellSlot
	{
		FIntPoint Key = FIntPoint::ZeroValue;
		bool bAssigned = false;
		bool bBuilt = false;
	};

	UPROPERTY(Transient) TObjectPtr<UStaticMesh> GrassMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CloverMesh;
	UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> GrassSlots;
	UPROPERTY(Transient) TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> CloverSlots;
	TArray<FCellSlot> Cells;
	TArray<int32> PendingSlots;
	FIntPoint WindowCenter = FIntPoint::ZeroValue;
	bool bHasWindow = false;
	bool bEnding = false;
	bool bReportedTerrain = false;

	UHierarchicalInstancedStaticMeshComponent* MakeLayer(UStaticMesh* Mesh, FName Name);
	void RefreshWindow(const FVector& PlayerPosition);
	bool BuildCell(int32 SlotIndex);
};
