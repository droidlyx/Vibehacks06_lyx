#include "VerdantCharacter.h"
#include "VerdantPlayerScale.h"
#include "VerdantEconomyRules.h"
#include "VerdantCreaturePresentation.h"
#include "VerdantTitanPresentation.h"
#include "VerdantWorld.h"
#include "VerdantHUD.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InputComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"
#include "HAL/IConsoleManager.h"

namespace VerdantScaleTest
{
	constexpr float MinimumSize = VerdantPlayerScale::StartingBodySize;
	constexpr float MaximumSize = 20.f;
	constexpr float ResetSize = VerdantPlayerScale::StartingBodySize;

	bool IsActive(const AVerdantCharacter* Player)
	{
#if !UE_BUILD_SHIPPING
		return Player && Player->AgentId == 0 && Player->ActorHasTag(TEXT("VerdantScaleTest"));
#else
		return false;
#endif
	}

#if !UE_BUILD_SHIPPING
	void Apply(AVerdantCharacter* Player, const FString& Action)
	{
		if (!IsValid(Player) || Player->AgentId != 0 || !Player->IsPlayerControlled()) return;
		auto* PC = Cast<APlayerController>(Player->GetController());
		if (!PC || Player->bDead) return;
		auto* HUD = Cast<AVerdantHUD>(PC->GetHUD());
		if (HUD && HUD->bConversationOpen) return; // Brackets typed in dialogue are text.
		if (Action == TEXT("status"))
		{
			const auto* Capsule = Player->GetCapsuleComponent();
			UE_LOG(LogTemp, Display,
			       TEXT(
				       "VERDANT_SCALE_TEST status size=%.4f height_m=%.3f active=%d capsule_radius=%.3f capsule_half=%.3f mesh_scale=%.4f arm=%.3f probe=%.3f grounded=%d nutrition=%.3f"
			       ),
			       Player->BodySize, Capsule->GetScaledCapsuleHalfHeight() * .02f, IsActive(Player),
			       Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(),
			       Player->GetMesh()->GetRelativeScale3D().Z,
			       Player->CameraArm->TargetArmLength, Player->CameraArm->ProbeSize,
			       Player->GetCharacterMovement()->IsMovingOnGround(), Player->Nutrition);
			return;
		}
		const bool Reset = Action == TEXT("reset");
		if (!Reset && Action != TEXT("grow") && Action != TEXT("shrink"))
		{
			UE_LOG(LogTemp, Warning, TEXT("Verdant.ScaleTest grow|shrink|reset|status"));
			return;
		}
		const float Before = Player->BodySize;
		const float Requested = Reset ? ResetSize : Before * (Action == TEXT("grow") ? 2.f : .5f);
		const float Target = FMath::Clamp(Requested, MinimumSize, MaximumSize);
		const bool WasActive = IsActive(Player);
		Player->Tags.AddUnique(TEXT("VerdantScaleTest"));
		// Reuse the real capsule/mesh/health update and its blocking-volume check.
		Player->CancelForConversation();
		Player->SetSize(Target);
		const bool Applied = FMath::IsNearlyEqual(Player->BodySize, Target);
		if ((Reset && Applied) || (!Applied && !WasActive)) Player->Tags.Remove(TEXT("VerdantScaleTest"));
		if (Reset && Applied) Player->SetSize(Target); // Restore normal movement tuning after clearing test mode.
		if (auto* World = Player->Valley())
		{
			if (!Applied) World->Notify(TEXT("缩放受阻：附近地形或物体没有足够空间，请移到开阔处。"));
			else if (Reset)
				World->Notify(FString::Printf(TEXT("已恢复出发体型 %.2f 米，退出缩放测试。"),
				                              VerdantPlayerScale::StartingHeightCm / 100.f));
			else
				World->Notify(
					FString::Printf(TEXT("尺度测试：%.2f 米 · [ 缩小 / ] 放大 / 反斜杠恢复 · 范围 0.18–36 米"), Player->BodySize * 1.8f));
		}
		UE_LOG(LogTemp, Display,
		       TEXT("VERDANT_SCALE_TEST action=%s before=%.4f target=%.4f after=%.4f applied=%d clamped=%d active=%d"),
		       *Action, Before, Target, Player->BodySize, Applied, !FMath::IsNearlyEqual(Requested, Target),
		       IsActive(Player));
	}

	void Console(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld()) return;
		Apply(Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0)),
		      Args.Num() ? Args[0].ToLower() : TEXT("status"));
	}

	void Bind(UInputComponent* Input, AVerdantCharacter* Player)
	{
		auto AddKey = [Input, Player](FKey Key, const TCHAR* Action)
		{
			FInputKeyBinding Binding(FInputChord(Key), IE_Pressed);
			Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(Player, [Player, Action]()
			{
				Apply(Player, Action);
			});
			Input->KeyBindings.Add(MoveTemp(Binding));
		};
		AddKey(EKeys::LeftBracket, TEXT("shrink"));
		AddKey(EKeys::RightBracket, TEXT("grow"));
		AddKey(EKeys::Backslash, TEXT("reset"));
		AddKey(EKeys::Subtract, TEXT("shrink"));
		AddKey(EKeys::Add, TEXT("grow"));
		auto& Manager = IConsoleManager::Get();
		if (!Manager.FindConsoleObject(TEXT("Verdant.ScaleTest")))
			Manager.RegisterConsoleCommand(
				TEXT("Verdant.ScaleTest"), TEXT("Development scale test: grow | shrink | reset | status"),
				FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Console), ECVF_Cheat);
		UE_LOG(LogTemp, Display,
		       TEXT("VERDANT_SCALE_TEST controls=[ shrink, ] grow, Backslash reset; height range 0.18-36m"));
	}
