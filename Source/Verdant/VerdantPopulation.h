#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VerdantPopulation.generated.h"

class AVerdantCharacter;
class AVerdantWorld;

USTRUCT()
struct FVerdantInstanceRelation
{
    GENERATED_BODY()
    UPROPERTY() int32 A = -1;
    UPROPERTY() int32 B = -1;
    UPROPERTY() FString Kind;
    UPROPERTY() FString Label;
    UPROPERTY() bool bSymmetric = false;
};

// Fixed personalities, seeded instance relations, and one shared real-delivery event.
UCLASS()
class VERDANT_API UVerdantPopulation : public UActorComponent
{
    GENERATED_BODY()
public:
    UVerdantPopulation();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    static UVerdantPopulation* Find(const UObject* Context);
    void SpawnResidents();
    bool HasSpawnedResidents() const { return bSpawned; }
    FString ContextFor(const AVerdantCharacter* Who) const;
    bool HasRelation(int32 A, int32 B, const FString& Kind) const;
    FString RequestSummary(const AVerdantCharacter* Who) const;
    // Called only after the production dialogue executor accepted a real donation plan.
    void ObserveDelivery(AVerdantCharacter* Donor);
    UPROPERTY() TArray<FVerdantInstanceRelation> Relations;
    int32 WorldSeed = 731031;
    FString SharedRequestState = TEXT("available");
private:
    void GenerateRelations(AVerdantWorld* World);
    float EventClock = 0.f;
    float DeliveryBaseline = 0.f;
    float DeliveryAmount = 0.f;
    int32 DeliveryLifeRevision = 0;
    bool bSpawned = false;
    bool bSpawnAttempted = false;
    bool bWaitingForTerrainLogged = false;
};
