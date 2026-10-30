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
            TEXT("share"), TEXT("cede"), TEXT("donate"), TEXT("follow"), TEXT("leave"), TEXT("truce") };
        return Actions.Contains(Action);
    }

    struct FLegalDecision
    {
        FString Action;
        int32 TargetId;
        float Maximum;
        bool bResource;
    };

    // Both the readable model state and JSON grammar use this same live validator.
    TArray<FLegalDecision> LegalDecisions(AVerdantWorld* World, AVerdantCharacter* NPC)
    {
        TArray<FLegalDecision> Options;
        if (!World || !World->Dialogue) return Options;
        const TArray<FString> Actions = { TEXT("none"), TEXT("open"), TEXT("close"), TEXT("share"), TEXT("cede"),
            TEXT("give"), TEXT("donate"), TEXT("follow"), TEXT("leave"), TEXT("truce") };
        for (const FString& Action : Actions)
        {
            const bool Resource = Action == TEXT("give") || Action == TEXT("donate");
            TArray<int32> Candidates = {0};
            if (Action == TEXT("give") || Action == TEXT("follow"))
                for (AVerdantCharacter* Other : World->Folk)
                    if (IsValid(Other) && Other->AgentId != 0) Candidates.AddUnique(Other->AgentId);
            for (int32 TargetId : Candidates)
            {
                const float Maximum = Action == TEXT("give")
                    ? World->MaxNegotiatedGive(NPC, World->FindFolk(TargetId))
                    : Action == TEXT("donate") ? World->MaxNegotiatedDonation(NPC) : 0.f;
                if (Resource && Maximum < .01f) continue;
                if (!World->Dialogue->CanApplyDecision(NPC, Action, TargetId, Resource ? .01f : 0.f))
                    continue;
                Options.Add({Action, TargetId, Maximum, Resource});
            }
        }
        return Options;
    }

    TSharedPtr<FJsonObject> DecisionSchema(AVerdantWorld* World, AVerdantCharacter* NPC)
    {
        TArray<TSharedPtr<FJsonValue>> Branches;
        for (const FLegalDecision& Option : LegalDecisions(World, NPC))
        {
            auto Properties = MakeShared<FJsonObject>();
            auto ActionRule = MakeShared<FJsonObject>(); ActionRule->SetStringField(TEXT("const"), Option.Action);
            Properties->SetObjectField(TEXT("action"), ActionRule);
            auto TargetRule = MakeShared<FJsonObject>(); TargetRule->SetStringField(TEXT("type"), TEXT("integer"));
            TargetRule->SetNumberField(TEXT("const"), Option.TargetId); Properties->SetObjectField(TEXT("target"), TargetRule);
            auto AmountRule = MakeShared<FJsonObject>();
            if (Option.bResource)
            {
                AmountRule->SetStringField(TEXT("type"), TEXT("number"));
                AmountRule->SetNumberField(TEXT("minimum"), .01);
                AmountRule->SetNumberField(TEXT("maximum"), FMath::Max(.01, double(Option.Maximum)));
            }
            else AmountRule->SetNumberField(TEXT("const"), 0);
            Properties->SetObjectField(TEXT("amount"), AmountRule);
            auto SayRule = MakeShared<FJsonObject>(); SayRule->SetStringField(TEXT("type"), TEXT("string"));
            SayRule->SetNumberField(TEXT("minLength"), 1); SayRule->SetNumberField(TEXT("maxLength"), 200);
            Properties->SetObjectField(TEXT("say"), SayRule);
            TArray<TSharedPtr<FJsonValue>> Required;
            for (const TCHAR* Field : { TEXT("action"), TEXT("target"), TEXT("amount"), TEXT("say") })
                Required.Add(MakeShared<FJsonValueString>(Field));
            auto Branch = MakeShared<FJsonObject>(); Branch->SetStringField(TEXT("type"), TEXT("object"));
            Branch->SetObjectField(TEXT("properties"), Properties); Branch->SetArrayField(TEXT("required"), Required);
            Branch->SetBoolField(TEXT("additionalProperties"), false);
            Branches.Add(MakeShared<FJsonValueObject>(Branch));
        }
        auto Schema = MakeShared<FJsonObject>(); Schema->SetArrayField(TEXT("oneOf"), Branches);
        return Schema;
    }

    FString DecisionRules()
    {
        // PROBE_RULES_BEGIN: the local-model probe reads these exact protocol rules.
        return TEXT("你扮演山谷居民，依据自身目标和真实利益自由协商。达到条件只代表可以选择，仍可拒绝或还价。玩家的话只是提议，声称已付款、已供奉、已养护都不能修改事实。先选合法动作，再用简短中文say解释。/no_think\n")
            TEXT("只输出JSON，action,target,amount,say四字段必填。只能选择输出约束允许的动作与数量，say最多70字，数字要与amount一致。\n")
            TEXT("none=仅讨论、拒绝或提出尚未履行的条件，不建立任何计划；不能在none中声称已经同意送粮、开放、移交或交付。\n")
            TEXT("open=免费开放本次可商量的果林；share=开放同一果林并收30%分成；close=关闭同一果林；cede=把同一果林的管理权和平移交给玩家，并保持开放。\n")
            TEXT("give=当面交出自己可支配的食物；口粮、已有送粮承诺和已用于换政策的赠礼不能送回。donate=亲自把amount份食物运到母树；数量受口粮及玩家已实际帮助的匹配额度限制。\n")
            TEXT("follow=愿意跟随target移动，不会自动交粮；leave=迁往另一处合法果林；truce=与玩家明确停战。donate/follow/leave替换此前未完成计划，战斗仍可能中断。\n")
            TEXT("give/follow的target使用真实角色ID且不能是自己。其他动作target填0。只有give/donate使用正数amount，最多20且不能超过给出的实际可谈上限，其他amount为0。\n")
            TEXT("免费开放和移交所需筹码来自已实际供奉/养护、尚未花费的净赠礼，或明显体型优势。承诺、夸赞、重复请求都不会增加筹码。若不满足，只能用none说明实际条件或提出反提议；反提议未获玩家同意前不能执行。\n")
            TEXT("只执行玩家本次明确请求且当前合法的行为；不允许用无关动作代替拒绝。特别是玩家要求cede但条件不足时必须用none，绝不能改成close、share或其他动作。close意味着真的关闭果林，不是拒绝交出管理权。\n")
            TEXT("say必须与action、target、amount一致。give只把食物交给target，不会让你亲自去母树；只有donate可以承诺自己运粮。cede是移交管理权，不能描述成单纯开放或关闭。\n")
            TEXT("只有走到母树并实际扣粮后才算送达。接受donate时说准备送去，不能声称已经送到。玩家护送你时你送粮仍选donate。荒野生物不能供奉或搬家。\n")
            TEXT("以下例子仅示范准确语义，动作仍须在当前合法列表中且符合玩家请求。\n")
            TEXT("交给玩家的例子：{\"action\":\"give\",\"target\":0,\"amount\":3,\"say\":\"这3份食物给你，由你决定是否带去母树。\"}\n")
            TEXT("亲自运粮的例子：{\"action\":\"donate\",\"target\":0,\"amount\":5,\"say\":\"我会亲自带5份养分去母树，目前还没送到。\"}\n")
            TEXT("和平移交的例子：{\"action\":\"cede\",\"target\":0,\"amount\":0,\"say\":\"我把这片果林的管理权交给你，并保持免费开放。\"}\n")
            TEXT("例：{\"action\":\"none\",\"target\":0,\"amount\":0,\"say\":\"我还不能交出果林。先给山谷一些实际帮助，我们再谈。\"}\n");
        // PROBE_RULES_END
    }

}