#endif
}

AVerdantCharacter::AVerdantCharacter()
{
	BodySize = VerdantPlayerScale::StartingBodySize;
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(32.f, 90.f);
	bUseControllerRotationYaw = false;
	auto* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0, 650, 0);
	Move->MaxWalkSpeed = 450;
	Move->MaxAcceleration = 1800;
	Move->BrakingDecelerationWalking = 1800;
	Move->bRunPhysicsWithNoController = true;
	Move->SetWalkableFloorAngle(75.f);
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Orbit"));
	CameraArm->SetupAttachment(RootComponent);
	CameraArm->TargetArmLength = 340;
	CameraArm->bUsePawnControlRotation = true;
	CameraArm->bEnableCameraLag = true;
	CameraArm->CameraLagSpeed = 12;
	CameraArm->ProbeSize = 12;
	CameraArm->bDoCollisionTest = true;
	CameraArm->bUseCameraLagSubstepping = true;
	CameraArm->CameraLagMaxDistance = 55;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm);
	Camera->FieldOfView = 72;
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV);
	FallbackBody = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	FallbackBody->SetupAttachment(RootComponent);
	CarriedNutrition = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CarriedNutrition"));
	CarriedNutrition->SetupAttachment(RootComponent);
	CarriedNutrition->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CarriedNutrition->SetCastShadow(false);
	CarriedNutrition->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
}

void AVerdantCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Reapply at spawn: Live Coding does not rerun existing component CDO constructors.
	GetCharacterMovement()->SetWalkableFloorAngle(75.f);
	GetMesh()->SetSkeletalMeshAsset(nullptr);
	if (AgentId == 0 || !VerdantTitanPresentation::Load(this))
	{
		GetMesh()->SetVisibility(false);
		BuildFallbackBody();
	}
	CarriedNutrition->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	AppliedSize = 0;
	// NPCs receive their own size before FinishSpawning; only the player starts tiny.
	// Also overrides an old component/CDO default after a Live Coding update.
	if (AgentId == 0) BodySize = VerdantPlayerScale::StartingBodySize;
	SetSize(BodySize);
	Health = MaxHealth();
	if (AgentId == 0)
	{
		if (auto* PC = Cast<APlayerController>(GetController()))
		{
			PC->SetControlRotation(FRotator(-14, 90, 0));
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->ViewPitchMin = -65;
				PC->PlayerCameraManager->ViewPitchMax = 40;
			}
			PC->SetInputMode(FInputModeGameOnly());
			PC->bShowMouseCursor = false;
		}
	}
	UpdateAppearance();
	UpdateCameraFraming(0.f, true);
	UpdateAnimation();
}

void AVerdantCharacter::BuildFallbackBody()
{
	VerdantCreaturePresentation::BuildHero(this);
}

float AVerdantCharacter::MaxHealth() const { return 100.f * FMath::Pow(FMath::Max(.3f, BodySize), 1.35f); }
float AVerdantCharacter::Strength() const { return 22.f * FMath::Pow(FMath::Max(.3f, BodySize), 1.3f); }
float AVerdantCharacter::Reach() const { return FMath::Max(95.f, 170.f * BodySize); }

float AVerdantCharacter::HeightCm() const
{
	return VerdantTitanPresentation::BaseHeight(this) * BodySize;
}

int32 AVerdantCharacter::FoodTier() const { return VerdantEconomy::Tier(HeightCm()); }

float AVerdantCharacter::FoodCapacity() const
{
	return AgentId == 0 ? VerdantEconomy::Capacity(HeightCm()) : MAX_flt;
}

float AVerdantCharacter::FreeFoodCapacity() const
{
	return FMath::Max(0.f, FoodCapacity() - FMath::Max(0.f, Food));
}

float AVerdantCharacter::GrowthSecondsRemaining() const
{
	return FMath::Max(0.f, VerdantEconomy::GrowthInterval - GrowthClock);
}

float AVerdantCharacter::RespawnSecondsRemaining() const { return FMath::Max(0.f, RespawnClock); }
bool AVerdantCharacter::IsRespawnProtected() const { return RespawnProtectionRemaining > 0.f; }

void AVerdantCharacter::TickAutomaticGrowth(float Dt)
{
	// Food is one numeric inventory. NPCs and the explicit scale preview never digest growth food.
	Nutrition = 0.f;
	if (AgentId != 0 || bDead || !FMath::IsFinite(Dt) || VerdantScaleTest::IsActive(this)) return;
	GrowthClock += FMath::Max(0.f, Dt);
	if (GrowthClock < VerdantEconomy::GrowthInterval) return;
	const float Cycles = FMath::FloorToFloat(GrowthClock / VerdantEconomy::GrowthInterval);
	GrowthClock = FMath::Fmod(GrowthClock, VerdantEconomy::GrowthInterval);
	if (!FMath::IsFinite(Food) || Food <= .0001f) return;
	// Account for a long frame without losing elapsed intervals or running an unbounded loop.
	const float Consumed = Food * (1.f - FMath::Pow(1.f - VerdantEconomy::GrowthFraction, Cycles));
	const float PreviousHeight = HeightCm();
	const float TargetHeight = VerdantEconomy::GrownHeight(PreviousHeight, Consumed);
	if (TargetHeight > PreviousHeight + .001f)
	{
		SetSize(TargetHeight / VerdantTitanPresentation::BaseHeight(this));
		// A blocked capsule expansion consumes nothing; retry at the next interval in open space.
		if (HeightCm() <= PreviousHeight + .001f)
		{
			if (auto* W = Valley()) W->Notify(TEXT("周围空间不足，本次成长未消耗食物。"));
			return;
		}
	}
	Food = FMath::Max(0.f, Food - Consumed);
	Health = FMath::Min(MaxHealth(), Health + Consumed * 1.9f);
	if (auto* W = Valley())
	{
		W->PlayCue(TEXT("Grow"), GetActorLocation(), BodySize);
		W->Notify(FString::Printf(TEXT("自动吸收 %.1f 份食物 · 身高 %.2f 米 · 第%d档"),
		                          Consumed, HeightCm() / 100.f, FoodTier()));
	}
}

