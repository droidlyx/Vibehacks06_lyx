#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VerdantDialoguePolicy.h"
#include "VerdantDialogue.generated.h"
class AVerdantCharacter;
class IHttpRequest;

enum class EVerdantConversationAvailability : uint8
{
	Ready,
	Invalid,
	TooFar,
	SizeMismatch,
	Cooldown,
	Busy,
	Unavailable
};

struct FVerdantDialogueJob
{
	int64 MessageId = 0, ThreadId = 0;
	TWeakObjectPtr<AVerdantCharacter> Speaker, Listener;
	FString Text;
	float ExpiresAt = 0.f;
	int32 SpeakerLife = 0, ListenerLife = 0, ListenerIntent = 0;
	bool bInitiative = false, bPlayerThread = false, bAddressed = false;
};

struct FVerdantWarnedAction
{
	TWeakObjectPtr<AVerdantCharacter> Actor, Target;
	FString Action;
	float Amount = 0.f, ExecuteAt = 0.f, ExpiresAt = 0.f;
	int32 ActorLife = 0, ActorIntent = 0, TargetLife = 0, TargetIntent = 0;
};

// A read-only feasibility snapshot, not an execution/completion receipt.
struct FVerdantDialogueOutcome
{
	FVerdantDialogueJob Job;
	FString Draft, Action, Result;
	int32 TargetId = 0;
	float Amount = 0.f;
	bool bCanExecute = false;
	int32 ListenerIntentAtCheck = 0;
	TWeakObjectPtr<AVerdantCharacter> TargetAtCheck;
	int32 TargetLifeAtCheck = 0;
	float ExpiresAt = 0.f;
};

UCLASS()
class VERDANT_API UVerdantDialogue : public UActorComponent
{
	GENERATED_BODY()

public:
	UVerdantDialogue();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool bBusy = false;
	FString Status = TEXT("DeepSeek 判断附近发言；不相关时结束，相关时回应");
	FString Endpoint = TEXT("https://api.qnaigc.com/v1/chat/completions");
	TArray<FString> Lines;
	TMap<int32, TArray<FString>> Memories;
	TWeakObjectPtr<AVerdantCharacter> Selected;
	TMap<int32, TArray<FString>> Transcripts;
	TMap<int32, FString> ConversationStatuses;
	TWeakObjectPtr<AVerdantCharacter> RequestNPC;
	void SelectNPC(AVerdantCharacter* NPC);
	void AppendNPCLine(int32 NPCId, const FString& Line);
	void SetNPCStatus(int32 NPCId, const FString& Message);
	void RememberExchange(int32 NPCId, const FString& Heard, const FString& Result);
	FString LastDecisionReason;
	bool CanConverse(const AVerdantCharacter* NPC) const;
	float GetHearingRadiusCm() const { return DialogueRadiusCm; }
	EVerdantConversationAvailability GetPlayerConversationAvailability(const AVerdantCharacter* NPC) const;
	bool CanAcceptPlayerMessage(const AVerdantCharacter* NPC) const;
	float PlayerSpeechCooldownRemaining() const;
	bool CanApplyDecision(const AVerdantCharacter* NPC, const FString& Action, int32 TargetId, float Amount,
	                      FString* Reason = nullptr) const;
	void Request(AVerdantCharacter* NPC, const FString& PlayerText);
	bool TrySendPlayerMessage(AVerdantCharacter* NPC, const FString& PlayerText);
	FString BuildState(AVerdantCharacter* NPC) const;
	bool ApplyDecision(AVerdantCharacter* NPC, const FString& Action, int32 TargetId, float Amount);

private:
	bool CanHear(const AVerdantCharacter* Speaker, const AVerdantCharacter* Listener) const;
	bool CanExchange(const AVerdantCharacter* Speaker, const AVerdantCharacter* Listener) const;
	float DialogueRadiusCm = VerdantDialoguePolicy::DefaultRadiusCm;
	float MaximumHeightRatio = VerdantDialoguePolicy::DefaultMaxHeightRatio;
	int32 MaxListenersPerSpeech = 3;
	float CloudRetryAfter = 0.f;
	bool IsCurrent(const FVerdantDialogueJob& Job) const;
	void EnqueueSpeech(AVerdantCharacter* Speaker, const FString& Text, int64 ThreadId, bool bPlayerThread,
	                   AVerdantCharacter* Addressed);
	void StartCloud(const FVerdantDialogueJob& Job);
	void FinishJob(const FVerdantDialogueJob& Job, const FString& Message = FString());
	void Publish(const FVerdantDialogueJob& Job, const FString& Say, const FString& Action, int32 TargetId,
	             float Amount, const FString& Rejection = FString());
	void CheckForReflection(const FVerdantDialogueJob& Job, const FString& Draft, const FString& Action,
	                        int32 TargetId, float Amount, const FString& Failure = FString());
	void StartReflection(const FVerdantDialogueOutcome& Outcome);
	void PublishReflection(const FVerdantDialogueOutcome& Outcome, const FString& Say);
	bool CanPublishReflection(const FVerdantDialogueOutcome& Outcome) const;
	void ConsiderInitiative();
	void TickWarnedActions();
	bool HasWarnedActionFor(const AVerdantCharacter* NPC) const;
	bool ReadingConversationWith(const AVerdantCharacter* NPC) const;
	TArray<FVerdantDialogueJob> HearingQueue;
	TArray<FVerdantWarnedAction> WarnedActions;
	TSet<FString> ResolvedDecisions;
	TSet<FString> CommittedDecisions;
	TSet<int32> PendingNPCs;
	TSet<FString> HeardPairs;
	TMap<int64, int32> ThreadLines;
	TMap<int64, float> ThreadExpiry;
	TMap<FString, FString> LastSituation;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CloudRequest;
	TFunction<void()> CompletedCloudReply;
	int64 NextMessageId = 0;
	float NextProactiveAt = 10.f, LastPlayerMessageAt = -100.f;
	int32 NextInitiatorIndex = 0;
	bool bPreferPlayerInitiative = true;
};
