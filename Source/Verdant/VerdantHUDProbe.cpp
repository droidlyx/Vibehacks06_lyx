// Explicit, local-only visual fixtures. Never runs during normal gameplay.
// Example: -VerdantHUDProbe -RenderOffscreen -ResX=1920 -ResY=1080
// Optional: -VerdantHUDProbeCase=dialogue (baseline, low-health, death, restored).
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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWidget.h"

namespace
{
	TSharedPtr<SWidget> FindHUDProbeButton(const TSharedRef<SWidget>& Root)
	{
		if (Root->GetTypeAsString() == TEXT("SButton"))return Root;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Button = FindHUDProbeButton(Children->GetChildAt(Index)))return Button;
		}
		return nullptr;
	}
}

void AVerdantGameMode::TickHUDProbe(float Dt)
{
	SmokeTime += Dt;
	auto* W = AVerdantWorld::Find(this);
	auto* P = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	auto* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	auto* HUD = PC ? Cast<AVerdantHUD>(PC->GetHUD()) : nullptr;
	if (!W || !P || !PC || !HUD)
	{
		if (SmokeTime > 30.f)FPlatformMisc::RequestExitWithStatus(false, 1);
		return;
	}
	FString SelectedCase;
	FParse::Value(FCommandLine::Get(),TEXT("VerdantHUDProbeCase="), SelectedCase);
	const TArray<FString> Cases = {TEXT("baseline"),TEXT("dialogue"),TEXT("low-health"),TEXT("death"),TEXT("restored")};
	const int32 SelectedIndex = SelectedCase.IsEmpty() ? INDEX_NONE : Cases.Find(SelectedCase);
	if (!SelectedCase.IsEmpty() && SelectedIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("VERDANT_HUD_PROBE unknown case: %s"), *SelectedCase);
		FPlatformMisc::RequestExitWithStatus(false, 1);
		return;
	}
	const FString Output = FPaths::ProjectSavedDir() / TEXT("Validation/HUD");
	int32 Width = 0, Height = 0;
	PC->GetViewportSize(Width, Height);
	const FString Resolution = FString::Printf(TEXT("%dx%d"), Width, Height);
	const bool AfterDialogueCapture = SelectedIndex == INDEX_NONE
		                                  ? SmokeStage == 4
		                                  : SelectedCase == TEXT("dialogue") && SmokeStage == 2;
	const FString InteractionMarker = TEXT("Slate keyboard interaction check: Escape from a conversation button.");
	if (AfterDialogueCapture && !FScreenshotRequest::IsScreenshotRequested() && SmokeTime > CaptureAt + .1f && !
		SmokeChecks.Contains(InteractionMarker))
	{
		SmokeChecks.Add(InteractionMarker);
		auto Check = [this](bool Pass, const TCHAR* Label)
		{
			const FString Result = (Pass ? TEXT("PASS ") : TEXT("FAIL ")) + FString(Label);
			SmokeChecks.Add(Result);
			if (!Pass)++SmokeFailures;
			UE_LOG(LogTemp, Display, TEXT("VERDANT_HUD_PROBE_INPUT %s"), *Result);
		};
		Check(HUD->bConversationOpen && PC->bShowMouseCursor && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored(),
		      TEXT("Conversation owns input before Escape"));
		auto& Slate = FSlateApplication::Get();
		TSharedPtr<SWidget> Focused = Slate.GetKeyboardFocusedWidget();
		TSharedPtr<SWidget> Button;
		FWidgetPath Path;
		if (Focused.IsValid() && Slate.GeneratePathToWidgetUnchecked(Focused.ToSharedRef(), Path))
		{
			for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
			{
				const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
				if (Widget->GetTypeAsString() == TEXT("SVerdantConversation"))
				{
					Button = FindHUDProbeButton(Widget);
					break;
				}
			}
		}
		Check(Button.IsValid(),TEXT("Find descendant SButton through actual focused-widget path"));
		if (Button.IsValid())
		{
			Slate.SetKeyboardFocus(Button, EFocusCause::Navigation);
			Check(Slate.GetKeyboardFocusedWidget() == Button,TEXT("Conversation button receives keyboard focus"));
			const FKeyEvent Escape(EKeys::Escape, FModifierKeysState(), uint32(0), false, 0, 0);
			Check(Slate.ProcessKeyDownEvent(Escape),TEXT("Slate routes and handles Escape from button focus"));
			Slate.ProcessKeyUpEvent(Escape);
		}
		Check(!HUD->bConversationOpen && !PC->bShowMouseCursor && !PC->IsMoveInputIgnored() && !PC->
		      IsLookInputIgnored(),TEXT("Escape closes conversation and restores gameplay input"));
	}
	const int32 CaseIndex = SelectedIndex == INDEX_NONE ? SmokeStage / 2 : SelectedIndex;
	if (CaseIndex >= Cases.Num() || (SelectedIndex != INDEX_NONE && SmokeStage >= 2))
	{
		// Leave two rendered seconds for the final asynchronous screenshot write.
		if (SmokeTime < CaptureAt + 2.f)return;
		SmokeChecks.Add(TEXT("Seeded visual fixtures only; no gameplay or model correctness claim."));
		FFileHelper::SaveStringArrayToFile(SmokeChecks, *(Output / (TEXT("capture-") + Resolution + TEXT(".txt"))),
		                                   FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Display, TEXT("VERDANT_HUD_PROBE_COMPLETE resolution=%s failures=%d"), *Resolution,
		       SmokeFailures);
		FPlatformMisc::RequestExitWithStatus(false, SmokeFailures ? 1 : 0);
		return;
	}
	if (SmokeTime < 3.f || SmokeTime < CaptureAt + 2.f)return;
	if (SmokeStage % 2 == 0)
	{
		IFileManager::Get().MakeDirectory(*Output, true);
		W->SetActorTickEnabled(false);
		for (AVerdantCharacter* N : W->Folk)
			if (IsValid(N))
			{
				N->SetActorTickEnabled(false);
				N->GetCharacterMovement()->StopMovementImmediately();
				N->GetCharacterMovement()->DisableMovement();
			}
		HUD->CloseConversation();
		P->bDead = false;
		P->Health = P->MaxHealth();
		if (Cases[CaseIndex] == TEXT("dialogue"))
		{
			auto* N = W->FindFolk(1);
			if (N && W->Dialogue)
			{
				const FVector Nearby = P->GetActorLocation() + P->GetActorForwardVector() * 180.f;
				N->SetActorLocation(
					W->SurfacePoint(Nearby, N->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.f));
				HUD->OpenConversation(N);
				W->Dialogue->Lines = {TEXT("旅人：我愿意照料这片果林，也想为母树带去一些养分。"),TEXT("米芽：先采些果子吧。我们可以一起照料土地，再商量怎样分享收成。")};
				W->Dialogue->Status = TEXT("视觉验证示例 · 本地固定对话，未调用模型");
			}
		}
		else if (Cases[CaseIndex] == TEXT("low-health"))
		{
			P->Health = P->MaxHealth() * .18f;
			P->Nutrition = 3.5f;
			W->Notify(TEXT("生命不足 · 远离危险，寻找愿意分享食物的居民。"));
		}
		else if (Cases[CaseIndex] == TEXT("death"))
		{
			P->Health = 0;
			P->bDead = true;
		}
		else if (Cases[CaseIndex] == TEXT("restored"))
		{
			W->TreeNutrition = W->TreeGoal;
			W->bRestored = true;
			W->DonationLedger.Add(0, 55.f);
			W->DonationLedger.Add(1, 45.f);
			W->DonationNames.Add(1,TEXT("米芽"));
			W->FinalizeSeasonRecord();
		}
		CaptureAt = SmokeTime;
		++SmokeStage;
		return;
	}
	const FString File = Output / (Cases[CaseIndex] + TEXT("-") + Resolution + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(File, true, false);
	SmokeChecks.Add(TEXT("Requested Slate-inclusive screenshot: ") + File);
	UE_LOG(LogTemp, Display, TEXT("VERDANT_HUD_PROBE_CAPTURE %s"), *File);
	CaptureAt = SmokeTime;
	++SmokeStage;
}