void AVerdantCharacter::RespawnPlayer()
{
	auto* W = Valley();
	if (AgentId != 0 || !bDead || !W) return;
	const float TargetHeight = VerdantEconomy::RespawnHeight(DeathHeightCm, VerdantPlayerScale::StartingHeightCm);
	Tags.Remove(TEXT("VerdantScaleTest"));
	AppliedSize = 0.f;
	SetSize(TargetHeight / VerdantTitanPresentation::BaseHeight(this));
	const float Overflow = FMath::Max(0.f, Food - FoodCapacity());
	if (Overflow > .0001f)
	{
		// The same inventory cannot remain on the corpse and return on the player.
		// The world owns this drop, which keeps the pre-death source tier.
		if (!W->SpawnFoodDrop(DeathPosition, Overflow, DeathFoodTier))
		{
			RespawnClock = 1.f;
			return;
		}
		Food -= Overflow;
	}
	const FVector Arrival(-1000.f, -5800.f, 0.f);
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Spawn = W->SurfacePoint(Arrival, HalfHeight + 8.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VerdantRespawn), false, this);
	for (int32 I = 0; I < 16; ++I)
	{
		const float Angle = I * 2.399963f;
		const float Radius = I == 0 ? 0.f : 120.f + 85.f * I;
		const FVector Candidate = W->SurfacePoint(Arrival + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius,
		                                          HalfHeight + 8.f);
		if (!GetWorld()->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Pawn,
		                                              FCollisionShape::MakeCapsule(
			                                              GetCapsuleComponent()->GetScaledCapsuleRadius(),
			                                              HalfHeight - 1.f), Params))
		{
			Spawn = Candidate;
			break;
		}
	}
	auto* Move = GetCharacterMovement();
	Move->StopMovementImmediately();
	ConsumeMovementInputVector();
	SetActorLocation(Spawn, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator(0.f, 78.f, 0.f));
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Move->SetMovementMode(MOVE_Walking);
	Move->bForceNextFloorCheck = true;
	bDead = false;
	++LifeRevision;
	++IntentRevision;
	Health = MaxHealth();
	Nutrition = 0.f;
	GrowthClock = RespawnClock = 0.f;
	RespawnProtectionRemaining = VerdantEconomy::RespawnProtection;
	AttackTimer = DodgeTimer = HurtTimer = DodgeRecoveryTimer = AttackInputBuffer = 0.f;
	QueuedAttackType = -1;
	bHitApplied = true;
	bSprinting = bHarvestHeld = bProtectLeader = false;
	HarvestRepeatTimer = 0.f;
	FollowId = HostileId = -1;
	DonationPledge = 0.f;
	CombatTarget.Reset();
	ActionCooldown = .3f;
	SocialActionCooldown = 3.f;
	if (auto* PC = Cast<APlayerController>(GetController()))
	{
		if (auto* HUD = Cast<AVerdantHUD>(PC->GetHUD())) HUD->CloseConversation();
		PC->FlushPressedKeys();
		PC->SetControlRotation(FRotator(-14.f, 78.f, 0.f));
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	AnimationState = -1;
	UpdateAppearance();
	UpdateAnimation();
	UpdateCameraFraming(0.f, true);
	W->Record(FString::Printf(TEXT("%s 缩小后复苏：身高 %.2f 米，世界与居民经历仍然保留。"),
	                          *DisplayName, HeightCm() / 100.f));
	W->Notify(FString::Printf(TEXT("已复活 · 第%d档 · %.2f米 · 3秒保护"), FoodTier(), HeightCm() / 100.f));
}

AVerdantWorld* AVerdantCharacter::Valley() const { return AVerdantWorld::Find(this); }

