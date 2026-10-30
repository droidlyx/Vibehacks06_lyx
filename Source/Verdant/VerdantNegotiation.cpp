// Authoritative bargaining limits. Text is never evidence of a payment or deed.
#include "VerdantWorld.h"
#include "VerdantCharacter.h"

namespace
{
    struct FSocialTerms
    {
        float Reserve, Charity, DonationBase, DonationMatch;
        float OpenHelp, OpenGift, CedeHelp, CedeGift;
        float FollowHelp, FollowGift;
    };

    FSocialTerms Terms(const AVerdantCharacter* NPC)
    {
        if (!NPC) return {0,0,0,0,0,0,0,0,0,0};
        if (NPC->bWild)
        {
            const float Reserve = NPC->AgentId == 4 ? 16.f : NPC->AgentId == 5 ? 24.f : 36.f;
            return {Reserve,0,0,0,30,12,50,25,0,5};
        }
        if (NPC->AgentId == 1) return {6,3,5,.5f,0,0,15,10,5,2};
        if (NPC->AgentId == 2) return {12,0,0,.5f,15,8,30,15,10,5};
        return {18,0,0,.75f,30,12,50,25,20,8};
    }

    bool RegisteredAlive(const AVerdantWorld* World, const AVerdantCharacter* Person)
    {
        return IsValid(World) && IsValid(Person) && !Person->bDead &&
            World->FindFolk(Person->AgentId) == Person;
    }

    bool PeacefulWithPlayer(const AVerdantWorld* World, const AVerdantCharacter* NPC)
    {
        const auto* Player = World->FindFolk(0);
        return NPC->HostileId != 0 && NPC->Trust.FindRef(0) >= 0.f &&
            (!RegisteredAlive(World, Player) || Player->HostileId != NPC->AgentId);
    }

    float PublicHelp(const AVerdantWorld* World)
    {
        return FMath::Max(0.f, World->DonationLedger.FindRef(0)) +
            FMath::Max(0.f, World->TendLedger.FindRef(0));
    }

    bool HasSizeAdvantage(const AVerdantWorld* World, const AVerdantCharacter* NPC, float Ratio)
    {
        const auto* Player = World->FindFolk(0);
        return RegisteredAlive(World, Player) &&
            Player->BodySize + .0001f >= NPC->BodySize * Ratio;
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
    return NPC && NPC->AgentId != 0 ? Terms(NPC).Reserve : 0.f;
}

float AVerdantWorld::MaxNegotiatedGive(const AVerdantCharacter* NPC, const AVerdantCharacter* Target) const
{
    if (!RegisteredAlive(this, NPC) || !RegisteredAlive(this, Target) || NPC == Target ||
        NPC->AgentId == 0 || !PeacefulWithPlayer(this, NPC))
        return 0.f;
    const FSocialTerms T = Terms(NPC);
    float Allowed = FMath::Min(20.f, FMath::Max(0.f, NPC->Food - T.Reserve - NPC->DonationPledge));
    if (Target->AgentId == 0)
    {
        // Initial charity is a lifetime NET allowance. A returned gift restores
        // unused credit, but payments already spent on concessions cannot be refunded.
        const float Refundable = FMath::Max(0.f, T.Charity + NetPlayerGift(NPC->AgentId)
            - SpentGiftCredit.FindRef(NPC->AgentId));
        Allowed = FMath::Min(Allowed, Refundable);
    }
    return Allowed;
}

float AVerdantWorld::MaxNegotiatedDonation(const AVerdantCharacter* NPC) const
{
    if (!RegisteredAlive(this, NPC) || NPC->AgentId == 0 || NPC->bWild ||
        bRestored || !PeacefulWithPlayer(this, NPC))
        return 0.f;
    const FSocialTerms T = Terms(NPC);
    const float RemainingMatch = FMath::Max(0.f, T.DonationBase + PublicHelp(this) * T.DonationMatch
        - DonationLedger.FindRef(NPC->AgentId));
    // A new pledge REPLACES the old pledge, so do not subtract it twice.
    return FMath::Min3(20.f, FMath::Max(0.f, NPC->Food - T.Reserve), RemainingMatch);
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
        FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation()) > 750.f * FMath::Max(1.f, Player->BodySize))
        return false;
    if (Action == TEXT("close")) return true;
    if (Action == TEXT("open") && Groves[GroveIndex].bOpen && Groves[GroveIndex].Share <= .001f)
        return true; // Existing free access is an idempotent no-op, even with no new credit.
    if (!PeacefulWithPlayer(this, NPC)) return false;
    if (Action == TEXT("share")) return true;
    if (Action != TEXT("open") && Action != TEXT("cede")) return false;

    const FSocialTerms T = Terms(NPC);
    const bool Ceding = Action == TEXT("cede");
    const float RequiredHelp = Ceding ? T.CedeHelp : T.OpenHelp;
    const float RequiredGift = Ceding ? T.CedeGift : T.OpenGift;
    if (PublicHelp(this) + .0001f >= RequiredHelp ||
        HasSizeAdvantage(this, NPC, Ceding ? 1.5f : 1.3f))
        return true;
    if (AvailableGiftCredit(NPC->AgentId) + .0001f >= RequiredGift)
    {
        if (OutGiftCost) *OutGiftCost = RequiredGift;
        return true;
    }
    return false;
}