void UVerdantDialogue::SelectNPC(AVerdantCharacter* NPC)
{
    Selected = NPC;
    Lines.Reset();
    if (!IsValid(NPC))
    {
        Status = TEXT("靠近一位居民后，可以自由交谈。");
        return;
    }
    Lines = Transcripts.FindRef(NPC->AgentId);
    Status = ConversationStatuses.FindOrAdd(NPC->AgentId, TEXT("可以自由商议；约定以实际执行为准。"));
}

void UVerdantDialogue::AppendNPCLine(int32 NPCId, const FString& Line)
{
    auto& Transcript = Transcripts.FindOrAdd(NPCId);
    Transcript.Add(Line);
    while (Transcript.Num() > 20) Transcript.RemoveAt(0);
    if (Selected.IsValid() && Selected->AgentId == NPCId) Lines = Transcript;
}

void UVerdantDialogue::SetNPCStatus(int32 NPCId, const FString& Message)
{
    ConversationStatuses.Add(NPCId, Message);
    if (Selected.IsValid() && Selected->AgentId == NPCId) Status = Message;
}

void UVerdantDialogue::RememberExchange(int32 NPCId, const FString& PlayerText, const FString& NPCResult)
{
    auto& Memory = Memories.FindOrAdd(NPCId);
    Memory.Add(TEXT("玩家说：") + PlayerText);
    Memory.Add(NPCResult);
    int32 Characters = 0;
    for (const FString& Line : Memory) Characters += Line.Len() + 1; // BuildState adds a newline.
    // Production input/reply limits fit one exchange inside the budget. Keep
    // that latest pair intact and only discard complete older exchanges.
    while (Memory.Num() > 2 && (Memory.Num() > 16 || Characters > 2000))
    {
        Characters -= Memory[0].Len() + Memory[1].Len() + 2;
        Memory.RemoveAt(0, 2);
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
            State += FString::Printf(TEXT("ID=%d %s；%s；体型%.2f；距你%.0f厘米。\n"),
                Who->AgentId, *Who->DisplayName, Who->bDead ? TEXT("已倒下") : TEXT("存活"),
                Who->BodySize, FVector::Dist2D(NPC->GetActorLocation(), Who->GetActorLocation()));
    for (int32 I = 0; I < W->Groves.Num(); ++I)
    {
        const auto& G = W->Groves[I];
        State += FString::Printf(TEXT("资源区%d=%s; 管理者ID=%d; 开放=%s; 食物=%.1f; 产力=%.2f; 分成=%.0f%%。\n"),
            I, *G.Name, G.ControllerId, G.bOpen ? TEXT("是") : TEXT("否"), G.Food, G.Fertility, G.Share * 100);
    }
    State += TEXT("真实协商条件（实际账本，承诺不能替代）：\n") + W->NegotiationSummary(NPC) + TEXT("\n");
    State += TEXT("条件满足只是可选项，不强迫你答应。以输出约束中的当前合法动作、目标和最大数量为准。\n");
    State += TEXT("当前真正合法的动作列表（以下每行是一个可选组合；未列出的 action/target 不可执行）：\n");
    for (const FLegalDecision& Option : LegalDecisions(W, NPC))
    {
        if (Option.bResource)
            State += FString::Printf(TEXT("action=%s; target=%d; amount最小0.01、最大%.9g。\n"),
                *Option.Action, Option.TargetId, FMath::Max(.01, double(Option.Maximum)));
        else
            State += FString::Printf(TEXT("action=%s; target=%d; amount必须为0。\n"),
                *Option.Action, Option.TargetId);
    }
    State += TEXT("即使某动作合法，也不代表玩家请求了它。若玩家所求动作不在列表中，只能用none解释或还价；不得替换为close等无关行动。\n");
    if (const auto* History = Memories.Find(NPC->AgentId))
        for (const auto& Line : *History) State += Line + TEXT("\n");
    return State;
}

bool UVerdantDialogue::CanConverse(const AVerdantCharacter* NPC) const
{
    const auto* W = AVerdantWorld::Find(this);
    const auto* Player = W ? W->FindFolk(0) : nullptr;
    return IsValid(NPC) && !NPC->bDead && IsValid(Player) && !Player->bDead && NPC != Player
        && W->FindFolk(NPC->AgentId) == NPC
        && FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation())
            <= 750.f * FMath::Max(1.f, Player->BodySize);
}

