#include "VerdantSpeechBubbles.h"
#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "VerdantDialogue.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/ObjectKey.h"

namespace
{
	constexpr int32 LinesPerPage = 5;
	const FLinearColor Ink(.94f, .91f, .79f, 1.f);
	const FLinearColor Gold(.86f, .66f, .32f, 1.f);
	const FLinearColor Leaf(.57f, .75f, .55f, 1.f);
	const FLinearColor Panel(.018f, .038f, .026f, .96f);

	struct FSpeechLayout
	{
		FString Text;
		FObjectKey FontKey;
		int32 FontSize = 0;
		float Width = 0.f;
		float DPIScale = 0.f;
		TArray<FString> Lines;
	};

	// Layout work happens only when speech/font/viewport changes, not every paint.
	TMap<FObjectKey, FSpeechLayout> Layouts;

	void DrawBubbleRect(UCanvas* Canvas, const FVector2D& At, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Item(At, Size, Color);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	void Line(UCanvas* Canvas, const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Width)
	{
		FCanvasLineItem Item(A, B);
		Item.SetColor(Color);
		Item.LineThickness = Width;
		Canvas->DrawItem(Item);
	}

	bool Overlaps(const FBox2D& A, const FBox2D& B, float Gap)
	{
		return A.Min.X < B.Max.X + Gap && A.Max.X + Gap > B.Min.X
			&& A.Min.Y < B.Max.Y + Gap && A.Max.Y + Gap > B.Min.Y;
	}
}

void VerdantSpeechBubbles::Draw(UCanvas* Canvas, APlayerController* PC, UFont* Font,
                                const AVerdantWorld* World, const AVerdantCharacter* Player)
{
	if (!Canvas || !Canvas->Canvas || !PC || !Font || !World || !Player || !FSlateApplication::IsInitialized())return;
	const float Now = World->GetWorld()->GetTimeSeconds();
	const float HUDScale = FMath::Max(.25f, FMath::Min(Canvas->SizeX / 1600.f, Canvas->SizeY / 900.f));
	const float Scale = FMath::Clamp(HUDScale, .55f, 1.5f);
	const float DPI = FMath::Max(.01f, Canvas->Canvas->GetDPIScale());
	const float Padding = 13.f * Scale;
	const float Width = FMath::Min(360.f * Scale, Canvas->SizeX - 36.f);
	const float TextWidth = Width - Padding * 2.f;
	const int32 FontSize = FMath::Max(10, FMath::RoundToInt(16.f * Scale));
	const int32 TitleSize = FMath::Max(9, FMath::RoundToInt(12.f * Scale));
	const FSlateFontInfo BodyFont(Font, FontSize);
	const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	auto SizeOf = [&](const FString& Text, const FSlateFontInfo& Info)
	{
		return FVector2D(Measure->Measure(FStringView(Text), Info, DPI)) / DPI;
	};
	const float LineHeight = SizeOf(TEXT("国Ag"), BodyFont).Y + 4.f * Scale;
	const float TitleHeight = SizeOf(TEXT("国Ag"), FSlateFontInfo(Font, TitleSize)).Y + 8.f * Scale;
	const FBox2D Safe(FVector2D(18.f, 18.f), FVector2D(Canvas->SizeX - 18.f, Canvas->SizeY - 175.f * HUDScale));
	if (TextWidth <= 0 || Safe.Max.Y <= Safe.Min.Y)return;

	for (auto It = Layouts.CreateIterator(); It; ++It)
		if (!IsValid(It.Key().ResolveObjectPtr()))It.RemoveCurrent();

	TArray<const AVerdantCharacter*> Speakers;
	const float HearingRadius = World->Dialogue ? World->Dialogue->GetHearingRadiusCm() : VerdantDialoguePolicy::DefaultRadiusCm;
	if (!Player->bDead && Player->SpeechUntil > Now && !Player->LastSpeech.IsEmpty())Speakers.Add(Player);
	for (const AVerdantCharacter* NPC : World->Folk)
		if (IsValid(NPC) && NPC != Player && !NPC->bDead && NPC->SpeechUntil > Now && !NPC->LastSpeech.IsEmpty()
			&& FVector::DistSquared(NPC->GetActorLocation(), Player->GetActorLocation()) <= FMath::Square(HearingRadius))
			Speakers.Add(NPC);
	Speakers.Sort([Player](const AVerdantCharacter& A, const AVerdantCharacter& B)
	{
		if ((&A == Player) != (&B == Player))return &A == Player;
		return FVector::DistSquared(A.GetActorLocation(), Player->GetActorLocation())
			< FVector::DistSquared(B.GetActorLocation(), Player->GetActorLocation());
	});

	TArray<FBox2D> Occupied;
	Occupied.Emplace(FVector2D(0, 0), FVector2D(360.f * HUDScale, 320.f * HUDScale));
	Occupied.Emplace(FVector2D(Canvas->SizeX - 370.f * HUDScale, 0), FVector2D(Canvas->SizeX, 320.f * HUDScale));
	FVector View;
	FRotator Rotation;
	PC->GetPlayerViewPoint(View, Rotation);
	int32 Drawn = 0;
	for (const AVerdantCharacter* Speaker : Speakers)
	{
		if (Drawn >= 5)break;
		const FVector Head = Speaker->GetActorLocation() + FVector(0, 0,
		                                                           Speaker->GetCapsuleComponent()->
		                                                                    GetScaledCapsuleHalfHeight() + 15.f *
		                                                           Speaker->BodySize);
		FVector2D Anchor;
		if (!PC->ProjectWorldLocationToScreen(Head, Anchor) || Anchor.X < 0 || Anchor.X > Canvas->SizeX
			|| Anchor.Y < 0 || Anchor.Y > Canvas->SizeY)
			continue;
		// A visible player's head may lie behind the bottom HUD; lift the card into
		// the safe area instead of dropping the player's own just-sent speech.
		if (Speaker != Player && !Safe.IsInsideOrOn(Anchor))continue;
		if (Speaker != Player)
		{
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(VerdantSpeechSight), false, Player);
			Query.AddIgnoredActor(Speaker);
			if (World->GetWorld()->LineTraceSingleByChannel(Hit, View, Head, ECC_Visibility, Query))continue;
		}

		auto& Layout = Layouts.FindOrAdd(FObjectKey(Speaker));
		if (Layout.Text != Speaker->LastSpeech || Layout.FontKey != FObjectKey(Font)
			|| Layout.FontSize != FontSize || !FMath::IsNearlyEqual(Layout.Width, TextWidth)
			|| !FMath::IsNearlyEqual(Layout.DPIScale, DPI))
		{
			Layout.Text = Speaker->LastSpeech;
			Layout.FontKey = FObjectKey(Font);
			Layout.FontSize = FontSize;
			Layout.Width = TextWidth;
			Layout.DPIScale = DPI;
			Layout.Lines.Reset();
			FString Current;
			for (int32 I = 0; I < Layout.Text.Len();)
			{
				const TCHAR Ch = Layout.Text[I];
				if (Ch == TEXT('\r'))
				{
					++I;
					continue;
				}
				if (Ch == TEXT('\n'))
				{
					Layout.Lines.Add(Current);
					Current.Reset();
					++I;
					continue;
				}
				// Keep UTF-16 surrogate pairs together when wrapping pasted text.
				const int32 Units = Ch >= 0xD800 && Ch <= 0xDBFF && I + 1 < Layout.Text.Len()
				                    && Layout.Text[I + 1] >= 0xDC00 && Layout.Text[I + 1] <= 0xDFFF
					                    ? 2
					                    : 1;
				const FString Glyph = Layout.Text.Mid(I, Units);
				if (!Current.IsEmpty() && SizeOf(Current + Glyph, BodyFont).X > TextWidth)
				{
					Layout.Lines.Add(Current);
					Current.Reset();
				}
				Current += Glyph;
				I += Units;
			}
			if (!Current.IsEmpty())Layout.Lines.Add(Current);
		}
		if (Layout.Lines.IsEmpty())continue;
		const int32 PageCount = FMath::DivideAndRoundUp(Layout.Lines.Num(), LinesPerPage);
		const float Duration = FMath::Max(1.f, Speaker->SpeechUntil - Speaker->SpeechStartedAt);
		const int32 Page = FMath::Clamp(FMath::FloorToInt((Now - Speaker->SpeechStartedAt) / Duration * PageCount), 0,
		                                PageCount - 1);
		const int32 FirstLine = Page * LinesPerPage;
		const int32 LineCount = FMath::Min(LinesPerPage, Layout.Lines.Num() - FirstLine);
		const float Height = Padding * 2.f + TitleHeight + LineHeight * LineCount;
		if (Height > Safe.GetSize().Y)continue;

		FBox2D Box;
		bool Placed = false;
		// Keep bubbles above the head; shift sideways/up only to avoid existing cards.
		for (int32 Row = 0; Row < 3 && !Placed; ++Row)
			for (float Offset : {0.f, -.7f, .7f})
			{
				const float X = FMath::Clamp(float(Anchor.X - Width * .5f + Offset * Width), float(Safe.Min.X),
				                             float(Safe.Max.X - Width));
				const float Y = FMath::Clamp(float(Anchor.Y - 26.f * Scale - Height - Row * (Height + 12.f * Scale)),
				                             float(Safe.Min.Y), float(Safe.Max.Y - Height));
				const FBox2D Candidate(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
				if (Occupied.ContainsByPredicate([&](const FBox2D& Other)
				{
					return Overlaps(Candidate, Other, 9.f * Scale);
				}))
					continue;
				Box = Candidate;
				Placed = true;
				break;
			}
		if (!Placed)continue;
		Occupied.Add(Box);
		++Drawn;

		const float Alpha = FMath::Clamp((Speaker->SpeechUntil - Now) / .6f, 0.f, 1.f);
		auto Fade = [Alpha](FLinearColor Color)
		{
			Color.A *= Alpha;
			return Color;
		};
		const FLinearColor Accent = Speaker == Player ? Gold : Leaf;
		DrawBubbleRect(Canvas, Box.Min + FVector2D(3, 4) * Scale, Box.GetSize(), Fade(FLinearColor(0, 0, 0, .35f)));
		DrawBubbleRect(Canvas, Box.Min, Box.GetSize(), Fade(Accent));
		DrawBubbleRect(Canvas, Box.Min + FVector2D(1, 1) * Scale, Box.GetSize() - FVector2D(2, 2) * Scale, Fade(Panel));
		const FVector2D TailStart(FMath::Clamp(Anchor.X, Box.Min.X + 14.f * Scale, Box.Max.X - 14.f * Scale),
		                          Box.Max.Y);
		if (Anchor.Y > Box.Max.Y)
		{
			const FVector2D Direction = (Anchor - TailStart).GetSafeNormal();
			const FVector2D Tip = TailStart + Direction * FMath::Min(18.f * Scale,
			                                                         float((Anchor - TailStart).Size()) * .75f);
			Line(Canvas, TailStart - FVector2D(5.f * Scale, 0), Tip, Fade(Accent), Scale);
			Line(Canvas, TailStart + FVector2D(5.f * Scale, 0), Tip, Fade(Accent), Scale);
		}
		auto TextAt = [&](const FVector2D& At, const FString& Text, int32 Size, const FLinearColor& Color)
		{
			FCanvasTextItem Item(At / DPI, FText::FromString(Text), FSlateFontInfo(Font, Size), Fade(Color));
			Item.Scale = FVector2D(1.f / DPI, 1.f / DPI);
			Canvas->DrawItem(Item);
		};
		FString Title = Speaker == Player ? TEXT("你") : Speaker->DisplayName;
		if (PageCount > 1)Title += FString::Printf(TEXT("  ·  %d/%d"), Page + 1, PageCount);
		TextAt(Box.Min + FVector2D(Padding, Padding), Title, TitleSize, Accent);
		for (int32 I = 0; I < LineCount; ++I)
			TextAt(Box.Min + FVector2D(Padding, Padding + TitleHeight + I * LineHeight), Layout.Lines[FirstLine + I],
			       FontSize, Ink);
	}
}
