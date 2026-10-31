#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "VerdantHUD.generated.h"
class SWidget;
class FActiveTimerHandle;
class SMultiLineEditableTextBox;
class SScrollBox;
class UFont;
class AVerdantCharacter;
template <typename OptionType>
class SComboBox;

struct FVerdantConversationTarget
{
	TWeakObjectPtr<AVerdantCharacter> NPC;
	FString Label;
	FText DisplayText;
	bool bReady = false;
	bool bBroadcast = false;
};

struct FVerdantHUDObservation
{
	TWeakObjectPtr<AVerdantCharacter> Character;
	float Health = 0.f;
	float BodySize = 0.f;
	bool bDead = false;
	bool bHitApplied = true;
	float StrikeFlashUntil = -1.f;
};

struct FVerdantHUDDamageFloat
{
	TWeakObjectPtr<AVerdantCharacter> Character;
	FVector Position = FVector::ZeroVector;
	float Amount = 0.f;
	float BornAt = 0.f;
	int32 Lane = 0;
	bool bPlayerDamage = false;
};

UCLASS()
class VERDANT_API AVerdantHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void DrawHUD() override;
	void ShowDamageNumber(AVerdantCharacter* Target, float Amount);
	void OpenConversation(AVerdantCharacter* NPC);
	void CloseConversation();
	bool HasCanvasFont() const;
	bool bConversationOpen = false;

private:
	FTimerHandle PresentationTimer;
	TFunction<void()> RefreshPresentation;
	bool bPausedForConversation = false;
	bool bJournalOpen = false;
	bool bSeasonPresented = false;
	bool bControlsOpen = false;
	void FillConversationPrompt(const FString& Text);
	void RefreshConversationTargets(AVerdantCharacter* Preferred);
	void UpdateConversationTargets();
	bool CanSendConversationTarget() const;
	TWeakPtr<FActiveTimerHandle> ConversationRefreshTimer;
	UPROPERTY(Transient)
	TObjectPtr<UFont> CanvasFont;
	TSharedPtr<SWidget> RootWidget;
	TSharedPtr<SWidget> SpeechOverlay;
	TSharedPtr<SMultiLineEditableTextBox> InputBox;
	TSharedPtr<SComboBox<TSharedPtr<FVerdantConversationTarget>>> TargetBox;
	TArray<TSharedPtr<FVerdantConversationTarget>> ConversationTargets;
	TSharedPtr<FVerdantConversationTarget> ConversationTarget;
	TSharedPtr<SScrollBox> JournalScroll;
	TMap<int32, FVerdantHUDObservation> Observations;
	TArray<FVerdantHUDDamageFloat> DamageFloats;
	float LastPlayerHeightCm = 81.f;
	float GrowthVisibleUntil = -1.f;
};
