#include "VerdantEmberSpeechWidget.h"
#include "VerdantHUD.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "VerdantWorld.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

// Adapted from SEmberConversation in the user's EmberZeldaStandalone project:
// Plugins/EmberDecision/Source/EmberDecision/Private/EmberConversation.cpp.
// This retains Ember's Slate canvas, border, wrapped CJK text and painted tail.
// Only the actor/data adapter, game-time lifetime and edge placement are changed.
// No Ember director, model service, input panel or gameplay decisions are imported.
namespace
{
	class SVerdantEmberSpeech : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SVerdantEmberSpeech)
			{
			}

		SLATE_END_ARGS()

		void Construct(const FArguments&, AVerdantHUD* InHUD)
		{
			HUD = InHUD;
			SetVisibility(EVisibility::HitTestInvisible);
			ChildSlot[SAssignNew(Canvas, SCanvas).Visibility(EVisibility::HitTestInvisible)];
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		                      FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style,
		                      bool Enabled) const override
		{
			for (const auto& Item : Bubbles)
				if (Item.bDrawTail)
				{
					const FVector2D Offset = Item.Anchor - Item.Tip;
					const FVector2D End = Item.Tip + Offset.GetSafeNormal() * FMath::Min(18., Offset.Size());
					FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
					                             TArray<FVector2D>{Item.Tip, End}, ESlateDrawEffect::None,
					                             FLinearColor(.85f, .9f, .78f, .8f), true, 1.5f);
				}
			return SCompoundWidget::OnPaint(Args, Geometry, Culling, Elements, Layer + 1, Style, Enabled);
		}

		virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override
		{
			SCompoundWidget::Tick(Geometry, Time, DeltaTime);
			for (auto& Item : Bubbles)
			{
				Item.bDrawTail = false;
				Item.Widget->SetVisibility(EVisibility::Collapsed);
			}
			if (!HUD.IsValid() || HUD->bConversationOpen)return;
			auto* PC = HUD->GetOwningPlayerController();
			auto* Player = PC ? Cast<AVerdantCharacter>(PC->GetPawn()) : nullptr;
			auto* World = AVerdantWorld::Find(HUD.Get());
			if (!PC || !IsValid(Player) || !World)return;
			int32 ViewX = 0, ViewY = 0;
			PC->GetViewportSize(ViewX, ViewY);
			const FVector2D Size = Geometry.GetLocalSize();
			if (ViewX <= 0 || ViewY <= 0 || Size.X < 80 || Size.Y < 80)return;
			const FVector2D PixelToLocal(Size.X / ViewX, Size.Y / ViewY);
			const float Now = World->GetWorld()->GetTimeSeconds();
			const float HearingRadius = World->Dialogue
				                            ? World->Dialogue->GetHearingRadiusCm()
				                            : VerdantDialoguePolicy::DefaultRadiusCm;
			SyncActors(World, Player);

			const auto PriorityReply = [Player](const AVerdantCharacter* Speaker)
			{
				return Speaker->IsDialogueReplyActive() && Speaker->GetDialogueReplyFocus() == Player;
			};
			TArray<int32> Order;
			for (int32 I = 0; I < Bubbles.Num(); ++I)
			{
				const auto* Speaker = Bubbles[I].Speaker.Get();
				if (!IsValid(Speaker) || Speaker->bDead || Speaker->LastSpeech.IsEmpty() || Now >= Speaker->SpeechUntil)
					continue;
				if (!PriorityReply(Speaker) && Speaker != Player && FVector::DistSquared(
					Speaker->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(HearingRadius))
					continue;
				Order.Add(I);
			}
			Order.Sort([&](int32 A, int32 B)
			{
				const auto* Left = Bubbles[A].Speaker.Get();
				const auto* Right = Bubbles[B].Speaker.Get();
				if (PriorityReply(Left) != PriorityReply(Right))return PriorityReply(Left);
				if ((Left == Player) != (Right == Player))return Left == Player;
				return FVector::DistSquared(Left->GetActorLocation(), Player->GetActorLocation())
					< FVector::DistSquared(Right->GetActorLocation(), Player->GetActorLocation());
			});

			TArray<FSlateRect> Occupied;
			for (int32 Index : Order)
			{
				auto& Item = Bubbles[Index];
				auto* Speaker = Item.Speaker.Get();
				const bool Priority = PriorityReply(Speaker);
				const bool Important = Priority || Speaker == Player;
				FVector2D Screen;
				bool Projected = PC->ProjectWorldLocationToScreen(HeadPosition(Speaker), Screen, true);
				const auto InViewport = [&](const FVector2D& Point)
				{
					return Point.X >= 0 && Point.X <= ViewX && Point.Y >= 0 && Point.Y <= ViewY;
				};
				if (Important && (!Projected || !InViewport(Screen)))
					Projected = PC->ProjectWorldLocationToScreen(Speaker->GetActorLocation(), Screen, true);
				if (!Important && (!Projected || !InViewport(Screen)))continue;
				if (Projected)Screen *= PixelToLocal;
				else Screen = FVector2D(Size.X * .5, Size.Y * .62);
				Screen.X = FMath::Clamp(Screen.X, 8., Size.X - 8.);
				Screen.Y = FMath::Clamp(Screen.Y, 8., Size.Y - 8.);

				const FString Title = Speaker == Player ? TEXT("旅人 · 你") : Speaker->DisplayName;
				const FString Text = Title + TEXT("\n") + Speaker->LastSpeech;
				if (Text != Item.LastText)
				{
					Item.LastText = Text;
					Item.Label->SetText(FText::FromString(Text));
				}
				const float Width = FMath::Min(270.f, float(Size.X - 16));
				Item.Label->SetWrapTextAt(Width - 24.f);
				// A collapsed widget does not participate in the Slate desired-size pass.
				Item.Widget->SetVisibility(EVisibility::HitTestInvisible);
				Item.Widget->SlatePrepass(Geometry.Scale);
				const float Height = FMath::Min(float(Size.Y - 16),
				                                FMath::Max(38.f, Item.Label->GetDesiredSize().Y + 24.f));
				Item.Slot->SetSize(FVector2D(Width, Height));
				FVector2D Position(FMath::Clamp(Screen.X - Width / 2., 8., Size.X - Width - 8.),
				                   FMath::Clamp(Screen.Y - Height, 8., Size.Y - Height - 8.));
				const auto IsFree = [&](const FVector2D& At)
				{
					const FSlateRect Rect(At.X, At.Y, At.X + Width, At.Y + Height);
					return !Occupied.ContainsByPredicate([&](const FSlateRect& Other)
					{
						return FSlateRect::DoRectanglesIntersect(Rect, Other);
					});
				};
				// Ember's original upward/sideways bubble avoidance.
				for (int32 Attempt = 0; Attempt < Bubbles.Num() && !IsFree(Position); ++Attempt)
				{
					const FSlateRect Rect(Position.X, Position.Y, Position.X + Width, Position.Y + Height);
					const FSlateRect* Hit = Occupied.FindByPredicate([&](const FSlateRect& Other)
					{
						return FSlateRect::DoRectanglesIntersect(Rect, Other);
					});
					if (!Hit)break;
					const float Above = Hit->Top - Height - 8.f;
					if (Above >= 8.f)Position.Y = Above;
					else
					{
						const float Left = Hit->Left - Width - 8.f;
						Position.X = FMath::Clamp(double(Left >= 8.f ? Left : Hit->Right + 8.f), 8.,
						                          Size.X - Width - 8.);
						Position.Y = 8;
					}
				}
				// Retain a currently delivered response even if its head has moved off
				// screen. Search the remaining screen space rather than dropping it.
				if (!IsFree(Position))
				{
					double Best = TNumericLimits<double>::Max();
					for (double Y = 8; Y <= Size.Y - Height - 8.; Y += FMath::Max(24.f, Height + 8.f))
						for (double X = 8; X <= Size.X - Width - 8.; X += FMath::Max(24.f, Width + 8.f))
						{
							const FVector2D Candidate(X, Y);
							const double Distance = (Candidate + FVector2D(Width / 2., Height) - Screen).SizeSquared();
							if (IsFree(Candidate) && Distance < Best)
							{
								Best = Distance;
								Position = Candidate;
							}
						}
					if (Best == TNumericLimits<double>::Max() && !Important)
					{
						Item.Widget->SetVisibility(EVisibility::Collapsed);
						continue;
					}
				}
				Item.Slot->SetPosition(Position);
				Item.Widget->SetVisibility(EVisibility::HitTestInvisible);
				Occupied.Emplace(Position.X, Position.Y, Position.X + Width, Position.Y + Height);
				Item.Anchor = Screen;
				Item.Tip = Position + FVector2D(Width / 2., Height);
				Item.bDrawTail = Projected && FVector2D::Distance(Item.Anchor, Item.Tip) > 4;
			}
		}

	private:
		struct FBubble
		{
			TWeakObjectPtr<AVerdantCharacter> Speaker;
			SCanvas::FSlot* Slot = nullptr;
			TSharedPtr<SBorder> Widget;
			TSharedPtr<STextBlock> Label;
			FString LastText;
			FVector2D Anchor, Tip;
			bool bDrawTail = false;
		};

		TWeakObjectPtr<AVerdantHUD> HUD;
		TSharedPtr<SCanvas> Canvas;
		TArray<FBubble> Bubbles;

		static FVector HeadPosition(const AVerdantCharacter* Speaker)
		{
			const auto* Mesh = Speaker->GetMesh();
			if (Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->IsVisible() && Mesh->Bounds.BoxExtent.Z > 0.f)
				return Mesh->Bounds.Origin + FVector(0, 0, Mesh->Bounds.BoxExtent.Z + 22.f);
			const float HalfHeight = Speaker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			return Speaker->GetActorLocation() + FVector(0, 0, HalfHeight + 22.f);
		}

		void SyncActors(AVerdantWorld* World, AVerdantCharacter* Player)
		{
			for (int32 I = Bubbles.Num() - 1; I >= 0; --I)
				if (!Bubbles[I].Speaker.IsValid())
				{
					Canvas->RemoveSlot(Bubbles[I].Widget.ToSharedRef());
					Bubbles.RemoveAt(I);
				}
			const auto Add = [this,Player](AVerdantCharacter* Speaker)
			{
				if (!IsValid(Speaker) || Bubbles.ContainsByPredicate([Speaker](const FBubble& B)
				{
					return B.Speaker.Get() == Speaker;
				}))
					return;
				FBubble Item;
				Item.Speaker = Speaker;
				Canvas->AddSlot().Expose(Item.Slot)
				[SAssignNew(Item.Widget, SBorder)
				.Padding(12)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(Speaker != Player
					                       ? FLinearColor(.025f, .065f, .07f, .94f)
					                       : FLinearColor(.13f, .085f, .025f, .95f))
				.Visibility(EVisibility::Collapsed)
				[
					SAssignNew(Item.Label, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
					.ColorAndOpacity(FLinearColor(.98f, .95f, .85f))
					.WrapTextAt(246)
					.Justification(ETextJustify::Center)
				]];
				Bubbles.Add(MoveTemp(Item));
			};
			Add(Player);
			for (AVerdantCharacter* Speaker : World->Folk)Add(Speaker);
		}
	};
}

TSharedRef<SWidget> VerdantEmberSpeech::Create(AVerdantHUD* HUD)
{
	return SNew(SVerdantEmberSpeech, HUD);
}
