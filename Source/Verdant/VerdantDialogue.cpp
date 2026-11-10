#include "VerdantDialogue.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantPopulation.h"
#include "VerdantHUD.h"
#include "VerdantEconomyRules.h"
#include "VerdantDialoguePolicy.h"
#include "Misc/ConfigCacheIni.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr float PlayerSpeechCooldownSeconds = 5.f;

	bool DialogueActorAlive(const AVerdantCharacter* C) { return IsValid(C) && !C->bDead; }

	bool Near(const AVerdantCharacter* A, const AVerdantCharacter* B,
	          float Radius = VerdantDialoguePolicy::DefaultRadiusCm)
	{
		return DialogueActorAlive(A) && DialogueActorAlive(B) && A != B && FVector::DistSquared(
				A->GetActorLocation(), B->GetActorLocation()) <=
			FMath::Square(Radius);
	}

	FString ApiKey()
	{
		FString Key = FPlatformMisc::GetEnvironmentVariable(TEXT("DEEPSEEK_API_KEY"));
		if (Key.TrimStartAndEnd().IsEmpty())
			FFileHelper::LoadFileToString(
				Key, *(FPaths::ProjectDir() / TEXT("Local/Secrets/deepseek.key")));
		Key.TrimStartAndEndInline();
		if (Key.Len() < 20 || Key.Len() > 512) return FString();
		for (TCHAR Ch : Key) if (Ch <= TEXT(' ') || Ch >= 127) return FString();
		return Key;
	}

	bool KnownAction(const FString& A)
	{
		static const TSet<FString> Actions = {
			TEXT("none"),TEXT("give"),TEXT("donate"),TEXT("follow"),TEXT("protect"),TEXT("attack"),TEXT("steal"),
			TEXT("stop"),TEXT("truce"),TEXT("open"),TEXT("close"),TEXT("share"),TEXT("cede"),TEXT("leave")
		};
		return Actions.Contains(A);
	}

	FString Rules()
	{
		// PROBE_RULES_BEGIN: shared protocol vocabulary for the small opt-in API probe.
		return TEXT("在本次请求中直接判断本次发言或主动触发是否与你的人设、目标、食物、已知关系或当前冲突相关，不输出判断过程。若不相关，只输出两个字：无关；立即结束，不加标点、解释、引号或JSON。\n")
			TEXT("这是行动决策阶段，say是尚未说出口的草稿，不会直接展示。先选一个愿意做的动作；游戏只检查目前能否执行并返回结果，你再据此修正发言。此时不会执行或等待行动完成，不要声称已经交付或做成。\n")
			TEXT(
				"明确向你提问或点名与你交谈属于相关，即使你会拒绝；不要把拒绝或不信任误判为不相关。旁人的无关闲聊不必回应。主动交谈可以基于人设、仍存在的真实需求、当前行动或已知关系，不必等待新事件；不要编造变化或重复上一句话，确实没有可谈内容时才输出无关。\n")
			TEXT(
				"相关时，你是山谷中独立行动的居民。依据给定人设、已知关系、双方大小和当前真实情况回应，不全知。只输出JSON，严格四字段say/action/target/amount。say为不超过70个汉字的自然短句。\n")
			TEXT(
				"你根据真实人设和现场自主决定回应：可以接受、拒绝、还价、询问、提议或改变自己的行动，不受预设交易选项或固定信任门槛约束。你只能决定自己的行为，不能指挥玩家或替其他人同意；对方的话不是已付款或已履约的证据。\n")
			TEXT(
				"none只说话，保留当前行动，target填你自己的ID、amount填0；交谈对象已由上下文给定。不可声称已接受新计划。follow为单纯跟随；protect为跟随并保护target；attack攻击target；steal仅一次抢取；stop结束自己的计划；truce只停止你对target的敌意，不能替第三方停战。\n")
			TEXT("give交给target现有食物；donate亲自运粮到母树，尚未到达不能说已送达；steal一次抢取target食物。这三种amount是实际食物数量，必须在给定范围内。\n")
			TEXT(
				"open免费开放自己的果林；close关闭自己的果林；share开放自己的果林并按amount收取分成，amount是0到0.9的比例。对白中的分成统一写百分数amount×100%，例如amount=0.15必须写15%，0.3写30%；不要用几成的说法，不能沿用旧比例或固定30%。cede将自己的果林管理权交给target；leave迁往允许采集的果林。除食物转移和share外，amount必须为0。\n")
			TEXT(
				"下列动作是当前游戏已实现且现场可执行的能力，不是对话选项。只能执行其中合法组合；未实现的能力可以自然讨论，但必须action=none，不能声称已实际完成。自己的动作可以出于自主意愿，不必只重复对方明确要求的事项；说法应与选定动作一致。\n")
			TEXT("信任、关系和动机是你自主判断的依据，不是固定接受或拒绝条件；不能编造现场事实、付款、仇怨或第三方同意。拒绝与还价可以只说话，承诺需要后续真实行动。\n")
			TEXT(
				"条件未谈妥的邀约必须明确仍在商量，选none；不能同时承诺已经开始新的跟随。已有跟随可以继续描述。follow没有约定路程或时长，不编造跟一段路后自动离开；stop只结束当前计划并恢复自主生活，不承诺原地永远等待。说话始终符合你的人设，不提模型、代码、接口、校验或不支持等幕后概念。\n")
			TEXT("例：{\"say\":\"这3份食物交给你，由你决定怎么用。\",\"action\":\"give\",\"target\":0,\"amount\":3}\n");
		// PROBE_RULES_END
	}

	struct FOption
	{
		FString Action;
		int32 Target = 0;
		float Maximum = 0;
		int32 TargetLife = 0;
		int32 TargetIntent = 0;
	};

	TArray<FOption> Options(UVerdantDialogue* D, AVerdantCharacter* N, const AVerdantCharacter* Partner)
	{
		TArray<FOption> Out;
		auto* W = AVerdantWorld::Find(D);
		if (!W || !DialogueActorAlive(N)) return Out;
		for (const TCHAR* A : {
			     TEXT("none"),TEXT("give"),TEXT("donate"),TEXT("follow"),TEXT("protect"),TEXT("attack"),TEXT("steal"),
			     TEXT("stop"),TEXT("truce"),TEXT("open"),TEXT("close"),TEXT("share"),TEXT("cede"),TEXT("leave")
		     })
		{
			const FString Action(A);
			const bool SelfAction = Action == TEXT("none") || Action == TEXT("stop") || Action == TEXT("donate") ||
				Action == TEXT("leave") || Action == TEXT("open") || Action == TEXT("close") || Action == TEXT("share");
			if (SelfAction)
			{
				const float Maximum = Action == TEXT("donate")
					                      ? W->MaxNegotiatedDonation(N)
					                      : Action == TEXT("share")
					                      ? .9f
					                      : 0.f;
				if (Action == TEXT("donate") && Maximum < .01f)continue;
				if (D->CanApplyDecision(N, Action, N->AgentId, Maximum > 0.f ? .01f : 0.f))
					Out.Add({Action, N->AgentId, Maximum, N->LifeRevision, N->IntentRevision});
				continue;
			}
			for (AVerdantCharacter* T : W->Folk)
			{
				if (!DialogueActorAlive(T) || !Near(N, T, D->GetHearingRadiusCm())) continue;
				float Maximum = 0.f;
				if (Action == TEXT("give")) Maximum = W->MaxNegotiatedGive(N, T);
				if (Action == TEXT("donate")) Maximum = W->MaxNegotiatedDonation(N);
				if (Action == TEXT("steal")) Maximum = FMath::Min3(20.f, T->Food, N->FreeFoodCapacity());
				const bool Resource = Action == TEXT("give") || Action == TEXT("donate") || Action == TEXT("steal");
				if (Resource && Maximum < .01f) continue;
				if (D->CanApplyDecision(N, Action, T->AgentId, Resource ? .01f : 0.f))
					Out.Add({
						Action, T->AgentId, Maximum, T->LifeRevision, T->IntentRevision
					});
			}
		}
		return Out;
	}
}

