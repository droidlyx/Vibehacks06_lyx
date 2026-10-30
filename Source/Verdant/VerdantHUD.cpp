#include "VerdantHUD.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"

namespace {
 FSlateFontInfo Font(int32 Size) {return FSlateFontInfo(FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),Size);}
 const FLinearColor Ink(.9f,.94f,.86f), Gold(1.f,.73f,.30f), Muted(.62f,.73f,.68f), Panel(.025f,.065f,.05f,.88f);
}
void AVerdantHUD::BeginPlay() {
 Super::BeginPlay();
 auto Player=[this](){return Cast<AVerdantCharacter>(GetOwningPawn());};
 auto World=[this](){return AVerdantWorld::Find(this);};
 auto Send=[this](){
  auto* W=AVerdantWorld::Find(this);
  if(W&&W->Dialogue&&InputBox.IsValid()&&!W->Dialogue->bBusy&&W->Dialogue->Selected.IsValid()) {
   const FString Line=InputBox->GetText().ToString().TrimStartAndEnd();
   if(!Line.IsEmpty()) {W->Dialogue->Request(W->Dialogue->Selected.Get(),Line);InputBox->SetText(FText::GetEmpty());}
  }
  return FReply::Handled();
 };
 RootWidget=SNew(SOverlay)
 +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24)
 [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(18)
  [SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("长大以后"))).Font(Font(28)).ColorAndOpacity(Gold)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,10)[SNew(STextBlock).Text(FText::FromString(TEXT("VERDANT ACCORD  /  生长季"))).Font(Font(11)).ColorAndOpacity(Muted)]
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(17)).ColorAndOpacity(Ink).Text_Lambda([Player](){auto* P=Player();return FText::FromString(P?FString::Printf(TEXT("身高 %.2f 米   力量 %.0f"),P->BodySize*1.8f,P->Strength()):TEXT("进入山谷……"));})]
   +SVerticalBox::Slot().AutoHeight().Padding(0,7)[SNew(SBox).WidthOverride(260).HeightOverride(7)[SNew(SProgressBar).FillColorAndOpacity(FLinearColor(.42,.79,.54)).Percent_Lambda([Player](){auto* P=Player();return P?P->Health/P->MaxHealth():0.f;})]]
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Ink).Text_Lambda([Player](){auto* P=Player();return FText::FromString(P?FString::Printf(TEXT("食物 %.0f  ·  吸收中 %.1f  ·  生命 %.0f"),P->Food,P->Nutrition,P->Health):TEXT(""));})]
  ]]
 +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)
 [SNew(SBox).WidthOverride(330)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(16)
  [SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(19)).ColorAndOpacity(Gold).Text_Lambda([World](){auto* W=World();return FText::FromString(W&&W->bRestored?TEXT("母树复苏 · 世界仍在继续"):TEXT("让母树重新结果"));})]
   +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SProgressBar).FillColorAndOpacity(Gold).Percent_Lambda([World](){auto* W=World();return W?FMath::Clamp(W->TreeNutrition/FMath::Max(1.f,W->TreeGoal),0.f,1.f):0.f;})]
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Ink).Text_Lambda([World](){auto* W=World();return FText::FromString(W?FString::Printf(TEXT("已供奉 %.0f / %.0f 份养分"),W->TreeNutrition,W->TreeGoal):TEXT(""));})]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Muted).AutoWrapText(true).Text_Lambda([World](){auto* W=World();return FText::FromString(W?W->WorldSummary():TEXT(""));})]
  ]]]
 +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(24,24,24,110)
 [SNew(SBox).WidthOverride(450)[SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Ink).ShadowOffset(FVector2D(1,1)).AutoWrapText(true).Text_Lambda([World](){auto* W=World();FString T;if(W){int32 Start=FMath::Max(0,W->Events.Num()-4);for(int32 I=Start;I<W->Events.Num();++I)T+=W->Events[I]+TEXT("\n");}return FText::FromString(T);})]]
 +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(24)
 [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(14)
  [SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("WASD 移动  ·  鼠标观察  ·  Shift 奔跑  ·  左/右键轻/重击  ·  空格闪避")))]
   +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,5)[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Gold).Text(FText::FromString(TEXT("E 采集/战利品   Q 食用成长   F 分享/供奉   T 自由交谈   G 管理果林   R 重开")))]
   +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Muted).Text_Lambda([World](){auto* W=World();return FText::FromString(W&&W->GetWorld()->GetTimeSeconds()<W->NoticeUntil?W->Notice:TEXT("食物可以让你长大，也可以改变别人和整个世界。"));})]
  ]]
 +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
 [SNew(SBorder).Visibility_Lambda([Player](){auto* P=Player();return P&&P->bDead?EVisibility::Visible:EVisibility::Collapsed;}).BorderBackgroundColor(Panel).Padding(35)
  [SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("旅程暂时结束"))).Font(Font(32)).ColorAndOpacity(Gold)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,14)[SNew(STextBlock).Text(FText::FromString(TEXT("按 R 再来一次。也许这次，可以先谈谈。"))).Font(Font(17)).ColorAndOpacity(Ink)]
  ]]
 +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
 [SNew(SBox).WidthOverride(680).Visibility_Lambda([this](){return bConversationOpen?EVisibility::Visible:EVisibility::Collapsed;})
  [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025,.055,.045,.98)).Padding(25)
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(24)).ColorAndOpacity(Gold).Text_Lambda([World](){auto* W=World();return FText::FromString(W&&W->Dialogue&&W->Dialogue->Selected.IsValid()?TEXT("与 ")+W->Dialogue->Selected->DisplayName+TEXT(" 交谈"):TEXT("交谈"));})]
    +SVerticalBox::Slot().AutoHeight().Padding(0,14)[SNew(STextBlock).Font(Font(16)).ColorAndOpacity(Ink).AutoWrapText(true).Text_Lambda([World](){FString T;auto* W=World();if(W&&W->Dialogue){auto& L=W->Dialogue->Lines;for(int32 I=FMath::Max(0,L.Num()-5);I<L.Num();++I)T+=L[I]+TEXT("\n\n");}return FText::FromString(T.IsEmpty()?TEXT("自由提出请求、交换条件，或了解对方的打算。\n例如：你愿意开放果林，让大家一起采集吗？"):T);})]
    +SVerticalBox::Slot().AutoHeight()[SAssignNew(InputBox,SMultiLineEditableTextBox).Font(Font(17)).HintText(FText::FromString(TEXT("输入你想说的话……"))).AutoWrapText(true).AllowMultiLine(false).OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Event){if(Event.GetKey()==EKeys::Escape){CloseConversation();return FReply::Handled();}return FReply::Unhandled();}).OnTextCommitted_Lambda([Send](const FText&,ETextCommit::Type Kind){if(Kind==ETextCommit::OnEnter)Send();})]
    +SVerticalBox::Slot().AutoHeight().Padding(0,10)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Muted).AutoWrapText(true).Text_Lambda([World](){auto* W=World();return FText::FromString(W&&W->Dialogue?W->Dialogue->Status:TEXT(""));})]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0)[SNew(SButton).IsFocusable(false).OnClicked_Lambda(Send)[SNew(STextBlock).Text(FText::FromString(TEXT("发送 / Enter"))).Font(Font(16))]]
     +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).IsFocusable(false).OnClicked_Lambda([this](){CloseConversation();return FReply::Handled();})[SNew(STextBlock).Text(FText::FromString(TEXT("结束交谈"))).Font(Font(16))]]
    ]
   ]
  ]
 ];
 if(GEngine&&GEngine->GameViewport)GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(),5);
}
void AVerdantHUD::EndPlay(const EEndPlayReason::Type Reason) {
 if(bConversationOpen)CloseConversation();
 if(GEngine&&GEngine->GameViewport&&RootWidget.IsValid())GEngine->GameViewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
 RootWidget.Reset();InputBox.Reset();Super::EndPlay(Reason);
}
void AVerdantHUD::OpenConversation(AVerdantCharacter* NPC) {
 auto* W=AVerdantWorld::Find(this);if(!W||!W->Dialogue||!IsValid(NPC)||NPC->bDead)return;
 W->Dialogue->Selected=NPC;bConversationOpen=true;
 if(auto* PC=GetOwningPlayerController()) {
  PC->StopMovement();PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);PC->bShowMouseCursor=true;
  FInputModeUIOnly Mode;if(InputBox.IsValid())Mode.SetWidgetToFocus(InputBox);PC->SetInputMode(Mode);
 }
 if(InputBox.IsValid())FSlateApplication::Get().SetKeyboardFocus(InputBox);
}
void AVerdantHUD::CloseConversation() {
 bConversationOpen=false;
 if(auto* PC=GetOwningPlayerController()) {PC->ResetIgnoreMoveInput();PC->ResetIgnoreLookInput();PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());}
}
void AVerdantHUD::DrawHUD() {
 Super::DrawHUD();if(!Canvas)return;
 auto* W=AVerdantWorld::Find(this);auto* P=Cast<AVerdantCharacter>(GetOwningPawn());auto* PC=GetOwningPlayerController();
 if(!W||!P||!PC)return;
 if(bConversationOpen && (P->bDead || !W->Dialogue || !W->Dialogue->Selected.IsValid() || W->Dialogue->Selected->bDead)) CloseConversation();
 auto Label=[this,PC](FVector Position,const FString& Text,FLinearColor Color,int32 Size){
  FVector2D At;if(!PC->ProjectWorldLocationToScreen(Position,At))return;
  if(At.X<20||At.X>Canvas->SizeX-20||At.Y<20||At.Y>Canvas->SizeY-100)return;
  FCanvasTextItem Item(At,FText::FromString(Text),Font(Size),Color);Item.bCentreX=true;Item.EnableShadow(FLinearColor::Black);Canvas->DrawItem(Item);
 };
 for(AVerdantCharacter* NPC:W->Folk) if(IsValid(NPC)&&NPC!=P) {
  float D=FVector::Dist2D(NPC->GetActorLocation(),P->GetActorLocation());if(D>3800)continue;
  FVector Head=NPC->GetActorLocation()+FVector(0,0,105*NPC->BodySize);
  FString Name=FString::Printf(TEXT("%s  ·  %.1fm"),*NPC->DisplayName,NPC->BodySize*1.8f);
  if(NPC->bDead)Name=NPC->Food>0?TEXT("倒下的 ")+NPC->DisplayName+TEXT(" · E 取走食物"):TEXT("");
  Label(Head,Name,NPC->HostileId==0?FLinearColor(1,.4,.3):Ink,14);
  if(!NPC->bDead&&D<1800) {
   Label(Head+FVector(0,0,30*NPC->BodySize),FString::Printf(TEXT("生命 %.0f / %.0f  ·  %s"),NPC->Health,NPC->MaxHealth(),*NPC->ActionLabel),Muted,11);
   if(NPC->SpeechUntil>GetWorld()->GetTimeSeconds())Label(Head+FVector(0,0,70*NPC->BodySize),NPC->LastSpeech.Left(42),Gold,14);
  }
 }
 for(const auto& G:W->Groves) if(FVector::Dist2D(P->GetActorLocation(),G.Position)<4000) Label(G.Position+FVector(0,0,300),G.Name,G.Color,17);
 Label(W->MotherPosition+FVector(0,0,1700),W->bRestored?TEXT("母树 · 新生"):TEXT("母树"),Gold,20);
}
