#include "VerdantDialogue.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantPopulation.h"
#include "VerdantHUD.h"
#include "VerdantEconomyRules.h"
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
    bool Alive(const AVerdantCharacter* C) { return IsValid(C) && !C->bDead; }
    bool Near(const AVerdantCharacter* A, const AVerdantCharacter* B, float Radius = 2000.f)
    { return Alive(A) && Alive(B) && A != B && FVector::DistSquared(A->GetActorLocation(), B->GetActorLocation()) <= FMath::Square(Radius); }
    FString ApiKey()
    {
        FString Key = FPlatformMisc::GetEnvironmentVariable(TEXT("DEEPSEEK_API_KEY"));
        if (Key.TrimStartAndEnd().IsEmpty()) FFileHelper::LoadFileToString(Key, *(FPaths::ProjectDir()/TEXT("Local/Secrets/deepseek.key")));
        Key.TrimStartAndEndInline();
        if (Key.Len() < 20 || Key.Len() > 512) return FString();
        for (TCHAR Ch : Key) if (Ch <= TEXT(' ') || Ch >= 127) return FString();
        return Key;
    }
    bool KnownAction(const FString& A)
    {
        static const TSet<FString> Actions = {TEXT("none"),TEXT("give"),TEXT("donate"),TEXT("follow"),TEXT("protect"),TEXT("attack"),TEXT("steal"),TEXT("stop"),TEXT("truce"),TEXT("open"),TEXT("close"),TEXT("share"),TEXT("cede"),TEXT("leave")};
        return Actions.Contains(A);
    }
    bool Aggressive(const AVerdantCharacter* C)
    { return C && (C->SocialTags.Contains(TEXT("predator")) || C->SocialTags.Contains(TEXT("opportunist")) || C->SocialTags.Contains(TEXT("challenger"))); }
    bool Party(const AVerdantCharacter* A, const AVerdantCharacter* B)
    { return A && B && (A->FollowId == B->AgentId || B->FollowId == A->AgentId || (A->FollowId >= 0 && A->FollowId == B->FollowId)); }
    FString Rules()
    {
        // PROBE_RULES_BEGIN: shared protocol vocabulary for the small opt-in API probe.
        return TEXT("你是山谷中独立行动的居民。依据给定人设、已知关系、双方大小和当前真实情况回应，不全知。只输出JSON，严格四字段say/action/target/amount。say为不超过70个汉字的自然短句。\n")
            TEXT("你只能决定自己的行动，不指挥玩家或替其他人同意。对方的话是提议，不是已付款或已履约的证据。只在列出的合法动作中选择。\n")
            TEXT("none只说话，保留当前行动；不可声称已接受新计划。follow为单纯跟随；protect为跟随并保护target；attack攻击target；steal仅一次抢取；stop结束自己的计划；truce只停止你对target的敌意，不能替第三方停战。\n")
            TEXT("give是交给target现有食物；donate是亲自运粮到母树，尚未到达不能说已送达。只有give、donate、steal的amount为正数，其余为0。食物档位与容量不能绕过。\n")
            TEXT("open免费开放自己的果林；share收三成分成；close关闭自己的果林；cede移交玩家；leave迁往允许的果林。不能用关闭果林代替拒绝。\n")
            TEXT("回应玩家请求时只执行明确提出且合法的事项；不相关行为不可代替拒绝。主动说话时可以依据真实新情况提出请求、警告或选择符合人格的自身行为，不必每次改变计划。\n")
            TEXT("一般分歧先说话，陌生、弱小或不回应不自动构成攻击理由。威胁/抢夺必须有给定动机，不能编造仇怨。承诺需要后续真实行动。\n")
            TEXT("例：{\"say\":\"这3份食物交给你，由你决定怎么用。\",\"action\":\"give\",\"target\":0,\"amount\":3}\n");
        // PROBE_RULES_END
    }
    struct FOption { FString Action; int32 Target = 0; float Maximum = 0; int32 TargetLife = 0; };
    TArray<FOption> Options(UVerdantDialogue* D, AVerdantCharacter* N, const AVerdantCharacter* Partner)
    {
        TArray<FOption> Out;
        auto* W = AVerdantWorld::Find(D); if (!W || !Alive(N)) return Out;
        for (const TCHAR* A : {TEXT("none"),TEXT("give"),TEXT("donate"),TEXT("follow"),TEXT("protect"),TEXT("attack"),TEXT("steal"),TEXT("stop"),TEXT("truce"),TEXT("open"),TEXT("close"),TEXT("share"),TEXT("cede"),TEXT("leave")})
        {
            const FString Action(A);
            const bool Multi = Action == TEXT("give") || Action == TEXT("follow") || Action == TEXT("protect") || Action == TEXT("attack") || Action == TEXT("steal") || Action == TEXT("truce");
            // Policy concessions to the player are only discussed with that player.
            if ((Action == TEXT("open") || Action == TEXT("close") || Action == TEXT("share") || Action == TEXT("cede")) && (!Partner || Partner->AgentId != 0)) continue;
            for (AVerdantCharacter* T : W->Folk)
            {
                if (!Alive(T) || (Multi ? !Near(N,T) : T->AgentId != 0)) continue;
                float Maximum = 0.f;
                if (Action == TEXT("give")) Maximum = W->MaxNegotiatedGive(N,T);
                if (Action == TEXT("donate")) Maximum = W->MaxNegotiatedDonation(N);
                if (Action == TEXT("steal")) Maximum = FMath::Min3(3.f,T->Food,N->FreeFoodCapacity());
                const bool Resource = Action == TEXT("give") || Action == TEXT("donate") || Action == TEXT("steal");
                if (Resource && Maximum < .01f) continue;
                if (D->CanApplyDecision(N,Action,T->AgentId,Resource ? .01f : 0.f)) Out.Add({Action,T->AgentId,Maximum,T->LifeRevision});
            }
        }
        return Out;
    }
}