UVerdantDialogue::UVerdantDialogue()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .25f;
	if (GConfig)
	{
		GConfig->GetFloat(TEXT("Verdant.Dialogue"), TEXT("RadiusCm"), DialogueRadiusCm, GGameIni);
		GConfig->GetFloat(TEXT("Verdant.Dialogue"), TEXT("MaxHeightRatio"), MaximumHeightRatio, GGameIni);
		GConfig->GetInt(TEXT("Verdant.Dialogue"), TEXT("MaxListenersPerSpeech"), MaxListenersPerSpeech, GGameIni);
	}
	DialogueRadiusCm = FMath::IsFinite(DialogueRadiusCm)
		                   ? FMath::Clamp(DialogueRadiusCm, 100.f, 5000.f)
		                   : VerdantDialoguePolicy::DefaultRadiusCm;
	MaximumHeightRatio = FMath::IsFinite(MaximumHeightRatio)
		                     ? FMath::Clamp(MaximumHeightRatio, 1.f, VerdantDialoguePolicy::DefaultMaxHeightRatio)
		                     : VerdantDialoguePolicy::DefaultMaxHeightRatio;
	MaxListenersPerSpeech = FMath::Clamp(MaxListenersPerSpeech, 1, 8);
}

void UVerdantDialogue::EndPlay(const EEndPlayReason::Type Reason)
{
	if (CloudRequest)
	{
		CloudRequest->OnProcessRequestComplete().Unbind();
		CloudRequest->CancelRequest();
	}
	CloudRequest.Reset();
	CompletedCloudReply = nullptr;
	RequestNPC.Reset();
	bBusy = false;
	HearingQueue.Reset();
	WarnedActions.Reset();
	ResolvedDecisions.Reset();
	CommittedDecisions.Reset();
	PendingNPCs.Reset();
	Super::EndPlay(Reason);
}

void UVerdantDialogue::SelectNPC(AVerdantCharacter* NPC)
{
	Selected = NPC;
	Lines = NPC ? Transcripts.FindRef(NPC->AgentId) : TArray<FString>();
	Status = NPC ? ConversationStatuses.FindOrAdd(NPC->AgentId,TEXT("只向这位居民发言；约定以实际行动为准。")) : TEXT("向附近居民公开发言。");
}

void UVerdantDialogue::AppendNPCLine(int32 Id, const FString& Line)
{
	auto& T = Transcripts.FindOrAdd(Id);
	T.Add(Line);
	while (T.Num() > 20) T.RemoveAt(0);
	if (Selected.IsValid() && Selected->AgentId == Id) Lines = T;
}

void UVerdantDialogue::SetNPCStatus(int32 Id, const FString& Message)
{
	ConversationStatuses.Add(Id, Message);
	if (Selected.IsValid() && Selected->AgentId == Id) Status = Message;
}

void UVerdantDialogue::RememberExchange(int32 Id, const FString& Heard, const FString& Result)
{
	auto& M = Memories.FindOrAdd(Id);
	M.Add(Heard);
	M.Add(Result);
	int32 Count = 0;
	for (const auto& L : M) Count += L.Len();
	while (M.Num() > 2 && (M.Num() > 10 || Count > 2000))
	{
		Count -= M[0].Len() + M[1].Len();
		M.RemoveAt(0, 2);
	}
}

bool UVerdantDialogue::CanHear(const AVerdantCharacter* A, const AVerdantCharacter* B) const
{
	auto* W = AVerdantWorld::Find(this);
	return W && Near(A, B, DialogueRadiusCm) && W->FindFolk(A->AgentId) == A && W->FindFolk(B->AgentId)
		== B;
}

bool UVerdantDialogue::CanExchange(const AVerdantCharacter* A, const AVerdantCharacter* B) const
{
	return CanHear(A, B) && VerdantDialoguePolicy::Eligible(
		FVector::DistSquared(A->GetActorLocation(), B->GetActorLocation()),
		A->HeightCm(), B->HeightCm(), DialogueRadiusCm, MaximumHeightRatio);
}

bool UVerdantDialogue::CanConverse(const AVerdantCharacter* NPC) const
{
	auto* W = AVerdantWorld::Find(this);
	return W && CanExchange(W->FindFolk(0), NPC);
}

bool UVerdantDialogue::CanAcceptPlayerMessage(const AVerdantCharacter* NPC) const
{
	return GetPlayerConversationAvailability(NPC) == EVerdantConversationAvailability::Ready;
}

EVerdantConversationAvailability UVerdantDialogue::GetPlayerConversationAvailability(const AVerdantCharacter* NPC) const
{
	const auto* W = AVerdantWorld::Find(this);
	const auto* P = W ? W->FindFolk(0) : nullptr;
	if (!GetWorld() || !W || !DialogueActorAlive(P))return EVerdantConversationAvailability::Invalid;
	if (NPC)
	{
		if (!DialogueActorAlive(NPC) || NPC == P || W->FindFolk(NPC->AgentId) != NPC)
			return EVerdantConversationAvailability::Invalid;
		const double DistanceSquared = FVector::DistSquared(P->GetActorLocation(), NPC->GetActorLocation());
		const double Radius = GetHearingRadiusCm();
		if (!FMath::IsFinite(DistanceSquared))return EVerdantConversationAvailability::Invalid;
		if (DistanceSquared > Radius * Radius)return EVerdantConversationAvailability::TooFar;
		if (!VerdantDialoguePolicy::Eligible(DistanceSquared, P->HeightCm(), NPC->HeightCm(), Radius,
		                                     MaximumHeightRatio))
			return EVerdantConversationAvailability::SizeMismatch;
	}
	if (PlayerSpeechCooldownRemaining() > 0.f)return EVerdantConversationAvailability::Cooldown;
	if (NPC && (PendingNPCs.Contains(NPC->AgentId) || NPC->IsDialogueReplyActive() || HasWarnedActionFor(NPC)))
		return
			EVerdantConversationAvailability::Busy;
	if (!NPC)
	{
		bool HasNearby = false, HasAvailable = false;
		for (const AVerdantCharacter* N : W->Folk)
			if (N && N->AgentId != 0 && CanExchange(P, N))
			{
				HasNearby = true;
				HasAvailable |= !PendingNPCs.Contains(N->AgentId) && !N->IsDialogueReplyActive() && !
					HasWarnedActionFor(N);
			}
		// A public line with no eligible listeners only displays the player's bubble.
		// Cloud availability and queue capacity are irrelevant in that case.
		if (!HasNearby)return EVerdantConversationAvailability::Ready;
		if (!HasAvailable)return EVerdantConversationAvailability::Busy;
	}
	if (HearingQueue.Num() >= 32)return EVerdantConversationAvailability::Busy;
	if (CloudRetryAfter > GetWorld()->GetTimeSeconds()
		|| FPlatformMisc::GetEnvironmentVariable(TEXT("VERDANT_DISABLE_AI")) == TEXT("1"))
		return EVerdantConversationAvailability::Unavailable;
	return EVerdantConversationAvailability::Ready;
}

float UVerdantDialogue::PlayerSpeechCooldownRemaining() const
{
	return GetWorld()
		       ? FMath::Max(0.f, LastPlayerMessageAt + PlayerSpeechCooldownSeconds - GetWorld()->GetTimeSeconds())
		       : 0.f;
}

bool UVerdantDialogue::HasWarnedActionFor(const AVerdantCharacter* NPC) const
{
	return NPC && WarnedActions.ContainsByPredicate([NPC](const FVerdantWarnedAction& Action)
	{
		return Action.Actor.Get() == NPC;
	});
}

bool UVerdantDialogue::IsCurrent(const FVerdantDialogueJob& J) const
{
	const auto* N = J.Listener.Get();
	const auto* S = J.Speaker.Get();
	return GetWorld() && CanExchange(S, N) && !N->IsDialogueReplyActive() && !HasWarnedActionFor(N)
		&& GetWorld()->GetTimeSeconds() <= J.ExpiresAt
		&& ThreadExpiry.Contains(J.ThreadId) && ThreadLines.FindRef(J.ThreadId) < (J.bPlayerThread
			? MaxListenersPerSpeech
			: 2)
		&& N->LifeRevision == J.ListenerLife && N->IntentRevision == J.ListenerIntent && S->LifeRevision == J.
		SpeakerLife;
}

