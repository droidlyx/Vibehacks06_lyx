#include "VerdantHUD.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
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
#include "Widgets/Layout/SScrollBox.h"
#include "Components/CapsuleComponent.h"
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
 constexpr float StartingHeightCm = .45f * 180.f;
 constexpr float DamageLifetime = .85f;
 constexpr int32 MaxDamageFloats = 6;
 FString Stance(const AVerdantCharacter* NPC) {
  if(!NPC)return TEXT("");
  if(NPC->HostileId==0)return TEXT("敌对");
  if(NPC->FollowId==0)return TEXT("同行");
  const float Trust=NPC->Trust.FindRef(0);
  if(Trust>=10.f)return TEXT("友善");
  if(Trust<0.f||NPC->bWild)return TEXT("戒备");
  return TEXT("可以交谈");
 }
 FLinearColor StanceColor(const AVerdantCharacter* NPC) {
  if(NPC->HostileId==0)return FLinearColor(1.f,.35f,.24f);
  if(NPC->FollowId==0||NPC->Trust.FindRef(0)>=10.f)return FLinearColor(.49f,.88f,.63f);
  if(NPC->Trust.FindRef(0)<0.f||NPC->bWild)return FLinearColor(1.f,.72f,.35f);
  return Ink;
 }
 FString PlayerStake(const AVerdantCharacter* P,const AVerdantWorld* W) {
  if(!P||!W)return TEXT("");
  int32 Groves=0;for(const auto& G:W->Groves)if(G.ControllerId==P->AgentId)++Groves;
  return FString::Printf(TEXT("你的筹码：携带 %.1f 份食物 · 管理 %d 片果林 · 身高 %.2f 米"),P->Food,Groves,P->BodySize*1.8f);
 }
 FString ContextHint(AVerdantCharacter* P,AVerdantWorld* W) {
  if(!P||!W||P->bDead)return TEXT("");
  for(AVerdantCharacter* N:W->Folk)if(IsValid(N)&&N->bDead&&N->Food>0&&FVector::Dist2D(P->GetActorLocation(),N->GetActorLocation())<220.f*FMath::Max(1.f,P->BodySize))
   return FString::Printf(TEXT("[E] 收取 %s 的 %.1f 份战利品"),*N->DisplayName,N->Food);
  if(FVector::Dist2D(P->GetActorLocation(),W->MotherPosition)<1000.f&&!W->bRestored) {
   const float Available=FMath::Min(P->Food,FMath::Max(0.f,W->TreeGoal-W->TreeNutrition));
   if(Available<.01f)return TEXT("母树需要养分 · 先采集或与居民协商分享");
   return FString::Printf(TEXT("母树还需 %.1f 份 · [F] 供奉 %.1f 份 · [Shift+F] 供奉全部可用的 %.1f 份"),W->TreeGoal-W->TreeNutrition,FMath::Min(5.f,Available),Available);
  }
  const int32 I=W->NearestGrove(P->GetActorLocation(),700.f);
  FString Text;
  if(I>=0) {
   const auto& G=W->Groves[I];auto* Keeper=W->FindFolk(G.ControllerId);
   const bool LivingKeeper=Keeper&&!Keeper->bDead;
   const bool Allowed=W->CanHarvest(I,P);
   Text=FString::Printf(TEXT("%s · %s · 生机 %.0f%% | "),*G.Name,LivingKeeper?*Keeper->DisplayName:TEXT("无人管理"),G.Fertility*100.f);
   if(!Allowed)Text+=TEXT("尚未获准采集，请与管理者协商");
   else if(G.Food<.5f)Text+=TEXT("果实暂空，正在再生");
   else {
    const float Take=FMath::Min(5.f,G.Food);
    const float Share=LivingKeeper&&Keeper!=P?Take*FMath::Clamp(G.Share,0.f,.9f):0.f;
    Text+=FString::Printf(TEXT("[按住 E] 每次实得 %.1f 份"),Take-Share);
    if(Share>.01f)Text+=FString::Printf(TEXT("（分成 %.1f）"),Share);
   }
   FString Management;
   if(Allowed) {
    const float Cost=W->TendCost(I);
    if(Cost>=.01f)Management=P->Food+.001f>=Cost
      ?FString::Printf(TEXT("[B] 用 %.1f 份食物养护"),Cost)
      :FString::Printf(TEXT("养护需 %.1f 份食物，你还差 %.1f"),Cost,Cost-P->Food);
   }
   if(G.ControllerId==P->AgentId) {
    const TCHAR* Next=!G.bOpen?TEXT("免费开放"):G.Share<.001f?TEXT("改为30%分成"):TEXT("关闭采集");
    if(!Management.IsEmpty())Management+=TEXT(" · ");
    Management+=FString::Printf(TEXT("[G] %s"),Next);
   } else if(!LivingKeeper) {
    if(!Management.IsEmpty())Management+=TEXT(" · ");
    Management+=TEXT("[G] 接管果林");
   }
   if(!Management.IsEmpty())Text+=TEXT("\n")+Management;
  }
  if(auto* N=P->NearestFolk(750.f*FMath::Max(1.f,P->BodySize))) {
   Text+=(Text.IsEmpty()?TEXT(""):TEXT("\n"))+FString::Printf(TEXT("[T] %s · %s · %s"),*N->DisplayName,*Stance(N),*N->ActionLabel);
   if(P->Food>.01f&&FVector::Dist2D(P->GetActorLocation(),W->MotherPosition)>=1000.f&&FVector::Dist2D(P->GetActorLocation(),N->GetActorLocation())<400.f*FMath::Max(1.f,P->BodySize))
    Text+=FString::Printf(TEXT(" · [F] 分享 %.1f 份"),FMath::Min(5.f,P->Food));
  }
  if(Text.IsEmpty()) {
   if(W->HarvestLedger.FindRef(TEXT("0:0"))<.01f&&W->Groves.Num()>0) {
    const FVector Delta=W->Groves[0].Position-P->GetActorLocation();
    const float Yaw=P->GetController()?P->GetController()->GetControlRotation().Yaw:P->GetActorRotation().Yaw;
    const float Relative=FMath::FindDeltaAngleDegrees(Yaw,Delta.Rotation().Yaw);
    const TCHAR* Direction=FMath::Abs(Relative)>140?TEXT("身后"):Relative>25?TEXT("右前方"):Relative< -25?TEXT("左前方"):TEXT("前方");
    Text=FString::Printf(TEXT("朝%s寻找苔石果林 · %.0f 米 · 可以先和米芽谈谈"),Direction,Delta.Size2D()/100.f);
   } else Text=TEXT("观察居民的打算，争取食物、采集权或果林管理权。");
  }
  return Text;
 }
 FString DeliveryHint(AVerdantWorld* W) {
  if(!W||!W->Dialogue||!W->Dialogue->Selected.IsValid())return TEXT("");
  auto* N=W->Dialogue->Selected.Get();
  if(N->bDead&&N->DonationPledge>.001f)return N->DisplayName+TEXT("已倒下，未完成的运送已中断。");
  if(N->DonationPledge>.001f) {
   const float Dist=FVector::Dist2D(N->GetActorLocation(),W->MotherPosition)/100.f;
   return FString::Printf(TEXT("%s → 母树 · 待送 %.0f 份 · 距目的地 %.0f 米"),*N->DisplayName,N->DonationPledge,Dist);
  }
  if(N->LastDonationTime>=0&&W->GetWorld()->GetTimeSeconds()-N->LastDonationTime<12.f)
   return FString::Printf(TEXT("%s 已送达 %.0f 份养分"),*N->DisplayName,N->LastDonatedAmount);
  if(N->FollowId==0&&!N->bDead)return N->DisplayName+TEXT("正在与你同行。");
  return TEXT("");
 }
}
bool AVerdantHUD::HasCanvasFont() const {
 return IsValid(CanvasFont.Get())&&CanvasFont->FontCacheType==EFontCacheType::Runtime
  &&CanvasFont->GetInternalCompositeFont().DefaultTypeface.Fonts.Num()>0;
}
void AVerdantHUD::BeginPlay() {
 Super::BeginPlay();
 // Canvas requires a real UFont even when Slate can render a filename-only font.
 // Keep the same Chinese-capable composite, with UObject lifetime owned by this HUD.
 const FSlateFontInfo SourceFont=Font(14);
 CanvasFont=NewObject<UFont>(this,TEXT("VerdantCanvasFont"),RF_Transient);
 CanvasFont->FontCacheType=EFontCacheType::Runtime;
 CanvasFont->GetMutableInternalCompositeFont()=*SourceFont.GetCompositeFont();
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
   +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)
   [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.07f,.14f,.09f,.95f)).Padding(8)
    .Visibility_Lambda([World](){auto* W=World();return W&&W->IsSeasonRecordVisible()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
    [SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(15)).ColorAndOpacity(Gold).Text_Lambda([World](){auto* W=World();return FText::FromString(W?W->SeasonBannerTitle:TEXT(""));})]
     +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Ink).AutoWrapText(true).Text_Lambda([World](){auto* W=World();return FText::FromString(W?W->SeasonBannerBody:TEXT(""));})]
    ]]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Muted).AutoWrapText(true).Text_Lambda([World](){auto* W=World();return FText::FromString(W?W->WorldSummary():TEXT(""));})]
   +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)
   [SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Gold).AutoWrapText(true)
    .Visibility_Lambda([World](){auto* W=World();return W&&W->RestoredSeason.bCaptured&&!W->IsSeasonRecordVisible()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
    .Text_Lambda([World](){auto* W=World();return FText::FromString(W?W->SeasonCoreSummary:TEXT(""));})]

  ]]]
 +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(20,20,20,180)
 [SNew(SBox).WidthOverride(320)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Ink).ShadowOffset(FVector2D(1,1)).AutoWrapText(true).Text_Lambda([World](){auto* W=World();FString T;if(W){int32 Start=FMath::Max(0,W->Events.Num()-2);for(int32 I=Start;I<W->Events.Num();++I)T+=W->Events[I]+TEXT("\n");}return FText::FromString(T);})]]
 +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(18)
 [SNew(SBox).WidthOverride(820)
  [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(10)
   .Visibility_Lambda([this](){return bConversationOpen?EVisibility::Collapsed:EVisibility::HitTestInvisible;})
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0,0,0,4)
    [SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Gold).AutoWrapText(true).WrapTextAt(790).Justification(ETextJustify::Center)
     .Text_Lambda([World](){return FText::FromString(DeliveryHint(World()));})
     .Visibility_Lambda([World](){return DeliveryHint(World()).IsEmpty()?EVisibility::Collapsed:EVisibility::HitTestInvisible;})]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0,0,0,5)
    [SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Ink).AutoWrapText(true).WrapTextAt(790).Justification(ETextJustify::Center)
     .Text_Lambda([Player,World](){return FText::FromString(ContextHint(Player(),World()));})]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
    [SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Muted).Text(FText::FromString(TEXT("WASD 移动 · 鼠标观察 · Shift 奔跑 · 左/右键轻/重击 · 空格闪避")))]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,4,0,0)
    [SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Gold).Text(FText::FromString(TEXT("E 采集/战利品 · Q 成长 · F 分享/供奉 · T 交谈 · B 养护 · G 管理 · R 重开")))]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0,4,0,0)
    [SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Ink).AutoWrapText(true).WrapTextAt(790).Justification(ETextJustify::Center)
     .Visibility_Lambda([World](){auto* W=World();return W&&W->GetWorld()->GetTimeSeconds()<W->NoticeUntil?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
     .Text_Lambda([World](){auto* W=World();return FText::FromString(W?W->Notice:TEXT(""));})]
   ]
  ]
 ]
 +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(20,190,20,0)
 [SNew(STextBlock).Font(Font(22)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1)).Text_Lambda([Player](){auto* P=Player();return FText::FromString(P&&P->GetWorld()->GetTimeSeconds()<P->FeedbackUntil?P->CombatFeedback:TEXT(""));})]
 +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
 [SNew(SBorder).Visibility_Lambda([Player](){auto* P=Player();return P&&P->bDead?EVisibility::Visible:EVisibility::Collapsed;}).BorderBackgroundColor(Panel).Padding(35)
  [SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("旅程暂时结束"))).Font(Font(32)).ColorAndOpacity(Gold)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,14)[SNew(STextBlock).Text(FText::FromString(TEXT("按 R 再来一次。也许这次，可以先谈谈。"))).Font(Font(17)).ColorAndOpacity(Ink)]
  ]]
 +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
 [SNew(SBox).WidthOverride(680).Visibility_Lambda([this](){return bConversationOpen?EVisibility::Visible:EVisibility::Collapsed;})
  [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025,.055,.045,.98)).Padding(20)
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()
    [SNew(STextBlock).Font(Font(22)).ColorAndOpacity(Gold).Text_Lambda([World](){auto* W=World();auto* N=W&&W->Dialogue?W->Dialogue->Selected.Get():nullptr;
      return FText::FromString(N?FString::Printf(TEXT("与 %s 交谈 · %s"),*N->DisplayName,*Stance(N)):TEXT("交谈"));})]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
    [SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Ink).AutoWrapText(true).Text_Lambda([World](){auto* W=World();auto* N=W&&W->Dialogue?W->Dialogue->Selected.Get():nullptr;
      return FText::FromString(N?N->ActionLabel+TEXT(" · ")+N->Motivation+TEXT("\n")+W->NegotiationSummary(N):TEXT(""));})]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,8)
    [SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Gold).AutoWrapText(true).Text_Lambda([Player,World](){return FText::FromString(PlayerStake(Player(),World()));})]
    +SVerticalBox::Slot().AutoHeight()
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)
     [SNew(SButton).HAlign(HAlign_Center).ContentPadding(5).OnClicked_Lambda([this](){FillConversationPrompt(TEXT("你现在最在意什么？我们怎样合作对彼此都有好处？"));return FReply::Handled();})
      [SNew(STextBlock).Font(Font(13)).Text(FText::FromString(TEXT("问问打算")))]]
     +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)
     [SNew(SButton).HAlign(HAlign_Center).ContentPadding(5).OnClicked_Lambda([this](){FillConversationPrompt(TEXT("我可以分享一些食物。你愿意给我采集许可或降低分成吗？请说说你的条件。"));return FReply::Handled();})
      [SNew(STextBlock).Font(Font(13)).Text(FText::FromString(TEXT("用食物交换")))]]
     +SHorizontalBox::Slot().FillWidth(1)
     [SNew(SButton).HAlign(HAlign_Center).ContentPadding(5).OnClicked_Lambda([this](){FillConversationPrompt(TEXT("如果我承担果林的养护，你愿意和平地把管理权交给我吗？你希望换取什么？"));return FReply::Handled();})
      [SNew(STextBlock).Font(Font(13)).Text(FText::FromString(TEXT("商量让地")))]]
    ]
    +SVerticalBox::Slot().AutoHeight().Padding(0,10)
    [SNew(SBox).HeightOverride(150)
     [SAssignNew(ConversationScroll,SScrollBox)
      +SScrollBox::Slot()
      [SNew(STextBlock).Font(Font(15)).ColorAndOpacity(Ink).AutoWrapText(true).Text_Lambda([World](){
       FString Text;auto* W=World();if(W&&W->Dialogue){const auto& Lines=W->Dialogue->Lines;for(int32 I=FMath::Max(0,Lines.Num()-6);I<Lines.Num();++I)Text+=Lines[I]+TEXT("\n\n");}
       return FText::FromString(Text.IsEmpty()?TEXT("可以先问条件，再提出交换。上方提议只填入文字，按发送才会开口。"):Text);
      })]
     ]
    ]
    +SVerticalBox::Slot().AutoHeight()
    [SAssignNew(InputBox,SMultiLineEditableTextBox).Font(Font(16)).HintText(FText::FromString(TEXT("自由说说你的提议……"))).AutoWrapText(true).AllowMultiLine(false)
     .OnKeyDownHandler_Lambda([this](const FGeometry&,const FKeyEvent& Event){if(Event.GetKey()==EKeys::Escape){CloseConversation();return FReply::Handled();}return FReply::Unhandled();})
     .OnTextCommitted_Lambda([Send](const FText&,ETextCommit::Type Kind){if(Kind==ETextCommit::OnEnter)Send();})]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8)
    [SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Muted).AutoWrapText(true).Text_Lambda([World](){auto* W=World();return FText::FromString(W&&W->Dialogue?W->Dialogue->Status:TEXT(""));})]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0)
     [SNew(SButton).IsEnabled_Lambda([World](){auto* W=World();return W&&W->Dialogue&&!W->Dialogue->bBusy;}).OnClicked_Lambda(Send)
      [SNew(STextBlock).Text(FText::FromString(TEXT("发送 / Enter"))).Font(Font(15))]]
     +SHorizontalBox::Slot().AutoWidth()
     [SNew(SButton).OnClicked_Lambda([this](){CloseConversation();return FReply::Handled();})
      [SNew(STextBlock).Text(FText::FromString(TEXT("结束交谈 / Esc"))).Font(Font(15))]]
    ]
   ]
  ]
 ];
 if(GEngine&&GEngine->GameViewport)GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(),5);
}
void AVerdantHUD::EndPlay(const EEndPlayReason::Type Reason) {
 if(bConversationOpen)CloseConversation();
 if(GEngine&&GEngine->GameViewport&&RootWidget.IsValid())GEngine->GameViewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
 RootWidget.Reset();InputBox.Reset();ConversationScroll.Reset();Observations.Reset();DamageFloats.Reset();Super::EndPlay(Reason);
}
void AVerdantHUD::OpenConversation(AVerdantCharacter* NPC) {
 auto* W=AVerdantWorld::Find(this);if(!W||!W->Dialogue||!IsValid(NPC)||NPC->bDead||!W->Dialogue->CanConverse(NPC))return;
 const bool ChangedNPC=W->Dialogue->Selected.Get()!=NPC;
 W->Dialogue->SelectNPC(NPC);
 if(ChangedNPC) {
  if(InputBox.IsValid())InputBox->SetText(FText::GetEmpty());
  LastDialogueLineCount=-1;
 }
 if(bConversationOpen){if(InputBox.IsValid())FSlateApplication::Get().SetKeyboardFocus(InputBox);return;}
 bConversationOpen=true;
 if(auto* P=Cast<AVerdantCharacter>(GetOwningPawn()))P->CancelForConversation();
 if(auto* PC=GetOwningPlayerController()) {
  PC->StopMovement();PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);PC->bShowMouseCursor=true;
  FInputModeUIOnly Mode;if(InputBox.IsValid())Mode.SetWidgetToFocus(InputBox);PC->SetInputMode(Mode);
 }
 if(InputBox.IsValid())FSlateApplication::Get().SetKeyboardFocus(InputBox);
}
void AVerdantHUD::FillConversationPrompt(const FString& Text) {
 if(!bConversationOpen||!InputBox.IsValid())return;
 InputBox->SetText(FText::FromString(Text));
 FSlateApplication::Get().SetKeyboardFocus(InputBox);
}
void AVerdantHUD::CloseConversation() {
 bConversationOpen=false;
 if(auto* PC=GetOwningPlayerController()) {PC->ResetIgnoreMoveInput();PC->ResetIgnoreLookInput();PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());}
}
void AVerdantHUD::DrawHUD() {
 Super::DrawHUD();if(!Canvas)return;
 auto* W=AVerdantWorld::Find(this);
 auto* P=Cast<AVerdantCharacter>(GetOwningPawn());
 auto* PC=GetOwningPlayerController();
 if(!W||!P||!PC)return;
 const float Now=GetWorld()->GetTimeSeconds();
 if(bConversationOpen&&(P->bDead||!W->Dialogue||!W->Dialogue->Selected.IsValid()||W->Dialogue->Selected->bDead))CloseConversation();
 else if(bConversationOpen&&!W->Dialogue->CanConverse(W->Dialogue->Selected.Get())) {
  CloseConversation();W->Notify(TEXT("对方已经走远，靠近后再交谈。"));
 }
 if(bConversationOpen&&ConversationScroll.IsValid()&&W->Dialogue&&LastDialogueLineCount!=W->Dialogue->Lines.Num()) {
  LastDialogueLineCount=W->Dialogue->Lines.Num();ConversationScroll->ScrollToEnd();
 }
 FVector ViewLocation;FRotator ViewRotation;PC->GetPlayerViewPoint(ViewLocation,ViewRotation);
 auto Project=[this,PC](const FVector& Position,FVector2D& At,float BottomReserve=175.f) {
  return PC->ProjectWorldLocationToScreen(Position,At)&&At.X>=18&&At.X<=Canvas->SizeX-18&&At.Y>=18&&At.Y<=Canvas->SizeY-BottomReserve;
 };
 auto TextAt=[this](FVector2D At,const FString& Text,FLinearColor Color,int32 Size,bool Center=true) {
  if(Text.IsEmpty()||!HasCanvasFont())return;
  FCanvasTextItem Item(At,FText::FromString(Text),FSlateFontInfo(CanvasFont.Get(),Size),Color);
  Item.bCentreX=Center;Item.EnableShadow(FLinearColor(0,0,0,Color.A));Canvas->DrawItem(Item);
 };
 auto Label=[&](const FVector& Position,const FString& Text,FLinearColor Color,int32 Size) {
  FVector2D At;if(Project(Position,At))TextAt(At,Text,Color,Size);
 };
 auto WorldLine=[&](const FVector& A,const FVector& B,FLinearColor Color,float Thickness) {
  FVector2D SA,SB;
  if(Project(A,SA,145.f)&&Project(B,SB,145.f))DrawLine(SA.X,SA.Y,SB.X,SB.Y,Color,Thickness);
 };
 auto Readable=[&](AVerdantCharacter* Who,const FVector& Head) {
  if(bConversationOpen||FVector::DistSquared2D(Who->GetActorLocation(),P->GetActorLocation())>FMath::Square(3800.f))return false;
  FVector2D At;if(!Project(Head,At))return false;
  // Do not observe damage behind the persistent corner panels or opaque scenery.
  if(At.Y<230.f&&(At.X<340.f||At.X>Canvas->SizeX-370.f))return false;
  FHitResult Obstacle;FCollisionQueryParams Query(SCENE_QUERY_STAT(VerdantHUDSight),false,P);Query.AddIgnoredActor(Who);
  return !GetWorld()->LineTraceSingleByChannel(Obstacle,ViewLocation,Head,ECC_Visibility,Query);
 };
 DamageFloats.RemoveAll([Now](const FVerdantHUDDamageFloat& Float){return !Float.Character.IsValid()||Float.Character->bDead||Now-Float.BornAt>=DamageLifetime;});
 auto Observe=[&](AVerdantCharacter* Who,const FVector& Head) {
  auto* Previous=Observations.Find(Who->AgentId);
  const bool SameActor=Previous&&Previous->Character.Get()==Who;
  float FlashUntil=SameActor?Previous->StrikeFlashUntil:-1.f;
  if(SameActor&&Who->AttackTimer>0&&!Previous->bHitApplied&&Who->bHitApplied)FlashUntil=Now+.11f;
  // Only a live, already observed actor's real health decrease can produce a number.
  // Growth/healing, first observation, shrinking, and the death transition do not.
  if(SameActor&&Who!=P&&!Who->bDead&&!Previous->bDead&&Who->HurtTimer>0&&Who->BodySize+.0001f>=Previous->BodySize) {
   const float Lost=Previous->Health-Who->Health;
   if(FMath::IsFinite(Lost)&&Lost>.05f) {
    int32 PerActor=0;for(const auto& Float:DamageFloats)if(Float.Character.Get()==Who)++PerActor;
    if(PerActor>=2) {
     const int32 Oldest=DamageFloats.IndexOfByPredicate([Who](const FVerdantHUDDamageFloat& Float){return Float.Character.Get()==Who;});
     if(Oldest!=INDEX_NONE)DamageFloats.RemoveAt(Oldest);
    }
    if(DamageFloats.Num()>=MaxDamageFloats)DamageFloats.RemoveAt(0);
    FVerdantHUDDamageFloat Float;Float.Character=Who;Float.Position=Head;Float.Amount=Lost;Float.BornAt=Now;
    DamageFloats.Add(Float);
   }
  }
  FVerdantHUDObservation Sample;Sample.Character=Who;Sample.Health=Who->Health;Sample.BodySize=Who->BodySize;
  Sample.bDead=Who->bDead;Sample.bHitApplied=Who->bHitApplied;Sample.StrikeFlashUntil=FlashUntil;
  Observations.Add(Who->AgentId,Sample);
 };
 TSet<int32> VisibleIds;
 TArray<AVerdantCharacter*> VisibleFolk;
 Observe(P,P->GetActorLocation());
 for(AVerdantCharacter* NPC:W->Folk)if(IsValid(NPC)&&NPC!=P) {
  const FVector Head=NPC->GetActorLocation()+FVector(0,0,105.f*NPC->BodySize);
  if(!Readable(NPC,Head)) {Observations.Remove(NPC->AgentId);continue;}
  VisibleIds.Add(NPC->AgentId);VisibleFolk.Add(NPC);Observe(NPC,Head);
  const float Distance=FVector::Dist2D(NPC->GetActorLocation(),P->GetActorLocation());
  FString Name=FString::Printf(TEXT("%s · %s · %.1fm"),*NPC->DisplayName,*Stance(NPC),NPC->BodySize*1.8f);
  if(NPC->bDead)Name=NPC->Food>0?TEXT("倒下的 ")+NPC->DisplayName+TEXT(" · E 收取食物"):TEXT("");
  Label(Head,Name,NPC->bDead?Muted:StanceColor(NPC),14);
  if(!NPC->bDead&&Distance<1800.f) {
   const bool ShowingDamage=DamageFloats.ContainsByPredicate([NPC](const FVerdantHUDDamageFloat& Float){return Float.Character.Get()==NPC;});
   if(!ShowingDamage)Label(Head+FVector(0,0,30.f*NPC->BodySize),NPC->ActionLabel,Muted,12);
   if(NPC->SpeechUntil>Now)Label(Head+FVector(0,0,70.f*NPC->BodySize),NPC->LastSpeech.Left(42),Gold,14);
  }
 }
 for(auto It=Observations.CreateIterator();It;++It)
  if(!It.Value().Character.IsValid()||(It.Key()!=P->AgentId&&!VisibleIds.Contains(It.Key())))It.RemoveCurrent();
 auto AttackArc=[&](AVerdantCharacter* Who) {
  if(Who->bDead||Who->AttackTimer<=0||bConversationOpen)return;
  const auto* Observation=Observations.Find(Who->AgentId);
  const bool Flash=Who->bHitApplied&&Observation&&Now<Observation->StrikeFlashUntil;
  if(Who->bHitApplied&&!Flash)return;
  const float Progress=FMath::Clamp(1.f-Who->AttackTimer/FMath::Max(.01f,Who->AttackDuration),0.f,1.f);
  FLinearColor Color=Who!=P&&Who->HostileId==P->AgentId?FLinearColor(1.f,.32f,.20f):Gold;
  Color.A=Flash?.95f:.30f+.55f*Progress;
  const float Radius=Who->Reach();
  const FVector Forward=Who->GetActorForwardVector().GetSafeNormal2D();
  const FVector Origin=Who->GetActorLocation();
  const float HalfAngle=FMath::RadiansToDegrees(FMath::Acos(.25f));
  constexpr int32 Segments=12;
  for(int32 I=0;I<Segments;++I) {
   const float A=FMath::Lerp(-HalfAngle,HalfAngle,float(I)/Segments);
   const float B=FMath::Lerp(-HalfAngle,HalfAngle,float(I+1)/Segments);
   WorldLine(W->SurfacePoint(Origin+Forward.RotateAngleAxis(A,FVector::UpVector)*Radius,10.f),
             W->SurfacePoint(Origin+Forward.RotateAngleAxis(B,FVector::UpVector)*Radius,10.f),Color,Flash?3.f:1.5f);
  }
  const FVector Tip=W->SurfacePoint(Origin+Forward*Radius,12.f);
  WorldLine(W->SurfacePoint(Origin+Forward*Radius*.35f,12.f),Tip,Color,2.f);
  for(float Side:{-1.f,1.f})
   WorldLine(Tip,W->SurfacePoint(Origin+Forward*Radius*.78f+Who->GetActorRightVector()*Side*Radius*.10f,12.f),Color,2.f);
 };
 AttackArc(P);
 VisibleFolk.Sort([P](const AVerdantCharacter& A,const AVerdantCharacter& B){
  const bool AThreat=A.HostileId==P->AgentId,BThreat=B.HostileId==P->AgentId;
  if(AThreat!=BThreat)return AThreat;
  return FVector::DistSquared2D(A.GetActorLocation(),P->GetActorLocation())<FVector::DistSquared2D(B.GetActorLocation(),P->GetActorLocation());
 });
 int32 OtherArcs=0;
 for(AVerdantCharacter* NPC:VisibleFolk)if(OtherArcs<2&&NPC->AttackTimer>0&&FVector::Dist2D(NPC->GetActorLocation(),P->GetActorLocation())<1800.f) {
  AttackArc(NPC);++OtherArcs;
 }
 for(const auto& Float:DamageFloats)if(Float.Character.IsValid()&&VisibleIds.Contains(Float.Character->AgentId)) {
  const float Age=Now-Float.BornAt;FLinearColor Color=Gold;Color.A=FMath::Clamp(1.f-Age/DamageLifetime,0.f,1.f);
  FVector2D At;
  if(Project(Float.Position+FVector(0,0,(28.f+Age*65.f)*Float.Character->BodySize),At)) {
   // Keep the short damage number beside the actor's centered name/action rows.
   At.X+=At.X+160.f<Canvas->SizeX?118.f:-118.f;
   TextAt(At,Float.Amount<10.f?FString::Printf(TEXT("-%.1f"),Float.Amount):FString::Printf(TEXT("-%.0f"),Float.Amount),Color,17);
  }
 }
 const float Height=P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2.f;
 if(Height>LastPlayerHeightCm+.02f)GrowthVisibleUntil=Now+2.4f;
 LastPlayerHeightCm=Height;
 const bool Absorbing=P->Nutrition>.01f&&P->BodySize<2.5f;
 if(!P->bDead&&!bConversationOpen&&P->AttackTimer<=0&&P->HurtTimer<=0&&Height>StartingHeightCm+.5f&&(Absorbing||Now<GrowthVisibleUntil)) {
  const FVector Feet=P->GetActorLocation()-FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
  const FVector Right=FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y).GetSafeNormal2D();
  const FVector Base=Feet+Right*FMath::Max(70.f,P->BodySize*80.f);
  FVector2D Bottom,Start,Top;
  if(Project(Base,Bottom)&&Project(Base+FVector(0,0,StartingHeightCm),Start)&&Project(Base+FVector(0,0,Height),Top)) {
   FLinearColor Color=Gold;Color.A=Absorbing?.8f:FMath::Clamp((GrowthVisibleUntil-Now)/.8f,0.f,.8f);
   DrawLine(Bottom.X,Bottom.Y,Top.X,Top.Y,Color,1.5f);
   DrawLine(Top.X-5,Top.Y,Top.X+5,Top.Y,Color,2.f);
   FLinearColor Old=Muted;Old.A=Color.A;
   DrawLine(Start.X-5,Start.Y,Start.X+5,Start.Y,Old,1.5f);
   if(FMath::Abs(Start.Y-Top.Y)>32.f) {
    TextAt(Top+FVector2D(10,-7),FString::Printf(TEXT("现在 %.2fm"),Height/100.f),Color,12,false);
    TextAt(Start+FVector2D(10,-7),TEXT("出发时 0.81m"),Old,12,false);
   } else TextAt(Top+FVector2D(10,-7),FString::Printf(TEXT("%.2fm · 出发时0.81m"),Height/100.f),Color,12,false);
  }
 }
 if(!bConversationOpen) {
  const float HalfHeight=P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
  const FVector ScreenRight=FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
  FVector2D HeadAt,FeetAt,CenterAt,SideAt;
  const bool BodyOnScreen=PC->ProjectWorldLocationToScreen(P->GetActorLocation()+FVector(0,0,HalfHeight),HeadAt)
   &&PC->ProjectWorldLocationToScreen(P->GetActorLocation()-FVector(0,0,HalfHeight),FeetAt)
   &&PC->ProjectWorldLocationToScreen(P->GetActorLocation(),CenterAt)
   &&PC->ProjectWorldLocationToScreen(P->GetActorLocation()+ScreenRight*P->GetCapsuleComponent()->GetScaledCapsuleRadius(),SideAt);
  const float BodyHalfWidth=BodyOnScreen?FMath::Max(36.f,float(FMath::Abs(SideAt.X-CenterAt.X))+20.f):0.f;
  auto PlaceLabel=[&](const FVector& Position,const FString& Text,FLinearColor Color,int32 Size) {
   FVector2D At;if(!Project(Position,At))return;
   // Conservative text bounds include the shadow and Chinese glyph widths.
   const float TextHalfWidth=Size*Text.Len()*.75f,TextHeight=Size*1.7f;
   if(At.Y<230.f&&(At.X-TextHalfWidth<340.f||At.X+TextHalfWidth>Canvas->SizeX-370.f))return;
   if(BodyOnScreen&&At.X+TextHalfWidth>=FMath::Min(HeadAt.X,FeetAt.X)-BodyHalfWidth
     &&At.X-TextHalfWidth<=FMath::Max(HeadAt.X,FeetAt.X)+BodyHalfWidth
     &&At.Y+TextHeight>=FMath::Min(HeadAt.Y,FeetAt.Y)-16.f&&At.Y<=FMath::Max(HeadAt.Y,FeetAt.Y)+16.f)return;
   TextAt(At,Text,Color,Size);
  };
  for(const auto& G:W->Groves)if(FVector::Dist2D(P->GetActorLocation(),G.Position)<11000.f)PlaceLabel(G.Position+FVector(0,0,300),G.Name,G.Color,17);
  PlaceLabel(W->MotherPosition+FVector(0,0,1700),W->bRestored?TEXT("母树 · 新生"):TEXT("母树"),Gold,20);
 }
}