void AVerdantCharacter::SetSize(float NewSize)
{
	if (!FMath::IsFinite(NewSize)) return;
	const bool ScaleTest = VerdantScaleTest::IsActive(this);
	NewSize = ScaleTest
		          ? FMath::Clamp(NewSize, VerdantScaleTest::MinimumSize, VerdantScaleTest::MaximumSize)
		          : FMath::Clamp(NewSize, AgentId == 0 ? VerdantPlayerScale::StartingBodySize : .35f, 2.5f);
	// Preserve the world-space foot position. Initial spawns use the constructor
	// capsule height; all later growth adjusts height without lifting the feet.
	const float ShapeScale = GetCapsuleComponent()->GetShapeScale();
	const float OldHalf = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float NewHalf = VerdantTitanPresentation::BaseHeight(this) * .5f * NewSize;
	const FVector NewCenter = GetActorLocation() + FVector(0, 0, NewHalf * ShapeScale - OldHalf);
	if (AppliedSize > 0 && NewSize > AppliedSize && GetWorld())
	{
		FCollisionQueryParams Q(SCENE_QUERY_STAT(VerdantGrowth), false, this);
		if (GetWorld()->OverlapBlockingTestByChannel(NewCenter, FQuat::Identity, ECC_Pawn,
		                                             FCollisionShape::MakeCapsule(
			                                             31.f * NewSize * ShapeScale, (NewHalf - 2.f) * ShapeScale),
		                                             Q))
			return;
	}
	const float HealthFraction = Health / FMath::Max(1.f, MaxHealth());
	BodySize = NewSize;
	GetCapsuleComponent()->SetCapsuleSize(32.f * BodySize, NewHalf, true);
	SetActorLocation(NewCenter, false);
	GetCharacterMovement()->bForceNextFloorCheck = true;
	Health = FMath::Clamp(HealthFraction, 0.f, 1.f) * MaxHealth();
	GetMesh()->SetRelativeLocation(FVector(0, 0, -NewHalf));
	GetMesh()->SetRelativeScale3D(FVector(BodySize));
	FallbackBody->SetRelativeLocation(FVector(0, 0, -NewHalf));
	FallbackBody->SetRelativeScale3D(FVector(BodySize));
	CarriedNutrition->SetRelativeLocation(FVector(43.f, 0, 20.f) * BodySize);
	CarriedNutrition->SetRelativeScale3D(FVector(.32f * BodySize));
	GetCharacterMovement()->MaxStepHeight = FMath::Min(ScaleTest ? 900.f : 90.f, 42.f * BodySize);
	const float MotionScale = AgentId == 0
		                          ? VerdantPlayerScale::SpeedMultiplier * BodySize /
		                          VerdantPlayerScale::ReferenceBodySize
		                          : 1.f;
	GetCharacterMovement()->MaxAcceleration = 1800.f * MotionScale;
	GetCharacterMovement()->BrakingDecelerationWalking = 1800.f * MotionScale;
	if (AgentId == 0)
		GetCharacterMovement()->MaxWalkSpeed = VerdantPlayerScale::MovementSpeed(BodySize, bSprinting ? 640.f : 430.f);
	GetCharacterMovement()->JumpZVelocity = 400.f * FMath::Sqrt(BodySize);
	AppliedSize = BodySize;
}

void AVerdantCharacter::Tick(float Dt)
{
	Super::Tick(Dt);
	SocialActionCooldown = FMath::Max(0.f, SocialActionCooldown - Dt);
	RespawnProtectionRemaining = FMath::Max(0.f, RespawnProtectionRemaining - Dt);
	if (bDead && AgentId == 0)
	{
		RespawnClock = FMath::Max(0.f, RespawnClock - FMath::Max(0.f, Dt));
		if (RespawnClock <= 0.f) RespawnPlayer();
		UpdateCameraFraming(Dt);
		UpdateAnimation();
		return;
	}
	ActionCooldown = FMath::Max(0.f, ActionCooldown - Dt);
	HarvestRepeatTimer = FMath::Max(0.f, HarvestRepeatTimer - Dt);
	if (bHarvestHeld && !bDead && AttackTimer <= 0 && DodgeTimer <= 0 && HurtTimer <= 0 && HarvestRepeatTimer <= 0)
	{
		Interact();
		HarvestRepeatTimer = .7f;
	}
	HurtTimer = FMath::Max(0.f, HurtTimer - Dt);
	DodgeTimer = FMath::Max(0.f, DodgeTimer - Dt);
	DodgeRecoveryTimer = FMath::Max(0.f, DodgeRecoveryTimer - Dt);
	AttackInputBuffer = FMath::Max(0.f, AttackInputBuffer - Dt);
	if (AttackInputBuffer <= 0)QueuedAttackType = -1;
	if (GetWorld()->GetTimeSeconds() > FeedbackUntil) CombatFeedback.Empty();
	if (AttackTimer > 0)
	{
		SetActorRotation(FRotator(0, LockedActionYaw, 0));
		const float Prior = AttackTimer;
		AttackTimer = FMath::Max(0.f, AttackTimer - Dt);
		const float StrikeAt = AttackDuration * .40f; // Contact at 60% of this attack's clock.
		if (!bHitApplied && Prior > StrikeAt && AttackTimer <= StrikeAt) ApplyStrike();
	}
	if (!bDead)
	{
		if (QueuedAttackType >= 0 && AttackTimer <= 0 && DodgeTimer <= 0 && HurtTimer <= 0)
		{
			const bool Heavy = QueuedAttackType == 1;
			QueuedAttackType = -1;
			AttackInputBuffer = 0;
			Attack(Heavy);
		}
		TickAutomaticGrowth(Dt);
		const float MovementSpeed = AgentId == 0
			                            ? VerdantPlayerScale::MovementSpeed(BodySize, bSprinting ? 640.f : 430.f)
			                            : (bSprinting ? 640.f : 430.f) * FMath::Pow(BodySize, .32f);
		GetCharacterMovement()->MaxWalkSpeed = MovementSpeed * (AttackTimer > 0 ? .35f : 1.f);
		if (AgentId != 0 && DodgeTimer <= 0 && (HurtTimer <= 0 || AttackTimer > 0)) TickNPC(Dt);
		if (DodgeTimer > 0)
		{
			const float Phase = 1.f - DodgeTimer / DodgeActiveDuration;
			const float BaseDodgeSpeed = AgentId == 0
				                             ? VerdantPlayerScale::MovementSpeed(BodySize, 630.f, .35f)
				                             : 630.f * FMath::Pow(BodySize, .35f);
			const float Speed = BaseDodgeSpeed * FMath::Lerp(1.f, .25f, Phase);
			auto* Move = GetCharacterMovement();
			Move->MaxWalkSpeed = Speed;
			// CharacterMovement resolves collision and slopes; no transform teleport during the roll.
			Move->Velocity = FVector(DodgeDirection.X * Speed, DodgeDirection.Y * Speed, Move->Velocity.Z);
		}
		if (GetActorLocation().Z < -8000)
		{
			if (auto* W = Valley()) SetActorLocation(W->SurfacePoint(FVector(0, -7000, 0), 110 * BodySize));
		}
	}
	GetCharacterMovement()->bOrientRotationToMovement = !bDead && AttackTimer <= 0 && DodgeTimer <= 0 && HurtTimer <= 0;
	if (AttackTimer > 0 || DodgeTimer > 0) SetActorRotation(FRotator(0, LockedActionYaw, 0));
	CarriedNutrition->SetVisibility(!bDead && DonationPledge > .001f && Food > .001f);
	UpdateCameraFraming(Dt);
	UpdateAnimation();
}