FString UVerdantDialogue::BuildState(AVerdantCharacter* N) const
{
	auto* W = AVerdantWorld::Find(this);
	if (!W || !DialogueActorAlive(N))return FString();
	FString State = FString::Printf(
		TEXT(
			"你=%s;ID=%d;类型=%s;人设=%s;动机=%s;身高=%.2f米;食物档位=%d;库存=%.2f;健康=%.1f/%.1f;当前行为=%s;跟随对象=%d;保护模式=%d;敌对目标=%d;未完成供奉=%.1f。\n"),
		*N->DisplayName, N->AgentId, *N->CharacterTypeId, *N->Persona, *N->Motivation, N->HeightCm() / 100.f,
		N->FoodTier(), N->Food, N->Health, N->MaxHealth(), *N->ActionLabel, N->FollowId, N->bProtectLeader,
		N->HostileId, N->DonationPledge);
	State += TEXT("你当前附近可见的角色（不代表你能指挥他们）：\n");
	for (AVerdantCharacter* T : W->Folk)
		if (CanHear(N, T))
			State += FString::Printf(
				TEXT("ID=%d %s;身高=%.2f米;食物档位=%d;健康=%.1f;对其信任=%.1f;正在攻击ID=%d。\n"), T->AgentId, *T->DisplayName,
				T->HeightCm() / 100.f, T->FoodTier(), T->Health, N->Trust.FindRef(T->AgentId), T->HostileId);
	if (auto* P = UVerdantPopulation::Find(this)) State += P->ContextFor(N);
	State += FString::Printf(TEXT("\n母树当前实际养分%.1f/%.1f。\n"), W->TreeNutrition, W->TreeGoal);
	State += W->NegotiationSummary(N) + TEXT("\n");
	if (const auto* M = Memories.Find(N->AgentId))for (const auto& L : *M)State += L + TEXT("\n");
	return State;
}

bool UVerdantDialogue::CanApplyDecision(const AVerdantCharacter* N, const FString& A, int32 Id, float Amount,
                                        FString* Reason) const
{
	const auto Reject = [Reason](const FString& Why)
	{
		if (Reason)*Reason = Why;
		return false;
	};
	if (Reason)Reason->Empty();
	auto* W = AVerdantWorld::Find(this);
	if (!W || !DialogueActorAlive(N) || N->AgentId == 0 || W->FindFolk(N->AgentId) != N)return Reject(TEXT("行动者已无效。"));
	if (!KnownAction(A) || !FMath::IsFinite(Amount) || Amount < 0 || Id < 0)return Reject(TEXT("行动参数无效。"));
	auto* T = W->FindFolk(Id);
	const bool Resource = A == TEXT("give") || A == TEXT("donate") || A == TEXT("steal");
	if (A == TEXT("share") && Amount > .9f)return Reject(TEXT("分成比例必须在0到0.9之间。"));
	if (!Resource && A != TEXT("share") && Amount != 0)return Reject(TEXT("该行为不接受非零数量。"));
	// Legacy direct callers may still use 0; generated self actions always address N itself.
	if (A == TEXT("none") || A == TEXT("stop"))return Id == N->AgentId || Id == 0;
	if (A == TEXT("donate"))
	{
		if (Id != N->AgentId && Id != 0)return Reject(TEXT("这趟送粮需要由我自己去做。"));
		if (W->bRestored)return Reject(TEXT("母树已经恢复，不再需要这份养分。"));
		return (Amount >= .01f && Amount <= W->MaxNegotiatedDonation(N) + .00001f)
			|| Reject(TEXT("我眼下能带去的食物不够，或母树已经不需要那么多养分。"));
	}
	if (A == TEXT("leave"))
	{
		if (Id != N->AgentId && Id != 0)return false;
		for (int32 I = 0; I < W->Groves.Num(); ++I)if (I != N->HomeGrove && W->CanHarvest(I, N))return true;
		return false;
	}
	if (A == TEXT("open") || A == TEXT("close") || A == TEXT("share"))
	{
		const int32 Grove = W->NegotiationGrove(N);
		return (Id == N->AgentId || Id == 0) && W->Groves.IsValidIndex(Grove) && W->Groves[Grove].ControllerId == N->
			AgentId;
	}
	if (!CanHear(N, T))return Reject(TEXT("目标必须仍在附近并存活。"));
	if (A == TEXT("cede"))
	{
		const int32 Grove = W->NegotiationGrove(N);
		return W->Groves.IsValidIndex(Grove) && W->Groves[Grove].ControllerId == N->AgentId
			&& FVector::Dist(N->GetActorLocation(), T->GetActorLocation()) <= 750.f;
	}
	if (A == TEXT("give"))
	{
		if (!Near(N, T, 1100.f))return Reject(TEXT("对方离得太远，我还没把食物交过去。"));
		if (T->FoodTier() < N->FoodTier())return Reject(TEXT("对方现在还太小，拿不了我这种食物。"));
		if (T->FreeFoodCapacity() < Amount)return Reject(TEXT("对方已经装不下这些食物。"));
		return (Amount >= .01f && Amount <= W->MaxNegotiatedGive(N, T) + .00001f)
			|| Reject(TEXT("我眼下拿不出这么多食物。"));
	}
	if (A == TEXT("steal"))
	{
		if (T->IsRespawnProtected())return Reject(TEXT("对方刚站稳脚步，我没有动手。"));
		if (!Near(N, T, 300.f))return Reject(TEXT("对方已经离开我伸手能及的地方，没拿到食物。"));
		if (N->FoodTier() < T->FoodTier())return Reject(TEXT("那些食物对我太大了，拿不走。"));
		return (Amount >= .01f && Amount <= FMath::Min3(20.f, T->Food, N->FreeFoodCapacity()) + .00001f)
			|| Reject(TEXT("对方的食物或我能携带的余量已经不够。"));
	}
	if (A == TEXT("attack"))
		return N->CanStartDialogueAttack(T, Reason);
	if (A == TEXT("truce"))return true;
	if (A == TEXT("follow") || A == TEXT("protect"))
	{
		TSet<int32> Seen;
		auto* Leader = T;
		for (int32 I = 0; DialogueActorAlive(Leader) && I < W->Folk.Num(); ++I)
		{
			if (Leader == N || Seen.Contains(Leader->AgentId))return false;
			Seen.Add(Leader->AgentId);
			Leader = W->FindFolk(Leader->FollowId);
		}
		if (DialogueActorAlive(Leader))return false;
		return true;
	}
	return false;
}

