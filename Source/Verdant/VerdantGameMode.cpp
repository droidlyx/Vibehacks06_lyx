#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantHUD.h"
#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

AVerdantGameMode::AVerdantGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AVerdantCharacter::StaticClass();
	HUDClass = AVerdantHUD::StaticClass();
}

void AVerdantGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Run before pawn spawning: this visual sphere surrounds the entire valley.
	// Old maps saved its disabled flag with a BlockAll profile, which reloads solid.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("VerdantSunsetSky"))) continue;
		It->SetActorEnableCollision(false);
		TInlineComponentArray<UPrimitiveComponent*> Components;
		It->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			Component->SetCollisionProfileName(TEXT("NoCollision"));
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetGenerateOverlapEvents(false);
			Component->SetCanEverAffectNavigation(false);
		}
	}
}

APawn* AVerdantGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer,
                                                                    const FTransform& SpawnTransform)
{
	// Editor camera / legacy PlayerStart heights are unrelated to generated ground.
	// Match the unscaled constructor capsule; BeginPlay's SetSize keeps its feet.
	const AVerdantWorld* Layout = AVerdantWorld::Find(this);
	if (!Layout) Layout = GetDefault<AVerdantWorld>();
	FTransform Arrival = SpawnTransform;
	Arrival.SetLocation(Layout->SurfacePoint(FVector(-1000, -5800, 0), 95.f));
	Arrival.SetRotation(FRotator(0, 78, 0).Quaternion());
	Arrival.SetScale3D(FVector::OneVector);
	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Arrival);
}

