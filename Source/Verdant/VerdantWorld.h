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
class UVerdantPopulation;
class AVerdantFoodPickup;

// One slot per opening natural food item; drops never acquire a slot.
struct FVerdantFoodSpawnSlot
{
	int32 Tier = 1;
	int32 GroveIndex = INDEX_NONE;
	bool bStarter = false;
	TWeakObjectPtr<AVerdantFoodPickup> Pickup;
};

USTRUCT()
struct FVerdantGrove
{
	GENERATED_BODY()
	UPROPERTY()
	FString Name;
	UPROPERTY()
	FVector Position = FVector::ZeroVector;
	UPROPERTY()
	float Food = 32.f;
	UPROPERTY()
	float Fertility = 1.f;
	UPROPERTY()
	int32 ControllerId = -1;
	UPROPERTY()
	bool bOpen = true;
	UPROPERTY()
	float Share = 0.f;
	UPROPERTY()
	TArray<int32> Permitted;
	UPROPERTY()
	FLinearColor Color = FLinearColor::Green;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Marker;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> FruitVisuals;
};

// Value-only records capture what existed at the start and at restoration.
// They never hold mutable character or grove references.
USTRUCT()
struct FVerdantSeasonPerson
{
	GENERATED_BODY()
	UPROPERTY()
	int32 AgentId = -1;
	UPROPERTY()
	FString Name;
	UPROPERTY()
	bool bWild = false;
	UPROPERTY()
	bool bAlive = false;
};

USTRUCT()
struct FVerdantSeasonGrove
{
	GENERATED_BODY()
	UPROPERTY()
	FString Name;
	UPROPERTY()
	int32 ControllerId = -1;
	UPROPERTY()
	FString ControllerName;
	UPROPERTY()
	bool bOpen = true;
	UPROPERTY()
	float Share = 0.f;
	UPROPERTY()
	float Food = 0.f;
	UPROPERTY()
	float Fertility = 0.f;
};

USTRUCT()
struct FVerdantSeasonSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	bool bCaptured = false;
	UPROPERTY()
	float AtSeconds = 0.f;
	UPROPERTY()
	float TreeNutrition = 0.f;
	UPROPERTY()
	int32 ResidentsAlive = 0;
	UPROPERTY()
	int32 ResidentsTotal = 0;
	UPROPERTY()
	int32 WildAlive = 0;
	UPROPERTY()
	int32 WildTotal = 0;
	UPROPERTY()
	int32 OpenGroves = 0;
	UPROPERTY()
	int32 FreeGroves = 0;
	UPROPERTY()
	int32 PlayerGroves = 0;
	UPROPERTY()
	float AverageFertility = 0.f;
	UPROPERTY()
	FString StateLabel;
	UPROPERTY()
	TArray<FVerdantSeasonPerson> People;
	UPROPERTY()
	TArray<FVerdantSeasonGrove> Groves;
	UPROPERTY()
	TMap<int32, float> Donations;
	UPROPERTY()
	TMap<int32, float> Tending;
	UPROPERTY()
	TMap<int32, FString> DonorNames;
};