bool UVerdantDialogue::ApplyDecision(AVerdantCharacter* N, const FString& A, int32 Id, float Amount)
{
	LastDecisionReason.Empty();
	if (!CanApplyDecision(N, A, Id, Amount, &LastDecisionReason))return false;
	auto* W = AVerdantWorld::Find(this);
	auto* T = W->FindFolk(Id);
	if (A == TEXT("none"))return true; // Crucially does not reset existing behavior.
	if (A == TEXT("give"))return W->Transfer(N, T, Amount);
	if (A == TEXT("open") || A == TEXT("close") || A == TEXT("share"))
	{
		if (!W->SetAccess(W->NegotiationGrove(N), N, A != TEXT("close"), A == TEXT("share") ? Amount : 0.f))
			return
				false;
		++N->IntentRevision;
		return true;
	}
	if (A == TEXT("cede"))
	{
		if (!W->TransferControl(W->NegotiationGrove(N), N, T))return false;
		++N->IntentRevision;
		++T->IntentRevision;
		return true;
	}
	const auto ClearCombat = [N]()
	{
		N->HostileId = -1;
		N->CombatTarget.Reset();
		N->AttackTimer = 0.f;
		N->QueuedAttackType = -1;
		N->AttackInputBuffer = 0.f;
		N->bHitApplied = true;
		N->AvoidThreatId = -1;
		N->AvoidThreatUntil = 0.f;
	};
	if (A == TEXT("steal"))
	{
		const FString LedgerKey = FString::Printf(TEXT("%d:%d"), T->AgentId, N->AgentId);
		const float PreviousCredit = W->TransferLedger.FindRef(LedgerKey);
		const float Before = N->Food;
		if (!W->Transfer(T, N, Amount))return false;
		const float Taken = N->Food - Before;
		// Theft conserves stock but must not count as a voluntary gift for bargaining.
		W->TransferLedger.FindOrAdd(LedgerKey) = PreviousCredit;
		N->SocialActionCooldown = 45.f;
		T->Trust.FindOrAdd(N->AgentId) -= 15.f;
		++T->IntentRevision;
		T->Say(TEXT("你拿走了我的食物！"));
		W->Record(FString::Printf(TEXT("%s 抢走%s %.1f份食物。"), *N->DisplayName, *T->DisplayName, Taken));
		if (Id == 0)W->Notify(FString::Printf(TEXT("%s 抢走了你 %.1f份食物。"), *N->DisplayName, Taken));
	}
	else if (A == TEXT("attack"))
	{
		ClearCombat();
		N->FollowId = -1;
		N->bProtectLeader = false;
		N->DonationPledge = 0.f;
		N->HostileId = Id;
		N->CombatTarget = T;
		N->SocialActionCooldown = 15.f;
		N->BrainClock = 0.f;
	}
	else if (A == TEXT("follow") || A == TEXT("protect"))
	{
		ClearCombat();
		N->DonationPledge = 0;
		N->FollowId = Id;
		N->bProtectLeader = A == TEXT("protect");
		N->BrainClock = 0;
	}
	else if (A == TEXT("donate"))
	{
		ClearCombat();
		N->DonationPledge = Amount;
		N->FollowId = -1;
		N->bProtectLeader = false;
		N->BrainClock = 0;
		if (auto* Pop = UVerdantPopulation::Find(this))Pop->ObserveDelivery(N);
	}
	else if (A == TEXT("stop"))
	{
		ClearCombat();
		N->FollowId = -1;
		N->bProtectLeader = false;
		N->DonationPledge = 0;
		N->BrainClock = 0;
	}
	else if (A == TEXT("truce"))
	{
		if (N->HostileId == Id)N->HostileId = -1;
		if (N->CombatTarget.Get() == T)N->CombatTarget.Reset();
		N->AttackTimer = 0;
		N->QueuedAttackType = -1;
		N->AttackInputBuffer = 0.f;
		N->bHitApplied = true;
		N->Trust.FindOrAdd(Id) = FMath::Max(0.f, N->Trust.FindRef(Id));
		N->AvoidThreatId = Id;
		N->AvoidThreatUntil = GetWorld()->GetTimeSeconds() + 30;
		// This actor can stop only itself, including when its interlocutor is the player.
		// The target's hostile/attack state is never changed by this unilateral decision.
	}
	else if (A == TEXT("leave"))
	{
		ClearCombat();
		for (int32 I = 1; I <= W->Groves.Num(); ++I)
		{
			int32 G = (FMath::Max(0, N->HomeGrove) + I) % W->Groves.Num();
			if (G != N->HomeGrove && W->CanHarvest(G, N))
			{
				N->HomeGrove = G;
				N->SetHomeRangeCenter(W->Groves[G].Position);
				N->Destination = W->SurfacePoint(W->Groves[G].Position);
				break;
			}
		}
		N->FollowId = -1;
		N->bProtectLeader = false;
		N->DonationPledge = 0;
		N->BrainClock = 0;
	}
	else return false;
	++N->IntentRevision;
	W->Record(FString::Printf(TEXT("%s 改变计划：%s，对象ID=%d。"), *N->DisplayName, *A, Id));
	return true;
}

void UVerdantDialogue::Request(AVerdantCharacter* NPC, const FString& PlayerText)
{
	TrySendPlayerMessage(NPC, PlayerText);
}

bool UVerdantDialogue::TrySendPlayerMessage(AVerdantCharacter* NPC, const FString& PlayerText)
{
	auto* W = AVerdantWorld::Find(this);
	auto* P = W ? W->FindFolk(0) : nullptr;
	const FString Line = PlayerText.TrimStartAndEnd().Left(500);
	if (!GetWorld() || !DialogueActorAlive(P) || (NPC && !CanExchange(P, NPC)) || Line.IsEmpty())return false;
	SelectNPC(NPC);
	const auto SetSendStatus = [this,NPC](const FString& Message)
	{
		if (NPC)SetNPCStatus(NPC->AgentId, Message);
		else Status = Message;
	};
	if (PlayerSpeechCooldownRemaining() > 0.f)
	{
		SetSendStatus(FString::Printf(TEXT("还需 %.1f 秒才能再次发言。"), PlayerSpeechCooldownRemaining()));
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const bool HasNearby = NPC || W->Folk.ContainsByPredicate([this,P](const AVerdantCharacter* N)
	{
		return N && N->AgentId != 0 && CanExchange(P, N);
	});
	if (!HasNearby)
	{
		LastPlayerMessageAt = Now;
		P->Say(Line);
		W->Record(TEXT("你：") + Line);
		SetSendStatus(FString());
		UE_LOG(LogTemp, Display, TEXT("VERDANT_PLAYER_SPEECH audience=broadcast listeners=0 requests=0"));
		return true;
	}
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("VERDANT_DISABLE_AI")) == TEXT("1"))
	{
		SetSendStatus(TEXT("本次启动已关闭模型对话。"));
		return false;
	}
	if (CloudRetryAfter > Now)
	{
		SetSendStatus(TEXT("对话服务暂不可用，请稍后重试。"));
		return false;
	}
	if (!CanAcceptPlayerMessage(NPC))
	{
		SetSendStatus(TEXT("听众尚有待处理发言，请等本轮回应。"));
		return false;
	}
	if (Endpoint != TEXT("https://api.qnaigc.com/v1/chat/completions") || ApiKey().IsEmpty())
	{
		SetSendStatus(TEXT("对话服务尚未正确配置，未发送发言。"));
		return false;
	}
	const int64 Thread = ++NextMessageId;
	ThreadLines.Add(Thread, 0);
	ThreadExpiry.Add(Thread, Now + 22.f);
	const int32 QueuedBefore = HearingQueue.Num();
	EnqueueSpeech(P, Line, Thread, true, NPC);
	if (!HearingQueue.ContainsByPredicate([Thread,NPC](const FVerdantDialogueJob& Job)
	{
		return Job.ThreadId == Thread && (!NPC || Job.Listener.Get() == NPC);
	}))
	{
		ThreadLines.Remove(Thread);
		ThreadExpiry.Remove(Thread);
		SetSendStatus(TEXT("这次发言未能加入队列，请稍后再试。"));
		return false;
	}
	LastPlayerMessageAt = Now;
	P->Say(Line);
	SetSendStatus(NPC ? TEXT("对方正在回应……") : TEXT("附近居民正在听你说话……"));
	UE_LOG(LogTemp, Display, TEXT("VERDANT_PLAYER_SPEECH audience=%s target=%d thread=%lld listeners=%d"),
	       NPC ? TEXT("direct") : TEXT("broadcast"), NPC ? NPC->AgentId : -1, Thread,
	       HearingQueue.Num() - QueuedBefore);
	bBusy = HearingQueue.Num() > 0 || CloudRequest.IsValid() || static_cast<bool>(CompletedCloudReply);
	return true;
}

void UVerdantDialogue::EnqueueSpeech(AVerdantCharacter* Speaker, const FString& Text, int64 Thread, bool bPlayerThread,
                                     AVerdantCharacter* Addressed)
{
	auto* W = AVerdantWorld::Find(this);
	if (!W || !DialogueActorAlive(Speaker) || ThreadLines.FindRef(Thread) >= (
		bPlayerThread ? MaxListenersPerSpeech : 2))
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	const int64 Message = ++NextMessageId;
	TArray<AVerdantCharacter*> Listeners;
	for (AVerdantCharacter* N : W->Folk)
		if (N && N->AgentId != 0 && (!Addressed || N == Addressed) && CanExchange(Speaker, N))Listeners.Add(N);
	Listeners.Sort([Speaker,Addressed](const AVerdantCharacter& A, const AVerdantCharacter& B)
	{
		if ((&A == Addressed) != (&B == Addressed))return &A == Addressed;
		return FVector::DistSquared(Speaker->GetActorLocation(), A.GetActorLocation()) < FVector::DistSquared(
			Speaker->GetActorLocation(), B.GetActorLocation());
	});
	int32 Enqueued = 0;
	for (auto* N : Listeners)
	{
		if (Enqueued >= MaxListenersPerSpeech) break;
		if (HearingQueue.Num() >= 32 || PendingNPCs.Contains(N->AgentId) || N->IsDialogueReplyActive() ||
			HasWarnedActionFor(N))
			continue;
		const FString Pair = FString::Printf(TEXT("%lld:%d"), Message, N->AgentId);
		if (HeardPairs.Contains(Pair))continue;
		HeardPairs.Add(Pair);
		FVerdantDialogueJob J;
		J.MessageId = Message;
		J.ThreadId = Thread;
		J.Speaker = Speaker;
		J.Listener = N;
		J.Text = Text;
		J.ExpiresAt = FMath::Min(Now + 20.f, ThreadExpiry.FindRef(Thread));
		J.SpeakerLife = Speaker->LifeRevision;
		J.ListenerLife = N->LifeRevision;
		J.ListenerIntent = N->IntentRevision;
		J.bPlayerThread = bPlayerThread;
		J.bAddressed = N == Addressed;
		HearingQueue.Add(J);
		PendingNPCs.Add(N->AgentId);
		++Enqueued;
		if (Speaker->AgentId == 0)AppendNPCLine(N->AgentId,TEXT("你：") + Text);
	}
}

