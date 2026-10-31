#include "VerdantHUD.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantPlayerScale.h"
#include "VerdantDialogue.h"
#include "VerdantFoodPickup.h"
#include "VerdantEconomyRules.h"
#include "VerdantPopulation.h"
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
#include "VerdantHUDStyle.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SSafeZone.h"
#include "TimerManager.h"
#include "Engine/UserInterfaceSettings.h"

namespace
{
	using namespace VerdantUI;
	constexpr float StartingHeightCm = VerdantPlayerScale::StartingHeightCm;
	constexpr float DamageLifetime = .85f;
	constexpr int32 MaxDamageFloats = 6;

	FString Stance(const AVerdantCharacter* NPC)
	{
		if (!NPC)return TEXT("");
		if (NPC->HostileId == 0)return TEXT("敌对");
		if (NPC->FollowId == 0)return NPC->bProtectLeader ? TEXT("保护你") : TEXT("同行");
		const float Trust = NPC->Trust.FindRef(0);
		if (Trust >= 10.f)return TEXT("友善");
		if (Trust < 0.f || NPC->bWild)return TEXT("戒备");
		return TEXT("可以交谈");
	}

	FLinearColor StanceColor(const AVerdantCharacter* NPC)
	{
		if (NPC->HostileId == 0)return FLinearColor(1.f, .35f, .24f);
		if (NPC->FollowId == 0 || NPC->Trust.FindRef(0) >= 10.f)return FLinearColor(.49f, .88f, .63f);
		if (NPC->Trust.FindRef(0) < 0.f || NPC->bWild)return FLinearColor(1.f, .72f, .35f);
		return Ink;
	}

	FString PlayerStake(const AVerdantCharacter* P, const AVerdantWorld* W)
	{
		if (!P || !W)return TEXT("");
		int32 Groves = 0;
		for (const auto& G : W->Groves)if (G.ControllerId == P->AgentId)++Groves;
		return FString::Printf(TEXT("你的筹码：携带 %.1f 份食物 · 管理 %d 片果林 · 身高 %.2f 米"), P->Food, Groves, P->BodySize * 1.8f);
	}

    FString ContextHint(AVerdantCharacter* P, AVerdantWorld* W)
    {
        if (!P || !W || P->bDead) return TEXT("");
        for (AVerdantCharacter* N : W->Folk)
            if (IsValid(N) && N != P && N->bDead && N->Food > .001f && FVector::Dist2D(P->GetActorLocation(),N->GetActorLocation())<FMath::Max(220.f,P->Reach()+130.f))
                return P->FoodTier() >= N->FoodTier()
                    ? FString::Printf(TEXT("[E] 第%d档掉落 %.1f份 · 超出容量的仍留在原地"),N->FoodTier(),N->Food)
                    : FString::Printf(TEXT("第%d档掉落 · 需要身高 %.2f米"),N->FoodTier(),VerdantEconomy::MinimumHeight(N->FoodTier())/100.f);
        AVerdantFoodPickup* Best=nullptr; float BestDistance=FMath::Square(500.f);
        for (AVerdantFoodPickup* F : W->FoodPickups)
        {
            if(!IsValid(F)||F->Remaining<=.001f)continue;
            const float D=FVector::DistSquared2D(P->GetActorLocation(),F->GetActorLocation());
            if(D<BestDistance){BestDistance=D;Best=F;}
        }
        FString Hint;
        if(Best)
            Hint=P->FoodTier()>=Best->FoodTier
                ? FString::Printf(TEXT("[E] 第%d档食物 · 剩余 %.1f · 可装 %.1f"),Best->FoodTier,Best->Remaining,P->FreeFoodCapacity())
                : FString::Printf(TEXT("第%d档食物 · 需要 %.2f米，当前 %.2f米"),Best->FoodTier,VerdantEconomy::MinimumHeight(Best->FoodTier)/100.f,P->HeightCm()/100.f);
        if(FVector::Dist2D(P->GetActorLocation(),W->MotherPosition)<1000.f&&!W->bRestored)
            Hint+=FString::Printf(TEXT("  [F] 供奉 · 母树还需 %.0f份"),W->TreeGoal-W->TreeNutrition);
        if(auto* N=P->NearestFolk(2000.f))
            Hint+=FString::Printf(TEXT("  [T] %s · 第%d档 · %s"),*N->DisplayName,N->FoodTier(),*Stance(N));
        if(Hint.IsEmpty())Hint=TEXT("寻找彩色食物 · 按住 E 获取 · 每10秒自动消耗库存的20%成长");
        return Hint;
    }

	FString DeliveryHint(AVerdantWorld* W)
	{
		if (!W || !W->Dialogue || !W->Dialogue->Selected.IsValid())return TEXT("");
		auto* N = W->Dialogue->Selected.Get();
		if (N->bDead && N->DonationPledge > .001f)return N->DisplayName + TEXT("已倒下，未完成的运送已中断。");
		if (N->DonationPledge > .001f)
		{
			const float Dist = FVector::Dist2D(N->GetActorLocation(), W->MotherPosition) / 100.f;
			return FString::Printf(TEXT("%s → 母树 · 待送 %.0f 份 · 距目的地 %.0f 米"), *N->DisplayName, N->DonationPledge, Dist);
		}
		if (N->LastDonationTime >= 0 && W->GetWorld()->GetTimeSeconds() - N->LastDonationTime < 12.f)
			return FString::Printf(TEXT("%s 已送达 %.0f 份养分"), *N->DisplayName, N->LastDonatedAmount);
		if (N->FollowId == 0 && !N->bDead)return N->DisplayName + TEXT("正在与你同行。");
		return TEXT("");
	}
}

