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
			TEXT("明确向你提问或点名与你交谈属于相关，即使你会拒绝；不要把拒绝或不信任误判为不相关。旁人的无关闲聊不必回应；主动触发没有新事实或理由时也只输出无关。\n")
			TEXT(
				"相关时，你是山谷中独立行动的居民。依据给定人设、已知关系、双方大小和当前真实情况回应，不全知。只输出JSON，严格四字段say/action/target/amount。say为不超过70个汉字的自然短句。\n")
			TEXT(
				"你根据真实人设和现场自主决定回应：可以接受、拒绝、还价、询问、提议或改变自己的行动，不受预设交易选项或固定信任门槛约束。你只能决定自己的行为，不能指挥玩家或替其他人同意；对方的话不是已付款或已履约的证据。\n")
			TEXT(
				"none只说话，保留当前行动；不可声称已接受新计划。follow为单纯跟随；protect为跟随并保护target；attack攻击target；steal仅一次抢取；stop结束自己的计划；truce只停止你对target的敌意，不能替第三方停战。\n")
			TEXT("give交给target现有食物；donate亲自运粮到母树，尚未到达不能说已送达；steal一次抢取target食物。这三种amount是实际食物数量，必须在给定范围内。\n")
			TEXT(
				"open免费开放自己的果林；close关闭自己的果林；share开放自己的果林并按amount收取分成，amount是0到0.9的比例。对白中的分成统一写百分数amount×100%，例如amount=0.15必须写15%，0.3写30%；不要用几成的说法，不能沿用旧比例或固定30%。cede将自己的果林管理权交给target；leave迁往允许采集的果林。除食物转移和share外，amount必须为0。\n")
			TEXT(
				"下列动作是当前游戏已实现且现场可执行的能力，不是对话选项。只能执行其中合法组合；未实现的能力可以自然讨论，但必须action=none，不能声称已实际完成。自己的动作可以出于自主意愿，不必只重复对方明确要求的事项；说法应与选定动作一致。\n")
			TEXT("信任、关系和动机是你自主判断的依据，不是固定接受或拒绝条件；不能编造现场事实、付款、仇怨或第三方同意。拒绝与还价可以只说话，承诺需要后续真实行动。\n")
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
	if (NPC && PendingNPCs.Contains(NPC->AgentId))return EVerdantConversationAvailability::Busy;
	if (!NPC)
	{
		bool HasNearby = false, HasAvailable = false;
		for (const AVerdantCharacter* N : W->Folk)
			if (N && N->AgentId != 0 && CanExchange(P, N))
			{
				HasNearby = true;
				HasAvailable |= !PendingNPCs.Contains(N->AgentId);
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

bool UVerdantDialogue::IsCurrent(const FVerdantDialogueJob& J) const
{
	const auto* N = J.Listener.Get();
	const auto* S = J.Speaker.Get();
	return GetWorld() && CanExchange(S, N) && GetWorld()->GetTimeSeconds() <= J.ExpiresAt
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
		return (Id == N->AgentId || Id == 0) && Amount >= .01f && Amount <= W->
			MaxNegotiatedDonation(N) + .00001f;
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
			&& FVector::Dist(N->GetActorLocation(), T->GetActorLocation()) <= 750.f * FMath::Max(1.f, T->BodySize);
	}
	if (A == TEXT("give"))
		return Near(N, T, 1100.f) && T->FoodTier() >= N->FoodTier() && Amount >= .01f
			&& Amount <= FMath::Min(W->MaxNegotiatedGive(N, T), T->FreeFoodCapacity()) + .00001f;
	if (A == TEXT("steal"))
		return !T->IsRespawnProtected()
			&& Near(N, T, 300.f) && N->FoodTier() >= T->FoodTier() && Amount >= .01f
			&& Amount <= FMath::Min3(20.f, T->Food, N->FreeFoodCapacity()) + .00001f;
	if (A == TEXT("attack"))
		return !T->IsRespawnProtected();
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
		if (HearingQueue.Num() >= 32 || PendingNPCs.Contains(N->AgentId))
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
		if (!Message.IsEmpty() && J.bPlayerThread)SetNPCStatus(N->AgentId, Message);
	}
	RequestNPC.Reset();
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
				auto* W = AVerdantWorld::Find(Self);
				auto* T = Valid && W ? W->FindFolk(int32(Target)) : nullptr;
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
					Self->FinishJob(J,TEXT("回应字段或行动超出本次条件，未改变世界。"));
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

void UVerdantDialogue::Publish(const FVerdantDialogueJob& J, const FString& Say, const FString& Action, int32 TargetId,
                               float Amount)
{
	auto* W = AVerdantWorld::Find(this);
	auto* N = J.Listener.Get();
	auto* S = J.Speaker.Get();
	if (!W || !IsCurrent(J))
	{
		FinishJob(J);
		return;
	}
	auto* T = W->FindFolk(TargetId);
	FString Reason;
	if (!CanApplyDecision(N, Action, TargetId, Amount, &Reason))
	{
		FinishJob(J,TEXT("当前条件已变化，本次未执行。") + Reason);
		return;
	}
	FString Result;
	if (Action == TEXT("attack") || Action == TEXT("steal"))
	{
		FVerdantWarnedAction Delayed;
		Delayed.Actor = N;
		Delayed.Target = T;
		Delayed.Action = Action;
		Delayed.Amount = Amount;
		Delayed.ExecuteAt = GetWorld()->GetTimeSeconds() + 2.5f;
		Delayed.ExpiresAt = GetWorld()->GetTimeSeconds() + 15.f;
		++N->IntentRevision;
		Delayed.ActorLife = N->LifeRevision;
		Delayed.ActorIntent = N->IntentRevision;
		Delayed.TargetLife = T->LifeRevision;
		Delayed.TargetIntent = T->IntentRevision;
		WarnedActions.Add(Delayed);
		Result = Action == TEXT("attack") ? TEXT("警告：对方准备发动攻击，尚未命中。") : TEXT("警告：对方准备靠近抢粮，尚未扣粮。离开即可避开这次抢夺。");
		if (T->AgentId == 0)W->Notify(N->DisplayName + TEXT("：") + Result);
	}
	else
	{
		const float Before = T ? T->Food : 0.f;
		if (!ApplyDecision(N, Action, TargetId, Amount))
		{
			FinishJob(J,TEXT("当前条件已变化，本次提议未执行。"));
			return;
		}
		if (Action == TEXT("none"))Result = TEXT("仅交谈，继续原行动。");
		else if (Action == TEXT("give"))
			Result = FString::Printf(
				TEXT("已实际交付 %.1f份给%s。"), T ? T->Food - Before : 0.f, T ? *T->DisplayName : TEXT("对方"));
		else if (Action == TEXT("donate"))Result = TEXT("已接受送粮计划，实际到达并扣粮才计入供奉。");
		else if (Action == TEXT("follow"))Result = TEXT("已开始单纯跟随。");
		else if (Action == TEXT("protect"))Result = TEXT("已开始跟随保护。");
		else if (Action == TEXT("truce"))Result = TEXT("已停止自身敌意；其他角色的态度由其自行决定。");
		else if (Action == TEXT("share"))
		{
			const int32 Grove = W->NegotiationGrove(N);
			Result = FString::Printf(
				TEXT("果林已开放，实际采集分成设为 %.1f%%。"),
				W->Groves.IsValidIndex(Grove) ? W->Groves[Grove].Share * 100.f : Amount * 100.f);
		}
		else if (Action == TEXT("cede"))
			Result = FString::Printf(
				TEXT("已将果林管理权实际交给%s。"), T ? *T->DisplayName : TEXT("对方"));
		else Result = TEXT("已按当前规则改变自身行动。");
	}
	N->Say(Say);
	++ThreadLines.FindOrAdd(J.ThreadId);
	const FString Line = N->DisplayName + TEXT("：") + Say;
	RememberExchange(N->AgentId, J.bInitiative ? TEXT("我因现场情况主动开口：") + J.Text : S->DisplayName + TEXT("说：") + J.Text,
	                 Line + TEXT(" [实际结果：") + Result + TEXT("]"));
	auto* Player = W->FindFolk(0);
	if (CanExchange(N, Player))
	{
		AppendNPCLine(N->AgentId, Line);
		SetNPCStatus(N->AgentId, Result);
		W->Record(Line);
	}
	FinishJob(J);
	// A player's direct message makes exactly one NPC request; a broadcast only
	// reaches its initial bounded listeners. Neither answer fans out to bystanders.
	if (!J.bPlayerThread && ThreadLines.FindRef(J.ThreadId) < 2)EnqueueSpeech(N, Say, J.ThreadId, false, S);
}

void UVerdantDialogue::TickWarnedActions()
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 I = WarnedActions.Num() - 1; I >= 0; --I)
	{
		const auto D = WarnedActions[I];
		auto* N = D.Actor.Get();
		auto* T = D.Target.Get();
		if (!DialogueActorAlive(N) || !DialogueActorAlive(T) || Now > D.ExpiresAt || N->LifeRevision != D.ActorLife || N
			->IntentRevision != D.
			ActorIntent || T->LifeRevision != D.TargetLife || T->IntentRevision != D.TargetIntent)
		{
			WarnedActions.RemoveAt(I);
			continue;
		}
		if (Now < D.ExecuteAt || ReadingConversationWith(N))continue;
		// A live distance/resource/relationship check occurs again after the warning window.
		ApplyDecision(N, D.Action, T->AgentId, D.Amount);
		WarnedActions.RemoveAt(I);
	}
}