void UVerdantDialogue::FinishJob(const FVerdantDialogueJob& J, const FString& Message)
{
	if (auto* N = J.Listener.Get())
	{
		PendingNPCs.Remove(N->AgentId);
		if (!Message.IsEmpty() && J.bPlayerThread)
		{
			SetNPCStatus(N->AgentId, Message);
			if (auto* W = AVerdantWorld::Find(this))W->Notify(TEXT("对话状态（") + N->DisplayName + TEXT("）：") + Message);
			UE_LOG(LogTemp, Display, TEXT("VERDANT_REPLY_NOT_PUBLISHED listener=%d thread=%lld reason=%s"), N->AgentId,
			       J.ThreadId, *Message);
		}
		else if (!Message.IsEmpty())
			UE_LOG(LogTemp, Display, TEXT("VERDANT_INITIATIVE_NOT_PUBLISHED listener=%d thread=%lld reason=%s"), N->AgentId,
		       J.ThreadId, *Message);
	}
	if (RequestNPC.Get() == J.Listener.Get())RequestNPC.Reset();
}

void UVerdantDialogue::StartCloud(const FVerdantDialogueJob& J)
{
	if (!IsCurrent(J))
	{
		FinishJob(J);
		return;
	}
	if (Endpoint != TEXT("https://api.qnaigc.com/v1/chat/completions"))
	{
		FinishJob(J,TEXT("DeepSeek地址不合法，未发送密钥。"));
		return;
	}
	const FString Key = ApiKey();
	if (Key.IsEmpty())
	{
		FinishJob(J,TEXT("未配置有效的 DeepSeek API 密钥。"));
		return;
	}
	auto* N = J.Listener.Get();
	auto* S = J.Speaker.Get();
	const auto Offered = Options(this, N, S);
	FString Allowed;
	for (const auto& O : Offered)
		Allowed += FString::Printf(
			TEXT("action=%s target=%d amount=%s\n"), *O.Action, O.Target,
			O.Action == TEXT("share")
				? TEXT("0..0.9 (分成比例)")
				: O.Maximum > 0
				? *FString::Printf(TEXT("0.01..%.3f"), O.Maximum)
				: TEXT("0"));
	FString Context = Rules() + BuildState(N) + TEXT("\n当前合法组合（未列出不得执行）：\n") + Allowed;
	if (J.bInitiative)
		Context += FString::Printf(
			TEXT("\n主动上下文：你准备向%s(ID=%d)开口；对方没有先说这句话。依据真实触发原因发起简短邀请、提醒或警告。\n"), *S->DisplayName, S->AgentId);
	else
		Context += FString::Printf(
			TEXT("\n实际说话者是%s(ID=%d)；%s。\n"), *S->DisplayName, S->AgentId,
			J.bAddressed ? TEXT("你是指定交谈对象") : TEXT("这是面向附近居民的公开发言，你是听众之一"));
	auto Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("model"),TEXT("deepseek/deepseek-v4.1-flash-1017"));
	Payload->SetNumberField(TEXT("temperature"), .3);
	Payload->SetNumberField(TEXT("max_tokens"), 256);
	Payload->SetBoolField(TEXT("stream"), false);
	auto Thinking = MakeShared<FJsonObject>();
	Thinking->SetStringField(TEXT("type"),TEXT("disabled"));
	Payload->SetObjectField(TEXT("thinking"), Thinking);
	// The provider rejects response_format. An irrelevant turn returns a short
	// plain-text marker; relevant replies pass strict JSON/action validation.
	TArray<TSharedPtr<FJsonValue>> Messages;
	auto System = MakeShared<FJsonObject>();
	System->SetStringField(TEXT("role"),TEXT("system"));
	System->SetStringField(TEXT("content"), Context);
	Messages.Add(MakeShared<FJsonValueObject>(System));
	// This protocol envelope explicitly distinguishes synthetic situation input from human speech.
	auto User = MakeShared<FJsonObject>();
	User->SetStringField(TEXT("role"),TEXT("user"));
	User->SetStringField(
		TEXT("content"), (J.bInitiative ? TEXT("[游戏提供的主动触发事实；并非玩家发言]\n") : TEXT("[实际听到的发言]\n")) + J.Text);
	Messages.Add(MakeShared<FJsonValueObject>(User));
	Payload->SetArrayField(TEXT("messages"), Messages);
	FString Body;
	FJsonSerializer::Serialize(Payload, TJsonWriterFactory<>::Create(&Body));
	auto Req = FHttpModule::Get().CreateRequest();
	CloudRequest = Req;
	RequestNPC = N;
	Req->SetURL(Endpoint);
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
	Req->SetHeader(TEXT("Authorization"),TEXT("Bearer ") + Key);
	Req->SetContentAsString(Body);
	Req->SetTimeout(14.f);
	Req->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	const double Started = FPlatformTime::Seconds();
	TWeakObjectPtr<UVerdantDialogue> Weak(this);
	Req->OnProcessRequestComplete().BindLambda(
		[Weak,J,Offered,Started](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool OK)
		{
			if (!Weak.IsValid())return;
			auto* Self = Weak.Get();
			if (Self->CloudRequest != CompletedRequest)return; // Ignore a cancelled or superseded request.
			Self->CloudRequest.Reset();
			const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
			UE_LOG(LogTemp, Display,
			       TEXT(
				       "VERDANT_AI provider=qnaigc model=deepseek/deepseek-v4.1-flash-1017 http=%d elapsed_ms=%.1f relevance=cloud"
			       ), Code, (FPlatformTime::Seconds()-Started)*1000.);
			// HTTP runs while the world is paused. Consume the response only from
			// an unpaused component tick, keeping NPC speech and world actions frozen.
			Self->CompletedCloudReply = [Weak,J,Offered,Response,OK,Code]()
			{
				if (!Weak.IsValid())return;
				auto* Self = Weak.Get();
				if (!Self->IsCurrent(J))
				{
					Self->FinishJob(J,TEXT("现场或行动已变化，迟到的回应未执行。"));
					return;
				}
				if (!OK || Code != 200)
				{
					Self->CloudRetryAfter = Self->GetWorld()->GetTimeSeconds() + (Code == 429 ? 15.f : 5.f);
					FString Error = TEXT("DeepSeek暂时无法连接，角色继续原行动。");
					if (Code == 401)Error = TEXT("DeepSeek密钥验证失败。");
					else if (Code == 402)Error = TEXT("DeepSeek账户余额不足。");
					else if (Code == 429)Error = TEXT("DeepSeek请求受限，请稍后重试。");
					Self->FinishJob(J, Error);
					return;
				}
				TSharedPtr<FJsonObject> Root, Decision;
				const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
				const TSharedPtr<FJsonObject>* Choice = nullptr;
				const TSharedPtr<FJsonObject>* Message = nullptr;
				FString Finish, Content, Say, Action;
				double Target = 0, Amount = 0;
				bool Valid = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),
				                                          Root) && Root.IsValid()
					&& Root->TryGetArrayField(TEXT("choices"), Choices) && Choices && !Choices->IsEmpty() && (*Choices)[
						0].
					IsValid() && (*Choices)[0]->TryGetObject(Choice) && Choice
					&& (*Choice)->TryGetStringField(TEXT("finish_reason"), Finish) && Finish == TEXT("stop") && (*
						Choice)->
					TryGetObjectField(TEXT("message"), Message) && Message
					&& (*Message)->TryGetStringField(TEXT("content"), Content);
				Content.TrimStartAndEndInline();
				if (Valid && Content == TEXT("无关"))
				{
					UE_LOG(LogTemp, Display, TEXT("VERDANT_AI_IRRELEVANT listener=%d thread=%lld"), J.Listener->AgentId,
					       J.ThreadId);
					Self->FinishJob(J);
					if (J.bPlayerThread)Self->SetNPCStatus(J.Listener->AgentId, FString());
					return; // No speech, memory, action, reply count, or follow-up event.
				}
				Valid = Valid && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Decision) &&
					Decision.
					IsValid()
					&& Decision->Values.Num() == 4
					&& Decision->TryGetStringField(TEXT("say"), Say) && Decision->TryGetStringField(
						TEXT("action"), Action)
					&& Decision->TryGetNumberField(TEXT("target"), Target) && Decision->TryGetNumberField(
						TEXT("amount"), Amount)
					&& FMath::IsFinite(Target) && Target >= 0 && Target <= MAX_int32 && FMath::Fmod(Target, 1.) == 0 &&
					FMath::IsFinite(Amount) && Amount >= 0 && Amount <= 20;
				Say.TrimStartAndEndInline();
				Valid = Valid && !Say.IsEmpty() && Say.Len() <= 200;
				// A no-op has no action target. Accept a model naming the actual
				// conversation partner and canonicalize it to the speaker's offered
				// self no-op. Never normalize a resource transfer or another action.
				if (Valid && Action == TEXT("none") && Amount == 0 && Target == J.Speaker->AgentId)
					Target = J.Listener->AgentId;
				auto* W = AVerdantWorld::Find(Self);
				auto* T = Valid && W ? W->FindFolk(int32(Target)) : nullptr;
				const bool DecisionWellFormed = Valid;
				if (Valid)
					Valid = Offered.ContainsByPredicate([&](const FOption& O)
					{
						return O.Action == Action && O.Target == int32(Target) && T && T->LifeRevision == O.TargetLife
							&& T->IntentRevision == O.TargetIntent && (
								O.Action == TEXT("share")
									? Amount >= 0 && Amount <= .9
									: O.Maximum > 0
									? Amount >= .01 && Amount <= double(O.Maximum) + .00001
									: Amount == 0);
					});
				if (!Valid)
				{
					UE_LOG(LogTemp, Warning,
					       TEXT(
						       "VERDANT_AI_INVALID listener=%d thread=%lld finish=%s json=%d fields=%d action=%s target=%.0f amount=%.3f reply=%s"
					       ),
					       J.Listener->AgentId, J.ThreadId, *Finish, Decision.IsValid(),
					       Decision.IsValid() ? Decision->Values.Num() : 0,
					       *Action, Target, Amount, *Content.Left(1000));
					if (DecisionWellFormed)
						Self->Publish(J, Say, Action, int32(Target), float(Amount),
						              TEXT("这次打算超出了眼下能够做到或已经谈妥的事，没有付诸行动；不要虚构成功或具体原因。"));
					else Self->FinishJob(J,TEXT("没有形成清楚的回应，这次未采取新的行动。"));
					return;
				}
				Self->Publish(J, Say, Action, int32(Target), float(Amount));
			};
			Self->bBusy = true;
		});
	if (!Req->ProcessRequest())
	{
		Req->OnProcessRequestComplete().Unbind();
		CloudRequest.Reset();
		CompletedCloudReply = nullptr;
		CloudRetryAfter = GetWorld()->GetTimeSeconds() + 5.f;
		FinishJob(J,TEXT("无法发起 DeepSeek 请求，原行动继续。"));
	}
	else UE_LOG(LogTemp, Display, TEXT("VERDANT_AI_REQUEST audience=%s listener=%d thread=%lld message=%lld"),
	            J.bPlayerThread ? (J.bAddressed ? TEXT("direct") : TEXT("broadcast")) : TEXT("npc"), N->AgentId,
	            J.ThreadId, J.MessageId);
}