UCLASS()
class VERDANT_API AVerdantWorld : public AActor
{
	GENERATED_BODY()

public:
	AVerdantWorld();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	static AVerdantWorld* Find(const UObject* Context);
	static float HeightAt(float X, float Y);
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Ground;
	UPROPERTY()
	TObjectPtr<UVerdantDialogue> Dialogue;
	UPROPERTY()
	TObjectPtr<UVerdantPopulation> Population;
	UPROPERTY()
	TArray<TObjectPtr<AVerdantFoodPickup>> FoodPickups;
	FRandomStream FoodRegrowthRandom{14664};
	TArray<FVerdantFoodSpawnSlot> FoodSpawnSlots;
	float NextFoodRefillAt = 0.f;
	void SpawnFoodField();
	void TickFoodField();
	AVerdantFoodPickup* FindFood(const AVerdantCharacter* Who, float MaxDistance = 6000.f,
	                             const TMap<TWeakObjectPtr<AVerdantFoodPickup>, float>* Avoided = nullptr) const;
	float CollectFood(AVerdantCharacter* Who, float Radius = 350.f);
	float LootCorpse(AVerdantCharacter* Corpse, AVerdantCharacter* Receiver, float Amount = MAX_flt);
	AVerdantFoodPickup* SpawnFoodDrop(const FVector& Position, float Amount, int32 Tier);
	UPROPERTY()
	TArray<FVerdantGrove> Groves;
	UPROPERTY()
	TArray<TObjectPtr<AVerdantCharacter>> Folk;
	UPROPERTY()
	TArray<FString> Events;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MotherCrown;
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
	TMap<FString, float> HarvestLedger;
	UPROPERTY()
	TMap<int32, float> DonationLedger;
	UPROPERTY()
	TMap<int32, float> TendLedger;
	UPROPERTY()
	TMap<FString, float> TransferLedger;
	UPROPERTY()
	TMap<int32, float> SpentGiftCredit;
	UPROPERTY()
	TMap<int32, FString> DonationNames;
	UPROPERTY()
	FVerdantSeasonSnapshot OpeningSeason;
	UPROPERTY()
	FVerdantSeasonSnapshot RestoredSeason;
	FString SeasonBannerTitle;
	FString SeasonBannerBody;
	FString SeasonCoreSummary;
	float SeasonNoticeUntil = -1.f;
	FVerdantSeasonSnapshot CaptureSeasonSnapshot() const;
	void BeginSeasonRecord();
	void FinalizeSeasonRecord();
	bool IsSeasonRecordVisible() const;
	void Record(const FString& Message);
	void Notify(const FString& Message);
	void PlayCue(FName Cue, FVector Position, float BodySize = 1.f);
	void PlayUISound(FName Cue);
	int32 NearestGrove(const FVector& At, float MaxDistance = 1800.f) const;
	AVerdantCharacter* FindFolk(int32 Id) const;
	bool CanHarvest(int32 GroveIndex, const AVerdantCharacter* Who) const;
	float Harvest(int32 GroveIndex, AVerdantCharacter* Who, float Amount = 5.f);
	bool Donate(AVerdantCharacter* Who, float Amount);
	bool Transfer(AVerdantCharacter* From, AVerdantCharacter* To, float Amount);
	bool Claim(int32 GroveIndex, AVerdantCharacter* Who);
	float TendCost(int32 GroveIndex) const;
	bool Tend(int32 GroveIndex, AVerdantCharacter* Who);
	bool TransferControl(int32 GroveIndex, AVerdantCharacter* From, AVerdantCharacter* To);
	float NetPlayerGift(int32 NpcId) const;
	float AvailableGiftCredit(int32 NpcId) const;
	float NegotiationReserve(const AVerdantCharacter* NPC) const;
	float MaxNegotiatedGive(const AVerdantCharacter* NPC, const AVerdantCharacter* Target) const;
	float MaxNegotiatedDonation(const AVerdantCharacter* NPC) const;
	int32 NegotiationGrove(const AVerdantCharacter* NPC) const;
	bool CanNegotiateFollow(const AVerdantCharacter* NPC, const AVerdantCharacter* Target) const;
	bool CanNegotiatePolicy(const AVerdantCharacter* NPC, const FString& Action, int32 GroveIndex,
	                        float* OutGiftCost = nullptr) const;
	bool ApplyNegotiatedPolicy(AVerdantCharacter* NPC, const FString& Action, int32 GroveIndex);
	FString NegotiationSummary(const AVerdantCharacter* NPC) const;

	bool SetAccess(int32 GroveIndex, AVerdantCharacter* Who, bool bOpen, float Share = 0.f);
	FString WorldSummary() const;
	FVector SurfacePoint(FVector P, float Lift = 0.f) const;
	UStaticMeshComponent* AddShape(const FString& Name, const TCHAR* MeshPath, FVector Position, FVector Scale,
	                               FLinearColor Color, bool Collision = false);
	UMaterialInterface* ColoredMaterial(FLinearColor Color, float Glow = 0.f);

private:
	UPROPERTY()
	TMap<FName, TObjectPtr<USoundBase>> CueCache;
	UPROPERTY()
	TObjectPtr<USoundAttenuation> CueAttenuation;
	UPROPERTY()
	TObjectPtr<USoundConcurrency> CueConcurrency;
	float EconomyClock = 0.f;
	bool bSimulationInitialized = false;
	float NextPopulationInitAt = 0.f;
	TMap<int32, float> LastHarvestNotice;
	float PolicyProbeElapsed = 0.f, PolicyProbeNextSample = 0.f, PolicyProbeInterventionAt = -1.f;
	bool bPolicyProbeInterventionApplied = false, bPolicyProbeFinished = false;
	TArray<FString> PolicyProbeSamples;
};