void AVerdantCharacter::UpdateCameraFraming(float Dt, bool bSnap)
{
	if (!Camera || !CameraArm) return;
	const float Blend = bSnap ? 1.f : 1.f - FMath::Exp(-5.f * FMath::Max(0.f, Dt));
	Camera->SetFieldOfView(FMath::Lerp(Camera->FieldOfView, bSprinting ? 78.f : 72.f, Blend));
	int32 Width = 1600, Height = 900;
	if (auto* PC = Cast<APlayerController>(GetController()))
	{
		int32 W = 0, H = 0;
		PC->GetViewportSize(W, H);
		if (W > 0 && H > 0)
		{
			Width = W;
			Height = H;
		}
	}
	const float Aspect = float(Width) / FMath::Max(1, Height);
	const float TanVerticalHalf = FMath::Max(
		.1f, FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView * .5f)) / Aspect);
	const float BodyHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f;
	const float Chest = BodyHeight * .62f;
	const FVector AimOffset(0, 0, Chest - BodyHeight * .5f);
	CameraArm->TargetOffset = FMath::Lerp(CameraArm->TargetOffset, AimOffset, Blend);
	CameraArm->SocketOffset = FMath::Lerp(CameraArm->SocketOffset, FVector(0, 18.f * BodySize, 0), Blend);
	const float Pitch = FMath::DegreesToRadians(Controller
		                                            ? Controller->GetControlRotation().Pitch
		                                            : CameraArm->GetComponentRotation().Pitch);
	const float CosPitch = FMath::Abs(FMath::Cos(Pitch)), SinPitch = FMath::Sin(Pitch);
	// Projection constraints keep feet above the bottom HUD and leave headroom.
	const float BottomReserve = FMath::Clamp(190.f / float(Height), .10f, .30f);
	const float BottomNDC = 1.f - 2.f * BottomReserve;
	const float TopNDC = .80f;
	const float FeetDistance = Chest * CosPitch / (TanVerticalHalf * BottomNDC) + Chest * SinPitch;
	const float HeadDistance = (BodyHeight - Chest) * CosPitch / (TanVerticalHalf * TopNDC) - (BodyHeight - Chest) *
		SinPitch;
	const bool ScaleTest = VerdantScaleTest::IsActive(this);
	const bool AdaptSmallBody = ScaleTest || AgentId == 0;
	const float SmallScale = AdaptSmallBody ? FMath::Min(1.f, BodySize / VerdantPlayerScale::ReferenceBodySize) : 1.f;
	const float FramingMinimum = FMath::Max(FeetDistance, HeadDistance) + 18.f * SmallScale;
	const float Desired = FMath::Max3(280.f * SmallScale, 320.f * FMath::Pow(BodySize, .65f), FramingMinimum);
	CameraArm->TargetArmLength = FMath::Max(FramingMinimum, FMath::Lerp(CameraArm->TargetArmLength, Desired, Blend));
	CameraArm->ProbeSize = AdaptSmallBody
		                       ? FMath::Clamp(12.f * BodySize / VerdantPlayerScale::ReferenceBodySize, 2.f, 35.f)
		                       : 12.f;
	CameraArm->CameraLagMaxDistance = FMath::Clamp(30.f + 20.f * BodySize, 35.f, 80.f) * SmallScale;
}

void AVerdantCharacter::SetCombatFeedback(const FString& Message, float Duration)
{
	CombatFeedback = Message;
	FeedbackUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + Duration : 0.f;
}

void AVerdantCharacter::UpdateAppearance()
{
	if (!BodyParts.IsEmpty()) VerdantCreaturePresentation::PaintHero(this);
	else VerdantTitanPresentation::Paint(this);
	if (auto* W = Valley())
		if (CarriedNutrition) CarriedNutrition->SetMaterial(0, W->ColoredMaterial(FLinearColor(1.f, .61f, .17f), .8f));
}

void AVerdantCharacter::UpdateAnimation()
{
	if (!BodyParts.IsEmpty()) VerdantCreaturePresentation::UpdateHero(this);
	else VerdantTitanPresentation::Update(this);
}

bool AVerdantCharacter::Attack(bool Heavy)
{
	if (bDead || AttackTimer > 0 || DodgeTimer > 0 || HurtTimer > 0) return false;
	StopHarvest();
	QueuedAttackType = -1;
	AttackInputBuffer = 0;
	bHeavyAttack = Heavy;
	AttackDuration = Heavy ? .85f : .55f;
	AttackTimer = AttackDuration;
	bHitApplied = false;
	AnimationState = -1;
	if (auto* W = Valley())W->PlayCue(TEXT("Swing"), GetActorLocation(), BodySize);
	LockedActionYaw = AgentId == 0 && Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
	SetActorRotation(FRotator(0, LockedActionYaw, 0));
	GetCharacterMovement()->bOrientRotationToMovement = false;
	UpdateAnimation();
	return true;
}