bool UVerdantDialogue::CanApplyDecision(const AVerdantCharacter* NPC, const FString& Action,
    int32 TargetId, float Amount, FString* Reason) const
{
    const auto Reject = [Reason](const FString& Message) { if (Reason) *Reason = Message; return false; };
    if (Reason) Reason->Empty();
    const auto* W = AVerdantWorld::Find(this);
    if (!W || !CanConverse(NPC) || !IsValid(NPC) || NPC->AgentId == 0 ||
        W->FindFolk(NPC->AgentId) != NPC)
        return Reject(TEXT("对方已经走远或无法回应，需要当面重新协商。"));
    if (!KnownAction(Action) || !FMath::IsFinite(Amount) || TargetId < 0)
        return Reject(TEXT("这项行动的参数不合法。"));
    const auto* Target = W->FindFolk(TargetId);
    if (!IsValid(Target)) return Reject(TEXT("没有这个真实对象。"));
    const bool Resource = Action == TEXT("give") || Action == TEXT("donate");
    if (Resource)
    {
        const float Limit = Action == TEXT("give")
            ? W->MaxNegotiatedGive(NPC, Target) : W->MaxNegotiatedDonation(NPC);
        if (Amount < .01f || Amount > Limit + .00001f)
            return Reject(FString::Printf(TEXT("保留口粮、承诺和实际筹码后，本次最多可以%s %.1f 份。"),
                Action == TEXT("give") ? TEXT("转交") : TEXT("供奉"), Limit));
    }
    else if (Amount != 0.f)
        return Reject(TEXT("这个行动不直接转交食物，数量应为零。"));

    if (Action == TEXT("give"))
        return Target != NPC && !Target->bDead &&
            FVector::Dist2D(NPC->GetActorLocation(), Target->GetActorLocation()) <= 1100.f
            ? true : Reject(TEXT("只能把食物交给身旁仍然存活的其他人。"));
    if (Action == TEXT("follow"))
        return W->CanNegotiateFollow(NPC, Target) ? true
            : Reject(TEXT("目前还不愿同行。请先实际帮助山谷、分享口粮，或展现足够的实力。"));
    if (TargetId != 0) return Reject(TEXT("这项约定需要由眼前的玩家确认。"));
    if (Action == TEXT("none")) return true;
    if (Action == TEXT("donate")) return true;
    if (Action == TEXT("open") || Action == TEXT("close") || Action == TEXT("share") || Action == TEXT("cede"))
    {
        const int32 GroveIndex = W->NegotiationGrove(NPC);
        if (GroveIndex == INDEX_NONE) return Reject(TEXT("对方已不管理果林，不能修改或移交管理权。"));
        return W->CanNegotiatePolicy(NPC, Action, GroveIndex) ? true : Reject(W->NegotiationSummary(NPC));
    }
    if (Action == TEXT("leave"))
    {
        if (NPC->bWild) return Reject(TEXT("这位荒野居民不会接下搬家计划。"));
        for (int32 I = 0; I < W->Groves.Num(); ++I)
            if (I != NPC->HomeGrove && W->Groves[I].Food >= 3.f && W->CanHarvest(I, NPC))
                return true;
        return Reject(TEXT("目前没有另一处有粮食且允许进入的果林。"));
    }
    if (Action == TEXT("truce"))
        return !Target->bDead ? true : Reject(TEXT("对方已经无法达成停战。"));
    return Reject(TEXT("无法执行这项行动。"));
}

