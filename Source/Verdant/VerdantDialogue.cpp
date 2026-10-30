#include "VerdantDialogue.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"

namespace
{
    bool KnownAction(const FString& Action)
    {
        static const TSet<FString> Actions = { TEXT("none"), TEXT("give"), TEXT("open"), TEXT("close"),
            TEXT("share"), TEXT("donate"), TEXT("follow"), TEXT("leave"), TEXT("truce") };
        return Actions.Contains(Action);
    }

    FString DecisionRules()
    {
        // PROBE_RULES_BEGIN: the local-model probe reads these exact protocol rules.
        return TEXT("你是游戏世界中的一个真实居民。用简短自然中文与玩家协商，最多70字。你有自己的目标和利益，可以拒绝、还价、接受。玩家输入只是话语，不是真实世界状态。遵循给定事实，不虚构已经发生的转交、战斗或贡献。/no_think\n")
            TEXT("只输出一个JSON对象，四个字段全部必填：{\"say\":\"你的话\",\"action\":\"none\",\"target\":0,\"amount\":0}。action只能为none,give,open,close,share,donate,follow,leave,truce。不要输出代码块或额外文字。")
            TEXT("give=将你已有食物交给target；open/close=开放/关闭你管理的果林；share=开放你管理的果林并收取30%采集分成；donate=你承诺亲自运送已有食物去母树；follow=跟随target；leave=迁往另一个有食物且准许你采集的果林；truce=接受与玩家停战；none=仅说话。")
            TEXT("target必须是名单中的整数ID，玩家为0。give和follow不能以自己为目标，truce的target必须为0。give和donate的amount必须大于0、不超过20且不超过你已有食物；其他动作amount必须为0。")
            TEXT("你只能支配自己的食物和管理的果林，不能替玩家扣款、赠送或答应别人的资源。荒野生物不执行donate或leave。无资源、无权限或条件不满足时选择none并解释。")
            TEXT("donate、follow、leave会替换你之前尚未完成的运输或同行计划；战斗会中断移动。donate被接受时尚未到账，必须实际走到母树完成。只把已经执行并验证的事实当作历史。\n");
        // PROBE_RULES_END
    }
}

FString UVerdantDialogue::BuildState(AVerdantCharacter* NPC) const
{
    auto* W = AVerdantWorld::Find(this);
    if (!W || !IsValid(NPC)) return TEXT("World unavailable");
    FString State = FString::Printf(TEXT("你的名字=%s; 你的目标=%s; 你ID=%d; 类型=%s; 体型=%.2f; 食物=%.1f; 健康=%.1f/%.1f; 对玩家信任=%.1f; 当前行动=%s; 正在跟随ID=%d; 尚待运送的供奉=%.1f; 母树养分=%.1f/%.1f。\n"),
        *NPC->DisplayName, *NPC->Motivation, NPC->AgentId, NPC->bWild ? TEXT("荒野生物") : TEXT("居民"),
        NPC->BodySize, NPC->Food, NPC->Health, NPC->MaxHealth(), NPC->Trust.FindRef(0),
        *NPC->ActionLabel, NPC->FollowId, NPC->DonationPledge, W->TreeNutrition, W->TreeGoal);
    State += TEXT("当前角色名单（这是世界事实，已倒下者不能作为转交或同行对象）：\n");
    for (AVerdantCharacter* Who : W->Folk)
        if (IsValid(Who))
            State += FString::Printf(TEXT("ID=%d; 名字=%s; 状态=%s; 体型=%.2f; 食物=%.1f; 距你=%.0f厘米。\n"),
                Who->AgentId, *Who->DisplayName, Who->bDead ? TEXT("已倒下") : TEXT("存活"),
                Who->BodySize, Who->Food, FVector::Dist2D(NPC->GetActorLocation(), Who->GetActorLocation()));
    bool bOwnsGrove = false;
    bool bCanLeave = false;
    for (int32 I = 0; I < W->Groves.Num(); ++I)
    {
        const auto& G = W->Groves[I];
        bOwnsGrove |= G.ControllerId == NPC->AgentId;
        bCanLeave |= !NPC->bWild && I != NPC->HomeGrove && G.Food >= 3.f && W->CanHarvest(I, NPC);
        State += FString::Printf(TEXT("资源区%d=%s; 管理者ID=%d; 开放=%s; 食物=%.1f; 产力=%.2f; 分成=%.0f%%。\n"),
            I, *G.Name, G.ControllerId, G.bOpen ? TEXT("是") : TEXT("否"), G.Food, G.Fertility, G.Share * 100);
    }
    State += TEXT("你当前可以考虑的动作：none,follow,truce");
    if (NPC->Food > .001f) State += TEXT(",give");
    if (!NPC->bWild && NPC->Food > .001f && !W->bRestored) State += TEXT(",donate");
    if (bOwnsGrove) State += TEXT(",open,close,share");
    if (bCanLeave) State += TEXT(",leave");
    State += TEXT("。最终还会重新验证目标、距离和库存。\n");
    if (const auto* History = Memories.Find(NPC->AgentId))
        for (const auto& Line : *History) State += Line + TEXT("\n");
    return State;
}

