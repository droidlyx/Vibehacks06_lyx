#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "VerdantHUD.generated.h"
class SWidget;
class SMultiLineEditableTextBox;
class SScrollBox;
class UFont;
class AVerdantCharacter;

struct FVerdantHUDObservation {
 TWeakObjectPtr<AVerdantCharacter> Character;
 float Health=0.f;
 float BodySize=0.f;
 bool bDead=false;
 bool bHitApplied=true;
 float StrikeFlashUntil=-1.f;
};
struct FVerdantHUDDamageFloat {
 TWeakObjectPtr<AVerdantCharacter> Character;
 FVector Position=FVector::ZeroVector;
 float Amount=0.f;
 float BornAt=0.f;
};

UCLASS()
class VERDANT_API AVerdantHUD : public AHUD {
 GENERATED_BODY()
public:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void DrawHUD() override;
 void OpenConversation(AVerdantCharacter* NPC);
 void CloseConversation();
 bool HasCanvasFont() const;
 bool bConversationOpen=false;
private:
 void FillConversationPrompt(const FString& Text);
 UPROPERTY(Transient) TObjectPtr<UFont> CanvasFont;
 TSharedPtr<SWidget> RootWidget;
 TSharedPtr<SMultiLineEditableTextBox> InputBox;
 TSharedPtr<SScrollBox> ConversationScroll;
 int32 LastDialogueLineCount=-1;
 TMap<int32,FVerdantHUDObservation> Observations;
 TArray<FVerdantHUDDamageFloat> DamageFloats;
 float LastPlayerHeightCm=81.f;
 float GrowthVisibleUntil=-1.f;
};