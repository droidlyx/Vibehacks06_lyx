#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VerdantCharacter.generated.h"
class AVerdantWorld;
class AVerdantFoodPickup;
class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UAnimSequence;

UCLASS()
class VERDANT_API AVerdantCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AVerdantCharacter();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
	UPROPERTY()
	TObjectPtr<USpringArmComponent> CameraArm;
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY()
	TObjectPtr<USceneComponent> FallbackBody;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;
	UPROPERTY()
	TObjectPtr<UAnimSequence> IdleAnimation;
	UPROPERTY()
	TObjectPtr<UAnimSequence> WalkAnimation;
	UPROPERTY()
	TObjectPtr<UAnimSequence> RunAnimation;
	UPROPERTY()
	TObjectPtr<UAnimSequence> AttackAnimation;
	UPROPERTY()
	TObjectPtr<UAnimSequence> HitAnimation;
	UPROPERTY()
	TObjectPtr<UAnimSequence> DeathAnimation;
	UPROPERTY()
	TObjectPtr<UAnimSequence> DodgeAnimation;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> CarriedNutrition;
	UPROPERTY(EditAnywhere)
	int32 AgentId = 0;
	UPROPERTY(EditAnywhere)
	FString DisplayName = TEXT("旅人");
	UPROPERTY(EditAnywhere)
	FString Motivation;
	UPROPERTY(EditAnywhere)
	float BodySize = .45f;
	UPROPERTY(EditAnywhere)
	FLinearColor BodyColor = FLinearColor(.25f, .65f, .45f);
	UPROPERTY(EditAnywhere)
	int32 Outfit = 0;
	UPROPERTY(EditAnywhere)
	bool bWild = false;
	float Health = 100.f;
	float Food = 8.f;
	float Nutrition = 0.f;
	// Stable instance data used by population, dialogue and the rule-based brain.
	FString CharacterTypeId;
	FString Persona;
	TArray<FString> SocialTags;
	FString ModelPath, IdlePath, WalkPath;
	FVector HomePosition = FVector::ZeroVector;
	int32 LifeRevision = 0;
	int32 IntentRevision = 0;
	bool bProtectLeader = false;
	float SocialActionCooldown = 0.f;
	float AttackTimer = 0.f;
	float AttackDuration = .55f;
	int32 QueuedAttackType = -1;
	float AttackInputBuffer = 0.f;
	float DodgeTimer = 0.f;
	float DodgeRecoveryTimer = 0.f;
	static constexpr float DodgeActiveDuration = .60f;
	static constexpr float DodgeInvulnerableStart = .04f;
	static constexpr float DodgeInvulnerableEnd = .34f;
	static constexpr float DodgeRepeatDelay = .95f;
	float HurtTimer = 0.f;
	float ActionCooldown = 0.f;
	bool bDead = false;
	bool bHeavyAttack = false;
	bool bHitApplied = false;
	bool bSprinting = false;
	bool bHarvestHeld = false;
	float HarvestRepeatTimer = 0.f;
	int32 HomeGrove = -1;
	int32 FollowId = -1;
	int32 HostileId = -1;
	float SharePledge = 0.f;
	float DonationPledge = 0.f;
	// Local intention and movement recovery; no teleporting or remote fulfilment.
	int32 AvoidThreatId = -1;
	float AvoidThreatUntil = 0.f;
	float MoveSampleAt = 0.f;
	FVector MoveSamplePosition = FVector::ZeroVector;
	FVector MoveSampleGoal = FVector::ZeroVector;
	float DetourUntil = 0.f;
	FVector DetourPoint = FVector::ZeroVector;
	int32 MoveRecoveryCount = 0;
	int32 AvoidGrove = -1;
	float AvoidGroveUntil = 0.f;
	// Short-lived routes and failed food targets belong to this NPC, not the world.
	TArray<FVector> LocalMovePath;
	FVector LocalMoveGoal = FVector::ZeroVector;
	FVector MoveSteeringDirection = FVector::ZeroVector;
	float MoveSteeringScale = 1.f;
	float MoveSteeringAt = 0.f;
	float MovePathSearchAt = 0.f;
	float MoveAcceptance = 0.f;
	int32 MoveIntentRevision = -1;
	TWeakObjectPtr<AVerdantFoodPickup> FoodTarget;
	TMap<TWeakObjectPtr<AVerdantFoodPickup>, float> AvoidFoodUntil;
	float FoodTargetDeadline = 0.f;
	float FoodAttemptAt = 0.f;
	int32 FoodCollectionFailures = 0;
	float NextTendAt = 0.f;
	float BrainClock = 0.f;
	float ReconsiderAt = 0.f;
	FVector Destination = FVector::ZeroVector;
	FString ActionLabel;
	FString LastSpeech;
	float SpeechStartedAt = 0.f;
	float SpeechUntil = 0.f;
	FString CombatFeedback;
	float FeedbackUntil = 0.f;
	float LastDonationTime = -1.f;
	float LastDonatedAmount = 0.f;
	TMap<int32, float> Trust;
	TWeakObjectPtr<AVerdantCharacter> CombatTarget;
	float MaxHealth() const;
	float Strength() const;
	float Reach() const;
	float HeightCm() const;
	int32 FoodTier() const;
	float FoodCapacity() const;
	float FreeFoodCapacity() const;
	float GrowthSecondsRemaining() const;
	float RespawnSecondsRemaining() const;
	bool IsRespawnProtected() const;
	bool Attack(bool Heavy = false);
	void ReceiveStrike(AVerdantCharacter* From, float Damage);
	bool Eat(float Amount = 5.f);
	void Dodge();
	bool IsDodgeInvulnerable() const;
	void CancelForConversation();
	void Interact();
	void Contribute();
	void Talk();
	void ToggleClaim();
	void TendGrove();
	void RestartJourney();
	void SetSize(float NewSize);
	void Say(const FString& Line);
	void BeginDialogueReply(const FString& Line, AVerdantCharacter* FocusPlayer);
	bool IsDialogueReplyActive() const;
	AVerdantCharacter* NearestFolk(float Radius = 900.f) const;
	AVerdantWorld* Valley() const;
	void TickNPC(float Dt);
	void MoveToward(const FVector& Goal, float Acceptance = 180.f);
	void UpdateAppearance();
	void UpdateAnimation();

private:
	void UpdateDialogueReplyPose();
	bool bDialogueReplyActive = false;
	TWeakObjectPtr<AVerdantCharacter> DialogueReplyFocus;
	void MoveForward(float Value);
	void MoveRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void StartHarvest();
	void StopHarvest();
	void RequestPlayerAttack(bool Heavy);
	void LightAttack();
	void HeavyAttack();
	void EatInput();
	void SprintStart();
	void SprintStop();
	void ApplyStrike();
	void BuildFallbackBody();
	void UpdateCameraFraming(float DeltaSeconds, bool bSnap = false);
	void SetCombatFeedback(const FString& Message, float Duration = .9f);
	float LockedActionYaw = 0.f;
	FVector DodgeDirection = FVector::ForwardVector;
	int32 AnimationState = -1;
	float AppliedSize = 0.f;
	float GrowthClock = 0.f;
	float RespawnClock = 0.f;
	float RespawnProtectionRemaining = 0.f;
	float DeathHeightCm = 0.f;
	int32 DeathFoodTier = 1;
	FVector DeathPosition = FVector::ZeroVector;
	void TickAutomaticGrowth(float DeltaSeconds);
	void RespawnPlayer();
};