void AVerdantCharacter::ApplyStrike()
{
	bHitApplied = true;
	auto* W = Valley();
	if (!W || bDead) return;
	float TotalDamage = 0.f;
	int32 Hits = 0;
	bool Evaded = false;
	FString Defeated;
	for (AVerdantCharacter* Other : W->Folk)
	{
		if (!IsValid(Other) || Other == this || Other->bDead) continue;
		// Companions protect their leader; player attacks remain an unrestricted choice.
		if (AgentId != 0 && FollowId >= 0 && (Other->AgentId == FollowId || Other->FollowId == FollowId)) continue;
		FVector Delta = Other->GetActorLocation() - GetActorLocation();
		const float Dist = Delta.Size2D();
		if (Dist > Reach() + Other->GetCapsuleComponent()->GetScaledCapsuleRadius()) continue;
		if (FMath::Abs(Delta.Z) > 100.f * (BodySize + Other->BodySize)) continue;
		if (FVector::DotProduct(GetActorForwardVector(), Delta.GetSafeNormal2D()) < .25f) continue;
		FHitResult Wall;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(VerdantMelee), false, this);
		Q.AddIgnoredActor(Other);
		if (GetWorld()->
			LineTraceSingleByChannel(Wall, GetActorLocation(), Other->GetActorLocation(), ECC_Visibility, Q))
			continue;
		const float Before = Other->Health;
		Evaded |= Other->IsDodgeInvulnerable();
		Other->ReceiveStrike(this, Strength() * (bHeavyAttack ? 1.8f : 1.f));
		if (Other->Health < Before)
		{
			TotalDamage += Before - Other->Health;
			++Hits;
		}
		if (Other->bDead)Defeated = Other->DisplayName;
	}
	if (!Defeated.IsEmpty()) SetCombatFeedback(TEXT("击败 ") + Defeated + TEXT(" · E 拾取养分"), 1.4f);
	else if (Hits > 0)
		SetCombatFeedback(
			FString::Printf(TEXT("%s命中 · %.0f 伤害"), bHeavyAttack ? TEXT("重击") : TEXT(""), TotalDamage));
	else SetCombatFeedback(Evaded ? TEXT("对方闪开了") : TEXT("挥空"), .55f);
}

void AVerdantCharacter::ReceiveStrike(AVerdantCharacter* From, float Damage)
{
	StopHarvest();
	if (bDead || RespawnProtectionRemaining > 0.f || !IsValid(From) || From == this || !FMath::IsFinite(Damage) ||
		Damage <= 0)return;
	if (IsDodgeInvulnerable())
	{
		SetCombatFeedback(TEXT("闪避成功"));
		return;
	}
	QueuedAttackType = -1;
	AttackInputBuffer = 0;
	bool ConversationInterrupted = false;
	if (AgentId == 0)
		if (auto* PC = Cast<APlayerController>(GetController()))
			if (auto* H = Cast<AVerdantHUD>(PC->GetHUD()))
				if (H->bConversationOpen)
				{
					H->CloseConversation();
					ConversationInterrupted = true;
				}
	const float ActualDamage = FMath::Min(Health, Damage);
	if (auto* W = Valley())W->PlayCue(TEXT("Hit"), GetActorLocation(), From->BodySize);
	Health = FMath::Max(0.f, Health - Damage);
	HurtTimer = .3f;
	++IntentRevision;
	HostileId = From->AgentId;
	CombatTarget = From;
	Trust.FindOrAdd(From->AgentId) -= 25;
	// Outside danger interrupts escorting; betrayal by the leader ends the agreement.
	if (FollowId == From->AgentId)
	{
		FollowId = -1;
		bProtectLeader = false;
	}
	const bool HeavyCommitted = bHeavyAttack && AttackTimer > 0;
	if (!HeavyCommitted)
	{
		AttackTimer = 0;
		bHitApplied = true;
		AnimationState = -1;
	}
	DodgeTimer = 0;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	const FVector Away = (GetActorLocation() - From->GetActorLocation()).GetSafeNormal2D();
	LaunchCharacter(Away * FMath::Min(300.f, 140.f * From->BodySize / FMath::Max(.4f, BodySize)) + FVector(0, 0, 45),
	                true, false);
	SetCombatFeedback(FString::Printf(TEXT("受击 -%.0f%s"), ActualDamage,
	                                  ConversationInterrupted ? TEXT(" · 交谈被打断") : TEXT("")));
	if (Health <= 0)
	{
		DeathPosition = GetActorLocation();
		DeathHeightCm = HeightCm();
		DeathFoodTier = FoodTier();
		++LifeRevision;
		++IntentRevision;
		bDead = true;
		AttackTimer = 0;
		bHitApplied = true;
		DodgeTimer = 0;
		bSprinting = false;
		GetCharacterMovement()->StopMovementImmediately();
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Nutrition = 0.f;
		GrowthClock = 0.f;
		bHarvestHeld = false;
		if (AgentId == 0)
		{
			RespawnClock = VerdantEconomy::RespawnDelay;
			GetCharacterMovement()->DisableMovement();
		}
		if (auto* W = Valley())
			W->Record(
				AgentId == 0
					? FString::Printf(TEXT("%s 被 %s 击倒，正在缩小后复苏。"), *DisplayName, *From->DisplayName)
					: FString::Printf(TEXT("%s 击败了 %s，留下 %.0f 份第%d档食物。"), *From->DisplayName, *DisplayName, Food,
					                  FoodTier()));
		AnimationState = -1;
	}
	UpdateAnimation();
}

bool AVerdantCharacter::Eat(float Amount)
{
	if (AgentId == 0)
	{
		if (auto* W = Valley()) W->
			Notify(FString::Printf(TEXT("食物每10秒自动转化成长，距离下一次 %.0f 秒。"), GrowthSecondsRemaining()));
		return false;
	}
	// NPCs keep their spawn size. Eating is only a paid emergency recovery action.
	if (bDead || !FMath::IsFinite(Amount) || Amount <= 0.f || Food <= 0.f || Health >= MaxHealth() - .001f) return
		false;
	const float Paid = FMath::Min3(Amount, Food, (MaxHealth() - Health) / 1.9f);
	Food -= Paid;
	Health = FMath::Min(MaxHealth(), Health + Paid * 1.9f);
	Nutrition = 0.f;
	return Paid > .001f;
}

