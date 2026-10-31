#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "VerdantGameMode.generated.h"
UCLASS()
class VERDANT_API AVerdantGameMode : public AGameModeBase {
 GENERATED_BODY()
public:
 AVerdantGameMode();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
private:
 TArray<FString> SmokeChecks;
 int32 SmokeFailures = 0;
 float SmokeStartSize = 0.f;
 float SmokeTargetHealth = 0.f;
 void TickWorldProbe(float Dt);
 void TickNPCProbe(float Dt);
 void TickSocialProbe(float Dt);
 void TickAIProbe(float Dt);
 void TickInputJourney(float Dt);
 void TickCombatProbe(float Dt);
 void TickHUDProbe(float Dt);
 float JourneyPhaseAt=0.f, JourneyFirstHarvest=-1.f, JourneyStartSize=0.f, JourneyFoodBefore=0.f;
 bool bJourneyWalking=false;
 int32 JourneyEatCount=0;
 int32 JourneyCareCount=0;
 float JourneyCareBefore=0.f, JourneyFertilityBefore=0.f;
 FVector JourneyOrigin=FVector::ZeroVector;
 int32 JourneyVisitIndex=0, JourneyCollectionGrove=-1, JourneyDialogueStart=0;
 int32 JourneyNegotiationsAttempted=0, JourneyNegotiationsReplied=0;
 float JourneyTravelDistance=0.f, JourneyNextPathSample=0.f;
 bool bJourneyHasPosition=false;
 FVector JourneyLastPosition=FVector::ZeroVector;
 TArray<FVector4> JourneyPath;
 TArray<FString> JourneyFullNotes;

 float ProbeNPCFoodBefore = 0.f;
 float CaptureAt = 0.f;
 float ProbeTreeBefore = 0.f;
 float ProbeInventoryBefore = 0.f;
 FVector ProbeStart = FVector::ZeroVector;
 float SmokeTime = 0.f;
 int32 SmokeStage = 0;
};