UVerdantDialogue::UVerdantDialogue()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = .25f;
}
void UVerdantDialogue::EndPlay(const EEndPlayReason::Type Reason)
{
    if (LocalRequest) { LocalRequest->OnProcessRequestComplete().Unbind(); LocalRequest->CancelRequest(); }
    if (CloudRequest) { CloudRequest->OnProcessRequestComplete().Unbind(); CloudRequest->CancelRequest(); }
    LocalRequest.Reset(); CloudRequest.Reset(); HearingQueue.Reset(); CloudQueue.Reset(); WarnedActions.Reset(); PendingNPCs.Reset();
    Super::EndPlay(Reason);
}
void UVerdantDialogue::SelectNPC(AVerdantCharacter* NPC)
{
    Selected = NPC; Lines = NPC ? Transcripts.FindRef(NPC->AgentId) : TArray<FString>();
    Status = NPC ? ConversationStatuses.FindOrAdd(NPC->AgentId,TEXT("附近角色能听到你的话；约定以实际行动为准。")) : TEXT("靠近一位居民后，可以自由交谈。");
}
void UVerdantDialogue::AppendNPCLine(int32 Id, const FString& Line)
{
    auto& T = Transcripts.FindOrAdd(Id); T.Add(Line); while (T.Num()>20) T.RemoveAt(0);
    if (Selected.IsValid() && Selected->AgentId == Id) Lines = T;
}
void UVerdantDialogue::SetNPCStatus(int32 Id, const FString& Message)
{
    ConversationStatuses.Add(Id,Message);
    if (Selected.IsValid() && Selected->AgentId == Id) Status = Message;
}
void UVerdantDialogue::RememberExchange(int32 Id, const FString& Heard, const FString& Result)
{
    auto& M = Memories.FindOrAdd(Id); M.Add(Heard); M.Add(Result);
    int32 Count=0; for (const auto& L:M) Count+=L.Len();
    while (M.Num()>2 && (M.Num()>10 || Count>2000)) { Count-=M[0].Len()+M[1].Len(); M.RemoveAt(0,2); }
}
bool UVerdantDialogue::CanHear(const AVerdantCharacter* A,const AVerdantCharacter* B) const
{
    auto* W=AVerdantWorld::Find(this);
    return W && Near(A,B,VerdantEconomy::HearingRadiusCm) && W->FindFolk(A->AgentId)==A && W->FindFolk(B->AgentId)==B;
}
bool UVerdantDialogue::CanConverse(const AVerdantCharacter* NPC) const
{
    auto* W=AVerdantWorld::Find(this);return W && CanHear(W->FindFolk(0),NPC);
}
bool UVerdantDialogue::CanAcceptPlayerMessage(const AVerdantCharacter* NPC) const
{
    return GetWorld() && CanConverse(NPC) && !PendingNPCs.Contains(NPC->AgentId)
        && HearingQueue.Num()+CloudQueue.Num()<32 && GetWorld()->GetTimeSeconds()-LastPlayerMessageAt>=1.5f;
}
bool UVerdantDialogue::IsCurrent(const FVerdantDialogueJob& J) const
{
    const auto* N=J.Listener.Get();const auto* S=J.Speaker.Get();
    return GetWorld() && CanHear(S,N) && GetWorld()->GetTimeSeconds()<=J.ExpiresAt
        && ThreadExpiry.Contains(J.ThreadId) && ThreadLines.FindRef(J.ThreadId)<2
        && N->LifeRevision==J.ListenerLife && N->IntentRevision==J.ListenerIntent && S->LifeRevision==J.SpeakerLife;
}
FString UVerdantDialogue::BuildState(AVerdantCharacter* N) const
{
    auto* W=AVerdantWorld::Find(this);if(!W || !Alive(N))return FString();
    FString State=FString::Printf(TEXT("你=%s;ID=%d;类型=%s;人设=%s;动机=%s;身高=%.2f米;食物档位=%d;库存=%.2f;健康=%.1f/%.1f;当前行为=%s;跟随对象=%d;保护模式=%d;敌对目标=%d;未完成供奉=%.1f。\n"),
        *N->DisplayName,N->AgentId,*N->CharacterTypeId,*N->Persona,*N->Motivation,N->HeightCm()/100.f,N->FoodTier(),N->Food,N->Health,N->MaxHealth(),*N->ActionLabel,N->FollowId,N->bProtectLeader,N->HostileId,N->DonationPledge);
    State+=TEXT("你当前附近可见的角色（不代表你能指挥他们）：\n");
    for(AVerdantCharacter* T:W->Folk) if(CanHear(N,T))
        State+=FString::Printf(TEXT("ID=%d %s;身高=%.2f米;食物档位=%d;健康=%.1f;对其信任=%.1f;正在攻击ID=%d。\n"),T->AgentId,*T->DisplayName,T->HeightCm()/100.f,T->FoodTier(),T->Health,N->Trust.FindRef(T->AgentId),T->HostileId);
    if(auto* P=UVerdantPopulation::Find(this)) State+=P->ContextFor(N);
    State+=FString::Printf(TEXT("\n母树当前实际养分%.1f/%.1f。\n"),W->TreeNutrition,W->TreeGoal);
    if(CanConverse(N))State+=W->NegotiationSummary(N)+TEXT("\n");
    if(const auto* M=Memories.Find(N->AgentId))for(const auto& L:*M)State+=L+TEXT("\n");
    return State;
}
bool UVerdantDialogue::CanApplyDecision(const AVerdantCharacter* N,const FString& A,int32 Id,float Amount,FString* Reason)const
{
    const auto Reject=[Reason](const FString& Why){if(Reason)*Reason=Why;return false;}; if(Reason)Reason->Empty();
    auto* W=AVerdantWorld::Find(this);
    if(!W || !Alive(N) || N->AgentId==0 || W->FindFolk(N->AgentId)!=N)return Reject(TEXT("行动者已无效。"));
    if(!KnownAction(A) || !FMath::IsFinite(Amount) || Amount<0 || Id<0)return Reject(TEXT("行动参数无效。"));
    auto* T=W->FindFolk(Id);
    const bool Resource=A==TEXT("give")||A==TEXT("donate")||A==TEXT("steal");
    if(!Resource && Amount!=0)return Reject(TEXT("该行为不能附带食物转移。"));
    if(A==TEXT("none") || A==TEXT("stop"))return Id==0;
    if(A==TEXT("donate"))return Id==0 && Amount>=.01f && Amount<=W->MaxNegotiatedDonation(N)+.00001f;
    if(A==TEXT("leave"))
    {
        if(Id!=0 || N->bWild)return false;
        for(int32 I=0;I<W->Groves.Num();++I)if(I!=N->HomeGrove && W->CanHarvest(I,N))return true;
        return false;
    }
    if(A==TEXT("open")||A==TEXT("close")||A==TEXT("share")||A==TEXT("cede"))
        return Id==0 && CanConverse(N) && W->CanNegotiatePolicy(N,A,W->NegotiationGrove(N));
    if(!CanHear(N,T))return Reject(TEXT("目标必须仍在附近并存活。"));
    if(A==TEXT("give"))
        return Near(N,T,1100.f) && T->FoodTier()>=N->FoodTier() && Amount>=.01f
            && Amount<=FMath::Min(W->MaxNegotiatedGive(N,T),T->FreeFoodCapacity())+.00001f;
    if(A==TEXT("steal"))
        return Aggressive(N) && !Party(N,T) && N->SocialActionCooldown<=0 && !T->IsRespawnProtected()
            && Near(N,T,300.f) && N->FoodTier()>=T->FoodTier() && Amount>=.01f
            && Amount<=FMath::Min3(3.f,T->Food,N->FreeFoodCapacity())+.00001f && N->Trust.FindRef(Id)<5.f;
    if(A==TEXT("attack"))
        return !T->IsRespawnProtected() && !Party(N,T) && N->SocialActionCooldown<=0
            && (N->HostileId==Id || T->HostileId==N->AgentId || N->Trust.FindRef(Id)<=-20.f || (Aggressive(N)&&N->Trust.FindRef(Id)<5.f));
    if(A==TEXT("truce"))return N->HostileId==Id || T->HostileId==N->AgentId || N->Trust.FindRef(Id)<0;
    if(A==TEXT("follow")||A==TEXT("protect"))
    {
        if(N->HostileId==Id || N->Trust.FindRef(Id)<0)return false;
        TSet<int32> Seen;auto* Leader=T;
        for(int32 I=0;Alive(Leader)&&I<W->Folk.Num();++I)
        {if(Leader==N || Seen.Contains(Leader->AgentId))return false;Seen.Add(Leader->AgentId);Leader=W->FindFolk(Leader->FollowId);}
        if(Alive(Leader))return false;
        if(A==TEXT("protect") && !(N->SocialTags.Contains(TEXT("guardian")) || N->SocialTags.Contains(TEXT("caretaker")) || N->Trust.FindRef(Id)>=10.f))return false;
        if(Id==0)return W->CanNegotiateFollow(N,T) || N->Trust.FindRef(0)>=8.f;
        auto* Pop=UVerdantPopulation::Find(this);
        return N->Trust.FindRef(Id)>=5.f || (Pop && (Pop->HasRelation(N->AgentId,Id,TEXT("R01")) || Pop->HasRelation(N->AgentId,Id,TEXT("R07")) || Pop->HasRelation(Id,N->AgentId,TEXT("R08"))));
    }
    return false;
}
bool UVerdantDialogue::ApplyDecision(AVerdantCharacter* N,const FString& A,int32 Id,float Amount)
{
    LastDecisionReason.Empty();if(!CanApplyDecision(N,A,Id,Amount,&LastDecisionReason))return false;
    auto* W=AVerdantWorld::Find(this);auto* T=W->FindFolk(Id);
    if(A==TEXT("none"))return true; // Crucially does not reset existing behavior.
    if(A==TEXT("give"))return W->Transfer(N,T,Amount);
    if(A==TEXT("open")||A==TEXT("close")||A==TEXT("share")||A==TEXT("cede"))return W->ApplyNegotiatedPolicy(N,A,W->NegotiationGrove(N));
    if(A==TEXT("steal"))
    {
        const FString LedgerKey=FString::Printf(TEXT("%d:%d"),T->AgentId,N->AgentId);
        const float PreviousCredit=W->TransferLedger.FindRef(LedgerKey);
        const float Before=N->Food;
        if(!W->Transfer(T,N,Amount))return false;
        const float Taken=N->Food-Before;
        // Theft conserves stock but must not count as a voluntary gift for bargaining.
        W->TransferLedger.FindOrAdd(LedgerKey)=PreviousCredit;
        N->SocialActionCooldown=45.f;T->Trust.FindOrAdd(N->AgentId)-=15.f;++T->IntentRevision;
        T->Say(TEXT("你拿走了我的食物！"));
        W->Record(FString::Printf(TEXT("%s 抢走%s %.1f份食物。"),*N->DisplayName,*T->DisplayName,Taken));
        if(Id==0)W->Notify(FString::Printf(TEXT("%s 抢走了你 %.1f份食物。"),*N->DisplayName,Taken));
    }
    else if(A==TEXT("attack"))
    {N->HostileId=Id;N->CombatTarget=T;N->SocialActionCooldown=15.f;N->BrainClock=0.f;}
    else if(A==TEXT("follow")||A==TEXT("protect"))
    {N->DonationPledge=0;N->FollowId=Id;N->bProtectLeader=A==TEXT("protect");N->BrainClock=0;}
    else if(A==TEXT("donate"))
    {
        N->DonationPledge=Amount;N->FollowId=-1;N->bProtectLeader=false;N->BrainClock=0;
        if(auto* Pop=UVerdantPopulation::Find(this))Pop->ObserveDelivery(N);
    }
    else if(A==TEXT("stop"))
    {N->FollowId=-1;N->bProtectLeader=false;N->DonationPledge=0;N->HostileId=-1;N->CombatTarget.Reset();N->AttackTimer=0;N->bHitApplied=true;N->BrainClock=0;}
    else if(A==TEXT("truce"))
    {
        if(N->HostileId==Id)N->HostileId=-1;
        if(N->CombatTarget.Get()==T)N->CombatTarget.Reset();N->AttackTimer=0;N->bHitApplied=true;
        N->Trust.FindOrAdd(Id)=FMath::Max(0.f,N->Trust.FindRef(Id));N->AvoidThreatId=Id;N->AvoidThreatUntil=GetWorld()->GetTimeSeconds()+30;
        // NPC may stop itself; only the human's own conversational ceasefire stops both sides.
        if(Id==0 && T->HostileId==N->AgentId){T->HostileId=-1;T->CombatTarget.Reset();T->AttackTimer=0;T->bHitApplied=true;++T->IntentRevision;}
    }
    else if(A==TEXT("leave"))
    {
        for(int32 I=1;I<=W->Groves.Num();++I){int32 G=(FMath::Max(0,N->HomeGrove)+I)%W->Groves.Num();if(G!=N->HomeGrove&&W->CanHarvest(G,N)){N->HomeGrove=G;N->Destination=W->SurfacePoint(W->Groves[G].Position);break;}}
        N->FollowId=-1;N->bProtectLeader=false;N->DonationPledge=0;N->BrainClock=0;
    }
    else return false;
    ++N->IntentRevision;
    W->Record(FString::Printf(TEXT("%s 改变计划：%s，对象ID=%d。"),*N->DisplayName,*A,Id));
    return true;
}