class SVerdantConversation : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerdantConversation)
		{
		}

		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Close = Args._OnClose;
		ChildSlot[Args._Content.Widget];
	}

	virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& E) override
	{
		if (E.GetKey() == EKeys::Escape || E.GetKey() == EKeys::Gamepad_FaceButton_Right)
		{
			Close.ExecuteIfBound();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	FSimpleDelegate Close;
};

bool AVerdantHUD::HasCanvasFont() const
{
	return IsValid(CanvasFont.Get()) && CanvasFont->FontCacheType == EFontCacheType::Runtime
		&& CanvasFont->GetInternalCompositeFont().DefaultTypeface.Fonts.Num() > 0;
}

void AVerdantHUD::BeginPlay()
{
	Super::BeginPlay();
	const FSlateFontInfo SourceFont = Font(14);
	CanvasFont = NewObject<UFont>(this,TEXT("VerdantCanvasFont"), RF_Transient);
	CanvasFont->FontCacheType = EFontCacheType::Runtime;
	CanvasFont->GetMutableInternalCompositeFont() = *SourceFont.GetCompositeFont();
	auto Player = [this]() { return Cast<AVerdantCharacter>(GetOwningPawn()); };
	auto World = [this]() { return AVerdantWorld::Find(this); };
	// Legacy gameplay exposes mutable fields. Sample at 10 Hz, push only changed text
	// into Slate; never rebuild world summaries or format strings during widget paint.
	auto Updates = MakeShared<TArray<TFunction<void()>>>();
	auto Text = [Updates](TFunction<FString()> Read, int32 Size, FLinearColor Color,
	                      bool Wrap = true)-> TSharedRef<STextBlock>
	{
		auto Widget = SNew(STextBlock).Font(Font(Size)).ColorAndOpacity(Color).AutoWrapText(Wrap).Visibility(
				EVisibility::Collapsed);
		Updates->Add([Weak=TWeakPtr<STextBlock>(Widget),Read,Previous=FString()]() mutable
		{
			if (auto W = Weak.Pin())
			{
				FString Value = Read();
				if (Value != Previous)
				{
					W->SetText(FText::FromString(Value));
					W->SetVisibility(Value.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
					Previous = MoveTemp(Value);
				}
			}
		});
		return Widget;
	};
	auto Bar = [Updates](TFunction<float()> Read, FLinearColor Color, float Height = 6.f)-> TSharedRef<SWidget>
	{
		auto Widget = SNew(SProgressBar).Style(&Theme().Bar).FillColorAndOpacity(Color).Percent(0.f);
		Updates->Add([Weak=TWeakPtr<SProgressBar>(Widget),Read]()
		{
			if (auto W = Weak.Pin())W->SetPercent(FMath::Clamp(Read(), 0.f, 1.f));
		});
		return SNew(SBox).HeightOverride(Height)
			[
				Widget
			];
	};
	auto Card = [](TSharedRef<SWidget> Content, FMargin Padding = FMargin(20))-> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(&Theme().Card).Padding(Padding)
			[
				Content
			];
	};
	auto Send = [this]()
	{
		auto* W = AVerdantWorld::Find(this);
		if (W && W->Dialogue && InputBox.IsValid() && W->Dialogue->Selected.IsValid() && W->Dialogue->CanAcceptPlayerMessage(W->Dialogue->Selected.Get()))
		{
			const FString Line = InputBox->GetText().ToString().TrimStartAndEnd();
			if (!Line.IsEmpty())
			{
				W->Dialogue->Request(W->Dialogue->Selected.Get(), Line);
				W->PlayUISound(TEXT("Confirm"));
				InputBox->SetText(FText::GetEmpty());
			}
		}
		if (InputBox.IsValid())FSlateApplication::Get().SetKeyboardFocus(InputBox);
		return FReply::Handled();
	};
	auto Proposal = [this](const FString& Title, const FString& Prompt)-> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(&Theme().Button)
			.HAlign(HAlign_Center)
			.ContentPadding(FMargin(12, 10))
			.OnClicked_Lambda([this,Prompt]()
			{
				FillConversationPrompt(Prompt);
				return FReply::Handled();
			})
			[
				Label(Title, 13, Ink)
			];
	};
	auto Alive = [Player,this]()
	{
		auto* P = Player();
		return !bConversationOpen && P && !P->bDead ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
	};
	auto Overlay = SNew(SOverlay)
		// The two corner modules share a visual language but leave the canopy and horizon open.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(32)
		[
			SNew(SBox).WidthOverride(310).Visibility_Lambda(Alive)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
						[
							SNew(SCrest)
						]
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								Label(TEXT("长大以后"), 24, Ink)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
							[
								Label(TEXT("V E R D A N T   A C C O R D"), 8, Gold)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
							[
								Label(TEXT("旅人  /  生长季"), 12, Muted)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 12)
					[
						Rule()
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							Text([Player]()
							{
								auto* P = Player();
								return P && P->Health / P->MaxHealth() < .3f ? TEXT("生命 · 危险") : TEXT("生命");
							}, 13, Ink)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Text([Player]()
							{
								auto* P = Player();
								return P ? FString::Printf(TEXT("%.0f / %.0f"), P->Health, P->MaxHealth()) : TEXT("—");
							}, 13, Ink, false)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 7, 0, 14)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							Bar([Player]()
							{
								auto* P = Player();
								return P ? P->Health / FMath::Max(1.f, P->MaxHealth()) : 0.f;
							}, Leaf, 7)
						]
						+ SOverlay::Slot()
						[
							SNew(SBox).Visibility_Lambda([Player]()
							{
								auto* P = Player();
								return P && P->Health / P->MaxHealth() < .3f
									       ? EVisibility::HitTestInvisible
									       : EVisibility::Collapsed;
							})
							[
								Bar([Player]()
								{
									auto* P = Player();
									return P ? P->Health / FMath::Max(1.f, P->MaxHealth()) : 0.f;
								}, Danger, 7)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								Label(TEXT("随身食物"), 12, Muted)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
							[
								Text([Player]()
								{
									auto* P = Player();
									return P ? FString::Printf(TEXT("%.0f / %.0f"), P->Food, P->FoodCapacity()) : TEXT("—");
								}, 24, Gold)
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								Label(TEXT("当前身高"), 12, Muted)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
							[
								Text([Player]()
								{
									auto* P = Player();
									return P ? FString::Printf(TEXT("%.2f 米"), P->BodySize * 1.8f) : TEXT("—");
								}, 24, Ink)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
					[
						Text([Player]()
						{
							auto* P = Player();
                            return P ? FString::Printf(TEXT("第%d档 · %.0f秒后自动成长%s"),P->FoodTier(),P->GrowthSecondsRemaining(),P->IsRespawnProtected()?TEXT(" · 复活保护"):TEXT("")) : TEXT("");
						}, 12, Muted)
					]
				)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(32)
		[
			SNew(SBox).WidthOverride(320).Visibility_Lambda(Alive)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Card(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							Label(TEXT("山 谷 的 心 愿     /     01"), 11, Gold)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
						[
							Text([World]()
							{
								auto* W = World();
								return W && W->bRestored ? TEXT("母树，重获新生") : TEXT("让母树重新结果");
							}, 22, Ink)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							Text([World]()
							{
								auto* W = World();
								return W && W->bRestored ? TEXT("本季已留痕，山谷的故事仍在继续。") : TEXT("分享收成，为山谷带回生机。");
							}, 12, Muted)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 7)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1)
							[
								Label(TEXT("已送达的养分"), 12, Muted)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								Text([World]()
								{
									auto* W = World();
									return W
										       ? FString::Printf(TEXT("%.0f / %.0f"), W->TreeNutrition, W->TreeGoal)
										       : TEXT("—");
								}, 16, Gold, false)
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							Bar([World]()
							{
								auto* W = World();
								return W ? W->TreeNutrition / FMath::Max(1.f, W->TreeGoal) : 0.f;
							}, Gold)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
						[
							Text([World,Player]()
							{
								auto* W = World();
								auto* P = Player();
								if (!W || !P)return FString();
								return W->bRestored
									       ? TEXT("已完成 · Tab 查看本季记录")
									       : FString::Printf(
										       TEXT("母树距你 %.0f 米  ·  靠近后 F 供奉"),
										       FVector::Dist2D(P->GetActorLocation(), W->MotherPosition) / 100.f);
							}, 12, Muted)
						]
					)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(SBox).Visibility_Lambda([this,World]()
					{
						auto* W = World();
						return bJournalOpen ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
					})
					[
						Card(SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								Text([World]()
								{
									auto* W = World();
									return W && W->RestoredSeason.bCaptured ? TEXT("本季记录") : TEXT("生态手记");
								}, 18, Gold)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
							[
								SNew(SBox).MaxDesiredHeight(300)
								[
									SAssignNew(JournalScroll, SScrollBox)
									+ SScrollBox::Slot()
									[
										Text([this,World]()
										{
											auto* W = World();
											if (!W || !bJournalOpen)return FString();
											return W->RestoredSeason.bCaptured
												       ? W->SeasonCoreSummary + TEXT("\n\n当前山谷\n") + W->WorldSummary()
												       : W->WorldSummary();
										}, 13, Ink)
									]
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
							[
								Label(TEXT("Tab 收起  ·  PgUp / PgDn 翻阅"), 11, Muted)
							]
						)
					]
				]
			]
		]
		// A small event ribbon, rather than a second permanent wall of prose.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(32, 0, 0, 188)
		[
			SNew(SBox).WidthOverride(300).Visibility_Lambda(Alive)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Label(TEXT("山 谷 见 闻"), 10, Gold)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					Text([World]()
					{
						auto* W = World();
						return W && W->Events.Num() ? W->Events.Last() : FString();
					}, 12, Ink)
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(24, 0, 24, 28)
		[
			SNew(SBox).WidthOverride(850).Visibility_Lambda(Alive)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
				[
					SNew(SBox).WidthOverride(730)
					[
						SNew(SBorder).BorderImage(&Theme().Inset).Padding(FMargin(18, 10))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								Text([Player,World]() { return ContextHint(Player(), World()); }, 14, Ink)
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								Text([World]() { return DeliveryHint(World()); }, 12, Gold)
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								Text([World]()
								{
									auto* W = World();
									return W && W->GetWorld()->GetTimeSeconds() < W->NoticeUntil
										       ? W->Notice
										       : FString();
								}, 13, Gold)
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Card(SNew(SHorizontalBox)
					     + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
					     [
						     Action(TEXT("E"),TEXT("采集"))
					     ]
					     + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
					     [
						     Action(TEXT("10秒"),TEXT("自动成长"))
					     ]
					     + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
					     [
						     Action(TEXT("F"),TEXT("分享 / 供奉"))
					     ]
					     + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
					     [
						     Action(TEXT("T"),TEXT("交谈"))
					     ]
					     + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
					     [
						     Action(TEXT("B"),TEXT("养护"))
					     ]
					     + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
					     [
						     Action(TEXT("G"),TEXT("管理"))
					     ]
					     , FMargin(12, 10))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)
				[
					Label(TEXT("Tab 生态手记     ·     H 操作指南"), 11, Ink)
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 60, 0, 0)
		[
			SNew(SBox).WidthOverride(500).Visibility_Lambda(Alive)
			[
				Text([Player]()
				{
					auto* P = Player();
					return P && P->GetWorld()->GetTimeSeconds() < P->FeedbackUntil ? P->CombatFeedback : FString();
				}, 20, Gold)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(580).Visibility_Lambda([this,Player]()
			{
				auto* P = Player();
				return bControlsOpen && !bConversationOpen && P && !P->bDead
					       ? EVisibility::SelfHitTestInvisible
					       : EVisibility::Collapsed;
			})
			[
				Card(SNew(SVerticalBox)
				     + SVerticalBox::Slot().AutoHeight()
				     [
					     Label(TEXT("旅人的随行指南"), 26, Gold)
				     ]
				     + SVerticalBox::Slot().AutoHeight().Padding(0, 14)
				     [
					     Rule()
				     ]
				     + SVerticalBox::Slot().AutoHeight()
				     [
					     Label(TEXT(
						     "WASD  移动    ·    鼠标  观察    ·    Shift  奔跑\n\n左键 / 右键  轻击 / 重击    ·    空格  闪避\n\n按住 E  连续采集 / 战利品    ·    每10秒自动消耗20%库存并成长\n\nF  分享 / 供奉    ·    Shift + F  批量供奉\n\nT  交谈    ·    B  养护土壤    ·    G  管理果林\n\nTab  生态手记    ·    R  重新开始这一局"), 15,
					           Ink)
				     ]
				     + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)
				     [
					     Label(TEXT("H / Esc 收起指南"), 12, Muted)
				     ]
				     , FMargin(28))
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(530).Visibility_Lambda([Player]()
			{
				auto* P = Player();
				return P && P->bDead ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
			})
			[
				Card(SNew(SVerticalBox)
				     + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				     [
					     SNew(SCrest)
				     ]
				     + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 8)
				     [
					     Label(TEXT("正在重新凝聚"), 30, Gold)
				     ]
				     + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				     [
					     Label(TEXT("损失15%体型后复活，世界和关系继续保留。"), 15, Ink)
				     ]
				     + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24, 0, 0)
				     [
					     Text([Player](){auto* P=Player();return P?FString::Printf(TEXT("%.1f秒后回到安全区域"),P->RespawnSecondsRemaining()):FString();},16,Gold)
				     ]
				     , FMargin(32))
			]
		]
		// Conversation is a single focus scope; Escape works from every child control.
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.006f, .012f, .009f, .64f))
			.Visibility_Lambda([this]()
			{
				return bConversationOpen ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
			})
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(780).Visibility_Lambda([this]()
			{
				return bConversationOpen ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				SNew(SVerdantConversation).OnClose(
					FSimpleDelegate::CreateUObject(this, &AVerdantHUD::CloseConversation))
				[
					Card(SNew(SVerticalBox)
					     + SVerticalBox::Slot().AutoHeight()
					     [
						     Label(TEXT("林 间 对 话     /     以 诚 相 待"), 11, Gold)
					     ]
					     + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 8)
					     [
						     Text([World]()
						     {
							     auto* W = World();
							     auto* N = W && W->Dialogue ? W->Dialogue->Selected.Get() : nullptr;
							     return N
								            ? FString::Printf(TEXT("%s  ·  %s"), *N->DisplayName, *Stance(N))
								            : TEXT("交谈");
						     }, 27, Ink)
					     ]
					     + SVerticalBox::Slot().AutoHeight()
					     [
						     Text([World]()
						     {
							     auto* W = World();
							     auto* N = W && W->Dialogue ? W->Dialogue->Selected.Get() : nullptr;
							     return N ? N->ActionLabel + TEXT(" · ") + N->Motivation : FString();
						     }, 13, Muted)
					     ]
					     + SVerticalBox::Slot().AutoHeight().Padding(0, 10)
					     [
						     Rule()
					     ]
					     + SVerticalBox::Slot().AutoHeight()
					     [
						     Text([World]()
						     {
							     auto* W = World();
							     auto* N = W && W->Dialogue ? W->Dialogue->Selected.Get() : nullptr;
							     return N ? W->NegotiationSummary(N) : FString();
						     }, 13, Ink)
					     ]
					     + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 14)
					     [
						     Text([Player,World]() { return PlayerStake(Player(), World()); }, 12, Gold)
					     ]
					     + SVerticalBox::Slot().AutoHeight()
					     [
						     SNew(SHorizontalBox)
						     + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)
						     [
							     Proposal(TEXT("问问打算"),TEXT("你现在最在意什么？我们怎样合作对彼此都有好处？"))
						     ]
						     + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)
						     [
							     Proposal(TEXT("用食物交换"),TEXT("我可以分享一些食物。你愿意给我采集许可或降低分成吗？请说说你的条件。"))
						     ]
						     + SHorizontalBox::Slot().FillWidth(1)
						     [
							     Proposal(TEXT("商量让地"),TEXT("如果我承担果林的养护，你愿意和平地把管理权交给我吗？你希望换取什么？"))
						     ]
					     ]
					     + SVerticalBox::Slot().AutoHeight().Padding(0, 14)
					     [
						     SNew(SBorder).BorderImage(&Theme().Inset).Padding(16)
						     [
							     SNew(SBox).HeightOverride(170)
							     [
								     SAssignNew(ConversationScroll, SScrollBox)
								     + SScrollBox::Slot()
								     [
									     Text([World]()
									     {
										     FString Value;
										     auto* W = World();
										     if (W && W->Dialogue)
											     for (const FString& Line : W->Dialogue->Lines)
												     Value +=
													     Line + TEXT("\n\n");
										     return Value.IsEmpty()
											            ? TEXT("先听听对方的打算，再提出你的交换条件。\n\n上方提议只填入文字，按发送才会开口。")
											            : Value;
									     }, 15, Ink)
								     ]
							     ]
						     ]
					     ]
					     + SVerticalBox::Slot().AutoHeight()
					     [
						     SAssignNew(InputBox, SMultiLineEditableTextBox)
						     .Style(&Theme().Input)
						     .Font(Font(16))
						     .ForegroundColor(Ink)
						     .HintText(FText::FromString(TEXT("说说你的提议……")))
						     .AutoWrapText(true)
						     .AllowMultiLine(false)
						     .Padding(FMargin(14, 12))
						     .OnTextCommitted_Lambda([Send](const FText&, ETextCommit::Type Kind)
						     {
							     if (Kind == ETextCommit::OnEnter)Send();
						     })
					     ]
					     + SVerticalBox::Slot().AutoHeight().Padding(0, 10)
					     [
						     Text([World]()
						     {
							     auto* W = World();
							     return W && W->Dialogue ? W->Dialogue->Status : FString();
						     }, 12, Muted)
					     ]
					     + SVerticalBox::Slot().AutoHeight()
					     [
						     SNew(SHorizontalBox)
						     + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
						     [
							     Label(TEXT("承诺需要行动，送达才计入养分。"), 12, Muted)
						     ]
						     + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
						     [
							     SNew(SButton)
							     .ButtonStyle(&Theme().Button)
							     .ContentPadding(FMargin(16, 10))
							     .IsEnabled_Lambda([this,World]()
							     {
								     auto* W = World();
								     return W && W->Dialogue && W->Dialogue->CanAcceptPlayerMessage(W->Dialogue->Selected.Get()) && InputBox.IsValid() && !InputBox->
									     GetText().ToString().TrimStartAndEnd().IsEmpty();
							     })
							     .OnClicked_Lambda(Send)
							     [
								     Label(TEXT("发送  Enter"), 14, Gold)
							     ]
						     ]
						     + SHorizontalBox::Slot().AutoWidth()
						     [
							     SNew(SButton)
							     .ButtonStyle(&Theme().Button)
							     .ContentPadding(FMargin(16, 10))
							     .OnClicked_Lambda([this]()
							     {
								     CloseConversation();
								     return FReply::Handled();
							     })
							     [
								     Label(TEXT("告别  Esc"), 14, Ink)
							     ]
						     ]
					     ]
					     , FMargin(28))
				]
			]
		];
	// Fit the reference height on wide displays; expand corner anchoring into spare width.
	RootWidget = SNew(SDPIScaler).DPIScale_Lambda([this]()
		{
			int32 X = 1600, Y = 900;
			if (auto* PC = GetOwningPlayerController())PC->GetViewportSize(X, Y);
			return FMath::Max(.25f, FMath::Min(X / 1600.f, Y / 900.f)) / FMath::Max(
				.01f, GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(X, Y)));
		})
		[
			SNew(SSafeZone).IsTitleSafe(true)
			[
				Overlay
			]
		];
	for (auto& Update : *Updates)Update();
	GetWorldTimerManager().SetTimer(PresentationTimer, FTimerDelegate::CreateWeakLambda(this, [this,Updates]()
	{
		for (auto& Update : *Updates)Update();
		auto* W = AVerdantWorld::Find(this);
		if (bConversationOpen && W && ConversationScroll.IsValid() && W->Dialogue && LastDialogueLineCount != W->
			Dialogue->Lines.Num())
		{
			LastDialogueLineCount = W->Dialogue->Lines.Num();
			ConversationScroll->ScrollToEnd();
		}
	}), .1f, true);
	if (GEngine && GEngine->GameViewport)GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), 5);
}

void AVerdantHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(PresentationTimer);
	if (bConversationOpen)CloseConversation();
	if (GEngine && GEngine->GameViewport && RootWidget.IsValid())
		GEngine->GameViewport->RemoveViewportWidgetContent(
			RootWidget.ToSharedRef());
	RootWidget.Reset();
	InputBox.Reset();
	ConversationScroll.Reset();
	JournalScroll.Reset();
	Observations.Reset();
	DamageFloats.Reset();
	Super::EndPlay(Reason);
}

void AVerdantHUD::OpenConversation(AVerdantCharacter* NPC)
{
	auto* W = AVerdantWorld::Find(this);
	if (!W || !W->Dialogue || !IsValid(NPC) || NPC->bDead || !W->Dialogue->CanConverse(NPC))return;
	const bool ChangedNPC = W->Dialogue->Selected.Get() != NPC;
	W->Dialogue->SelectNPC(NPC);
	if (ChangedNPC)
	{
		if (InputBox.IsValid())InputBox->SetText(FText::GetEmpty());
		LastDialogueLineCount = -1;
	}
	if (bConversationOpen)
	{
		if (InputBox.IsValid())FSlateApplication::Get().SetKeyboardFocus(InputBox);
		return;
	}
	bJournalOpen = false;
	bControlsOpen = false;
	bConversationOpen = true;
	W->PlayUISound(TEXT("Open"));
	if (auto* P = Cast<AVerdantCharacter>(GetOwningPawn()))P->CancelForConversation();
	if (auto* PC = GetOwningPlayerController())
	{
		PC->StopMovement();
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
		PC->bShowMouseCursor = true;
		FInputModeUIOnly Mode;
		if (InputBox.IsValid())Mode.SetWidgetToFocus(InputBox);
		PC->SetInputMode(Mode);
	}
	if (InputBox.IsValid())FSlateApplication::Get().SetKeyboardFocus(InputBox);
}

