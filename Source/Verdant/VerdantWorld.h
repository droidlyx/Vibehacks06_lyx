#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VerdantWorld.generated.h"
class AVerdantCharacter;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UVerdantDialogue;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
USTRUCT()
struct FVerdantGrove {
 GENERATED_BODY()
 UPROPERTY() FString Name;
 UPROPERTY() FVector Position = FVector::ZeroVector;
 UPROPERTY() float Food = 32.f;
 UPROPERTY() float Fertility = 1.f;
 UPROPERTY() int32 ControllerId = -1;
 UPROPERTY() bool bOpen = true;
 UPROPERTY() float Share = 0.f;
 UPROPERTY() TArray<int32> Permitted;
 UPROPERTY() FLinearColor Color = FLinearColor::Green;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Marker;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FruitVisuals;
};
UCLASS()
class VERDANT_API AVerdantWorld : public AActor {
 GENERATED_BODY()
public:
 AVerdantWorld();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 static AVerdantWorld* Find(const UObject* Context);
 static float HeightAt(float X, float Y);
 UPROPERTY() TObjectPtr<UProceduralMeshComponent> Ground;
 UPROPERTY() TObjectPtr<UVerdantDialogue> Dialogue;
 UPROPERTY() TArray<FVerdantGrove> Groves;
 UPROPERTY() TArray<TObjectPtr<AVerdantCharacter>> Folk;
 UPROPERTY() TArray<FString> Events;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> MotherCrown;
 float TreeNutrition = 0.f;
 float TreeGoal = 100.f;
 float Elapsed = 0.f;
 bool bRestored = false;
 FString Notice;
 float NoticeUntil = 0.f;
 FVector MotherPosition = FVector(0, 3600, 0);
 void BuildLandscape();
 void BuildScenery();
 void SpawnResidents();
 void UpdateGroveVisuals();
 void TickEconomy(float Dt);
 void TickPolicyProbe(float Dt);
 TMap<FString,float> HarvestLedger;
 void Record(const FString& Message);
 void Notify(const FString& Message);
 void PlayCue(FName Cue, FVector Position, float BodySize = 1.f);
 int32 NearestGrove(const FVector& At, float MaxDistance = 1800.f) const;
 AVerdantCharacter* FindFolk(int32 Id) const;
 bool CanHarvest(int32 GroveIndex, const AVerdantCharacter* Who) const;
 float Harvest(int32 GroveIndex, AVerdantCharacter* Who, float Amount = 5.f);
 bool Donate(AVerdantCharacter* Who, float Amount);
 bool Transfer(AVerdantCharacter* From, AVerdantCharacter* To, float Amount);
 bool Claim(int32 GroveIndex, AVerdantCharacter* Who);
 bool SetAccess(int32 GroveIndex, AVerdantCharacter* Who, bool bOpen, float Share = 0.f);
 FString WorldSummary() const;
 FVector SurfacePoint(FVector P, float Lift = 0.f) const;
 UStaticMeshComponent* AddShape(const FString& Name, const TCHAR* MeshPath, FVector Position, FVector Scale, FLinearColor Color, bool Collision = false);
 UMaterialInterface* ColoredMaterial(FLinearColor Color, float Glow = 0.f);
private:
 UPROPERTY() TMap<FName,TObjectPtr<USoundBase>> CueCache;
 UPROPERTY() TObjectPtr<USoundAttenuation> CueAttenuation;
 UPROPERTY() TObjectPtr<USoundConcurrency> CueConcurrency;
 float EconomyClock = 0.f;
 TMap<int32,float> LastHarvestNotice;
 float PolicyProbeElapsed=0.f, PolicyProbeNextSample=0.f, PolicyProbeInterventionAt=-1.f;
 bool bPolicyProbeInterventionApplied=false, bPolicyProbeFinished=false;
 TArray<FString> PolicyProbeSamples;
};