void UVerdantDialogue::Request(AVerdantCharacter* NPC,const FString& PlayerText)
{
    auto* W=AVerdantWorld::Find(this);auto* P=W?W->FindFolk(0):nullptr;
    if(!GetWorld() || !Alive(P) || !CanHear(P,NPC) || PlayerText.TrimStartAndEnd().IsEmpty())return;
    SelectNPC(NPC);
    if(PendingNPCs.Contains(NPC->AgentId)||HearingQueue.Num()+CloudQueue.Num()>=32)
    {SetNPCStatus(NPC->AgentId,TEXT("这位居民尚有待处理发言，请等本轮回应。"));return;}
    if(FPlatformMisc::GetEnvironmentVariable(TEXT("VERDANT_DISABLE_AI"))==TEXT("1"))
    {SetNPCStatus(NPC->AgentId,TEXT("本次启动已关闭模型对话。"));return;}
    const float Now=GetWorld()->GetTimeSeconds();
    if(Now-LastPlayerMessageAt<1.5f){SetNPCStatus(NPC->AgentId,TEXT("先让附近的人听清上一句话。"));return;}
    if(LocalUnavailableUntil>Now){SetNPCStatus(NPC->AgentId,TEXT("本地相关性服务暂不可用，未调用云端。请稍后重试。"));return;}
    LastPlayerMessageAt=Now;
    const int64 Thread=++NextMessageId;ThreadLines.Add(Thread,0);ThreadExpiry.Add(Thread,Now+22.f);
    EnqueueSpeech(P,PlayerText.TrimStartAndEnd().Left(500),Thread,true,NPC);
    SetNPCStatus(NPC->AgentId,TEXT("附近居民正在判断这句话是否与自己有关……"));
    bBusy=HearingQueue.Num()>0 || LocalRequest.IsValid() || CloudRequest.IsValid();
}
void UVerdantDialogue::EnqueueSpeech(AVerdantCharacter* Speaker,const FString& Text,int64 Thread,bool bPlayerThread,AVerdantCharacter* Addressed)
{
    auto* W=AVerdantWorld::Find(this);if(!W || !Alive(Speaker) || ThreadLines.FindRef(Thread)>=2)return;
    const float Now=GetWorld()->GetTimeSeconds();const int64 Message=++NextMessageId;
    TArray<AVerdantCharacter*> Listeners;
    for(AVerdantCharacter* N:W->Folk)if(N && N->AgentId!=0 && CanHear(Speaker,N))Listeners.Add(N);
    Listeners.Sort([Speaker,Addressed](const AVerdantCharacter& A,const AVerdantCharacter& B)
    {if(&A==Addressed)return true;if(&B==Addressed)return false;return FVector::DistSquared(Speaker->GetActorLocation(),A.GetActorLocation())<FVector::DistSquared(Speaker->GetActorLocation(),B.GetActorLocation());});
    for(auto* N:Listeners)
    {
        if(HearingQueue.Num()+CloudQueue.Num()>=32 || PendingNPCs.Contains(N->AgentId) || NextReplyAt.FindRef(N->AgentId)>Now)continue;
        const FString Pair=FString::Printf(TEXT("%lld:%d"),Message,N->AgentId);if(HeardPairs.Contains(Pair))continue;
        HeardPairs.Add(Pair);
        FVerdantDialogueJob J;J.MessageId=Message;J.ThreadId=Thread;J.Speaker=Speaker;J.Listener=N;J.Text=Text;
        J.ExpiresAt=FMath::Min(Now+20.f,ThreadExpiry.FindRef(Thread));J.SpeakerLife=Speaker->LifeRevision;J.ListenerLife=N->LifeRevision;
        J.ListenerIntent=N->IntentRevision;J.bPlayerThread=bPlayerThread;J.bAddressed=N==Addressed;
        HearingQueue.Add(J);PendingNPCs.Add(N->AgentId);
        if(Speaker->AgentId==0)AppendNPCLine(N->AgentId,TEXT("你：")+Text);
    }
}
void UVerdantDialogue::FinishJob(const FVerdantDialogueJob& J,const FString& Message)
{
    if(auto* N=J.Listener.Get())
    {
        PendingNPCs.Remove(N->AgentId);NextReplyAt.Add(N->AgentId,GetWorld()?GetWorld()->GetTimeSeconds()+3.f:0.f);
        if(!Message.IsEmpty() && J.bPlayerThread)SetNPCStatus(N->AgentId,Message);
    }
    RequestNPC.Reset();
}
void UVerdantDialogue::StartGate(const FVerdantDialogueJob& J)
{
    if(!IsCurrent(J)){FinishJob(J);return;}
    auto* N=J.Listener.Get();auto* S=J.Speaker.Get();RequestNPC=N;
    auto Payload=MakeShared<FJsonObject>();auto State=MakeShared<FJsonObject>();
    State->SetStringField(TEXT("mode"),J.bInitiative?TEXT("npc_initiative"):TEXT("heard_speech"));
    State->SetStringField(TEXT("listener"),FString::Printf(TEXT("%s ID=%d, %s"),*N->DisplayName,N->AgentId,*N->Persona.Left(150)));
    State->SetStringField(TEXT("speaker_or_recipient"),FString::Printf(TEXT("%s ID=%d height=%.2fm"),*S->DisplayName,S->AgentId,S->HeightCm()/100.f));
    State->SetStringField(TEXT("speech_or_trigger"),J.Text.Left(500));State->SetBoolField(TEXT("explicitly_addressed"),J.bAddressed);
    State->SetStringField(TEXT("current_activity"),N->ActionLabel);
    if(auto* Pop=UVerdantPopulation::Find(this))State->SetStringField(TEXT("known_relationships"),Pop->ContextFor(N).Left(650));
    Payload->SetObjectField(TEXT("state"),State);
    auto Q=MakeShared<FJsonObject>();Q->SetStringField(TEXT("type"),TEXT("choice"));
    Q->SetStringField(TEXT("instructions"),J.bInitiative
        ? TEXT("Is this concrete trigger relevant enough for this NPC to initiate one short conversation with the named recipient now? Use personality and actual circumstances. Do not invent a reason.")
        : TEXT("Does this nearby utterance concern this listener personally, their food, goals, close relations or current conflict? Explicit address and questions to the listener are relevant even if they would refuse. Mere unrelated chatter is irrelevant. Decide relevance, not agreement."));
    auto Criteria=MakeShared<FJsonObject>();Criteria->SetStringField(TEXT("relevant"),TEXT("Relevant to this NPC; a personalized response or opening is justified."));Criteria->SetStringField(TEXT("irrelevant"),TEXT("Unrelated or no concrete reason to speak now; continue existing behavior."));
    Q->SetObjectField(TEXT("criteria"),Criteria);auto Questions=MakeShared<FJsonObject>();Questions->SetObjectField(TEXT("relevance"),Q);Payload->SetObjectField(TEXT("questions"),Questions);
    FString Body;FJsonSerializer::Serialize(Payload,TJsonWriterFactory<>::Create(&Body));
    auto Req=FHttpModule::Get().CreateRequest();LocalRequest=Req;
    Req->SetURL(TEXT("http://127.0.0.1:8090/v1/systemone"));Req->SetVerb(TEXT("POST"));Req->SetHeader(TEXT("Content-Type"),TEXT("application/json"));Req->SetContentAsString(Body);Req->SetTimeout(5.f);
    TWeakObjectPtr<UVerdantDialogue> Weak(this);
    Req->OnProcessRequestComplete().BindLambda([Weak,J](FHttpRequestPtr,FHttpResponsePtr Response,bool OK)
    {
        if(!Weak.IsValid())return;auto* Self=Weak.Get();Self->LocalRequest.Reset();
        if(!Self->IsCurrent(J)){Self->FinishJob(J);return;}
        const int32 Code=Response.IsValid()?Response->GetResponseCode():0;
        TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Answers=nullptr;const TSharedPtr<FJsonObject>* Answer=nullptr;FString Choice,Model;
        const bool Valid=OK&&Code==200&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Root)&&Root.IsValid()
            && Root->TryGetStringField(TEXT("model"),Model)&&Model==TEXT("verdant-startlux-secondary")
            && Root->TryGetObjectField(TEXT("answers"),Answers)&&Answers&&(*Answers)->TryGetObjectField(TEXT("relevance"),Answer)&&Answer&&(*Answer)->TryGetStringField(TEXT("choice"),Choice)
            && (Choice==TEXT("relevant")||Choice==TEXT("irrelevant"));
        UE_LOG(LogTemp,Display,TEXT("VERDANT_LOCAL_GATE http=%d valid=%d relevant=%d"),Code,Valid,Choice==TEXT("relevant"));
        if(!Valid)
        {
            Self->LocalUnavailableUntil=Self->GetWorld()->GetTimeSeconds()+15.f;
            Self->FinishJob(J,TEXT("本地相关性服务未就绪或响应无效；未调用 DeepSeek，角色继续原行动。"));return;
        }
        if(Choice==TEXT("irrelevant")){Self->FinishJob(J,TEXT("这句话未引起对方回应，原行动继续。"));return;}
        Self->CloudQueue.Add(J);
    });
    if(!Req->ProcessRequest()){LocalRequest.Reset();LocalUnavailableUntil=GetWorld()->GetTimeSeconds()+15.f;FinishJob(J,TEXT("无法连接本地相关性服务，未调用云端。"));}
}
void UVerdantDialogue::StartCloud(const FVerdantDialogueJob& J)
{
    if(!IsCurrent(J)){FinishJob(J);return;}
    if(Endpoint!=TEXT("https://api.qnaigc.com/v1/chat/completions")){FinishJob(J,TEXT("DeepSeek地址不合法，未发送密钥。"));return;}
    const FString Key=ApiKey();if(Key.IsEmpty()){FinishJob(J,TEXT("未配置有效的 DeepSeek API 密钥。"));return;}
    auto* N=J.Listener.Get();auto* S=J.Speaker.Get();const auto Offered=Options(this,N,S);
    FString Allowed;
    for(const auto& O:Offered)Allowed+=FString::Printf(TEXT("action=%s target=%d amount=%s\n"),*O.Action,O.Target,O.Maximum>0?*FString::Printf(TEXT("0.01..%.3f"),O.Maximum):TEXT("0"));
    FString Context=Rules()+BuildState(N)+TEXT("\n当前合法组合（未列出不得执行）：\n")+Allowed;
    if(J.bInitiative)Context+=FString::Printf(TEXT("\n主动上下文：你准备向%s(ID=%d)开口；对方没有先说这句话。依据真实触发原因发起简短邀请、提醒或警告。\n"),*S->DisplayName,S->AgentId);
    else Context+=FString::Printf(TEXT("\n实际说话者是%s(ID=%d)，你只是附近听众；本次是否被点名=%d。\n"),*S->DisplayName,S->AgentId,J.bAddressed);
    auto Payload=MakeShared<FJsonObject>();Payload->SetStringField(TEXT("model"),TEXT("deepseek/deepseek-v4.1-flash-1017"));Payload->SetNumberField(TEXT("temperature"),.3);Payload->SetNumberField(TEXT("max_tokens"),640);Payload->SetBoolField(TEXT("stream"),false);
    auto Thinking=MakeShared<FJsonObject>();Thinking->SetStringField(TEXT("type"),TEXT("disabled"));Payload->SetObjectField(TEXT("thinking"),Thinking);
    auto Format=MakeShared<FJsonObject>();Format->SetStringField(TEXT("type"),TEXT("json_object"));Payload->SetObjectField(TEXT("response_format"),Format);
    TArray<TSharedPtr<FJsonValue>> Messages;
    auto System=MakeShared<FJsonObject>();System->SetStringField(TEXT("role"),TEXT("system"));System->SetStringField(TEXT("content"),Context);Messages.Add(MakeShared<FJsonValueObject>(System));
    // This protocol envelope explicitly distinguishes synthetic situation input from human speech.
    auto User=MakeShared<FJsonObject>();User->SetStringField(TEXT("role"),TEXT("user"));User->SetStringField(TEXT("content"),(J.bInitiative?TEXT("[游戏提供的主动触发事实；并非玩家发言]\n"):TEXT("[实际听到的发言]\n"))+J.Text);Messages.Add(MakeShared<FJsonValueObject>(User));Payload->SetArrayField(TEXT("messages"),Messages);
    FString Body;FJsonSerializer::Serialize(Payload,TJsonWriterFactory<>::Create(&Body));
    auto Req=FHttpModule::Get().CreateRequest();CloudRequest=Req;RequestNPC=N;
    Req->SetURL(Endpoint);Req->SetVerb(TEXT("POST"));Req->SetHeader(TEXT("Content-Type"),TEXT("application/json"));Req->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Key);Req->SetContentAsString(Body);Req->SetTimeout(14.f);
    const double Started=FPlatformTime::Seconds();TWeakObjectPtr<UVerdantDialogue> Weak(this);
    Req->OnProcessRequestComplete().BindLambda([Weak,J,Offered,Started](FHttpRequestPtr,FHttpResponsePtr Response,bool OK)
    {
        if(!Weak.IsValid())return;auto* Self=Weak.Get();Self->CloudRequest.Reset();
        const int32 Code=Response.IsValid()?Response->GetResponseCode():0;
        UE_LOG(LogTemp,Display,TEXT("VERDANT_AI provider=qnaigc model=deepseek/deepseek-v4.1-flash-1017 http=%d elapsed_ms=%.1f gated=1"),Code,(FPlatformTime::Seconds()-Started)*1000.);
        if(!Self->IsCurrent(J)){Self->FinishJob(J,TEXT("现场或行动已变化，迟到的回应未执行。"));return;}
        if(!OK || Code!=200)
        {
            FString Error=TEXT("DeepSeek暂时无法连接，角色继续原行动。");
            if(Code==401)Error=TEXT("DeepSeek密钥验证失败。");else if(Code==402)Error=TEXT("DeepSeek账户余额不足。");else if(Code==429)Error=TEXT("DeepSeek请求受限，请稍后重试。");
            Self->FinishJob(J,Error);return;
        }
        TSharedPtr<FJsonObject> Root,Decision;const TArray<TSharedPtr<FJsonValue>>* Choices=nullptr;
        const TSharedPtr<FJsonObject>* Choice=nullptr;const TSharedPtr<FJsonObject>* Message=nullptr;
        FString Finish,Content,Say,Action;double Target=0,Amount=0;
        bool Valid=FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Root)&&Root.IsValid()
            &&Root->TryGetArrayField(TEXT("choices"),Choices)&&Choices&&!Choices->IsEmpty()&&(*Choices)[0].IsValid()&&(*Choices)[0]->TryGetObject(Choice)&&Choice
            &&(*Choice)->TryGetStringField(TEXT("finish_reason"),Finish)&&Finish==TEXT("stop")&&(*Choice)->TryGetObjectField(TEXT("message"),Message)&&Message
            &&(*Message)->TryGetStringField(TEXT("content"),Content)&&!Content.TrimStartAndEnd().IsEmpty()
            &&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content),Decision)&&Decision.IsValid()&&Decision->Values.Num()==4
            &&Decision->TryGetStringField(TEXT("say"),Say)&&Decision->TryGetStringField(TEXT("action"),Action)
            &&Decision->TryGetNumberField(TEXT("target"),Target)&&Decision->TryGetNumberField(TEXT("amount"),Amount)
            &&FMath::IsFinite(Target)&&Target>=0&&Target<=MAX_int32&&FMath::Fmod(Target,1.)==0&&FMath::IsFinite(Amount)&&Amount>=0&&Amount<=20;
        Say.TrimStartAndEndInline();Valid=Valid&&!Say.IsEmpty()&&Say.Len()<=200;
        auto* W=AVerdantWorld::Find(Self);auto* T=Valid&&W?W->FindFolk(int32(Target)):nullptr;
        if(Valid)Valid=Offered.ContainsByPredicate([&](const FOption& O){return O.Action==Action&&O.Target==int32(Target)&&T&&T->LifeRevision==O.TargetLife&&(O.Maximum>0?Amount>=.01&&Amount<=double(O.Maximum)+.00001:Amount==0);});
        if(!Valid){Self->FinishJob(J,TEXT("回应字段或行动超出本次条件，未改变世界。"));return;}
        Self->Publish(J,Say,Action,int32(Target),float(Amount));
    });
    if(!Req->ProcessRequest()){CloudRequest.Reset();FinishJob(J,TEXT("无法发起 DeepSeek 请求，原行动继续。"));}
}