bool AVerdantWorld::ApplyNegotiatedPolicy(AVerdantCharacter* NPC, const FString& Action, int32 GroveIndex)
{
    float GiftCost = 0.f;
    if (!CanNegotiatePolicy(NPC, Action, GroveIndex, &GiftCost)) return false;
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

    // The authoritative change and this debit run synchronously on the game thread.
    // A rejected transfer never consumes bargaining credit.
    if (Applied && GiftCost > 0.f)
    {
        SpentGiftCredit.FindOrAdd(NPC->AgentId) += GiftCost;
        Record(FString::Printf(TEXT("%s 认可你先前实际交付的 %.1f 份赠礼，履行这次约定。"),
            *NPC->DisplayName, GiftCost));
    }
    return Applied;
}

bool AVerdantWorld::CanNegotiateFollow(const AVerdantCharacter* NPC, const AVerdantCharacter* Target) const
{
    if (!RegisteredAlive(this, NPC) || !RegisteredAlive(this, Target) || NPC == Target ||
        NPC->AgentId == 0 || !PeacefulWithPlayer(this, NPC))
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

    const auto* Player = FindFolk(0);
    if (!RegisteredAlive(this, Player)) return false;
    if (NPC->bWild)
        return HasSizeAdvantage(this, NPC, 1.1f) ||
            (AvailableGiftCredit(NPC->AgentId) >= 5.f && Player->BodySize + .0001f >= NPC->BodySize * .65f);
    const FSocialTerms T = Terms(NPC);
    return PublicHelp(this) + .0001f >= T.FollowHelp ||
        AvailableGiftCredit(NPC->AgentId) + .0001f >= T.FollowGift ||
        HasSizeAdvantage(this, NPC, 1.3f);
}

FString AVerdantWorld::NegotiationSummary(const AVerdantCharacter* NPC) const
{
    if (!RegisteredAlive(this, NPC)) return TEXT("对方已无法协商。");
    const auto* Player = FindFolk(0);
    const FSocialTerms T = Terms(NPC);
    FString Summary = FString::Printf(TEXT("留作口粮 %.1f 份；目前可赠予你 %.1f 份，可亲自供奉 %.1f 份。\n")
        TEXT("你已实际供奉 %.1f、养护 %.1f；给我的净赠礼 %.1f，其中尚可用于约定 %.1f。"),
        T.Reserve, MaxNegotiatedGive(NPC, Player), MaxNegotiatedDonation(NPC),
        DonationLedger.FindRef(0), TendLedger.FindRef(0),
        NetPlayerGift(NPC->AgentId), AvailableGiftCredit(NPC->AgentId));
    if (!PeacefulWithPlayer(this, NPC))
        return Summary + TEXT("\n目前彼此敌对，需要先停战。");
    const int32 GroveIndex = NegotiationGrove(NPC);
    if (Groves.IsValidIndex(GroveIndex))
    {
        Summary += TEXT("\n本次可商量的果林：") + Groves[GroveIndex].Name + TEXT("。");
        if (CanNegotiatePolicy(NPC, TEXT("open"), GroveIndex))
            Summary += TEXT("可以考虑免费开放。");
        else
            Summary += FString::Printf(TEXT("免费开放需实际供奉/养护累计 %.0f 份，或再交付可用赠礼至 %.0f 份；明显体型优势也可作为筹码。"),
                T.OpenHelp, T.OpenGift);
        if (CanNegotiatePolicy(NPC, TEXT("cede"), GroveIndex))
            Summary += TEXT("也可以考虑和平移交管理权。");
        else
            Summary += FString::Printf(TEXT("移交管理权需实际供奉/养护累计 %.0f 份，或可用赠礼 %.0f 份；也会考虑足够明显的体型优势。"),
                T.CedeHelp, T.CedeGift);
    }
    Summary += CanNegotiateFollow(NPC, Player)
        ? TEXT("\n具备商量同行的条件，是否同行由我决定。")
        : TEXT("\n同行前，请先分享口粮或实际帮助山谷。");
    return Summary;
}
