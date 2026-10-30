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
 void TickAIProbe(float Dt);
 float ProbeNPCFoodBefore = 0.f;
 float CaptureAt = 0.f;
 float ProbeTreeBefore = 0.f;
 float ProbeInventoryBefore = 0.f;
 FVector ProbeStart = FVector::ZeroVector;
 float SmokeTime = 0.f;
 int32 SmokeStage = 0;
};
