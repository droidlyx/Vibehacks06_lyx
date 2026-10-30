#include "VerdantCharacter.h"
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

AVerdantCharacter::AVerdantCharacter() {
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
 Move->SetWalkableFloorAngle(48.f);
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
 GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
}
void AVerdantCharacter::BeginPlay() {
 Super::BeginPlay();
 const TCHAR* Names[] = {TEXT("SK_Traveler"),TEXT("SK_Gatherer"),TEXT("SK_Keeper")};
 const FString Name = Names[FMath::Clamp(Outfit,0,2)];
 const FString Path = TEXT("/Game/Art/Characters/")+Name+TEXT(".")+Name;
 if (auto* Skin = LoadObject<USkeletalMesh>(nullptr,*Path)) GetMesh()->SetSkeletalMeshAsset(Skin);
 auto Animation = [](const TCHAR* Name) { return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Art/Characters/Animations/%s.%s"),Name,Name)); };
 IdleAnimation = Animation(TEXT("A_Idle")); WalkAnimation = Animation(TEXT("A_Walk"));
 RunAnimation = Animation(TEXT("A_Run")); AttackAnimation = Animation(TEXT("A_Attack"));
 HitAnimation = Animation(TEXT("A_Hit")); DeathAnimation = Animation(TEXT("A_Death"));
 DodgeAnimation = Animation(TEXT("A_Dodge"));
 CarriedNutrition->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
 if (!GetMesh()->GetSkeletalMeshAsset()) BuildFallbackBody();
 AppliedSize = 0;
 SetSize(BodySize);
 Health = MaxHealth();
 if (AgentId == 0) {
  if (auto* PC = Cast<APlayerController>(GetController())) {
   PC->SetControlRotation(FRotator(-14,90,0));
   if (PC->PlayerCameraManager) {
    PC->PlayerCameraManager->ViewPitchMin = -65;
    PC->PlayerCameraManager->ViewPitchMax = 40;
   }
   PC->SetInputMode(FInputModeGameOnly());
   PC->bShowMouseCursor = false;
  }
 }
 UpdateAppearance();
 UpdateCameraFraming(0.f,true);
 UpdateAnimation();
}
void AVerdantCharacter::BuildFallbackBody() {
 auto* Sphere = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
 auto Part = [&](const TCHAR* Name,FVector Position,FVector Scale) {
  auto* C = NewObject<UStaticMeshComponent>(this,Name);
  AddInstanceComponent(C);
  C->SetupAttachment(FallbackBody); C->SetStaticMesh(Sphere);
  C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  C->SetRelativeLocation(Position); C->SetRelativeScale3D(Scale);
  C->RegisterComponent(); BodyParts.Add(C);
 };
 Part(TEXT("Torso"),FVector(0,0,95),FVector(.48,.37,.68));
 Part(TEXT("Head"),FVector(0,0,150),FVector(.42,.42,.46));
 Part(TEXT("ArmL"),FVector(0,-32,95),FVector(.18,.18,.65));
 Part(TEXT("ArmR"),FVector(0,32,95),FVector(.18,.18,.65));
 Part(TEXT("LegL"),FVector(0,-13,33),FVector(.22,.23,.65));
 Part(TEXT("LegR"),FVector(0,13,33),FVector(.22,.23,.65));
}
float AVerdantCharacter::MaxHealth() const { return 100.f*FMath::Pow(FMath::Max(.3f,BodySize),1.35f); }
float AVerdantCharacter::Strength() const { return 22.f*FMath::Pow(FMath::Max(.3f,BodySize),1.3f); }
float AVerdantCharacter::Reach() const { return FMath::Max(95.f,170.f*BodySize); }
AVerdantWorld* AVerdantCharacter::Valley() const { return AVerdantWorld::Find(this); }
void AVerdantCharacter::SetSize(float NewSize) {
 NewSize = FMath::Clamp(NewSize,.35f,2.5f);
 // Preserve the world-space foot position. Initial spawns use the constructor
 // capsule height; all later growth adjusts height without lifting the feet.
 const float ShapeScale = GetCapsuleComponent()->GetShapeScale();
 const float OldHalf = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
 const float NewHalf = 90.f*NewSize;
 const FVector NewCenter = GetActorLocation()+FVector(0,0,NewHalf*ShapeScale-OldHalf);
 if (AppliedSize > 0 && NewSize > AppliedSize && GetWorld()) {
  FCollisionQueryParams Q(SCENE_QUERY_STAT(VerdantGrowth),false,this);
  if (GetWorld()->OverlapBlockingTestByChannel(NewCenter,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(31.f*NewSize*ShapeScale,(NewHalf-2.f)*ShapeScale),Q)) return;
 }
 const float HealthFraction = Health/FMath::Max(1.f,MaxHealth());
 BodySize = NewSize;
 GetCapsuleComponent()->SetCapsuleSize(32.f*BodySize,NewHalf,true);
 SetActorLocation(NewCenter,false);
 GetCharacterMovement()->bForceNextFloorCheck = true;
 Health = FMath::Clamp(HealthFraction,0.f,1.f)*MaxHealth();
 GetMesh()->SetRelativeLocation(FVector(0,0,-NewHalf));
 GetMesh()->SetRelativeScale3D(FVector(BodySize));
 FallbackBody->SetRelativeLocation(FVector(0,0,-NewHalf));
 FallbackBody->SetRelativeScale3D(FVector(BodySize));
 CarriedNutrition->SetRelativeLocation(FVector(43.f,0,20.f)*BodySize);
 CarriedNutrition->SetRelativeScale3D(FVector(.32f*BodySize));
 GetCharacterMovement()->MaxStepHeight = FMath::Min(90.f,42.f*BodySize);
 GetCharacterMovement()->JumpZVelocity = 400.f*FMath::Sqrt(BodySize);
 AppliedSize = BodySize;
}
void AVerdantCharacter::Tick(float Dt) {
 Super::Tick(Dt);
 ActionCooldown = FMath::Max(0.f,ActionCooldown-Dt);
 HarvestRepeatTimer = FMath::Max(0.f,HarvestRepeatTimer-Dt);
 if(bHarvestHeld && !bDead && AttackTimer<=0 && DodgeTimer<=0 && HurtTimer<=0 && HarvestRepeatTimer<=0) { Interact(); HarvestRepeatTimer=.7f; }
 HurtTimer = FMath::Max(0.f,HurtTimer-Dt);
 DodgeTimer = FMath::Max(0.f,DodgeTimer-Dt);
 DodgeRecoveryTimer = FMath::Max(0.f,DodgeRecoveryTimer-Dt);
 AttackInputBuffer=FMath::Max(0.f,AttackInputBuffer-Dt);
 if(AttackInputBuffer<=0)QueuedAttackType=-1;
 if (GetWorld()->GetTimeSeconds()>FeedbackUntil) CombatFeedback.Empty();
 if (AttackTimer > 0) {
  SetActorRotation(FRotator(0,LockedActionYaw,0));
  const float Prior = AttackTimer; AttackTimer = FMath::Max(0.f,AttackTimer-Dt);
  const float StrikeAt = AttackDuration*.40f; // Contact at 60% of this attack's clock.
  if (!bHitApplied && Prior > StrikeAt && AttackTimer <= StrikeAt) ApplyStrike();
 }
 if (!bDead) {
  if(QueuedAttackType>=0 && AttackTimer<=0 && DodgeTimer<=0 && HurtTimer<=0) {
   const bool Heavy=QueuedAttackType==1;QueuedAttackType=-1;AttackInputBuffer=0;Attack(Heavy);
  }
  if (Nutrition > 0) {
   const float Used = FMath::Min(Nutrition,2.2f*Dt);
   const float Prior = BodySize;
   const bool AtGrowthLimit = BodySize >= 2.5f;
   if (!AtGrowthLimit) SetSize(BodySize+Used*.013f/FMath::Max(.6f,BodySize*.7f));
   // Below the cap, a blocked growth attempt keeps its nutrition. At the cap,
   // already-eaten nutrition still digests at the same rate and restores health.
   if (AtGrowthLimit || BodySize > Prior) {
    Nutrition = FMath::Max(0.f,Nutrition-Used);
    Health = FMath::Min(MaxHealth(),Health+Used*.5f);
    if (AgentId==0 && Prior<2.5f && BodySize>=2.5f)
     if(auto* W=Valley())W->Notify(TEXT("已达到最大体型，剩余养分继续吸收并恢复生命。"));
   }
  }
  GetCharacterMovement()->MaxWalkSpeed = (bSprinting?640.f:430.f)*FMath::Pow(BodySize,.32f)*(AttackTimer>0?.35f:1.f);
  if (AgentId != 0 && DodgeTimer<=0 && (HurtTimer<=0 || AttackTimer>0)) TickNPC(Dt);
  if (DodgeTimer>0) {
   const float Phase=1.f-DodgeTimer/DodgeActiveDuration;
   const float Speed=630.f*FMath::Pow(BodySize,.35f)*FMath::Lerp(1.f,.25f,Phase);
   auto* Move=GetCharacterMovement();
   Move->MaxWalkSpeed=Speed;
   // CharacterMovement resolves collision and slopes; no transform teleport during the roll.
   Move->Velocity=FVector(DodgeDirection.X*Speed,DodgeDirection.Y*Speed,Move->Velocity.Z);
  }
  if (GetActorLocation().Z < -8000) { if (auto* W=Valley()) SetActorLocation(W->SurfacePoint(FVector(0,-7000,0),110*BodySize)); }
 }
 GetCharacterMovement()->bOrientRotationToMovement = !bDead && AttackTimer<=0 && DodgeTimer<=0 && HurtTimer<=0;
 if (AttackTimer>0 || DodgeTimer>0) SetActorRotation(FRotator(0,LockedActionYaw,0));
 CarriedNutrition->SetVisibility(!bDead && DonationPledge>.001f && Food>.001f);
 UpdateCameraFraming(Dt);
 UpdateAnimation();
}
void AVerdantCharacter::UpdateCameraFraming(float Dt, bool bSnap) {
 if (!Camera || !CameraArm) return;
 const float Blend=bSnap?1.f:1.f-FMath::Exp(-5.f*FMath::Max(0.f,Dt));
 Camera->SetFieldOfView(FMath::Lerp(Camera->FieldOfView,bSprinting?78.f:72.f,Blend));
 int32 Width=1600, Height=900;
 if (auto* PC=Cast<APlayerController>(GetController())) {
  int32 W=0,H=0; PC->GetViewportSize(W,H);
  if(W>0&&H>0){Width=W;Height=H;}
 }
 const float Aspect=float(Width)/FMath::Max(1,Height);
 const float TanVerticalHalf=FMath::Max(.1f,FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView*.5f))/Aspect);
 const float BodyHeight=GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2.f;
 const float Chest=BodyHeight*.62f;
 const FVector AimOffset(0,0,Chest-BodyHeight*.5f);
 CameraArm->TargetOffset=FMath::Lerp(CameraArm->TargetOffset,AimOffset,Blend);
 CameraArm->SocketOffset=FMath::Lerp(CameraArm->SocketOffset,FVector(0,18.f*BodySize,0),Blend);
 const float Pitch=FMath::DegreesToRadians(Controller?Controller->GetControlRotation().Pitch:CameraArm->GetComponentRotation().Pitch);
 const float CosPitch=FMath::Abs(FMath::Cos(Pitch)), SinPitch=FMath::Sin(Pitch);
 // Projection constraints keep feet above the bottom HUD and leave headroom.
 const float BottomReserve=FMath::Clamp(190.f/float(Height),.10f,.30f);
 const float BottomNDC=1.f-2.f*BottomReserve;
 const float TopNDC=.80f;
 const float FeetDistance=Chest*CosPitch/(TanVerticalHalf*BottomNDC)+Chest*SinPitch;
 const float HeadDistance=(BodyHeight-Chest)*CosPitch/(TanVerticalHalf*TopNDC)-(BodyHeight-Chest)*SinPitch;
 const float FramingMinimum=FMath::Max(FeetDistance,HeadDistance)+18.f;
 const float Desired=FMath::Max3(280.f,320.f*FMath::Pow(BodySize,.65f),FramingMinimum);
 CameraArm->TargetArmLength=FMath::Max(FramingMinimum,FMath::Lerp(CameraArm->TargetArmLength,Desired,Blend));
 CameraArm->CameraLagMaxDistance=FMath::Clamp(30.f+20.f*BodySize,35.f,80.f);
}
void AVerdantCharacter::SetCombatFeedback(const FString& Message,float Duration) {
 CombatFeedback=Message;
 FeedbackUntil=GetWorld()?GetWorld()->GetTimeSeconds()+Duration:0.f;
}
void AVerdantCharacter::UpdateAppearance() {
 if (auto* W=Valley()) {
  for (int32 I=0;I<BodyParts.Num();++I)
   BodyParts[I]->SetMaterial(0,W->ColoredMaterial(I==1?FLinearColor(.72,.53,.34):BodyColor));
  if(CarriedNutrition) CarriedNutrition->SetMaterial(0,W->ColoredMaterial(FLinearColor(1.f,.61f,.17f),.8f));
 }
}
void AVerdantCharacter::UpdateAnimation() {
 const float Speed=GetVelocity().Size2D();
 const bool HeavyCommitted=bHeavyAttack&&AttackTimer>0;
 const int32 State=bDead?5:DodgeTimer>0?6:HurtTimer>0&&!HeavyCommitted?4:AttackTimer>0?3:Speed>450*FMath::Pow(BodySize,.32f)?2:Speed>15?1:0;
 UAnimSequence* A=State==6?DodgeAnimation:State==5?DeathAnimation:State==3?AttackAnimation:State==4?HitAnimation:State==2?RunAnimation:State==1?WalkAnimation:IdleAnimation;
 if (State!=AnimationState) {
  if (A) { GetMesh()->PlayAnimation(A,State<3); GetMesh()->SetPlayRate(State==3||State==4||State==6?0.f:1.f); }
  AnimationState=State;
 }
 // These poses visualize the authoritative action clock. A hit cannot restart
 // an attack clip while its damage clock continues from a different instant.
 if(A&&(State==3||State==4||State==6)) {
  const float Phase=State==3?1.f-AttackTimer/FMath::Max(.01f,AttackDuration):State==6?1.f-DodgeTimer/DodgeActiveDuration:1.f-HurtTimer/.3f;
  GetMesh()->SetPosition(FMath::Clamp(Phase,0.f,1.f)*A->GetPlayLength(),false);
 }
 if (BodyParts.Num()>=6 && GetWorld()) {
  const float Swing=FMath::Sin(GetWorld()->GetTimeSeconds()*9)*FMath::Min(25.f,Speed*.05f);
  BodyParts[2]->SetRelativeRotation(FRotator(Swing,0,0));
  BodyParts[3]->SetRelativeRotation(FRotator(AttackTimer>0?-70.f:-Swing,0,0));
  BodyParts[4]->SetRelativeRotation(FRotator(-Swing,0,0)); BodyParts[5]->SetRelativeRotation(FRotator(Swing,0,0));
 }
}
bool AVerdantCharacter::Attack(bool Heavy) {
 if (bDead || AttackTimer>0 || DodgeTimer>0 || HurtTimer>0) return false;
 StopHarvest();QueuedAttackType=-1;AttackInputBuffer=0;
 bHeavyAttack=Heavy; AttackDuration=Heavy?.85f:.55f; AttackTimer=AttackDuration;
 bHitApplied=false; AnimationState=-1;
 if(auto* W=Valley())W->PlayCue(TEXT("Swing"),GetActorLocation(),BodySize);
 LockedActionYaw=AgentId==0&&Controller?Controller->GetControlRotation().Yaw:GetActorRotation().Yaw;
 SetActorRotation(FRotator(0,LockedActionYaw,0));
 GetCharacterMovement()->bOrientRotationToMovement=false;
 UpdateAnimation();
 return true;
}
void AVerdantCharacter::ApplyStrike() {
 bHitApplied=true;
 auto* W=Valley(); if(!W || bDead) return;
 float TotalDamage=0.f;int32 Hits=0;bool Evaded=false;
 FString Defeated;
 for(AVerdantCharacter* Other:W->Folk) {
  if(!IsValid(Other) || Other==this || Other->bDead) continue;
  // Companions protect their leader; player attacks remain an unrestricted choice.
  if(AgentId!=0 && FollowId>=0 && (Other->AgentId==FollowId || Other->FollowId==FollowId)) continue;
  FVector Delta=Other->GetActorLocation()-GetActorLocation();
  const float Dist=Delta.Size2D();
  if(Dist>Reach()+Other->GetCapsuleComponent()->GetScaledCapsuleRadius()) continue;
  if(FMath::Abs(Delta.Z)>100.f*(BodySize+Other->BodySize)) continue;
  if(FVector::DotProduct(GetActorForwardVector(),Delta.GetSafeNormal2D())<.25f) continue;
  FHitResult Wall; FCollisionQueryParams Q(SCENE_QUERY_STAT(VerdantMelee),false,this);Q.AddIgnoredActor(Other);
  if(GetWorld()->LineTraceSingleByChannel(Wall,GetActorLocation(),Other->GetActorLocation(),ECC_Visibility,Q)) continue;
  const float Before=Other->Health;
  Evaded|=Other->IsDodgeInvulnerable();
  Other->ReceiveStrike(this,Strength()*(bHeavyAttack?1.8f:1.f));
  if(Other->Health<Before){TotalDamage+=Before-Other->Health;++Hits;}
  if(Other->bDead)Defeated=Other->DisplayName;
 }
 if(!Defeated.IsEmpty()) SetCombatFeedback(TEXT("击败 ")+Defeated+TEXT(" · E 拾取养分"),1.4f);
 else if(Hits>0) SetCombatFeedback(FString::Printf(TEXT("%s命中 · %.0f 伤害"),bHeavyAttack?TEXT("重击"):TEXT(""),TotalDamage));
 else SetCombatFeedback(Evaded?TEXT("对方闪开了"):TEXT("挥空"),.55f);
}
void AVerdantCharacter::ReceiveStrike(AVerdantCharacter* From,float Damage) {
 StopHarvest();
 if(bDead || !IsValid(From) || From==this || !FMath::IsFinite(Damage) || Damage<=0)return;
 if(IsDodgeInvulnerable()){SetCombatFeedback(TEXT("闪避成功"));return;}
 QueuedAttackType=-1;AttackInputBuffer=0;
 bool ConversationInterrupted=false;
 if(AgentId==0) if(auto* PC=Cast<APlayerController>(GetController()))
  if(auto* H=Cast<AVerdantHUD>(PC->GetHUD())) if(H->bConversationOpen){H->CloseConversation();ConversationInterrupted=true;}
 const float ActualDamage=FMath::Min(Health,Damage);
 if(auto* W=Valley())W->PlayCue(TEXT("Hit"),GetActorLocation(),From->BodySize);
 Health=FMath::Max(0.f,Health-Damage); HurtTimer=.3f; HostileId=From->AgentId; CombatTarget=From;
 Trust.FindOrAdd(From->AgentId)-=25;
 // Outside danger interrupts escorting; betrayal by the leader ends the agreement.
 if(FollowId==From->AgentId)FollowId=-1;
 const bool HeavyCommitted=bHeavyAttack&&AttackTimer>0;
 if(!HeavyCommitted) {
  AttackTimer=0; bHitApplied=true; AnimationState=-1;
 }
 DodgeTimer=0;
 GetCharacterMovement()->bOrientRotationToMovement=false;
 const FVector Away=(GetActorLocation()-From->GetActorLocation()).GetSafeNormal2D();
 LaunchCharacter(Away*FMath::Min(300.f,140.f*From->BodySize/FMath::Max(.4f,BodySize))+FVector(0,0,45),true,false);
 SetCombatFeedback(FString::Printf(TEXT("受击 -%.0f%s"),ActualDamage,ConversationInterrupted?TEXT(" · 交谈被打断"):TEXT("")));
 if(Health<=0) {
  bDead=true; AttackTimer=0; bHitApplied=true; DodgeTimer=0; bSprinting=false;
  GetCharacterMovement()->StopMovementImmediately();
  GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
  Food+=FMath::Min(12.f,Nutrition*.35f); Nutrition=0;
  if(auto* W=Valley()) W->Record(FString::Printf(TEXT("%s 击败了 %s，留下 %.0f 份养分。"),*From->DisplayName,*DisplayName,Food));
  AnimationState=-1;
 }
 UpdateAnimation();
}
bool AVerdantCharacter::Eat(float Amount) {
 if(bDead || Food<=0 || Nutrition>=30.f) return false;
 const bool AtGrowthLimit=BodySize>=2.5f;
 if(AtGrowthLimit && Health>=MaxHealth()-.001f) {
  if(AgentId==0)if(auto* W=Valley())W->Notify(TEXT("体型已达上限且生命充足，可以把食物分享或供奉。"));
  return false;
 }
 Amount=FMath::Min3(Amount,Food,30.f-Nutrition);
 if(Amount<=0) return false;
 Food-=Amount; Nutrition+=Amount;
 Health=FMath::Min(MaxHealth(),Health+Amount*1.4f);
 if(AgentId==0) if(auto* W=Valley()) {
  W->Notify(AtGrowthLimit?TEXT("养分用于恢复生命，剩余部分会继续吸收。"):TEXT("养分开始吸收，身体会逐渐长大。"));
  W->PlayCue(TEXT("Grow"),GetActorLocation(),BodySize);
 }
 return true;
}
bool AVerdantCharacter::IsDodgeInvulnerable() const {
 const float Elapsed=DodgeActiveDuration-DodgeTimer;
 return !bDead&&DodgeTimer>0&&Elapsed>=DodgeInvulnerableStart&&Elapsed<DodgeInvulnerableEnd;
}
void AVerdantCharacter::Dodge() {
 StopHarvest();
 if(bDead || DodgeTimer>0 || DodgeRecoveryTimer>0 || HurtTimer>0 || AttackTimer>.25f)return;
 if(!GetCharacterMovement()->IsMovingOnGround()) {
  if(AgentId==0)SetCombatFeedback(TEXT("站稳后再闪避"),.55f);
  return;
 }
 QueuedAttackType=-1;AttackInputBuffer=0;
 DodgeDirection=GetPendingMovementInputVector().GetSafeNormal2D();
 if(DodgeDirection.IsNearlyZero())DodgeDirection=GetLastMovementInputVector().GetSafeNormal2D();
 if(DodgeDirection.IsNearlyZero())DodgeDirection=GetActorForwardVector();
 ConsumeMovementInputVector();
 DodgeTimer=DodgeActiveDuration; DodgeRecoveryTimer=DodgeRepeatDelay;
 AttackTimer=0; bHitApplied=true; AnimationState=-1;
 LockedActionYaw=DodgeDirection.Rotation().Yaw;
 SetActorRotation(FRotator(0,LockedActionYaw,0));
 GetCharacterMovement()->bOrientRotationToMovement=false;
 const float Speed=630.f*FMath::Pow(BodySize,.35f);
 GetCharacterMovement()->Velocity=DodgeDirection*Speed;
 SetCombatFeedback(TEXT("翻滚 · 留意收势空隙"),.65f);
 UpdateAnimation();
}
void AVerdantCharacter::CancelForConversation() {
 StopHarvest();QueuedAttackType=-1;AttackInputBuffer=0;
 AttackTimer=0; bHitApplied=true; bSprinting=false;
 if(DodgeTimer>0)DodgeRecoveryTimer=FMath::Max(DodgeRecoveryTimer,.25f);
 DodgeTimer=0;
 ConsumeMovementInputVector();
 GetCharacterMovement()->StopMovementImmediately();
 GetCharacterMovement()->bOrientRotationToMovement=HurtTimer<=0&&!bDead;
 AnimationState=-1;
 UpdateAnimation();
}
AVerdantCharacter* AVerdantCharacter::NearestFolk(float Radius) const {
 AVerdantCharacter* Best=nullptr; float D=Radius;
 if(auto* W=Valley()) for(AVerdantCharacter* Other:W->Folk) if(IsValid(Other)&&Other!=this&&!Other->bDead) {
  const float Dist=FVector::Dist2D(GetActorLocation(),Other->GetActorLocation());
  if(Dist<D) {D=Dist;Best=Other;}
 }
 return Best;
}
void AVerdantCharacter::Interact() {
 if(bDead || AttackTimer>0 || DodgeTimer>0 || HurtTimer>0 || ActionCooldown>0) return;
 auto* W=Valley();if(!W)return;
 for(AVerdantCharacter* Other:W->Folk) if(IsValid(Other)&&Other->bDead&&Other->Food>0&&FVector::Dist2D(GetActorLocation(),Other->GetActorLocation())<220.f*FMath::Max(1.f,BodySize)) {
  const float Taken=Other->Food; Food+=Taken;Other->Food=0;
  W->PlayCue(TEXT("Pickup"),GetActorLocation(),BodySize);
  W->Record(FString::Printf(TEXT("%s 取走战利品：%.0f 份养分。"),*DisplayName,Taken)); ActionCooldown=.4f; return;
 }
 const int32 Index=W->NearestGrove(GetActorLocation(),700.f);
 if(Index>=0) { W->Harvest(Index,this,5.f);ActionCooldown=.45f; }
 else W->Notify(TEXT("靠近果林的发光植物按 E 采集；靠近倒下的生物可取走战利品。"));
}
void AVerdantCharacter::Contribute() {
 if(bDead || ActionCooldown>0)return;
 auto* W=Valley();if(!W)return;
 if(FVector::Dist2D(GetActorLocation(),W->MotherPosition)<1000.f) {
  auto* PC=Cast<APlayerController>(GetController());
  const bool Bulk=PC&&(PC->IsInputKeyDown(EKeys::LeftShift)||PC->IsInputKeyDown(EKeys::RightShift));
  W->Donate(this,Bulk?Food:5.f);
 }
 else if(auto* Other=NearestFolk(400.f*FMath::Max(1.f,BodySize))) {
  const float Before=Food;
  if(W->Transfer(this,Other,5.f)) {
   const float Paid=Before-Food;
   float& Goodwill=Other->Trust.FindOrAdd(AgentId);
   // A fractional gift earns fractional trust; gifts alone cannot buy unbounded loyalty.
   if(Goodwill<12.f)Goodwill=FMath::Min(12.f,Goodwill+Paid*2.4f);
   Other->Say(TEXT("这份食物我会记得。"));
  }
 }
 else W->Notify(TEXT("靠近母树按 F 供奉；靠近居民按 F 分享 5 份食物。"));
 ActionCooldown=.4f;
}
void AVerdantCharacter::Talk() {
 if(bDead)return;
 if(auto* Other=NearestFolk(750.f*FMath::Max(1.f,BodySize))) {
  if(auto* PC=Cast<APlayerController>(GetController())) if(auto* H=Cast<AVerdantHUD>(PC->GetHUD())) H->OpenConversation(Other);
 } else if(auto* W=Valley()) W->Notify(TEXT("靠近一位居民，按 T 自由交谈。"));
}
void AVerdantCharacter::ToggleClaim() {
 if(bDead)return;auto* W=Valley();if(!W)return;
 int32 I=W->NearestGrove(GetActorLocation(),700.f);
 if(I<0) {W->Notify(TEXT("在果林范围内按 G 宣告管理；再次按 G 切换开放采集。"));return;}
 if(W->Groves[I].ControllerId==AgentId) {
  const auto& G=W->Groves[I];
  if(!G.bOpen)W->SetAccess(I,this,true,0.f);
  else if(G.Share<.001f)W->SetAccess(I,this,true,.3f);
  else W->SetAccess(I,this,false,0.f);
 }
 else W->Claim(I,this);
}
void AVerdantCharacter::TendGrove() {
 if(bDead||ActionCooldown>0||AttackTimer>0||DodgeTimer>0||HurtTimer>0)return;
 auto* W=Valley();if(!W)return;
 const int32 I=W->NearestGrove(GetActorLocation(),700.f);
 if(I<0){W->Notify(TEXT("靠近可以采集的果林，按 B 用食物养护土地。"));return;}
 if(!W->CanHarvest(I,this)){W->Notify(TEXT("先获得这里的采集许可，再参与养护。"));return;}
 if(W->Tend(I,this))ActionCooldown=.7f;
}
void AVerdantCharacter::Say(const FString& Line) { LastSpeech=Line;SpeechUntil=GetWorld()->GetTimeSeconds()+8.f; }
void AVerdantCharacter::RestartJourney() { UGameplayStatics::OpenLevel(this,FName(TEXT("Valley"))); }
void AVerdantCharacter::MoveForward(float V) { if(!bDead&&DodgeTimer<=0&&HurtTimer<=0&&Controller&&V!=0) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),V); }
void AVerdantCharacter::MoveRight(float V) { if(!bDead&&DodgeTimer<=0&&HurtTimer<=0&&Controller&&V!=0) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V); }
void AVerdantCharacter::LookYaw(float V) { AddControllerYawInput(V); }
void AVerdantCharacter::LookPitch(float V) { AddControllerPitchInput(V); }
void AVerdantCharacter::StartHarvest(){ bHarvestHeld=true; HarvestRepeatTimer=.7f; Interact(); }
void AVerdantCharacter::StopHarvest(){ bHarvestHeld=false; HarvestRepeatTimer=0; }
void AVerdantCharacter::RequestPlayerAttack(bool Heavy) {
 if(bDead||DodgeTimer>0||HurtTimer>0)return;
 if(AttackTimer>0) {
  // One late click is remembered, never an indefinitely repeating attack.
  if(AttackTimer<=.18f){QueuedAttackType=Heavy?1:0;AttackInputBuffer=.24f;}
  return;
 }
 Attack(Heavy);
}
void AVerdantCharacter::LightAttack(){RequestPlayerAttack(false);} void AVerdantCharacter::HeavyAttack(){RequestPlayerAttack(true);}
void AVerdantCharacter::EatInput(){Eat(5.f);} void AVerdantCharacter::SprintStart(){bSprinting=true;} void AVerdantCharacter::SprintStop(){bSprinting=false;}
void AVerdantCharacter::SetupPlayerInputComponent(UInputComponent* Input) {
 Super::SetupPlayerInputComponent(Input);
 Input->BindAxis("MoveForward",this,&ThisClass::MoveForward); Input->BindAxis("MoveRight",this,&ThisClass::MoveRight);
 Input->BindAxis("Turn",this,&ThisClass::LookYaw);Input->BindAxis("LookUp",this,&ThisClass::LookPitch);
 Input->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ThisClass::LightAttack);
 Input->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&ThisClass::HeavyAttack);
 Input->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ThisClass::Dodge);
 Input->BindKey(EKeys::E,IE_Pressed,this,&ThisClass::StartHarvest);
 Input->BindKey(EKeys::E,IE_Released,this,&ThisClass::StopHarvest);
 Input->BindKey(EKeys::Q,IE_Pressed,this,&ThisClass::EatInput);
 Input->BindKey(EKeys::F,IE_Pressed,this,&ThisClass::Contribute);
 Input->BindKey(EKeys::T,IE_Pressed,this,&ThisClass::Talk);
 Input->BindKey(EKeys::G,IE_Pressed,this,&ThisClass::ToggleClaim);
 Input->BindKey(EKeys::B,IE_Pressed,this,&ThisClass::TendGrove);
 Input->BindKey(EKeys::R,IE_Pressed,this,&ThisClass::RestartJourney);
 Input->BindKey(EKeys::LeftShift,IE_Pressed,this,&ThisClass::SprintStart);
 Input->BindKey(EKeys::LeftShift,IE_Released,this,&ThisClass::SprintStop);
}
