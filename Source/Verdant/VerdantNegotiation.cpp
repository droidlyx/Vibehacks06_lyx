// Execution limits and observed facts. The API decides willingness and terms;
// text alone is never evidence of a payment or deed.
#include "VerdantWorld.h"
#include "VerdantCharacter.h"

namespace
{
    bool RegisteredAlive(const AVerdantWorld* World, const AVerdantCharacter* Person)
    {
        return IsValid(World) && IsValid(Person) && !Person->bDead &&
            World->FindFolk(Person->AgentId) == Person;
    }
}

float AVerdantWorld::NetPlayerGift(int32 NpcId) const
{
    if (NpcId <= 0) return 0.f;
    return TransferLedger.FindRef(FString::Printf(TEXT("0:%d"), NpcId)) -
        TransferLedger.FindRef(FString::Printf(TEXT("%d:0"), NpcId));
}

float AVerdantWorld::AvailableGiftCredit(int32 NpcId) const
{
    return FMath::Max(0.f, NetPlayerGift(NpcId) - SpentGiftCredit.FindRef(NpcId));
}

float AVerdantWorld::NegotiationReserve(const AVerdantCharacter* NPC) const
{
    // Preserve the ordinary NPC brain's stocking targets. These are not
    // mandatory deductions or consent gates for an API-directed interaction.
    if (!NPC || NPC->AgentId == 0) return 0.f;
    if (NPC->bWild) return NPC->AgentId == 4 ? 16.f : NPC->AgentId == 5 ? 24.f : 36.f;
    return NPC->AgentId == 1 ? 6.f : NPC->AgentId == 2 ? 12.f : 18.f;
}

float AVerdantWorld::MaxNegotiatedGive(const AVerdantCharacter* NPC, const AVerdantCharacter* Target) const
{
    if (!RegisteredAlive(this, NPC) || !RegisteredAlive(this, Target) || NPC == Target ||
        NPC->AgentId == 0 || !FMath::IsFinite(NPC->Food))
        return 0.f;
    if (Target->FoodTier() < NPC->FoodTier()) return 0.f;
    const float Capacity = Target->FreeFoodCapacity();
    if (!FMath::IsFinite(Capacity)) return 0.f;
    return FMath::Min3(20.f, FMath::Max(0.f, Capacity), FMath::Max(0.f, NPC->Food));
}

float AVerdantWorld::MaxNegotiatedDonation(const AVerdantCharacter* NPC) const
{
    if (!RegisteredAlive(this, NPC) || NPC->AgentId == 0 || bRestored || !FMath::IsFinite(NPC->Food))
        return 0.f;
    // A new pledge replaces the old plan; actual food is deducted on arrival.
    return FMath::Min3(20.f, FMath::Max(0.f, NPC->Food), FMath::Max(0.f, TreeGoal - TreeNutrition));
}

int32 AVerdantWorld::NegotiationGrove(const AVerdantCharacter* NPC) const
{
    if (!RegisteredAlive(this, NPC)) return INDEX_NONE;
    if (Groves.IsValidIndex(NPC->HomeGrove) && Groves[NPC->HomeGrove].ControllerId == NPC->AgentId)
        return NPC->HomeGrove;
    int32 Best = INDEX_NONE;
    float Distance = BIG_NUMBER;
    for (int32 I = 0; I < Groves.Num(); ++I)
    {
        if (Groves[I].ControllerId != NPC->AgentId) continue;
        const float Current = FVector::DistSquared2D(NPC->GetActorLocation(), Groves[I].Position);
        if (Current < Distance) { Best = I; Distance = Current; }
    }
    return Best;
}

