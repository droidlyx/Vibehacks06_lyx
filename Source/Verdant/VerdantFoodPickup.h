#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VerdantFoodPickup.generated.h"

class UStaticMeshComponent;
class AVerdantWorld;
class AVerdantCharacter;

UCLASS()
class VERDANT_API AVerdantFoodPickup : public AActor
{
	GENERATED_BODY()

public:
	AVerdantFoodPickup();
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY()
	TObjectPtr<AVerdantWorld> Valley;
	int32 FoodTier = 1;
	int32 GroveIndex = INDEX_NONE;
	float Remaining = 5.f;
	float InitialAmount = 5.f;
	bool bRenewable = true;
	FVector Anchor;
	void Configure(AVerdantWorld* World, int32 Tier, float Amount, bool Renewable, int32 Grove = INDEX_NONE);
	void RefreshVisual();
	float Take(AVerdantCharacter* Receiver, float Requested = MAX_flt);
};