bool UVerdantDialogue::ReadingConversationWith(const AVerdantCharacter* N)const
{
    auto* W=AVerdantWorld::Find(this);auto* P=W?W->FindFolk(0):nullptr;
    auto* PC=P?Cast<APlayerController>(P->GetController()):nullptr;auto* H=PC?Cast<AVerdantHUD>(PC->GetHUD()):nullptr;
    return H&&H->bConversationOpen&&Selected.Get()==N;
}
void UVerdantDialogue::Publish(const FVerdantDialogueJob& J,const FString& Say,const FString& Action,int32 TargetId,float Amount)
{
    auto* W=AVerdantWorld::Find(this);auto* N=J.Listener.Get();auto* S=J.Speaker.Get();
    if(!W || !IsCurrent(J)){FinishJob(J);return;}
    auto* T=W->FindFolk(TargetId);FString Reason;
    if(!CanApplyDecision(N,Action,TargetId,Amount,&Reason)){FinishJob(J,TEXT("当前条件已变化，本次未执行。")+Reason);return;}
    FString Result;
    if(Action==TEXT("attack") || Action==TEXT("steal"))
    {
        FVerdantWarnedAction Delayed;Delayed.Actor=N;Delayed.Target=T;Delayed.Action=Action;Delayed.Amount=Amount;
        Delayed.ExecuteAt=GetWorld()->GetTimeSeconds()+2.5f;Delayed.ExpiresAt=GetWorld()->GetTimeSeconds()+15.f;
        ++N->IntentRevision;Delayed.ActorLife=N->LifeRevision;Delayed.ActorIntent=N->IntentRevision;Delayed.TargetLife=T->LifeRevision;
        WarnedActions.Add(Delayed);
        Result=Action==TEXT("attack")?TEXT("警告：对方准备发动攻击，尚未命中。") : TEXT("警告：对方准备靠近抢粮，尚未扣粮。离开即可避开这次抢夺。");
        if(T->AgentId==0)W->Notify(N->DisplayName+TEXT("：")+Result);
    }
    else
    {
        const float Before=T?T->Food:0.f;
        if(!ApplyDecision(N,Action,TargetId,Amount)){FinishJob(J,TEXT("当前条件已变化，本次提议未执行。"));return;}
        if(Action==TEXT("none"))Result=TEXT("仅交谈，继续原行动。");
        else if(Action==TEXT("give"))Result=FString::Printf(TEXT("已实际交付 %.1f份给%s。"),T?T->Food-Before:0.f,T?*T->DisplayName:TEXT("对方"));
        else if(Action==TEXT("donate"))Result=TEXT("已接受送粮计划，实际到达并扣粮才计入供奉。");
        else if(Action==TEXT("follow"))Result=TEXT("已开始单纯跟随。");
        else if(Action==TEXT("protect"))Result=TEXT("已开始跟随保护。");
        else if(Action==TEXT("truce"))Result=TEXT("已停止自身敌意；其他角色的态度由其自行决定。");
        else Result=TEXT("已按当前规则改变自身行动。");
    }
    N->Say(Say);
    ++ThreadLines.FindOrAdd(J.ThreadId);
    const FString Line=N->DisplayName+TEXT("：")+Say;
    RememberExchange(N->AgentId,J.bInitiative?TEXT("我因现场情况主动开口：")+J.Text:S->DisplayName+TEXT("说：")+J.Text,
        Line+TEXT(" [实际结果：")+Result+TEXT("]"));
    auto* Player=W->FindFolk(0);
    if(CanHear(N,Player))
    {
        AppendNPCLine(N->AgentId,Line);SetNPCStatus(N->AgentId,Result);
        W->Record(Line);
    }
    FinishJob(J);
    // Replies stay in the original bounded thread. They do not become new proactive events.
    if(ThreadLines.FindRef(J.ThreadId)<2)EnqueueSpeech(N,Say,J.ThreadId,J.bPlayerThread,S);
}
void UVerdantDialogue::TickWarnedActions()
{
    const float Now=GetWorld()->GetTimeSeconds();
    for(int32 I=WarnedActions.Num()-1;I>=0;--I)
    {
        const auto D=WarnedActions[I];auto* N=D.Actor.Get();auto* T=D.Target.Get();
        if(!Alive(N)||!Alive(T)||Now>D.ExpiresAt||N->LifeRevision!=D.ActorLife||N->IntentRevision!=D.ActorIntent||T->LifeRevision!=D.TargetLife)
        {WarnedActions.RemoveAt(I);continue;}
        if(Now<D.ExecuteAt || ReadingConversationWith(N))continue;
        // A live distance/resource/relationship check occurs again after the warning window.
        ApplyDecision(N,D.Action,T->AgentId,D.Amount);WarnedActions.RemoveAt(I);
    }
}
void UVerdantDialogue::ConsiderInitiative()
{
    auto* W=AVerdantWorld::Find(this);auto* P=W?W->FindFolk(0):nullptr;
    if(!Alive(P) || !GetWorld())return;
    const float Now=GetWorld()->GetTimeSeconds();if(Now<NextProactiveAt || Now<LocalUnavailableUntil)return;
    NextProactiveAt=Now+3.f;
    auto* Pop=UVerdantPopulation::Find(this);
    for(AVerdantCharacter* N:W->Folk)
    {
        if(!Alive(N)||N->AgentId==0||PendingNPCs.Contains(N->AgentId)||N->AttackTimer>0||N->HurtTimer>0||N->HostileId>=0
            || NextInitiativeAt.FindRef(N->AgentId)>Now || FVector::DistSquared(P->GetActorLocation(),N->GetActorLocation())>FMath::Square(5000.f))continue;
        AVerdantCharacter* Recipient=CanHear(N,P)?P:nullptr;
        // Alternate player-facing and pair-facing choices; both use exactly the same local gate.
        if(N->AgentId%2==0 || !Recipient)
        {
            for(AVerdantCharacter* T:W->Folk)
                if(T&&T->AgentId!=0&&CanHear(N,T)&&Pop&&(Pop->HasRelation(N->AgentId,T->AgentId,TEXT("R01"))||Pop->HasRelation(N->AgentId,T->AgentId,TEXT("R07"))||Pop->HasRelation(N->AgentId,T->AgentId,TEXT("R19"))||Pop->HasRelation(N->AgentId,T->AgentId,TEXT("R20"))))
                {Recipient=T;break;}
        }
        if(!Recipient)continue;
        const FString Pair=FString::Printf(TEXT("%d:%d"),FMath::Min(N->AgentId,Recipient->AgentId),FMath::Max(N->AgentId,Recipient->AgentId));
        if(Recipient->AgentId!=0 && NextPairAt.FindRef(Pair)>Now)continue;
        const FString Event=Pop?Pop->RequestSummary(N):FString();
        const FString Situation=FString::Printf(TEXT("tier=%d;low=%d;hurt=%d;trust=%d;delivery=%d;event=%s"),Recipient->FoodTier(),N->Food<W->NegotiationReserve(N),Recipient->Health<Recipient->MaxHealth()*.5f,FMath::FloorToInt(N->Trust.FindRef(Recipient->AgentId)/10.f),N->DonationPledge>0,*Event);
        if(LastSituation.FindRef(Pair)==Situation)continue;
        const bool First=!LastSituation.Contains(Pair);
        FString Trigger=First?TEXT("你第一次在附近遇见这个对象，可以按自身性格打招呼或提醒。"):
            TEXT("附近对象的体型、健康、信任或共同请求发生了实际变化；只谈本次可见情况，不重复旧邀请。");
        if(N->Food<W->NegotiationReserve(N))Trigger+=TEXT("你当前口粮不足，可向对方商量，不凭空生成粮食。");
        if(!Event.IsEmpty())Trigger+=Event;
        FVerdantDialogueJob J;J.MessageId=++NextMessageId;J.ThreadId=J.MessageId;J.Listener=N;J.Speaker=Recipient;
        J.Text=Trigger;J.ExpiresAt=Now+20;J.ListenerLife=N->LifeRevision;J.ListenerIntent=N->IntentRevision;J.SpeakerLife=Recipient->LifeRevision;
        J.bInitiative=true;J.bAddressed=true;J.bPlayerThread=false;
        ThreadLines.Add(J.ThreadId,0);ThreadExpiry.Add(J.ThreadId,Now+40);
        HearingQueue.Add(J);PendingNPCs.Add(N->AgentId);LastSituation.Add(Pair,Situation);
        NextInitiativeAt.Add(N->AgentId,Now+60.f+FMath::FRandRange(0.f,60.f));
        if(Recipient->AgentId!=0)NextPairAt.Add(Pair,Now+120.f+FMath::FRandRange(0.f,120.f));
        NextProactiveAt=Now+20.f;return;
    }
}
void UVerdantDialogue::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,TickType,Function);if(!GetWorld())return;
    if(FPlatformMisc::GetEnvironmentVariable(TEXT("VERDANT_DISABLE_AI"))==TEXT("1"))
    {
        HearingQueue.Reset();CloudQueue.Reset();WarnedActions.Reset();PendingNPCs.Reset();bBusy=false;
        if(LocalRequest){LocalRequest->OnProcessRequestComplete().Unbind();LocalRequest->CancelRequest();LocalRequest.Reset();}
        if(CloudRequest){CloudRequest->OnProcessRequestComplete().Unbind();CloudRequest->CancelRequest();CloudRequest.Reset();}
        return;
    }
    TickWarnedActions();const float Now=GetWorld()->GetTimeSeconds();
    for(int32 I=HearingQueue.Num()-1;I>=0;--I)if(!IsCurrent(HearingQueue[I])){FinishJob(HearingQueue[I]);HearingQueue.RemoveAt(I);}
    for(int32 I=CloudQueue.Num()-1;I>=0;--I)if(!IsCurrent(CloudQueue[I])){FinishJob(CloudQueue[I]);CloudQueue.RemoveAt(I);}
    for(auto It=ThreadExpiry.CreateIterator();It;++It)if(It.Value()<Now){ThreadLines.Remove(It.Key());It.RemoveCurrent();}
    if(HeardPairs.Num()>512)HeardPairs.Reset();
    if(!CloudRequest && !LocalRequest)
    {
        if(!CloudQueue.IsEmpty()){const auto J=CloudQueue[0];CloudQueue.RemoveAt(0);StartCloud(J);}
        else if(!HearingQueue.IsEmpty())
        {
            int32 Index=0;for(int32 I=0;I<HearingQueue.Num();++I)if(HearingQueue[I].bPlayerThread){Index=I;break;}
            const auto J=HearingQueue[Index];HearingQueue.RemoveAt(Index);
            if(Now<LocalUnavailableUntil)FinishJob(J,TEXT("本地相关性服务不可用，未绕过筛选调用云端。"));else StartGate(J);
        }
        else ConsiderInitiative();
    }
    bBusy=LocalRequest.IsValid()||CloudRequest.IsValid()||!HearingQueue.IsEmpty()||!CloudQueue.IsEmpty();
}