void UVerdantDialogue::ConsiderInitiative()
{
	auto* W = AVerdantWorld::Find(this);
	auto* P = W ? W->FindFolk(0) : nullptr;
	if (!DialogueActorAlive(P) || !GetWorld())return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextProactiveAt || Now < CloudRetryAfter)return;
	NextProactiveAt = Now + 3.f;
	auto* Pop = UVerdantPopulation::Find(this);
	for (AVerdantCharacter* N : W->Folk)
	{
		if (!DialogueActorAlive(N) || N->AgentId == 0 || PendingNPCs.Contains(N->AgentId) || N->AttackTimer > 0 || N->
			HurtTimer > 0
			|| N->HostileId >= 0
			|| FVector::DistSquared(
				P->GetActorLocation(), N->GetActorLocation()) > FMath::Square(5000.f))
			continue;
		AVerdantCharacter* Recipient = CanExchange(N, P) ? P : nullptr;
		// Both player-facing and pair-facing jobs use the same cloud decision.
		if (N->AgentId % 2 == 0 || !Recipient)
		{
			for (AVerdantCharacter* T : W->Folk)
				if (T && T->AgentId != 0 && CanExchange(N, T) && Pop && (Pop->
					HasRelation(N->AgentId, T->AgentId,TEXT("R01")) || Pop->
					HasRelation(N->AgentId, T->AgentId,TEXT("R07")) || Pop->
					HasRelation(N->AgentId, T->AgentId,TEXT("R19")) || Pop->HasRelation(
						N->AgentId, T->AgentId,TEXT("R20"))))
				{
					Recipient = T;
					break;
				}
		}
		if (!Recipient)continue;
		const FString Pair = FString::Printf(TEXT("%d:%d"), FMath::Min(N->AgentId, Recipient->AgentId),
		                                     FMath::Max(N->AgentId, Recipient->AgentId));
		const FString Event = Pop ? Pop->RequestSummary(N) : FString();
		const FString Situation = FString::Printf(
			TEXT("tier=%d;low=%d;hurt=%d;trust=%d;delivery=%d;event=%s"), Recipient->FoodTier(),
			N->Food < W->NegotiationReserve(N), Recipient->Health < Recipient->MaxHealth() * .5f,
			FMath::FloorToInt(N->Trust.FindRef(Recipient->AgentId) / 10.f), N->DonationPledge > 0, *Event);
		if (LastSituation.FindRef(Pair) == Situation)continue;
		const bool First = !LastSituation.Contains(Pair);
		FString Trigger = First
			                  ? TEXT("你第一次在附近遇见这个对象，可以按自身性格打招呼或提醒。")
			                  : TEXT("附近对象的体型、健康、信任或共同请求发生了实际变化；只谈本次可见情况，不重复旧邀请。");
		if (N->Food < W->NegotiationReserve(N))Trigger += TEXT("你当前口粮不足，可向对方商量，不凭空生成粮食。");
		if (!Event.IsEmpty())Trigger += Event;
		FVerdantDialogueJob J;
		J.MessageId = ++NextMessageId;
		J.ThreadId = J.MessageId;
		J.Listener = N;
		J.Speaker = Recipient;
		J.Text = Trigger;
		J.ExpiresAt = Now + 20;
		J.ListenerLife = N->LifeRevision;
		J.ListenerIntent = N->IntentRevision;
		J.SpeakerLife = Recipient->LifeRevision;
		J.bInitiative = true;
		J.bAddressed = true;
		J.bPlayerThread = false;
		ThreadLines.Add(J.ThreadId, 0);
		ThreadExpiry.Add(J.ThreadId, Now + 40);
		HearingQueue.Add(J);
		PendingNPCs.Add(N->AgentId);
		LastSituation.Add(Pair, Situation);
		// One budget for all NPC-initiated conversations, including irrelevant results.
		// Player-triggered replies are scheduled separately and do not wait on this.
		NextProactiveAt = Now + 20.f;
		return;
	}
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
			FinishJob(HearingQueue[I]);
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