bool AVerdantWorld::CanNegotiatePolicy(const AVerdantCharacter* NPC, const FString& Action,
    int32 GroveIndex, float* OutGiftCost) const
{
    if (OutGiftCost) *OutGiftCost = 0.f;
    const auto* Player = FindFolk(0);
    if (!RegisteredAlive(this, NPC) || !RegisteredAlive(this, Player) || NPC->AgentId == 0 ||
        !Groves.IsValidIndex(GroveIndex) || Groves[GroveIndex].ControllerId != NPC->AgentId ||
        FVector::Dist(NPC->GetActorLocation(), Player->GetActorLocation()) > 750.f * FMath::Max(1.f, Player->BodySize))
        return false;
    return Action == TEXT("close") || Action == TEXT("open") ||
        Action == TEXT("share") || Action == TEXT("cede");
}

bool AVerdantWorld::ApplyNegotiatedPolicy(AVerdantCharacter* NPC, const FString& Action, int32 GroveIndex)
{
    if (!CanNegotiatePolicy(NPC, Action, GroveIndex)) return false;
    bool Applied = false;
    if (Action == TEXT("cede"))
        Applied = TransferControl(GroveIndex, NPC, FindFolk(0));
    else if (Action == TEXT("open"))
    {
        if (Groves[GroveIndex].bOpen && Groves[GroveIndex].Share <= .001f)
            return true;
        Applied = SetAccess(GroveIndex, NPC, true, 0.f);
    }
    else if (Action == TEXT("close"))
        Applied = SetAccess(GroveIndex, NPC, false, 0.f);
    else if (Action == TEXT("share"))
        Applied = SetAccess(GroveIndex, NPC, true, .3f);

    return Applied;
}

bool AVerdantWorld::CanNegotiateFollow(const AVerdantCharacter* NPC, const AVerdantCharacter* Target) const
{
    if (!RegisteredAlive(this, NPC) || !RegisteredAlive(this, Target) || NPC == Target ||
        NPC->AgentId == 0)
        return false;
    // Reject both a newly closed loop and a pre-existing corrupt loop in the
    // requested leader's chain. Dead or missing leaders end the effective chain.
    TSet<int32> SeenLeaders;
    const AVerdantCharacter* Leader = Target;
    for (int32 Link = 0; RegisteredAlive(this, Leader) && Link < Folk.Num(); ++Link)
    {
        if (Leader == NPC || SeenLeaders.Contains(Leader->AgentId)) return false;
        SeenLeaders.Add(Leader->AgentId);
        Leader = FindFolk(Leader->FollowId);
    }
    if (RegisteredAlive(this, Leader)) return false;

    return true;
}

FString AVerdantWorld::NegotiationSummary(const AVerdantCharacter* NPC) const
{
    if (!RegisteredAlive(this, NPC)) return TEXT("对方已无法协商。");
    FString Summary = FString::Printf(TEXT("真实记录：我持有 %.1f 份食物，剩余容量 %.1f 份，尚未完成的运送计划 %.1f 份。\n")
        TEXT("玩家累计实际供奉 %.1f、养护 %.1f；我累计收到玩家赠粮 %.1f、交给玩家 %.1f；我累计实际供奉 %.1f。"),
        NPC->Food, NPC->FreeFoodCapacity(), NPC->DonationPledge,
        DonationLedger.FindRef(0), TendLedger.FindRef(0),
        TransferLedger.FindRef(FString::Printf(TEXT("0:%d"), NPC->AgentId)),
        TransferLedger.FindRef(FString::Printf(TEXT("%d:0"), NPC->AgentId)),
        DonationLedger.FindRef(NPC->AgentId));
    const int32 GroveIndex = NegotiationGrove(NPC);
    if (Groves.IsValidIndex(GroveIndex))
    {
        const auto& Grove = Groves[GroveIndex];
        Summary += FString::Printf(TEXT("\n我控制的当前果林：%s；现为%s，实际分成 %.0f%%。"),
            *Grove.Name, Grove.bOpen ? TEXT("开放") : TEXT("关闭"), Grove.Share * 100.f);
    }
    else Summary += TEXT("\n我当前没有控制果林。");
    return Summary;
}
