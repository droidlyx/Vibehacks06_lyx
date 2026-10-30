#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "VerdantHUD.generated.h"
class SWidget;
class SMultiLineEditableTextBox;
class AVerdantCharacter;
UCLASS()
class VERDANT_API AVerdantHUD : public AHUD {
 GENERATED_BODY()
public:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void DrawHUD() override;
 void OpenConversation(AVerdantCharacter* NPC);
 void CloseConversation();
 bool bConversationOpen = false;
private:
 TSharedPtr<SWidget> RootWidget;
 TSharedPtr<SMultiLineEditableTextBox> InputBox;
};