bool UVerdantDialogue::ReadingConversationWith(const AVerdantCharacter* N) const
{
	auto* W = AVerdantWorld::Find(this);
	auto* P = W ? W->FindFolk(0) : nullptr;
	auto* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	auto* H = PC ? Cast<AVerdantHUD>(PC->GetHUD()) : nullptr;
	return H && H->bConversationOpen && Selected.Get() == N;
}

void UVerdantDialogue::Publish(const FVerdantDialogueJob& J, const FString& Say, const FString& Action,
                               int32 TargetId, float Amount, const FString& Rejection)
{
	auto* N = J.Listener.Get();
	if (!IsCurrent(J))
	{
		FinishJob(J, TEXT("现场情况已经变化，这次没有采取新的行动。"));
		return;
	}
	const FString Key = FString::Printf(TEXT("%lld:%d"), J.MessageId, N->AgentId);
	if (ResolvedDecisions.Contains(Key))return;
	ResolvedDecisions.Add(Key);
	// Feasibility is immediate: never wait for a warning, movement or delivery here.
	CheckForReflection(J, Say, Action, TargetId, Amount, Rejection);
}

void UVerdantDialogue::CheckForReflection(const FVerdantDialogueJob& J, const FString& Draft,
                                         const FString& Action, int32 TargetId, float Amount,
                                         const FString& Failure)
{
	auto* W = AVerdantWorld::Find(this);
	auto* N = J.Listener.Get();
	if (!W || !DialogueActorAlive(N) || N->LifeRevision != J.ListenerLife)
	{
		FinishJob(J, TEXT("对方已无法继续这次交谈。"));
		return;
	}
	auto* T = W->FindFolk(TargetId);
	const FString TargetName = T ? T->DisplayName : TEXT("对方");
	FVerdantDialogueOutcome Outcome;
	Outcome.Job = J;
	Outcome.Draft = Draft;
	Outcome.Action = Action;
	Outcome.TargetId = TargetId;
	Outcome.Amount = Amount;
	FString Reason = Failure;
	Outcome.bCanExecute = Failure.IsEmpty() && CanApplyDecision(N, Action, TargetId, Amount, &Reason);
	if (!Outcome.bCanExecute)
		Outcome.Result = TEXT("目前不能接受并开始这件事；没有采取新的行动。原因：") +
			(Reason.IsEmpty() ? TEXT("眼下的条件不合适，不要编造具体原因。") : Reason);
	else if (Action == TEXT("none"))
	{
		Outcome.Result = TEXT("本次只交谈，不接受新计划。");
		if (auto* Leader = W->FindFolk(N->FollowId))
			Outcome.Result += (N->bProtectLeader ? TEXT("原本仍在跟随并保护") : TEXT("原本仍在跟随")) +
				Leader->DisplayName + TEXT("，这件既有安排不会改变。");
		else if (N->DonationPledge > .001f)
			Outcome.Result += FString::Printf(TEXT("原本尚有%.2f份养分需要送到母树。"), N->DonationPledge);
		else Outcome.Result += TEXT("继续原来的行动，不能说接受了新的同行或送粮安排。");
	}
	else if (Action == TEXT("give"))
		Outcome.Result = FString::Printf(TEXT("目前可以交给%s %.2f份食物；此刻还未交付，只能表达愿意或准备交给对方。"), *TargetName, Amount);
	else if (Action == TEXT("steal"))
		Outcome.Result = FString::Printf(TEXT("目前可以尝试从%s抢取%.2f份食物；此刻还没抢到，说完警告后才会尝试，对方仍可避开。"), *TargetName, Amount);
	else if (Action == TEXT("donate"))
		Outcome.Result = FString::Printf(TEXT("目前可以接受亲自运送%.2f份养分到母树的安排；此刻尚未建立新行程，也没有送达或扣粮。"), Amount);
	else if (Action == TEXT("follow") || Action == TEXT("protect"))
		Outcome.Result = FString::Printf(TEXT("目前可以接受%s%s的安排，说完后开始；不能说已经走到或完成保护，不编造期限、目的地或额外任务。"),
			Action == TEXT("protect") ? TEXT("跟随并保护") : TEXT("跟随"), *TargetName);
	else if (Action == TEXT("attack"))
		Outcome.Result = TEXT("目前可以接受攻击") + TargetName + TEXT("的打算；说完警告后再开始追击，尚未命中或击败。");
	else if (Action == TEXT("stop"))
		Outcome.Result = TEXT("目前可以结束原有跟随、送粮或敌对计划，之后恢复自主生活；不等于原地永远等待。");
	else if (Action == TEXT("truce"))
		Outcome.Result = TEXT("目前可以停止自己对") + TargetName + TEXT("的敌意；不能替对方或第三者承诺停战。");
	else if (Action == TEXT("cede"))
		Outcome.Result = TEXT("目前可以把自己果林的管理权交给") + TargetName + TEXT("，此刻尚未转交。");
	else if (Action == TEXT("leave"))
		Outcome.Result = TEXT("目前可以迁往另一片允许采集的果林；还未改变落脚点或抵达，不编造具体终点。");
	else if (Action == TEXT("close"))
		Outcome.Result = TEXT("目前可以关闭自己管理果林的公开采集，尚未改变现有安排。");
	else
		Outcome.Result = FString::Printf(TEXT("目前可以开放自己的果林并将采集分成设为%.1f%%，此刻尚未更改。"),
			Action == TEXT("share") ? Amount * 100.f : 0.f);
	Outcome.ListenerIntentAtCheck = N->IntentRevision;
	Outcome.TargetAtCheck = T;
	Outcome.TargetLifeAtCheck = T ? T->LifeRevision : 0;
	Outcome.ExpiresAt = GetWorld()->GetTimeSeconds() + 20.f;
	ThreadExpiry.FindOrAdd(J.ThreadId) = FMath::Max(ThreadExpiry.FindRef(J.ThreadId), Outcome.ExpiresAt);
	// Preflight is not a world event: do not store it as a completed deed in memory.
	UE_LOG(LogTemp, Display, TEXT("VERDANT_ACTION_PREFLIGHT message=%lld listener=%d action=%s target=%d amount=%.3f can_execute=%d"),
		J.MessageId, N->AgentId, *Action, TargetId, Amount, Outcome.bCanExecute);
	StartReflection(Outcome);
}

