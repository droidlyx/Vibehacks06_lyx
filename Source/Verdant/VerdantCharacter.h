#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VerdantCharacter.generated.h"
class AVerdantWorld;
class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UAnimSequence;
UCLASS()
class VERDANT_API AVerdantCharacter : public ACharacter {
 GENERATED_BODY()
public:
 AVerdantCharacter();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
 UPROPERTY() TObjectPtr<USpringArmComponent> CameraArm;
 UPROPERTY() TObjectPtr<UCameraComponent> Camera;
 UPROPERTY() TObjectPtr<USceneComponent> FallbackBody;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;
 UPROPERTY() TObjectPtr<UAnimSequence> IdleAnimation;
 UPROPERTY() TObjectPtr<UAnimSequence> WalkAnimation;
 UPROPERTY() TObjectPtr<UAnimSequence> RunAnimation;
 UPROPERTY() TObjectPtr<UAnimSequence> AttackAnimation;
 UPROPERTY() TObjectPtr<UAnimSequence> HitAnimation;
 UPROPERTY() TObjectPtr<UAnimSequence> DeathAnimation;
 UPROPERTY(EditAnywhere) int32 AgentId = 0;
 UPROPERTY(EditAnywhere) FString DisplayName = TEXT("旅人");
 UPROPERTY(EditAnywhere) FString Motivation;
 UPROPERTY(EditAnywhere) float BodySize = .45f;
 UPROPERTY(EditAnywhere) FLinearColor BodyColor = FLinearColor(.25f,.65f,.45f);
 UPROPERTY(EditAnywhere) int32 Outfit = 0;
 UPROPERTY(EditAnywhere) bool bWild = false;
 float Health = 100.f;
 float Food = 8.f;
 float Nutrition = 0.f;
 float AttackTimer = 0.f;
 float DodgeTimer = 0.f;
 float HurtTimer = 0.f;
 float ActionCooldown = 0.f;
 bool bDead = false;
 bool bHeavyAttack = false;
 bool bHitApplied = false;
 bool bSprinting = false;
 int32 HomeGrove = -1;
 int32 FollowId = -1;
 int32 HostileId = -1;
 float SharePledge = 0.f;
 float DonationPledge = 0.f;
 float BrainClock = 0.f;
 float ReconsiderAt = 0.f;
 FVector Destination = FVector::ZeroVector;
 FString ActionLabel;
 FString LastSpeech;
 float SpeechUntil = 0.f;
 TMap<int32, float> Trust;
 TWeakObjectPtr<AVerdantCharacter> CombatTarget;
 float MaxHealth() const;
 float Strength() const;
 float Reach() const;
 bool Attack(bool Heavy = false);
 void ReceiveStrike(AVerdantCharacter* From, float Damage);
 bool Eat(float Amount = 5.f);
 void Dodge();
 void Interact();
 void Contribute();
 void Talk();
 void ToggleClaim();
 void RestartJourney();
 void SetSize(float NewSize);
 void Say(const FString& Line);
 AVerdantCharacter* NearestFolk(float Radius = 900.f) const;
 AVerdantWorld* Valley() const;
 void TickNPC(float Dt);
 void MoveToward(const FVector& Goal, float Acceptance = 180.f);
 void UpdateAppearance();
 void UpdateAnimation();
private:
 void MoveForward(float Value);
 void MoveRight(float Value);
 void LookYaw(float Value);
 void LookPitch(float Value);
 void LightAttack();
 void HeavyAttack();
 void EatInput();
 void SprintStart();
 void SprintStop();
 void ApplyStrike();
 void BuildFallbackBody();
 int32 AnimationState = -1;
 float AppliedSize = 0.f;
};
