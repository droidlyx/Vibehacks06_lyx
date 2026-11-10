#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

// Asset-free botanical UI. All brushes and styles outlive the widgets using them.
namespace VerdantUI
{
	inline const FLinearColor Ink(.94f, .91f, .79f, 1.f);
	inline const FLinearColor Gold(.86f, .66f, .32f, 1.f);
	inline const FLinearColor Muted(.59f, .66f, .55f, 1.f);
	inline const FLinearColor Panel(.018f, .038f, .026f, .94f);
	inline const FLinearColor Leaf(.38f, .67f, .38f, 1.f);
	inline const FLinearColor Danger(.90f, .30f, .19f, 1.f);

	struct FTheme
	{
		FSlateRoundedBoxBrush Card;
		FSlateRoundedBoxBrush Inset;
		FSlateRoundedBoxBrush Key;
		FButtonStyle Button;
		FProgressBarStyle Bar;
		FEditableTextBoxStyle Input;

		FTheme()
			: Card(Panel, 12.f, FLinearColor(.39f, .35f, .21f, .65f), 1.f)
			  , Inset(FLinearColor(.008f, .020f, .012f, .64f), 7.f, FLinearColor(.29f, .37f, .23f, .28f), 1.f)
			  , Key(FLinearColor(.12f, .16f, .09f, 1.f), 5.f, FLinearColor(.49f, .46f, .29f, .8f), 1.f)
		{
			Input = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
			Input.SetBackgroundImageNormal(Inset)
			     .SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor(.012f, .028f, .019f), 6.f, Muted, 1.f))
			     .SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor(.012f, .028f, .019f), 6.f, Gold, 1.5f))
			     .SetBackgroundImageReadOnly(Inset).SetForegroundColor(Ink);
			Button.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.075f, .12f, .075f, 1.f), 6.f,
			                                       FLinearColor(.35f, .39f, .24f, 1.f), 1.f))
			      .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.19f, .25f, .12f, 1.f), 6.f, Gold, 1.5f))
			      .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.30f, .27f, .11f, 1.f), 6.f, Ink, 1.5f))
			      .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.035f, .055f, .035f, .65f), 6.f,
			                                         FLinearColor(.18f, .22f, .15f, .5f), 1.f))
			      .SetNormalForeground(Ink).SetHoveredForeground(Ink).SetPressedForeground(Ink).SetDisabledForeground(
				      Muted)
			      .SetNormalPadding(FMargin(12.f, 8.f)).SetPressedPadding(FMargin(12.f, 9.f, 12.f, 7.f));
			Bar.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(.007f, .016f, .009f, .9f), 3.f))
			   .SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 3.f))
			   .SetMarqueeImage(FSlateRoundedBoxBrush(Leaf, 3.f))
			   .SetEnableFillAnimation(false);
		}
	};

	inline const FTheme& Theme()
	{
		static const FTheme Instance;
		return Instance;
	}

	inline FSlateFontInfo Font(int32 Size)
	{
		static const TSharedPtr<const FCompositeFont> Composite = MakeShared<FStandaloneCompositeFont>(
			NAME_None, FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"),
			EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		return FSlateFontInfo(Composite, static_cast<float>(Size));
	}

	inline TSharedRef<STextBlock> Label(const FString& Text, int32 Size = 14, FLinearColor Color = Ink)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size)).ColorAndOpacity(Color);
	}

	inline TSharedRef<SWidget> Rule()
	{
		return SNew(SBox).HeightOverride(1.f)
			[
				SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(
					FLinearColor(.52f, .43f, .23f, .35f))
			];
	}

	inline TSharedRef<SWidget> Keycap(const FString& KeyText)
	{
		return SNew(SBorder).BorderImage(&Theme().Key).Padding(FMargin(7.f, 3.f)).HAlign(HAlign_Center)
			[
				Label(KeyText, 11, Gold)
			];
	}

	inline TSharedRef<SWidget> Action(const FString& KeyText, const FString& ActionText)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Keycap(KeyText)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7.f, 0.f, 0.f, 0.f)
			[
				Label(ActionText, 13, Ink)
			];
	}

	class SCrest : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCrest)
			{
			}

		SLATE_END_ARGS()

		void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }

	protected:
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(54.f, 54.f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
		                      FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style,
		                      bool bParentEnabled) const override
		{
			const FVector2f Size = Geometry.GetLocalSize();
			const float Scale = FMath::Min(Size.X, Size.Y) / 54.f;
			const FVector2f Offset = (Size - FVector2f(54.f * Scale, 54.f * Scale)) * .5f;
			const FLinearColor Tint = Style.GetColorAndOpacityTint() * (bParentEnabled ? 1.f : .45f);
			auto Stroke = [&](TArray<FVector2f> Points, FLinearColor Color, float Width)
			{
				for (FVector2f& Point : Points)Point = Offset + Point * Scale;
				FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points),
				                             ESlateDrawEffect::None, Color * Tint, true, Width * Scale);
			};
			TArray<FVector2f> Ring;
			for (int32 I = 0; I <= 64; ++I)
			{
				const float Angle = 2.f * PI * I / 64.f;
				Ring.Add(FVector2f(27.f + 24.f * FMath::Cos(Angle), 27.f + 24.f * FMath::Sin(Angle)));
			}
			Stroke(MoveTemp(Ring), FLinearColor(.52f, .45f, .25f, .75f), 1.f);
			// A sapling in a seal: curved stem, two pointed leaves and a fine ground arc.
			Stroke({FVector2f(26.f, 41.f), FVector2f(27.f, 33.f), FVector2f(27.f, 25.f), FVector2f(30.f, 19.f)}, Gold,
			       1.7f);
			auto DrawLeaf = [&](FVector2f Base, FVector2f Tip, float Breadth)
			{
				const FVector2f Axis = Tip - Base;
				const FVector2f Perpendicular = FVector2f(-Axis.Y, Axis.X).GetSafeNormal();
				TArray<FVector2f> Outline;
				for (int32 I = 0; I <= 12; ++I)
				{
					float T = I / 12.f;
					Outline.Add(Base + Axis * T + Perpendicular * FMath::Sin(PI * T) * Breadth);
				}
				for (int32 I = 11; I >= 0; --I)
				{
					float T = I / 12.f;
					Outline.Add(Base + Axis * T - Perpendicular * FMath::Sin(PI * T) * Breadth * .65f);
				}
				Stroke(MoveTemp(Outline), Gold, 1.5f);
				Stroke({Base, Tip}, FLinearColor(.62f, .61f, .32f, .8f), .8f);
			};
			DrawLeaf(FVector2f(27.f, 29.f), FVector2f(15.f, 18.f), 4.f);
			DrawLeaf(FVector2f(28.f, 24.f), FVector2f(39.f, 12.f), 4.5f);
			Stroke({
				       FVector2f(17.f, 42.f), FVector2f(22.f, 40.f), FVector2f(27.f, 39.5f), FVector2f(32.f, 40.f),
				       FVector2f(37.f, 42.f)
			       }, Gold, 1.f);
			return Layer;
		}
	};
}