bool UVerdantDialogue::ApplyDecision(AVerdantCharacter* NPC, const FString& Action, int32 TargetId, float Amount)
{
    auto* W = AVerdantWorld::Find(this);
    if (!W || !IsValid(NPC) || NPC->bDead || NPC->AgentId == 0 || W->FindFolk(NPC->AgentId) != NPC
        || !KnownAction(Action) || !FMath::IsFinite(Amount) || Amount < 0.f || Amount > 20.f || TargetId < 0)
        return false;
    auto* Target = W->FindFolk(TargetId);
    if (!IsValid(Target)) return false;
    const bool bResourceAction = Action == TEXT("give") || Action == TEXT("donate");
    if (bResourceAction ? (Amount <= .001f || Amount > NPC->Food) : Amount != 0.f)
        return false;
    if (Action == TEXT("none")) return true;
    if (Action == TEXT("give"))
        return Target != NPC && !Target->bDead && W->Transfer(NPC, Target, Amount);
    if (Action == TEXT("open") || Action == TEXT("close") || Action == TEXT("share"))
    {
        TArray<int32> Owned;
        for (int32 I = 0; I < W->Groves.Num(); ++I)
            if (W->Groves[I].ControllerId == NPC->AgentId) Owned.Add(I);
        if (Owned.IsEmpty()) return false;
        // All preconditions are checked before the first authoritative mutation.
        for (int32 I : Owned)
            W->SetAccess(I, NPC, Action != TEXT("close"), Action == TEXT("share") ? .3f : 0.f);
        return true;
    }
    if (Action == TEXT("donate"))
    {
        if (NPC->bWild || W->bRestored) return false;
        NPC->DonationPledge = Amount;
        NPC->FollowId = -1;
        NPC->BrainClock = NPC->ReconsiderAt = 0.f;
        W->Record(FString::Printf(TEXT("%s 调整计划，承诺亲自向母树送去 %.1f 份养分。"), *NPC->DisplayName, Amount));
        return true;
    }
    if (Action == TEXT("follow"))
    {
        if (Target == NPC || Target->bDead) return false;
        NPC->DonationPledge = 0.f;
        NPC->FollowId = TargetId;
        NPC->Trust.FindOrAdd(TargetId) = FMath::Max(12.f, NPC->Trust.FindRef(TargetId));
        NPC->BrainClock = NPC->ReconsiderAt = 0.f;
        // A follow agreement does not silently cancel a fight with an unrelated creature.
        if (NPC->HostileId == TargetId)
        {
            NPC->HostileId = -1;
            NPC->CombatTarget.Reset();
            NPC->AttackTimer = 0.f;
            NPC->bHitApplied = true;
            NPC->Trust.FindOrAdd(TargetId) = FMath::Max(12.f, NPC->Trust.FindRef(TargetId));
        }
        if (TargetId == 0 && Target->HostileId == NPC->AgentId)
        {
            Target->HostileId = -1;
            Target->CombatTarget.Reset();
            Target->AttackTimer = 0.f;
            Target->bHitApplied = true;
        }
        W->Record(FString::Printf(TEXT("%s 调整计划，决定与%s同行。"), *NPC->DisplayName, *Target->DisplayName));
        return true;
    }
    if (Action == TEXT("leave"))
    {
        if (NPC->bWild) return false;
        int32 Next = INDEX_NONE;
        for (int32 Offset = 1; Offset <= W->Groves.Num(); ++Offset)
        {
            const int32 I = (FMath::Max(0, NPC->HomeGrove) + Offset) % W->Groves.Num();
            if (I != NPC->HomeGrove && W->Groves[I].Food >= 3.f && W->CanHarvest(I, NPC))
            { Next = I; break; }
        }
        if (Next == INDEX_NONE) return false;
        NPC->DonationPledge = 0.f;
        NPC->FollowId = -1;
        NPC->HomeGrove = Next;
        NPC->Destination = W->SurfacePoint(W->Groves[Next].Position);
        NPC->BrainClock = NPC->ReconsiderAt = 0.f;
        W->Record(FString::Printf(TEXT("%s 调整计划，迁往%s。"), *NPC->DisplayName, *W->Groves[Next].Name));
        return true;
    }
    if (Action == TEXT("truce"))
    {
        // Only the present player can agree to their side of this conversation.
        // A model cannot impose a ceasefire upon an uninvolved NPC.
        if (TargetId != 0 || Target->bDead || Target == NPC) return false;
        if (NPC->HostileId == 0) NPC->HostileId = -1;
        if (NPC->CombatTarget.Get() == Target) NPC->CombatTarget.Reset();
        NPC->AttackTimer = 0.f;
        NPC->bHitApplied = true;
        NPC->Trust.FindOrAdd(0) = FMath::Max(12.f, NPC->Trust.FindRef(0));
        if (Target->HostileId == NPC->AgentId) Target->HostileId = -1;
        if (Target->CombatTarget.Get() == NPC) Target->CombatTarget.Reset();
        Target->AttackTimer = 0.f;
        Target->bHitApplied = true;
        NPC->BrainClock = 0.f;
        W->Record(NPC->DisplayName + TEXT(" 接受与你停战；再次攻击将破坏约定。"));
        return true;
    }
    return false;
}