void AVerdantHUD::FillConversationPrompt(const FString& Text)
{
	if (!bConversationOpen || !InputBox.IsValid())return;
	InputBox->SetText(FText::FromString(Text));
	if (auto* W = AVerdantWorld::Find(this)) W->PlayUISound(TEXT("Tick"));
	FSlateApplication::Get().SetKeyboardFocus(InputBox);
}

void AVerdantHUD::CloseConversation()
{
	if (bConversationOpen)
		if (auto* W = AVerdantWorld::Find(this)) W->PlayUISound(TEXT("Close"));
	bConversationOpen = false;
	if (auto* PC = GetOwningPlayerController())
	{
		PC->ResetIgnoreMoveInput();
		PC->ResetIgnoreLookInput();
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void AVerdantHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)return;
	auto* W = AVerdantWorld::Find(this);
	auto* P = Cast<AVerdantCharacter>(GetOwningPawn());
	auto* PC = GetOwningPlayerController();
	if (!W || !P || !PC)return;
	if (W->RestoredSeason.bCaptured && !bSeasonPresented && !bConversationOpen)
	{
		bJournalOpen = true;
		bSeasonPresented = true;
	}
	if (!bConversationOpen && !P->bDead)
	{
		if (PC->WasInputKeyJustPressed(EKeys::Tab))
		{
			bJournalOpen = !bJournalOpen;
			W->PlayUISound(bJournalOpen ? TEXT("Open") : TEXT("Close"));
		}
		if (JournalScroll.IsValid() && bJournalOpen)
		{
			float Step = PC->WasInputKeyJustPressed(EKeys::PageDown)
				             ? 230.f
				             : PC->WasInputKeyJustPressed(EKeys::PageUp)
				             ? -230.f
				             : 0.f;
			if (Step != 0.f)
				JournalScroll->SetScrollOffset(
					FMath::Clamp(JournalScroll->GetScrollOffset() + Step, 0.f, JournalScroll->GetScrollOffsetOfEnd()));
		}
		if (PC->WasInputKeyJustPressed(EKeys::H))
		{
			bControlsOpen = !bControlsOpen;
			W->PlayUISound(bControlsOpen ? TEXT("Open") : TEXT("Close"));
		}
		if (PC->WasInputKeyJustPressed(EKeys::Escape))
		{
			if (bControlsOpen || bJournalOpen) W->PlayUISound(TEXT("Close"));
			bControlsOpen = false;
			bJournalOpen = false;
		}
	}
	const float HUDScale = FMath::Max(.25f, FMath::Min(Canvas->SizeX / 1600.f, Canvas->SizeY / 900.f));
	const float Now = GetWorld()->GetTimeSeconds();
	if (bConversationOpen && (P->bDead || !W->Dialogue || !W->Dialogue->Selected.IsValid() || W->Dialogue->Selected->
		bDead))
		CloseConversation();
	else if (bConversationOpen && !W->Dialogue->CanConverse(W->Dialogue->Selected.Get()))
	{
		CloseConversation();
		W->Notify(TEXT("对方已经走远，靠近后再交谈。"));
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	auto Project = [this,PC,HUDScale](const FVector& Position, FVector2D& At, float BottomReserve = 175.f)
	{
		return PC->ProjectWorldLocationToScreen(Position, At) && At.X >= 18 && At.X <= Canvas->SizeX - 18 && At.Y >= 18
			&& At.Y <= Canvas->SizeY - BottomReserve * HUDScale;
	};
	auto TextAt = [this](FVector2D At, const FString& Text, FLinearColor Color, int32 Size, bool Center = true)
	{
		if (Text.IsEmpty() || !HasCanvasFont())return;
		FCanvasTextItem Item(At, FText::FromString(Text), FSlateFontInfo(CanvasFont.Get(), Size), Color);
		Item.bCentreX = Center;
		Item.EnableShadow(FLinearColor(0, 0, 0, Color.A));
		Canvas->DrawItem(Item);
	};
	auto Label = [&](const FVector& Position, const FString& Text, FLinearColor Color, int32 Size)
	{
		FVector2D At;
		if (Project(Position, At))TextAt(At, Text, Color, Size);
	};
	auto WorldLine = [&](const FVector& A, const FVector& B, FLinearColor Color, float Thickness)
	{
		FVector2D SA, SB;
		if (Project(A, SA, 145.f) && Project(B, SB, 145.f))DrawLine(SA.X, SA.Y, SB.X, SB.Y, Color, Thickness);
	};
    if(!bConversationOpen&&!P->bDead)
    {
        int32 FoodLabels=0;
        for (AVerdantFoodPickup* Food : W->FoodPickups)
        {
            if(!IsValid(Food)||Food->Remaining<=.001f||FVector::DistSquared2D(Food->GetActorLocation(),P->GetActorLocation())>FMath::Square(800.f))continue;
            const bool Allowed=P->FoodTier()>=Food->FoodTier;
            Label(Food->GetActorLocation()+FVector(0,0,35.f),FString::Printf(TEXT("%d档 · %.0f%s"),Food->FoodTier,Food->Remaining,Allowed?TEXT(""):TEXT(" · 未解锁")),Allowed?Gold:Muted,12);
            if(++FoodLabels>=10)break;
        }
    }
	auto Readable = [&](AVerdantCharacter* Who, const FVector& Head)
	{
		if (bConversationOpen || FVector::DistSquared2D(Who->GetActorLocation(), P->GetActorLocation()) >
			FMath::Square(3800.f))
			return false;
		FVector2D At;
		if (!Project(Head, At))return false;
		// Do not observe damage behind the persistent corner panels or opaque scenery.
		if (At.Y < 320.f * HUDScale && (At.X < 360.f * HUDScale || At.X > Canvas->SizeX - 370.f * HUDScale))
			return
				false;
		FHitResult Obstacle;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VerdantHUDSight), false, P);
		Query.AddIgnoredActor(Who);
		return !GetWorld()->LineTraceSingleByChannel(Obstacle, ViewLocation, Head, ECC_Visibility, Query);
	};
	DamageFloats.RemoveAll([Now](const FVerdantHUDDamageFloat& Float)
	{
		return !Float.Character.IsValid() || Float.Character->bDead || Now - Float.BornAt >= DamageLifetime;
	});
	auto Observe = [&](AVerdantCharacter* Who, const FVector& Head)
	{
		auto* Previous = Observations.Find(Who->AgentId);
		const bool SameActor = Previous && Previous->Character.Get() == Who;
		float FlashUntil = SameActor ? Previous->StrikeFlashUntil : -1.f;
		if (SameActor && Who->AttackTimer > 0 && !Previous->bHitApplied && Who->bHitApplied)FlashUntil = Now + .11f;
		// Only a live, already observed actor's real health decrease can produce a number.
		// Growth/healing, first observation, shrinking, and the death transition do not.
		if (SameActor && Who != P && !Who->bDead && !Previous->bDead && Who->HurtTimer > 0 && Who->BodySize + .0001f >=
			Previous->BodySize)
		{
			const float Lost = Previous->Health - Who->Health;
			if (FMath::IsFinite(Lost) && Lost > .05f)
			{
				int32 PerActor = 0;
				for (const auto& Float : DamageFloats)if (Float.Character.Get() == Who)++PerActor;
				if (PerActor >= 2)
				{
					const int32 Oldest = DamageFloats.IndexOfByPredicate([Who](const FVerdantHUDDamageFloat& Float)
					{
						return Float.Character.Get() == Who;
					});
					if (Oldest != INDEX_NONE)DamageFloats.RemoveAt(Oldest);
				}
				if (DamageFloats.Num() >= MaxDamageFloats)DamageFloats.RemoveAt(0);
				FVerdantHUDDamageFloat Float;
				Float.Character = Who;
				Float.Position = Head;
				Float.Amount = Lost;
				Float.BornAt = Now;
				DamageFloats.Add(Float);
			}
		}
		FVerdantHUDObservation Sample;
		Sample.Character = Who;
		Sample.Health = Who->Health;
		Sample.BodySize = Who->BodySize;
		Sample.bDead = Who->bDead;
		Sample.bHitApplied = Who->bHitApplied;
		Sample.StrikeFlashUntil = FlashUntil;
		Observations.Add(Who->AgentId, Sample);
	};
	TSet<int32> VisibleIds;
	TArray<AVerdantCharacter*> VisibleFolk;
	Observe(P, P->GetActorLocation());
	for (AVerdantCharacter* NPC : W->Folk)
		if (IsValid(NPC) && NPC != P)
		{
			const float HalfHeight = NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector Head = NPC->GetActorLocation() + FVector(0, 0, HalfHeight + 15.f * NPC->BodySize);
			if (!Readable(NPC, Head))
			{
				Observations.Remove(NPC->AgentId);
				continue;
			}
			VisibleIds.Add(NPC->AgentId);
			VisibleFolk.Add(NPC);
			Observe(NPC, Head);
			const float Distance = FVector::Dist2D(NPC->GetActorLocation(), P->GetActorLocation());
			FString Name = FString::Printf(TEXT("%s · %s · %.1fm"), *NPC->DisplayName, *Stance(NPC),
			                               HalfHeight * .02f);
			if (NPC->bDead)Name = NPC->Food > 0 ? TEXT("倒下的 ") + NPC->DisplayName + FString::Printf(TEXT(" · 第%d档掉落"),NPC->FoodTier()) : TEXT("");
			Label(Head, Name, NPC->bDead ? Muted : StanceColor(NPC), 14);
			// Injured creatures retain a compact bar below their name until healed.
			// This shares the name's visibility/occlusion gate; damage numbers remain
			// to the side and action/speech labels remain above it.
			const float MaximumHealth = FMath::Max(1.f, NPC->MaxHealth());
			if (!NPC->bDead && FMath::IsFinite(NPC->Health) && NPC->Health < MaximumHealth - .01f)
			{
				FVector2D At;
				if (Project(Head, At))
				{
					const float BarScale = FMath::Clamp(HUDScale, .75f, 1.5f);
					const float Width = 96.f * BarScale, Height = 6.f * BarScale;
					const float Border = BarScale;
					const float Left = At.X - Width * .5f, Top = At.Y + 22.f;
					if (Left >= 18.f && Left + Width <= Canvas->SizeX - 18.f
						&& Top + Height + Border <= Canvas->SizeY - 175.f * HUDScale)
					{
						const float Fraction = FMath::Clamp(NPC->Health / MaximumHealth, 0.f, 1.f);
						const FLinearColor Fill = Fraction < .3f
							                          ? FLinearColor(.95f, .34f, .23f, .95f)
							                          : FLinearColor(.58f, .77f, .40f, .95f);
						DrawRect(FLinearColor(.04f, .06f, .04f, .9f), Left - Border, Top - Border,
						         Width + Border * 2.f, Height + Border * 2.f);
						DrawRect(FLinearColor(.20f, .23f, .18f, .95f), Left, Top, Width, Height);
						DrawRect(Fill, Left, Top, Width * Fraction, Height);
					}
				}
			}
			if (!NPC->bDead && Distance < 1800.f)
			{
				const bool ShowingDamage = DamageFloats.ContainsByPredicate([NPC](const FVerdantHUDDamageFloat& Float)
				{
					return Float.Character.Get() == NPC;
				});
				if (!ShowingDamage)Label(Head + FVector(0, 0, 30.f * NPC->BodySize), NPC->ActionLabel, Muted, 12);
				if (NPC->SpeechUntil > Now)
					Label(Head + FVector(0, 0, 70.f * NPC->BodySize), NPC->LastSpeech.Left(42),
					      Gold, 14);
			}
		}
	for (auto It = Observations.CreateIterator(); It; ++It)
		if (!It.Value().Character.IsValid() || (It.Key() != P->AgentId && !VisibleIds.Contains(It.Key())))
			It.
				RemoveCurrent();
	auto AttackArc = [&](AVerdantCharacter* Who)
	{
		if (Who->bDead || Who->AttackTimer <= 0 || bConversationOpen)return;
		const auto* Observation = Observations.Find(Who->AgentId);
		const bool Flash = Who->bHitApplied && Observation && Now < Observation->StrikeFlashUntil;
		if (Who->bHitApplied && !Flash)return;
		const float Progress = FMath::Clamp(1.f - Who->AttackTimer / FMath::Max(.01f, Who->AttackDuration), 0.f, 1.f);
		FLinearColor Color = Who != P && Who->HostileId == P->AgentId ? FLinearColor(1.f, .32f, .20f) : Gold;
		Color.A = Flash ? .95f : .30f + .55f * Progress;
		const float Radius = Who->Reach();
		const FVector Forward = Who->GetActorForwardVector().GetSafeNormal2D();
		const FVector Origin = Who->GetActorLocation();
		const float HalfAngle = FMath::RadiansToDegrees(FMath::Acos(.25f));
		constexpr int32 Segments = 12;
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A = FMath::Lerp(-HalfAngle, HalfAngle, float(I) / Segments);
			const float B = FMath::Lerp(-HalfAngle, HalfAngle, float(I + 1) / Segments);
			WorldLine(W->SurfacePoint(Origin + Forward.RotateAngleAxis(A, FVector::UpVector) * Radius, 10.f),
			          W->SurfacePoint(Origin + Forward.RotateAngleAxis(B, FVector::UpVector) * Radius, 10.f), Color,
			          Flash ? 3.f : 1.5f);
		}
		const FVector Tip = W->SurfacePoint(Origin + Forward * Radius, 12.f);
		WorldLine(W->SurfacePoint(Origin + Forward * Radius * .35f, 12.f), Tip, Color, 2.f);
		for (float Side : {-1.f, 1.f})
			WorldLine(Tip, W->SurfacePoint(
				          Origin + Forward * Radius * .78f + Who->GetActorRightVector() * Side * Radius * .10f, 12.f),
			          Color, 2.f);
	};
	// The player's creature motion communicates the attack. The old projected
	// sector/arrow reads as a stray line during bumps and slams; only NPC attacks
	// need this warning overlay.
	VisibleFolk.Sort([P](const AVerdantCharacter& A, const AVerdantCharacter& B)
	{
		const bool AThreat = A.HostileId == P->AgentId, BThreat = B.HostileId == P->AgentId;
		if (AThreat != BThreat)return AThreat;
		return FVector::DistSquared2D(A.GetActorLocation(), P->GetActorLocation()) < FVector::DistSquared2D(
			B.GetActorLocation(), P->GetActorLocation());
	});
	int32 OtherArcs = 0;
	for (AVerdantCharacter* NPC : VisibleFolk)
		if (OtherArcs < 2 && NPC->AttackTimer > 0 && FVector::Dist2D(NPC->GetActorLocation(), P->GetActorLocation()) <
			1800.f)
		{
			AttackArc(NPC);
			++OtherArcs;
		}
	for (const auto& Float : DamageFloats)
		if (Float.Character.IsValid() && VisibleIds.Contains(Float.Character->AgentId))
		{
			const float Age = Now - Float.BornAt;
			FLinearColor Color = Gold;
			Color.A = FMath::Clamp(1.f - Age / DamageLifetime, 0.f, 1.f);
			FVector2D At;
			if (Project(Float.Position + FVector(0, 0, (28.f + Age * 65.f) * Float.Character->BodySize), At))
			{
				// Keep the short damage number beside the actor's centered name/action rows.
				At.X += At.X + 160.f < Canvas->SizeX ? 118.f : -118.f;
				TextAt(At, Float.Amount < 10.f
					           ? FString::Printf(TEXT("-%.1f"), Float.Amount)
					           : FString::Printf(TEXT("-%.0f"), Float.Amount), Color, 17);
			}
		}
	const float Height = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f;
	if (Height > LastPlayerHeightCm + .02f)GrowthVisibleUntil = Now + 2.4f;
	LastPlayerHeightCm = Height;
	const bool Absorbing = P->Nutrition > .01f && P->BodySize < 2.5f;
	if (!P->bDead && !bConversationOpen && P->AttackTimer <= 0 && P->HurtTimer <= 0 && Height > StartingHeightCm + .5f
		&& (Absorbing || Now < GrowthVisibleUntil))
	{
		const FVector Feet = P->GetActorLocation() - FVector(
			0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FVector Right = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y).GetSafeNormal2D();
		const float RulerScale = FMath::Min(1.f, P->BodySize / VerdantPlayerScale::ReferenceBodySize);
		const FVector Base = Feet + Right * FMath::Max(70.f * RulerScale, P->BodySize * 80.f);
		FVector2D Bottom, Start, Top;
		if (Project(Base, Bottom) && Project(Base + FVector(0, 0, StartingHeightCm), Start) && Project(
			Base + FVector(0, 0, Height), Top))
		{
			FLinearColor Color = Gold;
			Color.A = Absorbing ? .8f : FMath::Clamp((GrowthVisibleUntil - Now) / .8f, 0.f, .8f);
			DrawLine(Bottom.X, Bottom.Y, Top.X, Top.Y, Color, 1.5f);
			DrawLine(Top.X - 5, Top.Y, Top.X + 5, Top.Y, Color, 2.f);
			FLinearColor Old = Muted;
			Old.A = Color.A;
			DrawLine(Start.X - 5, Start.Y, Start.X + 5, Start.Y, Old, 1.5f);
			if (FMath::Abs(Start.Y - Top.Y) > 32.f)
			{
				TextAt(Top + FVector2D(10, -7), FString::Printf(TEXT("现在 %.2fm"), Height / 100.f), Color, 12, false);
				TextAt(Start + FVector2D(10, -7), FString::Printf(TEXT("出发时 %.2fm"), StartingHeightCm / 100.f), Old, 12, false);
			}
			else
				TextAt(Top + FVector2D(10, -7), FString::Printf(TEXT("%.2fm · 出发时%.2fm"), Height / 100.f, StartingHeightCm / 100.f), Color, 12,
				       false);
		}
	}
	if (!bConversationOpen)
	{
		const float HalfHeight = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector ScreenRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);
		FVector2D HeadAt = FVector2D::ZeroVector, FeetAt = FVector2D::ZeroVector, CenterAt = FVector2D::ZeroVector,
		          SideAt = FVector2D::ZeroVector;
		const bool BodyOnScreen = PC->ProjectWorldLocationToScreen(P->GetActorLocation() + FVector(0, 0, HalfHeight),
		                                                           HeadAt)
			&& PC->ProjectWorldLocationToScreen(P->GetActorLocation() - FVector(0, 0, HalfHeight), FeetAt)
			&& PC->ProjectWorldLocationToScreen(P->GetActorLocation(), CenterAt)
			&& PC->ProjectWorldLocationToScreen(
				P->GetActorLocation() + ScreenRight * P->GetCapsuleComponent()->GetScaledCapsuleRadius(), SideAt);
		const float BodyHalfWidth = BodyOnScreen
			                            ? FMath::Max(36.f, float(FMath::Abs(SideAt.X - CenterAt.X)) + 20.f)
			                            : 0.f;
		auto PlaceLabel = [&](const FVector& Position, const FString& Text, FLinearColor Color, int32 Size)
		{
			FVector2D At;
			if (!Project(Position, At))return;
			// Conservative text bounds include the shadow and Chinese glyph widths.
			const float TextHalfWidth = Size * Text.Len() * .75f, TextHeight = Size * 1.7f;
			if (At.Y < 320.f * HUDScale && (At.X - TextHalfWidth < 360.f * HUDScale || At.X + TextHalfWidth > Canvas->
				SizeX - 370.f * HUDScale))
				return;
			if (BodyOnScreen && At.X + TextHalfWidth >= FMath::Min(HeadAt.X, FeetAt.X) - BodyHalfWidth
				&& At.X - TextHalfWidth <= FMath::Max(HeadAt.X, FeetAt.X) + BodyHalfWidth
				&& At.Y + TextHeight >= FMath::Min(HeadAt.Y, FeetAt.Y) - 16.f && At.Y <= FMath::Max(HeadAt.Y, FeetAt.Y)
				+ 16.f)
				return;
			TextAt(At, Text, Color, Size);
		};
		for (const auto& G : W->Groves)
			if (FVector::Dist2D(P->GetActorLocation(), G.Position) < 11000.f)
				PlaceLabel(
					G.Position + FVector(0, 0, 300), G.Name, G.Color, 17);
		PlaceLabel(W->MotherPosition + FVector(0, 0, 1700), W->bRestored ? TEXT("母树 · 新生") : TEXT("母树"), Gold, 20);
	}
}