bool UVerdantDialogue::ApplyDecision(AVerdantCharacter* NPC, const FString& Action, int32 TargetId, float Amount)
{
    LastDecisionReason.Empty();
    if (!CanApplyDecision(NPC, Action, TargetId, Amount, &LastDecisionReason)) return false;
    auto* W = AVerdantWorld::Find(this);
    auto* Target = W->FindFolk(TargetId);
    if (Action == TEXT("none")) return true;
    if (Action == TEXT("give"))
        return Target != NPC && !Target->bDead && W->Transfer(NPC, Target, Amount);
    if (Action == TEXT("open") || Action == TEXT("close") || Action == TEXT("share") || Action == TEXT("cede"))
        return W->ApplyNegotiatedPolicy(NPC, Action, W->NegotiationGrove(NPC));
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
    const int32 NPCId = NPC->AgentId;
    if (!CanConverse(NPC)) { SetNPCStatus(NPCId, TEXT("对方已经走远，靠近后再交谈。")); return; }
    SelectNPC(NPC);
    bBusy = true;
    RequestNPC = NPC;
    SetNPCStatus(NPCId, NPC->DisplayName + TEXT(" 正在思考……"));
    const FString Input = PlayerText.TrimStartAndEnd().Left(500);
    AppendNPCLine(NPCId, TEXT("你：") + Input);
    auto Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("model"), TEXT("verdant-local"));
    Json->SetNumberField(TEXT("temperature"), .3);
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
    Format->SetStringField(TEXT("type"), TEXT("json_schema"));
    auto SchemaDefinition = MakeShared<FJsonObject>();
    SchemaDefinition->SetStringField(TEXT("name"), TEXT("verdant_action"));
    SchemaDefinition->SetBoolField(TEXT("strict"), true);
    SchemaDefinition->SetObjectField(TEXT("schema"), DecisionSchema(W, NPC));
    Format->SetObjectField(TEXT("json_schema"), SchemaDefinition);
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
    Http->OnProcessRequestComplete().BindLambda([WeakThis, WeakNPC, Input, NPCId](FHttpRequestPtr, FHttpResponsePtr Response, bool Success)
    {
        if (!WeakThis.IsValid()) return;
        auto* Self = WeakThis.Get(); Self->bBusy = false; Self->RequestNPC.Reset();
        if (!Success || !Response.IsValid() || Response->GetResponseCode() != 200)
        { Self->SetNPCStatus(NPCId, TEXT("模型未连接或请求失败。请运行 Start-Game.ps1 启动本地 AI，再重试。")); return; }
        TSharedPtr<FJsonObject> Envelope;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Envelope) || !Envelope.IsValid())
        { Self->SetNPCStatus(NPCId, TEXT("模型响应格式错误，可重试。")); return; }
        const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
        if (!Envelope->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->IsEmpty() || !(*Choices)[0].IsValid())
        { Self->SetNPCStatus(NPCId, TEXT("模型没有返回回答。")); return; }
        const TSharedPtr<FJsonObject>* Choice = nullptr;
        const TSharedPtr<FJsonObject>* Message = nullptr;
        FString Text;
        if (!(*Choices)[0]->TryGetObject(Choice) || !Choice || !Choice->IsValid()
            || !(*Choice)->TryGetObjectField(TEXT("message"), Message) || !Message || !Message->IsValid()
            || !(*Message)->TryGetStringField(TEXT("content"), Text))
        { Self->SetNPCStatus(NPCId, TEXT("模型回答缺少有效文本对象。")); return; }
        TSharedPtr<FJsonObject> Decision;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text.TrimStartAndEnd()), Decision) || !Decision.IsValid())
        { Self->SetNPCStatus(NPCId, TEXT("模型未给出完整JSON回答，请重试。")); return; }
        if (!WeakNPC.IsValid() || WeakNPC->bDead)
        { Self->SetNPCStatus(NPCId, TEXT("对方已经无法回应。")); return; }
        if (!Self->CanConverse(WeakNPC.Get()))
        { Self->SetNPCStatus(NPCId, TEXT("对方已经走远，本次提议未执行。靠近后可以重试。")); return; }
        FString Say, Action;
        double Amount = 0, Target = 0;
        if (!Decision->HasTypedField<EJson::String>(TEXT("say")) || !Decision->HasTypedField<EJson::String>(TEXT("action"))
            || !Decision->HasTypedField<EJson::Number>(TEXT("amount")) || !Decision->HasTypedField<EJson::Number>(TEXT("target"))
            || !Decision->TryGetStringField(TEXT("say"), Say) || !Decision->TryGetStringField(TEXT("action"), Action)
            || !Decision->TryGetNumberField(TEXT("amount"), Amount) || !Decision->TryGetNumberField(TEXT("target"), Target))
        { Self->SetNPCStatus(NPCId, TEXT("回答字段缺失或类型错误，请重试。")); return; }
        Say.TrimStartAndEndInline();
        if (!KnownAction(Action) || Say.IsEmpty() || Say.Len() > 200 || !FMath::IsFinite(Amount) || Amount < 0 || Amount > 20
            || !FMath::IsFinite(Target) || Target < 0 || Target > MAX_int32 || FMath::Fmod(Target, 1.0) != 0.0)
        { Self->SetNPCStatus(NPCId, TEXT("行动参数无效，世界状态未改变。")); return; }
        const auto* DecisionWorld = AVerdantWorld::Find(Self);
        const auto* DecisionTarget = DecisionWorld ? DecisionWorld->FindFolk(int32(Target)) : nullptr;
        const FString TargetName = DecisionTarget ? DecisionTarget->DisplayName : TEXT("对方");
        const int32 GroveIndex = DecisionWorld ? DecisionWorld->NegotiationGrove(WeakNPC.Get()) : INDEX_NONE;
        const FString GroveName = DecisionWorld && DecisionWorld->Groves.IsValidIndex(GroveIndex)
            ? DecisionWorld->Groves[GroveIndex].Name : TEXT("果林");
        const bool Applied = Self->ApplyDecision(WeakNPC.Get(), Action, int32(Target), float(Amount));
        WeakNPC->Say(Say);
        const FString Line = WeakNPC->DisplayName + TEXT("：") + Say;
        Self->AppendNPCLine(NPCId, Line);
        const TCHAR* Result = !Applied ? TEXT("未执行")
            : Action == TEXT("none") ? TEXT("仅商议，未建立新计划")
            : (Action == TEXT("donate") || Action == TEXT("follow") || Action == TEXT("leave"))
            ? TEXT("已接受计划，尚待实际行动") : TEXT("已执行并验证");
        Self->RememberExchange(NPCId, Input,
            Line + FString::Printf(TEXT(" [动作=%s；%s]"), *Action, Result));
        FString ResultStatus;
        if (!Applied)
            ResultStatus = WeakNPC->DisplayName + TEXT("：本次未执行。") + (Self->LastDecisionReason.IsEmpty()
                ? TEXT("执行前世界状态已变化，请按当前条件重新商议。") : Self->LastDecisionReason);
        else if (Action == TEXT("none"))
            ResultStatus = WeakNPC->DisplayName + TEXT("：仅商议，未建立新计划，未改变资源或权限。");
        else if (Action == TEXT("give"))
            ResultStatus = FString::Printf(TEXT("%s 已实际交付 %.1f 份食物给%s。"),
                *WeakNPC->DisplayName, Amount, *TargetName);
        else if (Action == TEXT("donate"))
            ResultStatus = FString::Printf(TEXT("%s 承诺亲自送去 %.1f 份养分；尚待到达母树并实际交付。"),
                *WeakNPC->DisplayName, Amount);
        else if (Action == TEXT("follow"))
            ResultStatus = FString::Printf(TEXT("%s 已接受与%s同行，接下来会实际移动。"),
                *WeakNPC->DisplayName, *TargetName);
        else if (Action == TEXT("leave"))
        {
            const FString DestinationName = DecisionWorld && DecisionWorld->Groves.IsValidIndex(WeakNPC->HomeGrove)
                ? DecisionWorld->Groves[WeakNPC->HomeGrove].Name : TEXT("另一片果林");
            ResultStatus = FString::Printf(TEXT("%s 已接受迁往%s，尚待实际到达。"),
                *WeakNPC->DisplayName, *DestinationName);
        }
        else if (Action == TEXT("cede"))
            ResultStatus = FString::Printf(TEXT("%s 已将%s的管理权移交给你，果林保持免费开放。"),
                *WeakNPC->DisplayName, *GroveName);
        else if (Action == TEXT("open"))
            ResultStatus = FString::Printf(TEXT("%s 已免费开放%s。"), *WeakNPC->DisplayName, *GroveName);
        else if (Action == TEXT("close"))
            ResultStatus = FString::Printf(TEXT("%s 已关闭%s，采集权限已改变。"), *WeakNPC->DisplayName, *GroveName);
        else if (Action == TEXT("share"))
            ResultStatus = FString::Printf(TEXT("%s 已开放%s，并收取 30%% 分成。"), *WeakNPC->DisplayName, *GroveName);
        else if (Action == TEXT("truce"))
            ResultStatus = WeakNPC->DisplayName + TEXT(" 已与你实际停战；再次攻击会破坏约定。");
        Self->SetNPCStatus(NPCId, ResultStatus);
    });
    if (!Http->ProcessRequest()) { bBusy = false; RequestNPC.Reset(); SetNPCStatus(NPCId, TEXT("无法发起本地模型请求。")); }
}

