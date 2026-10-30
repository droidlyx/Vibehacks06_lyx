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
 Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
 Camera->SetupAttachment(CameraArm);
 Camera->FieldOfView = 72;
 FallbackBody = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
 FallbackBody->SetupAttachment(RootComponent);
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
 GetCharacterMovement()->MaxStepHeight = FMath::Min(90.f,42.f*BodySize);
 GetCharacterMovement()->JumpZVelocity = 400.f*FMath::Sqrt(BodySize);
 AppliedSize = BodySize;
}
void AVerdantCharacter::Tick(float Dt) {
 Super::Tick(Dt);
 ActionCooldown = FMath::Max(0.f,ActionCooldown-Dt);
 HurtTimer = FMath::Max(0.f,HurtTimer-Dt);
 DodgeTimer = FMath::Max(0.f,DodgeTimer-Dt);
 if (AttackTimer > 0) {
  const float Prior = AttackTimer; AttackTimer = FMath::Max(0.f,AttackTimer-Dt);
  const float StrikeAt = bHeavyAttack ? .28f : .20f;
  if (!bHitApplied && Prior > StrikeAt && AttackTimer <= StrikeAt) ApplyStrike();
 }
 if (!bDead) {
  if (Nutrition > 0 && BodySize < 2.49f) {
   const float Used = FMath::Min(Nutrition,2.2f*Dt);
   const float Prior = BodySize;
   SetSize(BodySize+Used*.013f/FMath::Max(.6f,BodySize*.7f));
   if (BodySize > Prior) { Nutrition -= Used; Health = FMath::Min(MaxHealth(),Health+Used*.5f); }
  }
  GetCharacterMovement()->MaxWalkSpeed = (bSprinting?640.f:430.f)*FMath::Pow(BodySize,.32f)*(AttackTimer>0?.35f:1.f);
  if (AgentId != 0) TickNPC(Dt);
  if (GetActorLocation().Z < -8000) { if (auto* W=Valley()) SetActorLocation(W->SurfacePoint(FVector(0,-7000,0),110*BodySize)); }
 }
 CameraArm->TargetArmLength = FMath::FInterpTo(CameraArm->TargetArmLength,360.f*FMath::Pow(BodySize,.65f),Dt,3.f);
 CameraArm->SocketOffset = FVector(0,28.f*BodySize,55.f*BodySize);
 Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView,bSprinting?78.f:72.f,Dt,5.f));
 UpdateAnimation();
}
void AVerdantCharacter::UpdateAppearance() {
 if (auto* W=Valley()) for (int32 I=0;I<BodyParts.Num();++I)
  BodyParts[I]->SetMaterial(0,W->ColoredMaterial(I==1?FLinearColor(.72,.53,.34):BodyColor));
}
void AVerdantCharacter::UpdateAnimation() {
 const float Speed=GetVelocity().Size2D();
 int32 State=bDead?5:AttackTimer>0?3:HurtTimer>0?4:Speed>450*FMath::Pow(BodySize,.32f)?2:Speed>15?1:0;
 if (State!=AnimationState) {
  UAnimSequence* A=State==5?DeathAnimation:State==3?AttackAnimation:State==4?HitAnimation:State==2?RunAnimation:State==1?WalkAnimation:IdleAnimation;
  if (A) { GetMesh()->PlayAnimation(A,State<3); GetMesh()->SetPlayRate(State==3?(bHeavyAttack?1.f:1.4f):1.f); }
  AnimationState=State;
 }
 if (BodyParts.Num()>=6 && GetWorld()) {
  const float Swing=FMath::Sin(GetWorld()->GetTimeSeconds()*9)*FMath::Min(25.f,Speed*.05f);
  BodyParts[2]->SetRelativeRotation(FRotator(Swing,0,0));
  BodyParts[3]->SetRelativeRotation(FRotator(AttackTimer>0?-70.f:-Swing,0,0));
  BodyParts[4]->SetRelativeRotation(FRotator(-Swing,0,0)); BodyParts[5]->SetRelativeRotation(FRotator(Swing,0,0));
 }
}
bool AVerdantCharacter::Attack(bool Heavy) {
 if (bDead || AttackTimer>0 || DodgeTimer>0 || HurtTimer>.12f) return false;
 bHeavyAttack=Heavy; AttackTimer=Heavy?.85f:.55f; bHitApplied=false; AnimationState=-1;
 if (AgentId==0 && Controller) SetActorRotation(FRotator(0,Controller->GetControlRotation().Yaw,0));
 return true;
}
void AVerdantCharacter::ApplyStrike() {
 bHitApplied=true;
 auto* W=Valley(); if(!W || bDead) return;
 for(AVerdantCharacter* Other:W->Folk) {
  if(!IsValid(Other) || Other==this || Other->bDead) continue;
  FVector Delta=Other->GetActorLocation()-GetActorLocation();
  const float Dist=Delta.Size2D();
  if(Dist>Reach()+Other->GetCapsuleComponent()->GetScaledCapsuleRadius()) continue;
  if(FMath::Abs(Delta.Z)>100.f*(BodySize+Other->BodySize)) continue;
  if(FVector::DotProduct(GetActorForwardVector(),Delta.GetSafeNormal2D())<.25f) continue;
  FHitResult Wall; FCollisionQueryParams Q(SCENE_QUERY_STAT(VerdantMelee),false,this);Q.AddIgnoredActor(Other);
  if(GetWorld()->LineTraceSingleByChannel(Wall,GetActorLocation(),Other->GetActorLocation(),ECC_Visibility,Q)) continue;
  Other->ReceiveStrike(this,Strength()*(bHeavyAttack?1.8f:1.f));
 }
}
void AVerdantCharacter::ReceiveStrike(AVerdantCharacter* From,float Damage) {
 if(bDead || DodgeTimer>.13f || !From || From==this) return;
 Health=FMath::Max(0.f,Health-Damage); HurtTimer=.3f; HostileId=From->AgentId; CombatTarget=From;
 Trust.FindOrAdd(From->AgentId)-=25;
 FollowId=-1;
 const FVector Away=(GetActorLocation()-From->GetActorLocation()).GetSafeNormal2D();
 LaunchCharacter(Away*FMath::Min(300.f,140.f*From->BodySize/FMath::Max(.4f,BodySize))+FVector(0,0,45),true,false);
 if(Health<=0) {
  bDead=true; AttackTimer=0; GetCharacterMovement()->StopMovementImmediately();
  GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
  Food+=FMath::Min(12.f,Nutrition*.35f); Nutrition=0;
  if(auto* W=Valley()) W->Record(FString::Printf(TEXT("%s 击败了 %s，留下 %.0f 份养分。"),*From->DisplayName,*DisplayName,Food));
 }
 AnimationState=-1;
}
bool AVerdantCharacter::Eat(float Amount) {
 if(bDead || Food<=0 || Nutrition>=30.f || BodySize>=2.49f) return false;
 Amount=FMath::Min3(Amount,Food,30.f-Nutrition);
 if(Amount<=0) return false;
 Food-=Amount; Nutrition+=Amount;
 Health=FMath::Min(MaxHealth(),Health+Amount*1.4f);
 if(AgentId==0) if(auto* W=Valley()) W->Notify(TEXT("养分开始吸收，身体会逐渐长大。"));
 return true;
}
void AVerdantCharacter::Dodge() {
 if(bDead || DodgeTimer>0 || AttackTimer>.25f) return;
 DodgeTimer=.45f; AttackTimer=0;
 FVector Dir=GetLastMovementInputVector().GetSafeNormal2D(); if(Dir.IsNearlyZero()) Dir=GetActorForwardVector();
 LaunchCharacter(Dir*650.f*FMath::Pow(BodySize,.35f)+FVector(0,0,45),true,false);
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
 if(bDead || ActionCooldown>0) return;
 auto* W=Valley();if(!W)return;
 for(AVerdantCharacter* Other:W->Folk) if(IsValid(Other)&&Other->bDead&&Other->Food>0&&FVector::Dist2D(GetActorLocation(),Other->GetActorLocation())<220.f*FMath::Max(1.f,BodySize)) {
  const float Taken=Other->Food; Food+=Taken;Other->Food=0;
  W->Record(FString::Printf(TEXT("%s 取走战利品：%.0f 份养分。"),*DisplayName,Taken)); ActionCooldown=.4f; return;
 }
 const int32 Index=W->NearestGrove(GetActorLocation(),700.f);
 if(Index>=0) { W->Harvest(Index,this,5.f);ActionCooldown=.45f; }
 else W->Notify(TEXT("靠近果林的发光植物按 E 采集；靠近倒下的生物可取走战利品。"));
}
void AVerdantCharacter::Contribute() {
 if(bDead || ActionCooldown>0)return;
 auto* W=Valley();if(!W)return;
 if(FVector::Dist2D(GetActorLocation(),W->MotherPosition)<1000.f) W->Donate(this,5.f);
 else if(auto* Other=NearestFolk(400.f*FMath::Max(1.f,BodySize))) { if(W->Transfer(this,Other,5.f)) {Other->Trust.FindOrAdd(AgentId)+=12;Other->Say(TEXT("这份食物我会记得。"));} }
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
 if(W->Groves[I].ControllerId==AgentId) W->SetAccess(I,this,!W->Groves[I].bOpen);
 else W->Claim(I,this);
}
void AVerdantCharacter::Say(const FString& Line) { LastSpeech=Line;SpeechUntil=GetWorld()->GetTimeSeconds()+8.f; }
void AVerdantCharacter::RestartJourney() { UGameplayStatics::OpenLevel(this,FName(TEXT("Valley"))); }
void AVerdantCharacter::MoveForward(float V) { if(!bDead&&Controller&&V!=0) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),V); }
void AVerdantCharacter::MoveRight(float V) { if(!bDead&&Controller&&V!=0) AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V); }
void AVerdantCharacter::LookYaw(float V) { AddControllerYawInput(V); }
void AVerdantCharacter::LookPitch(float V) { AddControllerPitchInput(V); }
void AVerdantCharacter::LightAttack(){Attack(false);} void AVerdantCharacter::HeavyAttack(){Attack(true);}
void AVerdantCharacter::EatInput(){Eat(5.f);} void AVerdantCharacter::SprintStart(){bSprinting=true;} void AVerdantCharacter::SprintStop(){bSprinting=false;}
void AVerdantCharacter::SetupPlayerInputComponent(UInputComponent* Input) {
 Super::SetupPlayerInputComponent(Input);
 Input->BindAxis("MoveForward",this,&ThisClass::MoveForward); Input->BindAxis("MoveRight",this,&ThisClass::MoveRight);
 Input->BindAxis("Turn",this,&ThisClass::LookYaw);Input->BindAxis("LookUp",this,&ThisClass::LookPitch);
 Input->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ThisClass::LightAttack);
 Input->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&ThisClass::HeavyAttack);
 Input->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ThisClass::Dodge);
 Input->BindKey(EKeys::E,IE_Pressed,this,&ThisClass::Interact);
 Input->BindKey(EKeys::Q,IE_Pressed,this,&ThisClass::EatInput);
 Input->BindKey(EKeys::F,IE_Pressed,this,&ThisClass::Contribute);
 Input->BindKey(EKeys::T,IE_Pressed,this,&ThisClass::Talk);
 Input->BindKey(EKeys::G,IE_Pressed,this,&ThisClass::ToggleClaim);
 Input->BindKey(EKeys::R,IE_Pressed,this,&ThisClass::RestartJourney);
 Input->BindKey(EKeys::LeftShift,IE_Pressed,this,&ThisClass::SprintStart);
 Input->BindKey(EKeys::LeftShift,IE_Released,this,&ThisClass::SprintStop);
}