bool AVerdantCharacter::IsDodgeInvulnerable() const
{
	const float Elapsed = DodgeActiveDuration - DodgeTimer;
	return !bDead && DodgeTimer > 0 && Elapsed >= DodgeInvulnerableStart && Elapsed < DodgeInvulnerableEnd;
}

void AVerdantCharacter::Dodge()
{
	StopHarvest();
	if (bDead || DodgeTimer > 0 || DodgeRecoveryTimer > 0 || HurtTimer > 0 || AttackTimer > .25f)return;
	if (!GetCharacterMovement()->IsMovingOnGround())
	{
		if (AgentId == 0)SetCombatFeedback(TEXT("站稳后再闪避"), .55f);
		return;
	}
	QueuedAttackType = -1;
	AttackInputBuffer = 0;
	DodgeDirection = GetPendingMovementInputVector().GetSafeNormal2D();
	if (DodgeDirection.IsNearlyZero())DodgeDirection = GetLastMovementInputVector().GetSafeNormal2D();
	if (DodgeDirection.IsNearlyZero())DodgeDirection = GetActorForwardVector();
	ConsumeMovementInputVector();
	DodgeTimer = DodgeActiveDuration;
	DodgeRecoveryTimer = DodgeRepeatDelay;
	AttackTimer = 0;
	bHitApplied = true;
	AnimationState = -1;
	LockedActionYaw = DodgeDirection.Rotation().Yaw;
	SetActorRotation(FRotator(0, LockedActionYaw, 0));
	GetCharacterMovement()->bOrientRotationToMovement = false;
	const float Speed = AgentId == 0
		                    ? VerdantPlayerScale::MovementSpeed(BodySize, 630.f, .35f)
		                    : 630.f * FMath::Pow(BodySize, .35f);
	GetCharacterMovement()->Velocity = DodgeDirection * Speed;
	SetCombatFeedback(TEXT("翻滚 · 留意收势空隙"), .65f);
	if (auto* W = Valley()) W->PlayCue(TEXT("ClothDodge"), GetActorLocation(), BodySize);
	UpdateAnimation();
}

void AVerdantCharacter::CancelForConversation()
{
	StopHarvest();
	QueuedAttackType = -1;
	AttackInputBuffer = 0;
	AttackTimer = 0;
	bHitApplied = true;
	bSprinting = false;
	if (DodgeTimer > 0)DodgeRecoveryTimer = FMath::Max(DodgeRecoveryTimer, .25f);
	DodgeTimer = 0;
	ConsumeMovementInputVector();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->bOrientRotationToMovement = HurtTimer <= 0 && !bDead;
	AnimationState = -1;
	UpdateAnimation();
}

AVerdantCharacter* AVerdantCharacter::NearestFolk(float Radius) const
{
	AVerdantCharacter* Best = nullptr;
	float D = Radius;
	if (auto* W = Valley())
		for (AVerdantCharacter* Other : W->Folk)
			if (IsValid(Other) && Other != this && !Other->bDead)
			{
				const float Dist = FVector::Dist2D(GetActorLocation(), Other->GetActorLocation());
				if (Dist < D)
				{
					D = Dist;
					Best = Other;
				}
			}
	return Best;
}

void AVerdantCharacter::Interact()
{
	if (bDead || AttackTimer > 0.f || DodgeTimer > 0.f || HurtTimer > 0.f || ActionCooldown > 0.f) return;
	auto* W = Valley();
	if (!W) return;
	if (FreeFoodCapacity() <= .001f)
	{
		W->Notify(TEXT("食物已装满，等待自动成长腾出容量。"));
		return;
	}
	for (AVerdantCharacter* Other : W->Folk)
	{
		if (!IsValid(Other) || Other == this || Other->AgentId == 0 || !Other->bDead || Other->Food <= .001f) continue;
		if (FVector::Dist2D(GetActorLocation(), Other->GetActorLocation()) > 220.f * FMath::Max(1.f, BodySize)) continue
			;
		if (FoodTier() < Other->FoodTier())
		{
			W->Notify(FString::Printf(TEXT("这是第%d档掉落，需要至少 %.2f 米身高。"), Other->FoodTier(),
			                          VerdantEconomy::MinimumHeight(Other->FoodTier()) / 100.f));
			continue;
		}
		if (W->LootCorpse(Other, this) > .001f)
		{
			ActionCooldown = .4f;
			return;
		}
	}
	if (W->CollectFood(this, FMath::Max(180.f, 250.f * BodySize)) > .001f) ActionCooldown = .45f;
}

void AVerdantCharacter::Contribute()
{
	if (bDead || ActionCooldown > 0)return;
	auto* W = Valley();
	if (!W)return;
	if (FVector::Dist2D(GetActorLocation(), W->MotherPosition) < 1000.f)
	{
		auto* PC = Cast<APlayerController>(GetController());
		const bool Bulk = PC && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
		W->Donate(this, Bulk ? Food : 5.f);
	}
	else if (auto* Other = NearestFolk(400.f * FMath::Max(1.f, BodySize)))
	{
		const float Before = Food;
		if (W->Transfer(this, Other, 5.f))
		{
			const float Paid = Before - Food;
			float& Goodwill = Other->Trust.FindOrAdd(AgentId);
			// A fractional gift earns fractional trust; gifts alone cannot buy unbounded loyalty.
			if (Goodwill < 12.f)Goodwill = FMath::Min(12.f, Goodwill + Paid * 2.4f);
			Other->Say(TEXT("这份食物我会记得。"));
		}
	}
	else W->Notify(TEXT("靠近母树按 F 供奉；靠近居民按 F 分享 5 份食物。"));
	ActionCooldown = .4f;
}

