#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
void AVerdantGameMode::TickWorldProbe(float Dt) {
 SmokeTime+=Dt;
 auto* W=AVerdantWorld::Find(this);auto* P=Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
 if(!W||!P||!W->Dialogue)return;
 auto Check=[this](bool Pass,const FString& Label){const FString L=(Pass?TEXT("PASS "):TEXT("FAIL "))+Label;SmokeChecks.Add(L);if(!Pass)++SmokeFailures;UE_LOG(LogTemp,Display,TEXT("VERDANT_WORLD_CHECK %s"),*L);};
 auto Place=[W](AVerdantCharacter* Who,FVector Position){Who->SetActorLocation(W->SurfacePoint(Position,Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5));Who->GetCharacterMovement()->StopMovementImmediately();};
 auto* Courier=W->FindFolk(2);
 if(!Courier)return;
 if(SmokeStage==0&&SmokeTime>3) {
  for(AVerdantCharacter* N:W->Folk)if(N&&N!=P){N->SetActorTickEnabled(false);N->GetCharacterMovement()->StopMovementImmediately();N->GetCharacterMovement()->DisableMovement();N->HostileId=-1;N->CombatTarget.Reset();}
  Place(P,W->Groves[2].Position);
  const float ClosedBefore=P->Food;
  Check(W->Harvest(2,P,5)==0&&P->Food==ClosedBefore,TEXT("Closed territory denies real collection"));
  Check(W->Dialogue->ApplyDecision(W->FindFolk(3),TEXT("open"),0,0),TEXT("Authorized negotiated access change accepted"));
  Check(W->Harvest(2,P,5)>0,TEXT("Negotiated opening enables actual collection"));
  const float Before=W->TreeNutrition;
  Courier->Food=12;Courier->Nutrition=0;Courier->DonationPledge=0;
  Place(Courier,FVector(2000,-800,0));Place(P,FVector(-8000,-6000,0));
  Courier->SetActorTickEnabled(true);Courier->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
  ProbeStart=Courier->GetActorLocation();ProbeTreeBefore=W->TreeNutrition;ProbeInventoryBefore=Courier->Food;
  Check(W->Dialogue->ApplyDecision(Courier,TEXT("donate"),0,10),TEXT("Delivery agreement creates a real pending obligation"));
  Check(W->TreeNutrition==Before,TEXT("Distant promise does not instantly credit the Mother Tree"));
  SmokeStage=1;
 }
 else if(SmokeStage==1&&SmokeTime>8) {
  Check(FVector::Dist2D(ProbeStart,Courier->GetActorLocation())>150,TEXT("Courier physically travels after agreeing"));
  SmokeStage=2;
 }
 else if(SmokeStage==2&&(Courier->DonationPledge<=.01f||SmokeTime>40)) {
  Check(Courier->DonationPledge<=.01f&&W->TreeNutrition>=ProbeTreeBefore+9.99f,TEXT("Promised food arrives through travel and donation"));
  Check(FVector::Dist2D(Courier->GetActorLocation(),W->MotherPosition)<1100,TEXT("Delivery happens at the Mother Tree"));
  Courier->Food=12;Courier->Nutrition=0;Courier->DonationPledge=0;
  Place(Courier,FVector(2000,-800,0));ProbeTreeBefore=W->TreeNutrition;
  W->Dialogue->ApplyDecision(Courier,TEXT("donate"),0,10);
  Courier->ReceiveStrike(P,99999.f);
  Check(Courier->bDead,TEXT("Combat can interrupt a delivery"));
  ProbeInventoryBefore=Courier->Food;
  Place(P,Courier->GetActorLocation());P->ActionCooldown=0;const float PlayerBefore=P->Food;P->Interact();
  Check(FMath::IsNearlyEqual(P->Food-PlayerBefore,ProbeInventoryBefore,.01f)&&Courier->Food==0,TEXT("Defeated courier drops its remaining actual inventory once"));
  P->ActionCooldown=0;const float Once=P->Food;P->Interact();
  Check(P->Food==Once,TEXT("The same corpse cannot create repeated loot"));
  SmokeStage=3;
 }
 else if(SmokeStage==3&&SmokeTime>45) {
  Check(FMath::IsNearlyEqual(W->TreeNutrition,ProbeTreeBefore,.01f),TEXT("Dead courier cannot fulfil a remaining pledge remotely"));
  Check(W->Groves[1].ControllerId==-1,TEXT("Controller death frees territory in the live economy"));
  SmokeChecks.Add(FString::Printf(TEXT("Failures: %d"),SmokeFailures));
  const FString Dir=FPaths::ProjectSavedDir()/TEXT("Validation");IFileManager::Get().MakeDirectory(*Dir,true);
  FFileHelper::SaveStringArrayToFile(SmokeChecks,*(Dir/TEXT("world-probe-results.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("VERDANT_WORLD_PROBE_COMPLETE failures=%d"),SmokeFailures);
  SmokeStage=4;FPlatformMisc::RequestExitWithStatus(false,SmokeFailures?1:0);
 }
}
