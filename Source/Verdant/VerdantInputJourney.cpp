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
#include "Misc/Parse.h"
#include "Components/CapsuleComponent.h"
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
 const bool bStewardJourney=FParse::Param(FCommandLine::Get(),TEXT("VerdantStewardJourney"));
 const bool bFullJourney=bStewardJourney||FParse::Param(FCommandLine::Get(),TEXT("VerdantFullJourney"));
 if(!bJourneyHasPosition){JourneyLastPosition=P->GetActorLocation();bJourneyHasPosition=true;}
 JourneyTravelDistance+=FVector::Dist2D(JourneyLastPosition,P->GetActorLocation());JourneyLastPosition=P->GetActorLocation();
 if(SmokeTime>=JourneyNextPathSample) {
  const FVector Point=P->GetActorLocation();JourneyPath.Add(FVector4(Point.X,Point.Y,Point.Z,SmokeTime));
  JourneyNextPathSample=SmokeTime+1.f;
 }

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
  if(auto* H=Cast<AVerdantHUD>(PC->GetHUD()))if(H->bConversationOpen)H->CloseConversation();
  Stop();Key(EKeys::E,false);IFileManager::Get().MakeDirectory(*Dir,true);
  auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("kind"),TEXT("normal-game-engine-input-replay"));
  Result->SetBoolField(TEXT("physical_desktop_input"),false);Result->SetBoolField(TEXT("resource_or_size_grants"),false);
  Result->SetNumberField(TEXT("elapsed_seconds"),SmokeTime);Result->SetNumberField(TEXT("stage"),SmokeStage);Result->SetNumberField(TEXT("first_harvest_seconds"),JourneyFirstHarvest);
  Result->SetNumberField(TEXT("start_size"),JourneyStartSize);Result->SetNumberField(TEXT("end_size"),P->BodySize);
  Result->SetNumberField(TEXT("player_food"),P->Food);Result->SetNumberField(TEXT("tree_nutrition"),W->TreeNutrition);
  Result->SetNumberField(TEXT("failures"),SmokeFailures);Result->SetStringField(TEXT("world"),W->WorldSummary());
  TArray<TSharedPtr<FJsonValue>> Notes;for(auto& S:SmokeChecks)Notes.Add(MakeShared<FJsonValueString>(S));Result->SetArrayField(TEXT("checks"),Notes);

  Result->SetBoolField(TEXT("full_journey"),bFullJourney);
  Result->SetBoolField(TEXT("steward_journey"),bStewardJourney);
  Result->SetNumberField(TEXT("player_care_food_paid"),W->TendLedger.FindRef(0));
  Result->SetNumberField(TEXT("restoration_player_groves"),W->RestoredSeason.PlayerGroves);
  Result->SetBoolField(TEXT("completed_full_goal"),W->bRestored);
  Result->SetNumberField(TEXT("restored_at_seconds"),W->RestoredSeason.bCaptured?W->RestoredSeason.AtSeconds:-1.f);
  Result->SetNumberField(TEXT("travel_distance_m"),JourneyTravelDistance/100.f);
  Result->SetNumberField(TEXT("extra_negotiations_attempted"),JourneyNegotiationsAttempted);
  Result->SetNumberField(TEXT("extra_negotiations_replied"),JourneyNegotiationsReplied);
  auto Contributions=MakeShared<FJsonObject>();
  for(const auto& Pair:W->DonationLedger)Contributions->SetNumberField(FString::FromInt(Pair.Key),Pair.Value);
  Result->SetObjectField(TEXT("actual_donations_by_actor_id"),Contributions);
  auto Harvests=MakeShared<FJsonObject>();
  for(const auto& Pair:W->HarvestLedger)Harvests->SetNumberField(Pair.Key,Pair.Value);
  Result->SetObjectField(TEXT("net_harvest_ledger"),Harvests);
  TArray<TSharedPtr<FJsonValue>> DialogueLines;
  if(W->Dialogue)for(const FString& Line:W->Dialogue->Lines)DialogueLines.Add(MakeShared<FJsonValueString>(Line));
  Result->SetArrayField(TEXT("actual_model_dialogue"),DialogueLines);
  TArray<TSharedPtr<FJsonValue>> RouteNotes;
  for(const FString& Line:JourneyFullNotes)RouteNotes.Add(MakeShared<FJsonValueString>(Line));
  Result->SetArrayField(TEXT("route_events"),RouteNotes);
  TArray<TSharedPtr<FJsonValue>> PathSamples;
  for(const FVector4& Point:JourneyPath) {
   auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("time"),Point.W);
   Sample->SetNumberField(TEXT("x"),Point.X);Sample->SetNumberField(TEXT("y"),Point.Y);Sample->SetNumberField(TEXT("z"),Point.Z);
   PathSamples.Add(MakeShared<FJsonValueObject>(Sample));
  }
  Result->SetArrayField(TEXT("actual_path_cm"),PathSamples);
  TArray<TSharedPtr<FJsonValue>> FinalResidents;
  for(AVerdantCharacter* N:W->Folk)if(IsValid(N)) {
   auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("id"),N->AgentId);R->SetStringField(TEXT("name"),N->DisplayName);
   R->SetBoolField(TEXT("dead"),N->bDead);R->SetNumberField(TEXT("food"),N->Food);R->SetNumberField(TEXT("size"),N->BodySize);
   R->SetNumberField(TEXT("home_grove"),N->HomeGrove);R->SetNumberField(TEXT("pending_donation"),N->DonationPledge);
   FinalResidents.Add(MakeShared<FJsonValueObject>(R));
  }
  Result->SetArrayField(TEXT("final_residents"),FinalResidents);
  TArray<TSharedPtr<FJsonValue>> FinalGroves;
  for(int32 I=0;I<W->Groves.Num();++I) {
   const auto& G=W->Groves[I];auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("index"),I);
   R->SetStringField(TEXT("name"),G.Name);R->SetNumberField(TEXT("controller_id"),G.ControllerId);
   R->SetBoolField(TEXT("open"),G.bOpen);R->SetNumberField(TEXT("share"),G.Share);
   R->SetNumberField(TEXT("food"),G.Food);R->SetNumberField(TEXT("fertility"),G.Fertility);
   FinalGroves.Add(MakeShared<FJsonValueObject>(R));
  }
  Result->SetArrayField(TEXT("final_groves"),FinalGroves);

  FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Result,Writer);
  FFileHelper::SaveStringToFile(Json,*(Dir/(bStewardJourney?TEXT("steward-journey.json"):bFullJourney?TEXT("full-journey.json"):TEXT("input-journey.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("VERDANT_JOURNEY_COMPLETE failures=%d seconds=%.1f"),SmokeFailures,SmokeTime);
  SmokeStage=99;FPlatformMisc::RequestExitWithStatus(false,SmokeFailures?1:0);
 };
 if(P->bDead||SmokeTime>(bFullJourney?300.f:180.f)){Check(false,P->bDead?TEXT("Player died during normal progression loop"):TEXT("Normal input journey timed out"));Finish();return;}
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
 else if(SmokeStage==14&&SmokeTime-JourneyPhaseAt>2.f) {
  if(bFullJourney) {
   Stop();Key(EKeys::E,false);
   JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: original growth/combat/first-offering loop complete; continuing with named negotiations."),SmokeTime));
   JourneyVisitIndex=0;JourneyPhaseAt=SmokeTime;SmokeStage=15;
  } else Finish();
 }
 else if(bFullJourney&&SmokeStage==15&&Walk(W->Groves[1].Position,550.f)) {
  JourneyFoodBefore=P->Food;Key(EKeys::E,true);JourneyPhaseAt=SmokeTime;SmokeStage=16;
 }
 else if(bFullJourney&&SmokeStage==16&&P->Food>=10.f) {
  Key(EKeys::E,false);JourneyCareBefore=W->TendLedger.FindRef(0);JourneyFertilityBefore=W->Groves[1].Fertility;
  JourneyFoodBefore=P->Food;JourneyCareCount=0;JourneyPhaseAt=SmokeTime;SmokeStage=17;
 }
 else if(bFullJourney&&SmokeStage==17&&SmokeTime-JourneyPhaseAt>.8f) {
  if(JourneyCareCount<2){Tap(EKeys::B);++JourneyCareCount;JourneyPhaseAt=SmokeTime;}
  else {
   const float CarePaid=W->TendLedger.FindRef(0)-JourneyCareBefore;
   Check(CarePaid>.01f&&FMath::IsNearlyEqual(JourneyFoodBefore-P->Food,CarePaid,.01f),TEXT("B input invests actual collected food in grove care"));
   Check(W->Groves[1].Fertility>JourneyFertilityBefore+.1f,TEXT("Paid care improves the actual grove rather than granting dialogue credit alone"));
   JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: invested %.2f actual food in care before negotiating access/control."),SmokeTime,CarePaid));
   JourneyPhaseAt=SmokeTime;SmokeStage=20;
  }
 }
 else if(bFullJourney&&SmokeStage==20) {
  if(W->bRestored){JourneyPhaseAt=SmokeTime;SmokeStage=40;return;}
  if(JourneyVisitIndex>=4){JourneyPhaseAt=SmokeTime;SmokeStage=30;return;}
  const int32 VisitId=JourneyVisitIndex<2?(bStewardJourney?1:3):2;
  auto* N=W->FindFolk(VisitId);
  if(!N||N->bDead) {
   JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: resident %d unavailable; retained the real outcome and moved on."),SmokeTime,VisitId));
   ++JourneyVisitIndex;return;
  }
  const float Range=P->GetCapsuleComponent()->GetScaledCapsuleRadius()+N->GetCapsuleComponent()->GetScaledCapsuleRadius()+35.f;
  if(Walk(N->GetActorLocation(),FMath::Max(125.f,Range))) {
   Tap(EKeys::T);JourneyPhaseAt=SmokeTime;SmokeStage=21;
  }
 }
 else if(bFullJourney&&SmokeStage==21&&SmokeTime-JourneyPhaseAt>.25f) {
  const int32 VisitId=JourneyVisitIndex<2?(bStewardJourney?1:3):2;
  auto* H=Cast<AVerdantHUD>(PC->GetHUD());
  const bool Correct=H&&H->bConversationOpen&&W->Dialogue&&W->Dialogue->Selected.IsValid()
   &&W->Dialogue->Selected->AgentId==VisitId;
  if(!Correct) {
   if(H&&H->bConversationOpen)H->CloseConversation();
   JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: face-to-face conversation with %d unavailable; no remote request was made."),SmokeTime,VisitId));
   ++JourneyVisitIndex;JourneyPhaseAt=SmokeTime;SmokeStage=20;return;
  }
  JourneyDialogueStart=W->Dialogue->Lines.Num();
  ++JourneyNegotiationsAttempted;
  const FString Prompt=bStewardJourney
   ?(JourneyVisitIndex%2==0
     ?TEXT("我已经实际供奉母树、养护土地。我想承担果林的管理责任，请把你管理的果林交给我。我希望你继续生活在山谷里，不会伤害你。你愿意现在移交管理权吗？")
     :TEXT("在保留自己口粮的前提下，你愿意亲自给母树送去目前能够承担的养分吗？请只承诺你真正愿意送的数量。"))
   :(JourneyVisitIndex%2==0
     ?TEXT("我已经供奉并养护土地。你愿意保留口粮，亲自把你现在能够承担的养分送到母树吗？请按真实情况提出数量。")
     :TEXT("请免费开放你管理的果林，取消采集分成，让大家都能采集。你可以拒绝；我想知道你的真实打算。"));
  JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: requested %s from resident %d through the real dialogue service."),
   SmokeTime,bStewardJourney?(JourneyVisitIndex%2==0?TEXT("peaceful stewardship"):TEXT("affordable physical delivery")):(JourneyVisitIndex%2==0?TEXT("affordable physical delivery"):TEXT("free access")),VisitId));
  W->Dialogue->Request(W->Dialogue->Selected.Get(),Prompt);
  JourneyPhaseAt=SmokeTime;SmokeStage=22;
 }
 else if(bFullJourney&&SmokeStage==22&&!W->Dialogue->bBusy&&SmokeTime-JourneyPhaseAt>.2f) {
  const bool Replied=W->Dialogue->Lines.Num()>JourneyDialogueStart+1;
  if(Replied)++JourneyNegotiationsReplied;
  JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: negotiation %d reply=%s; %s"),SmokeTime,JourneyVisitIndex+1,
   Replied?TEXT("received"):TEXT("unavailable/interrupted"),*W->Dialogue->Status));
  // A refusal, counteroffer or failed proximity check stays a real outcome.
  // Do not manufacture acceptance or call the world executor from this probe.
  if(auto* H=Cast<AVerdantHUD>(PC->GetHUD()))if(H->bConversationOpen)H->CloseConversation();
  ++JourneyVisitIndex;JourneyPhaseAt=SmokeTime;SmokeStage=20;
 }
 else if(bFullJourney&&SmokeStage==30) {
  Stop();Key(EKeys::E,false);
  if(W->bRestored){JourneyPhaseAt=SmokeTime;SmokeStage=40;return;}
  const float Remaining=FMath::Max(0.f,W->TreeGoal-W->TreeNutrition);
  if(P->Food>=Remaining-.01f&&P->Food>.01f){JourneyPhaseAt=SmokeTime;SmokeStage=33;return;}
  int32 Best=INDEX_NONE;float BestScore=-BIG_NUMBER;
  for(int32 I=0;I<W->Groves.Num();++I) {
   if(!W->CanHarvest(I,P))continue;
   const auto& G=W->Groves[I];
   const float Net=G.Food*(G.ControllerId==P->AgentId?1.f:1.f-G.Share);
   const float Score=Net*2.f-FVector::Dist2D(P->GetActorLocation(),G.Position)*.002f;
   if(Score>BestScore){Best=I;BestScore=Score;}
  }
  if(Best==INDEX_NONE) {
   if(P->Food>.01f){JourneyPhaseAt=SmokeTime;SmokeStage=33;return;}
   if(SmokeTime-JourneyPhaseAt>8.f) {
    Check(false,TEXT("Full journey has no legally accessible food source after the actual negotiations"));Finish();
   }
   return;
  }
  JourneyCollectionGrove=Best;
  JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: walking to grove %d; player food %.2f, tree %.2f, grove stock %.2f."),
   SmokeTime,Best,P->Food,W->TreeNutrition,W->Groves[Best].Food));
  JourneyPhaseAt=SmokeTime;SmokeStage=31;
 }
 else if(bFullJourney&&SmokeStage==31) {
  if(W->bRestored){Stop();JourneyPhaseAt=SmokeTime;SmokeStage=40;return;}
  if(!W->Groves.IsValidIndex(JourneyCollectionGrove)||!W->CanHarvest(JourneyCollectionGrove,P)) {
   Stop();JourneyFullNotes.Add(TEXT("Collection access changed before arrival; reselecting without overriding policy."));
   JourneyPhaseAt=SmokeTime;SmokeStage=30;return;
  }
  if(Walk(W->Groves[JourneyCollectionGrove].Position,550.f)) {
   const auto& G=W->Groves[JourneyCollectionGrove];
   if(bStewardJourney&&G.ControllerId==0&&G.bOpen&&G.Share<.001f)Tap(EKeys::G);
   JourneyFoodBefore=P->Food;Key(EKeys::E,true);JourneyPhaseAt=SmokeTime;SmokeStage=32;
  }
 }
 else if(bFullJourney&&SmokeStage==32) {
  const bool Enough=P->Food+W->TreeNutrition>=W->TreeGoal-.01f;
  const bool Accessible=W->Groves.IsValidIndex(JourneyCollectionGrove)&&W->CanHarvest(JourneyCollectionGrove,P);
  const bool Empty=Accessible&&W->Groves[JourneyCollectionGrove].Food<.5f;
  if(W->bRestored||Enough||!Accessible||(Empty&&SmokeTime-JourneyPhaseAt>2.f)||SmokeTime-JourneyPhaseAt>10.f) {
   Key(EKeys::E,false);
   JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: collected %.2f net food at grove %d using held E."),
    SmokeTime,P->Food-JourneyFoodBefore,JourneyCollectionGrove));
   JourneyPhaseAt=SmokeTime;
   if(W->bRestored)SmokeStage=40;
   else if(Enough||(P->Food>=10.f&&Empty))SmokeStage=33;
   else SmokeStage=30;
  }
 }
 else if(bFullJourney&&SmokeStage==33) {
  if(W->bRestored){Stop();JourneyPhaseAt=SmokeTime;SmokeStage=40;return;}
  if(Walk(W->MotherPosition+FVector(0,-800,0),100.f)) {
   JourneyFoodBefore=P->Food;ProbeTreeBefore=W->TreeNutrition;
   Key(EKeys::LeftShift,true);JourneyPhaseAt=SmokeTime;SmokeStage=34;
  }
 }
 else if(bFullJourney&&SmokeStage==34&&SmokeTime-JourneyPhaseAt>.2f) {
  Tap(EKeys::F);JourneyPhaseAt=SmokeTime;SmokeStage=35;
 }
 else if(bFullJourney&&SmokeStage==35&&SmokeTime-JourneyPhaseAt>.4f) {
  Key(EKeys::LeftShift,false);
  const float Paid=JourneyFoodBefore-P->Food;
  JourneyFullNotes.Add(FString::Printf(TEXT("%.1fs: Shift+F offered %.2f actual player food; tree now %.2f/%.2f."),
   SmokeTime,Paid,W->TreeNutrition,W->TreeGoal));
  if(Paid<=.001f&&!W->bRestored&&JourneyFoodBefore>.01f) {
   Check(false,TEXT("Full-journey bulk-offering input did not transfer the available inventory"));Finish();return;
  }
  JourneyPhaseAt=SmokeTime;SmokeStage=W->bRestored?40:30;
 }
 else if(bFullJourney&&SmokeStage==40&&SmokeTime-JourneyPhaseAt>1.f) {
  Key(EKeys::E,false);
  // Walk back through ordinary movement to frame the restored tree, rather than teleporting the camera.
  if(!Walk(W->MotherPosition+FVector(-350,-2200,0),100.f))return;
  Stop();
  Check(W->bRestored&&FMath::IsNearlyEqual(W->TreeNutrition,W->TreeGoal,.01f),
   TEXT("Complete 100-food restoration reached using ordinary resources and production interactions"));
  float RecordedDonations=0.f;for(const auto& Pair:W->DonationLedger)RecordedDonations+=Pair.Value;
  Check(FMath::IsNearlyEqual(RecordedDonations,W->TreeNutrition,.02f),
   TEXT("Each resident's actual contribution ledger sums to the restored tree"));
  if(bStewardJourney)Check(W->RestoredSeason.PlayerGroves>=2&&W->RestoredSeason.ResidentsAlive==3,TEXT("Peaceful stewardship forms a distinct world outcome with all main residents alive"));
  PC->SetControlRotation(FRotator(12.f,(W->MotherPosition-P->GetActorLocation()).Rotation().Yaw,0));
  JourneyPhaseAt=SmokeTime;SmokeStage=41;
 }
 else if(bFullJourney&&SmokeStage==41&&SmokeTime-JourneyPhaseAt>1.f) {
  FScreenshotRequest::RequestScreenshot(Dir/(bStewardJourney?TEXT("steward-journey-world.png"):TEXT("full-journey-world.png")),true,false);
  JourneyPhaseAt=SmokeTime;SmokeStage=42;
 }
 else if(bFullJourney&&SmokeStage==42&&SmokeTime-JourneyPhaseAt>2.f)Finish();

}
