#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "VerdantHUD.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
void AVerdantGameMode::TickAIProbe(float Dt) {
 SmokeTime+=Dt;
 auto* W=AVerdantWorld::Find(this);auto* P=Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
 if(!W||!P||!W->Dialogue)return;
 auto* N=W->FindFolk(1);if(!N)return;
 const FString Output=FPaths::ProjectSavedDir()/TEXT("Validation");
 auto Check=[this](bool Pass,const FString& Label){const FString S=(Pass?TEXT("PASS "):TEXT("FAIL "))+Label;SmokeChecks.Add(S);if(!Pass)++SmokeFailures;UE_LOG(LogTemp,Display,TEXT("VERDANT_AI_CHECK %s"),*S);};
 if(SmokeStage==0&&SmokeTime>3) {
  for(AVerdantCharacter* F:W->Folk)if(F&&F!=P){F->SetActorTickEnabled(false);F->GetCharacterMovement()->StopMovementImmediately();F->GetCharacterMovement()->DisableMovement();}
  N->Food=20;N->Nutrition=0;N->Trust.FindOrAdd(0)=100;
  P->Food=5;P->Nutrition=0;
  P->SetActorLocation(W->SurfacePoint(N->GetActorLocation()+FVector(0,-220,0),P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+5));
  P->GetCharacterMovement()->StopMovementImmediately();P->SetActorRotation(FRotator(0,90,0));
  if(P->GetController())P->GetController()->SetControlRotation(FRotator(-5,90,0));
  if(auto* PC=Cast<APlayerController>(P->GetController()))if(auto* HUD=Cast<AVerdantHUD>(PC->GetHUD()))HUD->OpenConversation(N);
  ProbeInventoryBefore=P->Food;ProbeNPCFoodBefore=N->Food;
  W->Dialogue->Request(N,TEXT("米芽，请给我3份食物，我想把它们送到母树那里。"));
  Check(W->Dialogue->bBusy,TEXT("Game sent a real local-model request"));SmokeStage=1;
 }
 else if(SmokeStage==1&&!W->Dialogue->bBusy) {
  Check(W->Dialogue->Lines.Num()>=2&&W->Dialogue->Memories.Contains(1),TEXT("Real Chinese model reply parsed and recorded by game"));
  Check(P->Food>=0&&N->Food>=0&&FMath::IsNearlyEqual(P->Food+N->Food,ProbeInventoryBefore+ProbeNPCFoodBefore,.01f),TEXT("Negotiated action preserves actual resource total"));
  for(const FString& Line:W->Dialogue->Lines)SmokeChecks.Add(Line);
  SmokeChecks.Add(W->Dialogue->Status);CaptureAt=SmokeTime+1.f;SmokeStage=2;
 }
 else if(SmokeStage==2&&SmokeTime>CaptureAt) {
  IFileManager::Get().MakeDirectory(*Output,true);
  FScreenshotRequest::RequestScreenshot(Output/TEXT("valley-dialogue.png"),true,false);CaptureAt=SmokeTime+2.f;SmokeStage=3;
 }
 else if((SmokeStage==3&&SmokeTime>CaptureAt)||SmokeTime>60) {
  if(SmokeTime>60)Check(false,TEXT("AI integration probe timed out"));
  SmokeChecks.Add(FString::Printf(TEXT("Failures: %d"),SmokeFailures));
  FFileHelper::SaveStringArrayToFile(SmokeChecks,*(Output/TEXT("ai-game-results.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("VERDANT_AI_PROBE_COMPLETE failures=%d"),SmokeFailures);
  SmokeStage=4;FPlatformMisc::RequestExitWithStatus(false,SmokeFailures?1:0);
 }
}