void UVerdantDialogue::PublishReflection(const FVerdantDialogueOutcome& Outcome, const FString& Say)
{
	const auto& J = Outcome.Job;
	if (!CanPublishReflection(Outcome))
	{
		FinishJob(J, TEXT("现场已经变化，这句回应未发布，也没有采取新的行动。"));
		return;
	}
	auto* W = AVerdantWorld::Find(this);
	auto* N = J.Listener.Get();
	auto* S = J.Speaker.Get();
	const FString Key = FString::Printf(TEXT("%lld:%d"), J.MessageId, N->AgentId);
	if (CommittedDecisions.Contains(Key))return;
	auto* T = W->FindFolk(Outcome.TargetId);
	FString Result = TEXT("仅交谈，未接受新的行动安排。");
	if (!Outcome.bCanExecute && CanApplyDecision(N, Outcome.Action, Outcome.TargetId, Outcome.Amount))
	{
		// Do not publish a stale refusal or silently turn it into an accepted action.
		FinishJob(J, TEXT("刚才判断的条件已经变化，本次没有发布旧回应，也没有开始新的行动。"));
		return;
	}
	if (Outcome.bCanExecute && Outcome.Action != TEXT("none"))
	{
		FString Reason;
		// Recheck at commit, after the network wait, but never wait for physical completion.
		if (!T || T != Outcome.TargetAtCheck.Get() || T->LifeRevision != Outcome.TargetLifeAtCheck ||
			!CanApplyDecision(N, Outcome.Action, Outcome.TargetId, Outcome.Amount, &Reason))
		{
			FinishJob(J, TEXT("条件刚刚变化，这次没有开始新的行动。") + Reason);
			return;
		}
		if (Outcome.Action == TEXT("attack") || Outcome.Action == TEXT("steal"))
		{
			FVerdantWarnedAction D;
			D.Actor = N;
			D.Target = T;
			D.Action = Outcome.Action;
			D.Amount = Outcome.Amount;
			// Start the warning AFTER the final reply is ready, not before requesting it.
			D.ExecuteAt = GetWorld()->GetTimeSeconds() + AVerdantCharacter::DialogueReplyDuration;
			D.ExpiresAt = D.ExecuteAt + 10.f;
			++N->IntentRevision;
			D.ActorLife = N->LifeRevision;
			D.ActorIntent = N->IntentRevision;
			D.TargetLife = T->LifeRevision;
			D.TargetIntent = T->IntentRevision;
			WarnedActions.Add(D);
			Result = TEXT("已发出警告，话说完后才尝试行动；尚未命中或取得食物。");
		}
		else
		{
			const float BeforeFood = T->Food;
			if (!ApplyDecision(N, Outcome.Action, Outcome.TargetId, Outcome.Amount))
			{
				FinishJob(J, TEXT("条件刚刚变化，这次没有开始新的行动。") + LastDecisionReason);
				return;
			}
			if (Outcome.Action == TEXT("give"))
				Result = FString::Printf(TEXT("实际交给%s %.2f份食物。"), *T->DisplayName, T->Food - BeforeFood);
			else if (Outcome.Action == TEXT("follow") || Outcome.Action == TEXT("protect") ||
				Outcome.Action == TEXT("donate") || Outcome.Action == TEXT("leave"))
				Result = TEXT("新行程已建立，回答结束后继续行动；尚未完成旅程、保护或供奉。");
			else Result = TEXT("已按约定改变自身或自己果林的安排。");
		}
	}
	CommittedDecisions.Add(Key);
	N->BeginDialogueReply(Say, S);
	++ThreadLines.FindOrAdd(J.ThreadId);
	const FString Line = N->DisplayName + TEXT("：") + Say;
	RememberExchange(N->AgentId, J.bInitiative ? TEXT("我因现场情况主动开口：") + J.Text : S->DisplayName + TEXT("说：") + J.Text,
		Line + TEXT(" [实际结果：") + Result + TEXT("]"));
	if (CanExchange(N, W->FindFolk(0)))
	{
		AppendNPCLine(N->AgentId, Line);
		SetNPCStatus(N->AgentId, FString());
		W->Record(S->AgentId == 0 ? Line : N->DisplayName + TEXT(" 对") + S->DisplayName + TEXT("：") + Say);
	}
	UE_LOG(LogTemp, Display, TEXT("VERDANT_NPC_REPLY listener=%d thread=%lld action=%s stage=preflight_reflected can_execute=%d duration=%.1f"),
		N->AgentId, J.ThreadId, *Outcome.Action, Outcome.bCanExecute, AVerdantCharacter::DialogueReplyDuration);
	FinishJob(J);
	if (!J.bPlayerThread && ThreadLines.FindRef(J.ThreadId) < 2)EnqueueSpeech(N, Say, J.ThreadId, false, S);
}

void UVerdantDialogue::TickWarnedActions()
{
	// Warning completion never launches a model request and does not block other conversations.
	auto* W = AVerdantWorld::Find(this);
	if (!W)return;
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 I = WarnedActions.Num() - 1; I >= 0; --I)
	{
		const auto D = WarnedActions[I];
		auto* N = D.Actor.Get();
		auto* T = D.Target.Get();
		FString Failure;
		if (!DialogueActorAlive(N) || !DialogueActorAlive(T) || N->LifeRevision != D.ActorLife || T->LifeRevision != D.TargetLife)
			Failure = TEXT("有人已经无法继续，原先的行动取消。");
		else if (Now > D.ExpiresAt)Failure = TEXT("时机已过，没有再动手。");
		else if (N->IntentRevision != D.ActorIntent || T->IntentRevision != D.TargetIntent)
			Failure = TEXT("双方处境或打算已变化，原先的行动取消。");
		if (Failure.IsEmpty() && (Now < D.ExecuteAt || N->IsDialogueReplyActive() || ReadingConversationWith(N)))continue;
		WarnedActions.RemoveAt(I);
		const float FoodBefore = DialogueActorAlive(N) ? N->Food : 0.f;
		const bool Applied = Failure.IsEmpty() && ApplyDecision(N, D.Action, T->AgentId, D.Amount);
		if (!Applied && Failure.IsEmpty())
			Failure = LastDecisionReason.IsEmpty() ? TEXT("眼下已无法动手，这次没有成功。") : LastDecisionReason;
		const FString Result = !Applied ? Failure : D.Action == TEXT("steal")
			? FString::Printf(TEXT("实际取得%.2f份食物。"), N->Food - FoodBefore)
			: TEXT("已开始追击，尚未保证命中或击败。");
		if (IsValid(N))
		{
			RememberExchange(N->AgentId, TEXT("先前警告后的实际进展："), Result);
			SetNPCStatus(N->AgentId, Result);
			W->Record(N->DisplayName + TEXT("：") + Result);
			if (!Applied && CanExchange(N, W->FindFolk(0)))
				W->Notify(TEXT("行动变化（") + N->DisplayName + TEXT("）：") + Result);
		}
		UE_LOG(LogTemp, Display, TEXT("VERDANT_WARNED_ACTION action=%s applied=%d result=%s"), *D.Action, Applied, *Result);
	}
}