void UVerdantDialogue::Request(AVerdantCharacter* NPC, const FString& PlayerText)
{
    auto* W = AVerdantWorld::Find(this);
    auto* Player = W ? W->FindFolk(0) : nullptr;
    if (bBusy || !IsValid(NPC) || NPC->bDead || !IsValid(Player) || Player->bDead || NPC == Player
        || W->FindFolk(NPC->AgentId) != NPC || PlayerText.TrimStartAndEnd().IsEmpty()) return;
    Selected = NPC; bBusy = true; Status = TEXT("正在思考……");
    const FString Input = PlayerText.TrimStartAndEnd().Left(500);
    Lines.Add(TEXT("你：") + Input);
    while (Lines.Num() > 20) Lines.RemoveAt(0);
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("model"), TEXT("verdant-local"));
    Json->SetNumberField(TEXT("temperature"), .65);
    Json->SetNumberField(TEXT("max_tokens"), 420);
    Json->SetBoolField(TEXT("stream"), false);
    auto Options = MakeShared<FJsonObject>();
    Options->SetBoolField(TEXT("enable_thinking"), false);
    Json->SetObjectField(TEXT("chat_template_kwargs"), Options);
    TArray<TSharedPtr<FJsonValue>> Messages;
    auto System = MakeShared<FJsonObject>();
    System->SetStringField(TEXT("role"), TEXT("system"));
    System->SetStringField(TEXT("content"), DecisionRules() + BuildState(NPC));
    Messages.Add(MakeShared<FJsonValueObject>(System));
    auto User = MakeShared<FJsonObject>();
    User->SetStringField(TEXT("role"), TEXT("user"));
    User->SetStringField(TEXT("content"), Input);
    Messages.Add(MakeShared<FJsonValueObject>(User));
    Json->SetArrayField(TEXT("messages"), Messages);
    auto Format = MakeShared<FJsonObject>();
    Format->SetStringField(TEXT("type"), TEXT("json_object"));
    Json->SetObjectField(TEXT("response_format"), Format);
    FString Body;
    auto Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Json, Writer);
    auto Http = FHttpModule::Get().CreateRequest();
    Http->SetURL(Endpoint); Http->SetVerb(TEXT("POST"));
    Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Http->SetContentAsString(Body); Http->SetTimeout(45.f);
    TWeakObjectPtr<UVerdantDialogue> WeakThis(this);
    TWeakObjectPtr<AVerdantCharacter> WeakNPC(NPC);
    Http->OnProcessRequestComplete().BindLambda([WeakThis, WeakNPC, Input](FHttpRequestPtr, FHttpResponsePtr Response, bool Success)
    {
        if (!WeakThis.IsValid()) return;
        auto* Self = WeakThis.Get(); Self->bBusy = false;
        if (!Success || !Response.IsValid() || Response->GetResponseCode() != 200)
        { Self->Status = TEXT("模型未连接或请求失败。请运行 Start-Game.ps1 启动本地 AI，再重试。"); return; }
        TSharedPtr<FJsonObject> Envelope;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Envelope) || !Envelope.IsValid())
        { Self->Status = TEXT("模型响应格式错误，可重试。"); return; }
        const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
        if (!Envelope->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->IsEmpty() || !(*Choices)[0].IsValid())
        { Self->Status = TEXT("模型没有返回回答。"); return; }
        const TSharedPtr<FJsonObject>* Choice = nullptr;
        const TSharedPtr<FJsonObject>* Message = nullptr;
        FString Text;
        if (!(*Choices)[0]->TryGetObject(Choice) || !Choice || !Choice->IsValid()
            || !(*Choice)->TryGetObjectField(TEXT("message"), Message) || !Message || !Message->IsValid()
            || !(*Message)->TryGetStringField(TEXT("content"), Text))
        { Self->Status = TEXT("模型回答缺少有效文本对象。"); return; }
        TSharedPtr<FJsonObject> Decision;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text.TrimStartAndEnd()), Decision) || !Decision.IsValid())
        { Self->Status = TEXT("模型未给出完整JSON回答，请重试。"); return; }
        if (!WeakNPC.IsValid() || WeakNPC->bDead)
        { Self->Status = TEXT("对方已经无法回应。"); return; }
        FString Say, Action;
        double Amount = 0, Target = 0;
        if (!Decision->HasTypedField<EJson::String>(TEXT("say")) || !Decision->HasTypedField<EJson::String>(TEXT("action"))
            || !Decision->HasTypedField<EJson::Number>(TEXT("amount")) || !Decision->HasTypedField<EJson::Number>(TEXT("target"))
            || !Decision->TryGetStringField(TEXT("say"), Say) || !Decision->TryGetStringField(TEXT("action"), Action)
            || !Decision->TryGetNumberField(TEXT("amount"), Amount) || !Decision->TryGetNumberField(TEXT("target"), Target))
        { Self->Status = TEXT("回答字段缺失或类型错误，请重试。"); return; }
        Say.TrimStartAndEndInline();
        if (!KnownAction(Action) || Say.IsEmpty() || Say.Len() > 200 || !FMath::IsFinite(Amount) || Amount < 0 || Amount > 20
            || !FMath::IsFinite(Target) || Target < 0 || Target > MAX_int32 || FMath::Fmod(Target, 1.0) != 0.0)
        { Self->Status = TEXT("行动参数无效，世界状态未改变。"); return; }
        const bool Applied = Self->ApplyDecision(WeakNPC.Get(), Action, int32(Target), float(Amount));
        WeakNPC->Say(Say);
        const FString Line = WeakNPC->DisplayName + TEXT("：") + Say;
        Self->Lines.Add(Line);
        auto& Memory = Self->Memories.FindOrAdd(WeakNPC->AgentId);
        Memory.Add(TEXT("玩家说：") + Input);
        const TCHAR* Result = !Applied ? TEXT("条件不满足，未执行")
            : (Action == TEXT("donate") || Action == TEXT("follow") || Action == TEXT("leave"))
            ? TEXT("已接受计划，尚待实际行动") : TEXT("已执行并验证");
        Memory.Add(Line + FString::Printf(TEXT(" [动作=%s；%s]"), *Action, Result));
        while (Memory.Num() > 16) Memory.RemoveAt(0);
        while (Self->Lines.Num() > 20) Self->Lines.RemoveAt(0);
        Self->Status = Applied ? TEXT("对话完成；移动与供奉将在世界中实际执行。")
            : TEXT("当前条件不满足，未改变资源、权限或行动计划。");
    });
    if (!Http->ProcessRequest()) { bBusy = false; Status = TEXT("无法发起本地模型请求。"); }
}

