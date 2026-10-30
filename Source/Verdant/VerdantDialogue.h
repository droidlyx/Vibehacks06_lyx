#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VerdantDialogue.generated.h"
class AVerdantCharacter;
UCLASS()
class VERDANT_API UVerdantDialogue : public UActorComponent {
 GENERATED_BODY()
public:
 bool bBusy = false;
 FString Status = TEXT("本地模型尚未连接");
 FString Endpoint = TEXT("http://127.0.0.1:8066/v1/chat/completions");
 TArray<FString> Lines;
 TMap<int32,TArray<FString>> Memories;
 TWeakObjectPtr<AVerdantCharacter> Selected;
 TMap<int32,TArray<FString>> Transcripts;
 TMap<int32,FString> ConversationStatuses;
 TWeakObjectPtr<AVerdantCharacter> RequestNPC;
 void SelectNPC(AVerdantCharacter* NPC);
 void AppendNPCLine(int32 NPCId, const FString& Line);
 void SetNPCStatus(int32 NPCId, const FString& Message);
 void RememberExchange(int32 NPCId, const FString& PlayerText, const FString& NPCResult);
 FString LastDecisionReason;
 bool CanConverse(const AVerdantCharacter* NPC) const;
 bool CanApplyDecision(const AVerdantCharacter* NPC, const FString& Action, int32 TargetId, float Amount, FString* Reason = nullptr) const;
 void Request(AVerdantCharacter* NPC, const FString& PlayerText);
 FString BuildState(AVerdantCharacter* NPC) const;
 bool ApplyDecision(AVerdantCharacter* NPC, const FString& Action, int32 TargetId, float Amount);
};