void AVerdantGameMode::BeginPlay()
{
	Super::BeginPlay();
	auto* W = AVerdantWorld::Find(this);
	if (!W) W = GetWorld()->SpawnActor<AVerdantWorld>();
	if (W)
		if (auto* P = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
		{
			P->SetActorLocation(W->SurfacePoint(FVector(-1000, -5800, 0),
			                                    P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f));
			P->GetCharacterMovement()->StopMovementImmediately();
			P->SetActorRotation(FRotator(0, 78, 0));
			if (P->GetController())P->GetController()->SetControlRotation(FRotator(-10, 78, 0));
		}
	UE_LOG(LogTemp, Display, TEXT("VERDANT_READY world=%d"), W!=nullptr);
}

void AVerdantGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	if (FParse::Param(FCommandLine::Get(),TEXT("VerdantHUDProbe")))
	{
		TickHUDProbe(Dt);
		return;
	}
	if (FParse::Param(FCommandLine::Get(),TEXT("VerdantNPCProbe")))
	{
		TickNPCProbe(Dt);
		return;
	}
	if (FParse::Param(FCommandLine::Get(),TEXT("VerdantSocialProbe")))
	{
		TickSocialProbe(Dt);
		return;
	}
	if (FParse::Param(FCommandLine::Get(),TEXT("VerdantCombatProbe")))
	{
		TickCombatProbe(Dt);
		return;
	}
	if (FParse::Param(FCommandLine::Get(),TEXT("VerdantInputJourney")) || FParse::Param(
		FCommandLine::Get(),TEXT("VerdantStewardJourney")))
	{
		TickInputJourney(Dt);
		return;
	}
	if (FParse::Param(FCommandLine::Get(),TEXT("VerdantAIProbe")))
	{
		TickAIProbe(Dt);
		return;
	}
	if (FParse::Param(FCommandLine::Get(),TEXT("VerdantWorldProbe")))
	{
		TickWorldProbe(Dt);
		return;
	}
	if (!FParse::Param(FCommandLine::Get(),TEXT("VerdantSmoke")))return;
	SmokeTime += Dt;
	auto* W = AVerdantWorld::Find(this);
	auto* P = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	auto Check = [this](bool Pass, const FString& Label)
	{
		const FString Line = (Pass ? TEXT("PASS ") : TEXT("FAIL ")) + Label;
		SmokeChecks.Add(Line);
		if (!Pass)++SmokeFailures;
		UE_LOG(LogTemp, Display, TEXT("VERDANT_CHECK %s"), *Line);
	};
	if (!W || !P)
	{
		if (SmokeTime > 20)
		{
			Check(false,TEXT("World and player startup"));
			FPlatformMisc::RequestExitWithStatus(false, 1);
		}
		return;
	}
	const FString Output = FPaths::ProjectSavedDir() / TEXT("Validation");
	if (SmokeStage == 0 && SmokeTime > 3.f)
	{
		IFileManager::Get().MakeDirectory(*Output, true);
		Check(W->Groves.Num() == 3,TEXT("Three independent resource groves"));
		Check(W->Folk.Num() >= 7,TEXT("Player and six distinct residents spawned"));
		FHitResult Hit;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(P);
		const FVector Pos = P->GetActorLocation();
		const bool Grounded = GetWorld()->LineTraceSingleByChannel(Hit, Pos, Pos - FVector(0, 0, 500), ECC_Visibility,
		                                                           Params);
		Check(Grounded && Hit.GetComponent() == W->Ground,TEXT("Procedural terrain has live collision beneath player"));
		Check(P->GetCharacterMovement()->IsMovingOnGround(),TEXT("Player stands on generated landscape"));

		for (AVerdantCharacter* N : W->Folk)if (N && N != P)
		{
			N->SetActorTickEnabled(false);
			N->GetCharacterMovement()->StopMovementImmediately();
			N->GetCharacterMovement()->DisableMovement();
		}
		auto* GroveOwner = W->FindFolk(2);
		P->SetActorLocation(W->SurfacePoint(W->Groves[1].Position,
		                                    P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3));
		const float Before = P->Food + W->Groves[1].Food + (GroveOwner ? GroveOwner->Food : 0.f);
		const float Collected = W->Harvest(1, P, 5.f);
		const float After = P->Food + W->Groves[1].Food + (GroveOwner ? GroveOwner->Food : 0.f);
		Check(Collected > 0 && FMath::IsNearlyEqual(Before, After, .01f),
		      TEXT("Harvest and actual controller share conserve food"));
		P->SetActorLocation(W->SurfacePoint(W->Groves[2].Position,
		                                    P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3));
		Check(W->Harvest(2, P, 5.f) == 0,TEXT("Closed grove rejects unauthorized harvest"));
		P->SetActorLocation(W->SurfacePoint(FVector(-1000, -5800, 0),
		                                    P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3));
		SmokeStartSize = P->BodySize;
		Check(P->Eat(5),TEXT("Eating transfers food into nutrition reserve"));
		SmokeStage = 1;
	}
	else if (SmokeStage == 1 && SmokeTime > 5.f && CaptureAt == 0)
	{
		FScreenshotRequest::RequestScreenshot(Output / TEXT("valley-start.png"), true, false);
		CaptureAt = SmokeTime;
	}
	else if (SmokeStage == 1 && SmokeTime > 10.f)
	{
		Check(P->BodySize > SmokeStartSize + .02f,TEXT("Continuous growth occurs over simulated time"));
		Check(P->GetCharacterMovement()->IsMovingOnGround(),TEXT("Growth preserves ground contact"));
		const float SmallStrength = P->Strength();
		P->SetSize(1.4f);
		Check(FMath::IsNearlyEqual(P->BodySize, 1.4f, .01f),TEXT("Growth test area permits the requested body size"));
		Check(P->Strength() > SmallStrength * 2,TEXT("Larger body directly increases combat strength"));
		P->SetActorLocation(W->SurfacePoint(FVector(-1000, -5800, 0),
		                                    P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3));
		if (auto* Target = W->FindFolk(1))
		{
			Target->SetActorLocation(W->SurfacePoint(FVector(-800, -5800, 0),
			                                         Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3));
			Target->Health = Target->MaxHealth();
			SmokeTargetHealth = Target->Health;
			if (P->GetController())P->GetController()->SetControlRotation(FRotator::ZeroRotator);
			P->SetActorRotation(FRotator::ZeroRotator);
			Check(P->Attack(false),TEXT("Player starts a melee attack"));
		}
		SmokeStage = 2;
	}
	else if (SmokeStage == 2 && SmokeTime > 12.f)
	{
		auto* Target = W->FindFolk(1);
		Check(Target && Target->Health < SmokeTargetHealth,TEXT("Timed melee hit damages another creature"));
		auto* Trader = W->FindFolk(2);
		if (Trader)
		{
			Trader->SetActorLocation(P->GetActorLocation() + FVector(150, 0, 0));
			float Before = P->Food + Trader->Food;
			bool Given = W->Transfer(P, Trader, 2);
			Check(Given && FMath::IsNearlyEqual(Before, P->Food + Trader->Food, .01f),
			      TEXT("Sharing is a real conserving transfer"));
		}
		P->SetActorLocation(W->SurfacePoint(W->MotherPosition + FVector(0, -700, 0),
		                                    P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3));
		P->Food = 120;
		const float Before = P->Food + W->TreeNutrition;
		bool Donated = W->Donate(P, 100.f);
		Check(Donated && W->bRestored && FMath::IsNearlyEqual(Before, P->Food + W->TreeNutrition, .01f),
		      TEXT("Mother Tree restoration consumes actual nutrition"));
		const FString BeforeSummary = W->WorldSummary();
		const bool Changed = W->SetAccess(2, W->FindFolk(3), true);
		Check(Changed && W->WorldSummary() != BeforeSummary,TEXT("Outcome changes when actual grove access changes"));
		P->SetActorLocation(W->SurfacePoint(W->MotherPosition + FVector(-350, -1800, 0),
		                                    P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3));
		P->GetCharacterMovement()->StopMovementImmediately();
		P->SetActorRotation(FRotator(0, 80, 0));
		if (P->GetController())P->GetController()->SetControlRotation(FRotator(12, 80, 0));
		CaptureAt = 0;
		SmokeStage = 3;
	}
	else if (SmokeStage == 3 && SmokeTime > 14.f && CaptureAt == 0)
	{
		FScreenshotRequest::RequestScreenshot(Output / TEXT("valley-restored.png"), true, false);
		CaptureAt = SmokeTime;
	}
	else if (SmokeStage == 3 && SmokeTime > 18.f)
	{
		SmokeChecks.Add(FString::Printf(TEXT("Failures: %d"), SmokeFailures));
		FFileHelper::SaveStringArrayToFile(SmokeChecks, *(Output / TEXT("smoke-results.txt")),
		                                   FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Display, TEXT("VERDANT_SMOKE_COMPLETE failures=%d"), SmokeFailures);
		SmokeStage = 4;
		FPlatformMisc::RequestExitWithStatus(false, SmokeFailures ? 1 : 0);
	}
}