void AVerdantCharacter::Talk()
{
	if (bDead)return;
	if (auto* Other = NearestFolk(VerdantEconomy::HearingRadiusCm))
	{
		if (auto* PC = Cast<APlayerController>(GetController()))
			if (auto* H = Cast<AVerdantHUD>(PC->GetHUD()))
				H->
					OpenConversation(Other);
	}
	else if (auto* W = Valley()) W->Notify(TEXT("靠近一位居民，按 T 自由交谈。"));
}

void AVerdantCharacter::ToggleClaim()
{
	if (bDead)return;
	auto* W = Valley();
	if (!W)return;
	int32 I = W->NearestGrove(GetActorLocation(), 700.f);
	if (I < 0)
	{
		W->Notify(TEXT("在果林范围内按 G 宣告管理；再次按 G 切换开放采集。"));
		return;
	}
	if (W->Groves[I].ControllerId == AgentId)
	{
		const auto& G = W->Groves[I];
		if (!G.bOpen)W->SetAccess(I, this, true, 0.f);
		else if (G.Share < .001f)W->SetAccess(I, this, true, .3f);
		else W->SetAccess(I, this, false, 0.f);
	}
	else W->Claim(I, this);
}

void AVerdantCharacter::TendGrove()
{
	if (bDead || ActionCooldown > 0 || AttackTimer > 0 || DodgeTimer > 0 || HurtTimer > 0)return;
	auto* W = Valley();
	if (!W)return;
	const int32 I = W->NearestGrove(GetActorLocation(), 700.f);
	if (I < 0)
	{
		W->Notify(TEXT("靠近可以采集的果林，按 B 用食物养护土地。"));
		return;
	}
	if (!W->CanHarvest(I, this))
	{
		W->Notify(TEXT("先获得这里的采集许可，再参与养护。"));
		return;
	}
	if (W->Tend(I, this))ActionCooldown = .7f;
}

void AVerdantCharacter::Say(const FString& Line)
{
	LastSpeech = Line;
	SpeechUntil = GetWorld()->GetTimeSeconds() + 8.f;
}

void AVerdantCharacter::RestartJourney() { UGameplayStatics::OpenLevel(this, FName(TEXT("Valley"))); }

void AVerdantCharacter::MoveForward(float V)
{
	if (!bDead && DodgeTimer <= 0 && HurtTimer <= 0 && Controller && V != 0)
		AddMovementInput(
			FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::X), V);
}

void AVerdantCharacter::MoveRight(float V)
{
	if (!bDead && DodgeTimer <= 0 && HurtTimer <= 0 && Controller && V != 0)
		AddMovementInput(
			FRotationMatrix(FRotator(0, Controller->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), V);
}

void AVerdantCharacter::LookYaw(float V) { AddControllerYawInput(V); }
void AVerdantCharacter::LookPitch(float V) { AddControllerPitchInput(V); }

void AVerdantCharacter::StartHarvest()
{
	bHarvestHeld = true;
	HarvestRepeatTimer = .7f;
	Interact();
}

void AVerdantCharacter::StopHarvest()
{
	bHarvestHeld = false;
	HarvestRepeatTimer = 0;
}

void AVerdantCharacter::RequestPlayerAttack(bool Heavy)
{
	if (bDead || DodgeTimer > 0 || HurtTimer > 0)return;
	if (AttackTimer > 0)
	{
		// One late click is remembered, never an indefinitely repeating attack.
		if (AttackTimer <= .18f)
		{
			QueuedAttackType = Heavy ? 1 : 0;
			AttackInputBuffer = .24f;
		}
		return;
	}
	Attack(Heavy);
}

void AVerdantCharacter::LightAttack() { RequestPlayerAttack(false); }
void AVerdantCharacter::HeavyAttack() { RequestPlayerAttack(true); }
void AVerdantCharacter::EatInput() { Eat(5.f); }
void AVerdantCharacter::SprintStart() { bSprinting = true; }
void AVerdantCharacter::SprintStop() { bSprinting = false; }

void AVerdantCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	Input->BindAxis("MoveForward", this, &ThisClass::MoveForward);
	Input->BindAxis("MoveRight", this, &ThisClass::MoveRight);
	Input->BindAxis("Turn", this, &ThisClass::LookYaw);
	Input->BindAxis("LookUp", this, &ThisClass::LookPitch);
	Input->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ThisClass::LightAttack);
	Input->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ThisClass::HeavyAttack);
	Input->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ThisClass::Dodge);
	Input->BindKey(EKeys::E, IE_Pressed, this, &ThisClass::StartHarvest);
	Input->BindKey(EKeys::E, IE_Released, this, &ThisClass::StopHarvest);
	Input->BindKey(EKeys::Q, IE_Pressed, this, &ThisClass::EatInput);
	Input->BindKey(EKeys::F, IE_Pressed, this, &ThisClass::Contribute);
	Input->BindKey(EKeys::T, IE_Pressed, this, &ThisClass::Talk);
	Input->BindKey(EKeys::G, IE_Pressed, this, &ThisClass::ToggleClaim);
	Input->BindKey(EKeys::B, IE_Pressed, this, &ThisClass::TendGrove);
	Input->BindKey(EKeys::R, IE_Pressed, this, &ThisClass::RestartJourney);
	Input->BindKey(EKeys::LeftShift, IE_Pressed, this, &ThisClass::SprintStart);
	Input->BindKey(EKeys::LeftShift, IE_Released, this, &ThisClass::SprintStop);
#if !UE_BUILD_SHIPPING
	VerdantScaleTest::Bind(Input, this);
#endif
}
