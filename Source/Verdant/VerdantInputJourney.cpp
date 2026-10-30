// Normal-resource input replay. No teleporting, freezing NPCs, food grants or size setters.
#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "VerdantHUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
void AVerdantGameMode::TickInputJourney(float Dt) {
 SmokeTime+=Dt;
 auto* W=AVerdantWorld::Find(this);auto* P=Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
 auto* PC=P?Cast<APlayerController>(P->GetController()):nullptr;
 if(!W||!P||!PC)return;
 auto Key=[PC](FKey K,bool Down){PC->InputKey(FInputKeyParams(K,Down?IE_Pressed:IE_Released,Down?1.0:0.0,false));};
 auto Tap=[&Key](FKey K){Key(K,true);Key(K,false);};
 auto Stop=[this,&Key](){Key(EKeys::W,false);Key(EKeys::LeftShift,false);bJourneyWalking=false;};
 auto Walk=[this,PC,P,&Key,&Stop](FVector Goal,float Range){
  FVector Delta=Goal-P->GetActorLocation();
  if(Delta.Size2D()<=Range){if(bJourneyWalking)Stop();return true;}
  PC->SetControlRotation(FRotator(-12.f,Delta.Rotation().Yaw,0));
  if(!bJourneyWalking){Key(EKeys::W,true);Key(EKeys::LeftShift,true);bJourneyWalking=true;}
  return false;
 };
 auto Check=[this](bool Pass,const FString& Label){const FString Line=(Pass?TEXT("PASS "):TEXT("FAIL "))+Label;SmokeChecks.Add(Line);if(!Pass)++SmokeFailures;UE_LOG(LogTemp,Display,TEXT("VERDANT_JOURNEY %s"),*Line);};
 const FString Dir=FPaths::ProjectSavedDir()/TEXT("Validation");
 auto Finish=[&](){
  Stop();Key(EKeys::E,false);IFileManager::Get().MakeDirectory(*Dir,true);
  auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("kind"),TEXT("normal-game-engine-input-replay"));
  Result->SetBoolField(TEXT("physical_desktop_input"),false);Result->SetBoolField(TEXT("resource_or_size_grants"),false);
  Result->SetNumberField(TEXT("elapsed_seconds"),SmokeTime);Result->SetNumberField(TEXT("stage"),SmokeStage);Result->SetNumberField(TEXT("first_harvest_seconds"),JourneyFirstHarvest);
  Result->SetNumberField(TEXT("start_size"),JourneyStartSize);Result->SetNumberField(TEXT("end_size"),P->BodySize);
  Result->SetNumberField(TEXT("player_food"),P->Food);Result->SetNumberField(TEXT("tree_nutrition"),W->TreeNutrition);
  Result->SetNumberField(TEXT("failures"),SmokeFailures);Result->SetStringField(TEXT("world"),W->WorldSummary());
  TArray<TSharedPtr<FJsonValue>> Notes;for(auto& S:SmokeChecks)Notes.Add(MakeShared<FJsonValueString>(S));Result->SetArrayField(TEXT("checks"),Notes);
  FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Result,Writer);
  FFileHelper::SaveStringToFile(Json,*(Dir/TEXT("input-journey.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("VERDANT_JOURNEY_COMPLETE failures=%d seconds=%.1f"),SmokeFailures,SmokeTime);
  SmokeStage=99;FPlatformMisc::RequestExitWithStatus(false,SmokeFailures?1:0);
 };
 if(P->bDead||SmokeTime>180.f){Check(false,P->bDead?TEXT("Player died during normal progression loop"):TEXT("Normal input journey timed out"));Finish();return;}
 if(SmokeStage==0&&SmokeTime>3) {
  IFileManager::Get().MakeDirectory(*Dir,true);
  JourneyOrigin=P->GetActorLocation();JourneyStartSize=P->BodySize;SmokeStage=1;JourneyPhaseAt=SmokeTime;
 }
 else if(SmokeStage==1&&Walk(W->Groves[0].Position,560.f)) {
  Check(FVector::Dist2D(JourneyOrigin,P->GetActorLocation())>1000.f,TEXT("Held movement key moves player across actual terrain"));
  JourneyFoodBefore=P->Food;Key(EKeys::E,true);JourneyPhaseAt=SmokeTime;SmokeStage=2;
 }
 else if(SmokeStage==2&&(P->Food>=JourneyFoodBefore+15.f||SmokeTime-JourneyPhaseAt>5.f)) {
  Key(EKeys::E,false);JourneyFirstHarvest=SmokeTime;
  Check(P->Food>=JourneyFoodBefore+10.f,TEXT("One held E gathers multiple real batches without repeated taps"));
  JourneyFoodBefore=P->Food;JourneyEatCount=0;JourneyPhaseAt=SmokeTime;SmokeStage=3;
 }
 else if(SmokeStage==3&&SmokeTime-JourneyPhaseAt>.3f) {
  if(JourneyEatCount<4){Tap(EKeys::Q);++JourneyEatCount;JourneyPhaseAt=SmokeTime;}
  else {Check(P->Food<=JourneyFoodBefore-15.f,TEXT("Q input eats existing food rather than granting growth"));SmokeStage=4;JourneyPhaseAt=SmokeTime;}
 }
 else if(SmokeStage==4) {
  auto* N=W->FindFolk(1);
  if(!N||N->bDead){Check(false,TEXT("Friendly resident unavailable during first journey"));Finish();return;}
  if(Walk(N->GetActorLocation(),480.f)){Tap(EKeys::T);JourneyPhaseAt=SmokeTime;SmokeStage=5;}
 }
 else if(SmokeStage==5&&SmokeTime-JourneyPhaseAt>.5f) {
  auto* H=Cast<AVerdantHUD>(PC->GetHUD());
  const bool Open=H&&H->bConversationOpen&&W->Dialogue->Selected.IsValid();
  Check(Open,TEXT("T input opens the actual conversation window"));
  if(!Open){Finish();return;}
  W->Dialogue->Request(W->Dialogue->Selected.Get(),TEXT("我想复苏母树。你愿意亲自带5份自己的食物过去吗？"));
  JourneyPhaseAt=SmokeTime;SmokeStage=6;
 }
 else if(SmokeStage==6&&!W->Dialogue->bBusy) {
  Check(W->Dialogue->Lines.Num()>=2,TEXT("Normal-state resident responds through the real local model"));
  for(const auto& L:W->Dialogue->Lines)SmokeChecks.Add(L);
  FScreenshotRequest::RequestScreenshot(Dir/TEXT("journey-conversation.png"),true,false);
  JourneyPhaseAt=SmokeTime;SmokeStage=7;
 }
 else if(SmokeStage==7&&SmokeTime-JourneyPhaseAt>1.f) {
  if(auto* H=Cast<AVerdantHUD>(PC->GetHUD()))H->CloseConversation();
  Check(P->BodySize>JourneyStartSize*1.2f,TEXT("Earned growth develops during travel and conversation"));
  SmokeStage=8;JourneyPhaseAt=SmokeTime;
 }
 else if(SmokeStage==8) {
  auto* Rival=W->FindFolk(4);
  if(!Rival){Check(false,TEXT("First wilderness opponent exists"));Finish();return;}
  if(Rival->bDead) {Stop();JourneyFoodBefore=P->Food;JourneyPhaseAt=SmokeTime;SmokeStage=9;return;}
  const FVector Delta=Rival->GetActorLocation()-P->GetActorLocation();
  PC->SetControlRotation(FRotator(-10.f,Delta.Rotation().Yaw,0));
  if(Delta.Size2D()>P->Reach()+20.f)Walk(Rival->GetActorLocation(),P->Reach()+20.f);
  else {
   if(bJourneyWalking)Stop();
   if(P->AttackTimer<=0&&P->HurtTimer<=.12f&&P->DodgeTimer<=0)Tap(EKeys::RightMouseButton);
  }
 }
 else if(SmokeStage==9) {
  auto* Rival=W->FindFolk(4);
  if(!Rival){Check(false,TEXT("Defeated opponent remains as a lootable body"));Finish();return;}
  if(!Walk(Rival->GetActorLocation(),160.f))return;
  if(Rival->Food>0&&P->AttackTimer<=0) {Key(EKeys::E,true);}
  if(Rival->Food<=0) {
   Key(EKeys::E,false);Check(P->Food-JourneyFoodBefore>=15.f,TEXT("Winning a normal encounter returns at least 15 actual food"));
   JourneyPhaseAt=SmokeTime;SmokeStage=10;
  }
 }
 else if(SmokeStage==10&&Walk(W->MotherPosition+FVector(0,-800,0),100.f)) {
  JourneyFoodBefore=P->Food;Key(EKeys::LeftShift,true);JourneyPhaseAt=SmokeTime;SmokeStage=11;
 }
 else if(SmokeStage==11&&SmokeTime-JourneyPhaseAt>.2f) {
  Tap(EKeys::F);JourneyPhaseAt=SmokeTime;SmokeStage=12;
 }
 else if(SmokeStage==12&&SmokeTime-JourneyPhaseAt>.4f) {
  Key(EKeys::LeftShift,false);
  Check(P->LastDonationTime>=JourneyPhaseAt-.1f&&P->LastDonatedAmount>=10.f,TEXT("One Shift+F makes a real bulk offering"));
  Check(P->Food<JourneyFoodBefore,TEXT("Offering consumes the player's earned inventory"));
  PC->SetControlRotation(FRotator(8.f,90.f,0));
  JourneyPhaseAt=SmokeTime;SmokeStage=13;
 }
 else if(SmokeStage==13&&SmokeTime-JourneyPhaseAt>2.f) {
  FScreenshotRequest::RequestScreenshot(Dir/TEXT("journey-world.png"),true,false);JourneyPhaseAt=SmokeTime;SmokeStage=14;
 }
 else if(SmokeStage==14&&SmokeTime-JourneyPhaseAt>2.f)Finish();
}
