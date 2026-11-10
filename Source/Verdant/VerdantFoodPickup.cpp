#include "VerdantFoodPickup.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantEconomyRules.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

AVerdantFoodPickup::AVerdantFoodPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fruit"));
	SetRootComponent(Visual);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(false);
}

void AVerdantFoodPickup::Configure(AVerdantWorld* World, int32 Tier, float Amount, bool Renewable, int32 Grove)
{
	Valley = World;
	FoodTier = FMath::Clamp(Tier, 1, 4);
	Remaining = InitialAmount = FMath::Max(0.f, Amount);
	bRenewable = Renewable;
	GroveIndex = Grove;
	Anchor = GetActorLocation();
	Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	const FLinearColor Colors[] = {
		FLinearColor(.25f, .9f, .35f), FLinearColor(1.f, .78f, .15f), FLinearColor(1.f, .34f, .08f),
		FLinearColor(.75f, .3f, 1.f)
	};
	if (World) Visual->SetMaterial(0, World->ColoredMaterial(Colors[FoodTier - 1], .25f));
	RefreshVisual();
}

void AVerdantFoodPickup::RefreshVisual()
{
	SetActorHiddenInGame(Remaining <= .001f);
	const float Diameters[] = {12.f, 28.f, 60.f, 110.f};
	const float Diameter = Diameters[FoodTier - 1];
	// Keep the tier silhouette after a partial pickup; only the displayed count changes.
	Visual->SetWorldScale3D(FVector(Diameter / 100.f, Diameter / 100.f, Diameter / 120.f));
}

float AVerdantFoodPickup::Take(AVerdantCharacter* Receiver, float Requested)
{
	if (!IsValid(Receiver) || Receiver->bDead || !Valley || Remaining <= .001f) return 0.f;
	if (FVector::DistSquared(Receiver->GetActorLocation(), GetActorLocation()) > FMath::Square(
		FMath::Max(180.f, Receiver->Reach() + 130.f)))
		return 0.f;
	FHitResult Obstruction;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VerdantFoodReach), false, Receiver);
	Query.AddIgnoredActor(this);
	if (GetWorld()->LineTraceSingleByChannel(Obstruction, Receiver->GetActorLocation(), GetActorLocation(),
	                                         ECC_Visibility, Query))
		return 0.f;
	if (GroveIndex != INDEX_NONE && !Valley->CanHarvest(GroveIndex, Receiver)) return 0.f;
	AVerdantCharacter* GroveOwner = Valley->Groves.IsValidIndex(GroveIndex)
		                                ? Valley->FindFolk(Valley->Groves[GroveIndex].ControllerId)
		                                : nullptr;
	const float Share = IsValid(GroveOwner) && !GroveOwner->bDead && GroveOwner != Receiver
		                    ? FMath::Clamp(Valley->Groves[GroveIndex].Share, 0.f, .9f)
		                    : 0.f;
	if (Share > 0.f && GroveOwner->FoodTier() < FoodTier) return 0.f;
	float GrossLimit = FMath::Min(Remaining, Receiver->FreeFoodCapacity() / (1.f - Share));
	if (Share > 0.f) GrossLimit = FMath::Min(GrossLimit, GroveOwner->FreeFoodCapacity() / Share);
	const float Gross =
		VerdantEconomy::TransferAmount(Remaining, GrossLimit, Requested, Receiver->FoodTier(), FoodTier);
	if (Gross <= .001f) return 0.f;
	const float Tax = Gross * Share;
	const float Taken = Gross - Tax;
	Remaining -= Gross;
	Receiver->Food += Taken;
	if (Tax > 0.f) GroveOwner->Food += Tax;
	if (GroveIndex != INDEX_NONE)
		Valley->HarvestLedger.FindOrAdd(FString::Printf(TEXT("%d:%d"), Receiver->AgentId, GroveIndex)) += Taken;
	if (Receiver->AgentId == 0)
	{
		Valley->Notify(FString::Printf(
			TEXT("吸收第%d档食物 +%.1f · 剩余 %.1f · 库存 %.1f / %.1f"), FoodTier, Taken, Remaining, Receiver->Food,
			Receiver->FoodCapacity()));
		Valley->PlayCue(TEXT("Pickup"), GetActorLocation(), Receiver->BodySize);
	}
	if (Remaining <= .001f)
	{
		Remaining = 0.f;
		// Natural food is replaced at a new location by the world's 30-second cycle.
		// Keep it hidden until then; death drops have no refill slot.
		if (!bRenewable) Destroy();
	}
	RefreshVisual();
	return Taken;
}