void UVerdantDialogue::ConsiderInitiative()
{
	auto* W = AVerdantWorld::Find(this);
	auto* P = W ? W->FindFolk(0) : nullptr;
	if (!DialogueActorAlive(P) || !GetWorld())return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextProactiveAt || Now < CloudRetryAfter)return;
	// One global opportunity, not one request per NPC. Busy ticks never accumulate a backlog.
	NextProactiveAt = Now + 10.f;
	auto* Pop = UVerdantPopulation::Find(this);
	const int32 Count = W->Folk.Num();
	if (Count == 0)return;
	const auto AvailableNPC = [this](AVerdantCharacter* N)
	{
		return DialogueActorAlive(N) && N->AgentId != 0 && !PendingNPCs.Contains(N->AgentId)
			&& !N->IsDialogueReplyActive() && !HasWarnedActionFor(N) && N->AttackTimer <= 0 && N->HurtTimer <= 0
			&& N->HostileId < 0 && !ReadingConversationWith(N);
	};
	const auto FindRecipient = [&](AVerdantCharacter* N, bool ToPlayer, bool RequireAudible) -> AVerdantCharacter*
	{
		if (ToPlayer)return CanExchange(N, P) ? P : nullptr;
		AVerdantCharacter* Best = nullptr;
		double BestScore = TNumericLimits<double>::Max();
		for (AVerdantCharacter* T : W->Folk)
		{
			if (T == N || !AvailableNPC(T) || !CanExchange(N, T))continue;
			if (RequireAudible && !CanHear(T, P))continue;
			const bool Related = Pop && Pop->Relations.ContainsByPredicate([N,T](const FVerdantInstanceRelation& R)
			{
				return (R.A == N->AgentId && R.B == T->AgentId) || (R.B == N->AgentId && R.A == T->AgentId);
			});
			// Every authored relationship can motivate a conversation; strangers may greet too.
			const double Score = FVector::DistSquared(N->GetActorLocation(), T->GetActorLocation())
				+ (Related ? 0.0 : 100000000.0);
			if (Score < BestScore)
			{
				Best = T;
				BestScore = Score;
			}
		}
		return Best;
	};
	// Alternate the preferred audience and rotate initiators. Audible candidates get
	// both audience passes before any off-screen-area fallback spends the opportunity.
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		const bool ToPlayer = Pass % 2 == 0 ? bPreferPlayerInitiative : !bPreferPlayerInitiative;
		for (int32 Offset = 0; Offset < Count; ++Offset)
		{
			const int32 Index = (NextInitiatorIndex + Offset) % Count;
			AVerdantCharacter* N = W->Folk[Index];
			if (!AvailableNPC(N) || FVector::DistSquared(P->GetActorLocation(), N->GetActorLocation()) >
				FMath::Square(5000.f))
				continue;
			if (Pass < 2 && !CanHear(N, P))continue;
			AVerdantCharacter* Recipient = FindRecipient(N, ToPlayer, Pass < 2);
			if (!Recipient)continue;
			const FString Pair = FString::Printf(TEXT("%d:%d"), N->AgentId, Recipient->AgentId);
			const FString Event = Pop ? Pop->RequestSummary(N) : FString();
			const FString Situation = FString::Printf(
				TEXT("tier=%d;low=%d;hurt=%d;trust=%d;delivery=%d;event=%s"), Recipient->FoodTier(),
				N->Food < W->NegotiationReserve(N), Recipient->Health < Recipient->MaxHealth() * .5f,
				FMath::FloorToInt(N->Trust.FindRef(Recipient->AgentId) / 10.f), N->DonationPledge > 0, *Event);
			const bool First = !LastSituation.Contains(Pair);
			const bool Changed = !First && LastSituation.FindRef(Pair) != Situation;
			FString Trigger = First
				                  ? TEXT("你第一次主动与附近这个对象交谈，可以按自身性格打招呼、了解对方或提醒当前处境。")
				                  : Changed
				                  ? TEXT("你观察到附近对象或自身的体型、健康、食物、信任或共同请求与上次不同；只谈给定真实情况。")
				                  : TEXT(
					                  "你与对方仍在附近。即使没有新事件，也可以根据自己的性格、当前行动、已知关系或真实需求，自然闲谈、关心、询问或提出请求。不重复上一句话，不虚构情况发生变化，不重复索要已完成的承诺。");
			if (N->Food < W->NegotiationReserve(N))Trigger += TEXT("你当前口粮不足，可向对方商量，不凭空生成粮食。");
			if (!Event.IsEmpty())Trigger += Event;
			FVerdantDialogueJob J;
			J.MessageId = ++NextMessageId;
			J.ThreadId = J.MessageId;
			J.Listener = N;
			J.Speaker = Recipient;
			J.Text = Trigger;
			J.ExpiresAt = Now + 20.f;
			J.ListenerLife = N->LifeRevision;
			J.ListenerIntent = N->IntentRevision;
			J.SpeakerLife = Recipient->LifeRevision;
			J.bInitiative = true;
			J.bAddressed = true;
			J.bPlayerThread = false;
			ThreadLines.Add(J.ThreadId, 0);
			ThreadExpiry.Add(J.ThreadId, Now + 40.f);
			HearingQueue.Add(J);
			PendingNPCs.Add(N->AgentId);
			LastSituation.Add(Pair, Situation);
			NextInitiatorIndex = (Index + 1) % Count;
			bPreferPlayerInitiative = Recipient->AgentId != 0;
			UE_LOG(LogTemp, Display,
			       TEXT("VERDANT_INITIATIVE speaker=%d recipient=%d kind=%s reason=%s at=%.1f next=%.1f"),
			       N->AgentId, Recipient->AgentId, Recipient->AgentId == 0 ? TEXT("player") : TEXT("npc"),
			       First ? TEXT("first") : Changed ? TEXT("changed") : TEXT("routine"), Now, NextProactiveAt);
			return;
		}
	}
	UE_LOG(LogTemp, Display,
	       TEXT("VERDANT_INITIATIVE_SKIP reason=no_eligible_pair at=%.1f radius_cm=%.0f max_height_ratio=%.1f"),
	       Now, DialogueRadiusCm, MaximumHeightRatio);
}

void UVerdantDialogue::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	if (!GetWorld())return;
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("VERDANT_DISABLE_AI")) == TEXT("1"))
	{
		HearingQueue.Reset();
		WarnedActions.Reset();
		PendingNPCs.Reset();
		CompletedCloudReply = nullptr;
		RequestNPC.Reset();
		bBusy = false;
		if (CloudRequest)
		{
			CloudRequest->OnProcessRequestComplete().Unbind();
			CloudRequest->CancelRequest();
			CloudRequest.Reset();
		}
		return;
	}
	if (GetWorld()->IsPaused())return;
	if (CompletedCloudReply)
	{
		const auto Completion = MoveTemp(CompletedCloudReply);
		CompletedCloudReply = nullptr;
		Completion();
	}
	TickWarnedActions();
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 I = HearingQueue.Num() - 1; I >= 0; --I)
		if (!IsCurrent(HearingQueue[I]))
		{
			FinishJob(HearingQueue[I], TEXT("对话已过期或现场条件变化，未发送。"));
			HearingQueue.RemoveAt(I);
		}
	for (auto It = ThreadExpiry.CreateIterator(); It; ++It)
		if (It.Value() < Now)
		{
			ThreadLines.Remove(It.Key());
			It.RemoveCurrent();
		}
	if (HeardPairs.Num() > 512)HeardPairs.Reset();
	if (!CloudRequest && !CompletedCloudReply && Now >= CloudRetryAfter)
	{
		if (!HearingQueue.IsEmpty())
		{
			int32 Index = 0;
			for (int32 I = 0; I < HearingQueue.Num(); ++I)
				if (HearingQueue[I].bPlayerThread)
				{
					Index = I;
					break;
				}
			const auto J = HearingQueue[Index];
			HearingQueue.RemoveAt(Index);
			StartCloud(J);
		}
		else ConsiderInitiative();
	}
	bBusy = CloudRequest.IsValid() || static_cast<bool>(CompletedCloudReply) || !HearingQueue.IsEmpty();
}
