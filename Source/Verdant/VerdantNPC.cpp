#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "VerdantDialogue.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

namespace
{
    bool Alive(const AVerdantCharacter* Who)
    {
        return IsValid(Who) && !Who->bDead;
    }

    FVector WildHome(int32 AgentId)
    {
        if (AgentId == 4) return FVector(-1800.f, -300.f, 0.f);
        if (AgentId == 5) return FVector(2800.f, 3000.f, 0.f);
        return FVector(-3300.f, 6200.f, 0.f);
    }
}

void AVerdantCharacter::MoveToward(const FVector& Goal, float Acceptance)
{
    if (bDead || !GetCharacterMovement())
        return;
    const FVector Here = GetActorLocation();
    FVector Direction = (Goal - Here).GetSafeNormal2D();
    if (FVector::DistSquared2D(Here, Goal) <= FMath::Square(Acceptance))
    {
        GetCharacterMovement()->StopMovementImmediately();
        return;
    }

    const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(VerdantNPCMove), false, this);
    if (const auto* W = Valley())
    {
        for (AVerdantCharacter* Other : W->Folk)
        {
            if (!IsValid(Other) || Other == this)
                continue;
            Params.AddIgnoredActor(Other);
            if (Other->bDead)
                continue;
            const FVector Difference = Here - Other->GetActorLocation();
            const float Clearance = Radius + Other->GetCapsuleComponent()->GetScaledCapsuleRadius() + 35.f;
            const float Distance = Difference.Size2D();
            if (Distance > 1.f && Distance < Clearance)
                Direction += Difference.GetSafeNormal2D() * (1.f - Distance / Clearance) * 1.4f;
        }
    }
    Direction = Direction.GetSafeNormal2D();
    const float ProbeDistance = FMath::Max(110.f, Radius * 2.5f);
    const FCollisionShape Probe = FCollisionShape::MakeSphere(FMath::Max(18.f, Radius * .75f));
    FHitResult Obstacle;
    if (GetWorld()->SweepSingleByChannel(Obstacle, Here, Here + Direction * ProbeDistance,
        FQuat::Identity, ECC_WorldStatic, Probe, Params))
    {
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
        const float Preferred = AgentId % 2 == 0 ? 1.f : -1.f;
        bool bFound = false;
        for (float Sign : { Preferred, -Preferred })
        {
            const FVector Alternative = (Side * Sign + Direction * .22f).GetSafeNormal2D();
            FHitResult SideHit;
            if (!GetWorld()->SweepSingleByChannel(SideHit, Here, Here + Alternative * ProbeDistance,
                FQuat::Identity, ECC_WorldStatic, Probe, Params))
            {
                Direction = Alternative;
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            GetCharacterMovement()->StopMovementImmediately();
            BrainClock = FMath::Min(BrainClock, .15f);
            return;
        }
    }
    AddMovementInput(Direction, 1.f, true);
}

void AVerdantCharacter::TickNPC(float Dt)
{
    auto* W = Valley();
    if (!W || bDead || AgentId == 0)
        return;
    const float Now = GetWorld()->GetTimeSeconds();
    BrainClock = FMath::Max(0.f, BrainClock - Dt);

    // Retaliation is based on actual attacks. Peaceful inhabitants do not seek the player.
    AVerdantCharacter* Enemy = W->FindFolk(HostileId);
    if (!Alive(Enemy) || Enemy == this)
    {
        HostileId = -1;
        CombatTarget.Reset();
        Enemy = nullptr;
    }
    if (!Enemy)
    {
        float BestDistance = bWild ? 560.f : 620.f;
        for (AVerdantCharacter* Other : W->Folk)
        {
            if (!Alive(Other) || Other == this || (bWild && Other->bWild))
                continue;
            const float Distance = FVector::Dist2D(GetActorLocation(), Other->GetActorLocation());
            const bool bKnownThreat = Other->HostileId == AgentId ||
                (Trust.Contains(Other->AgentId) && Trust.FindRef(Other->AgentId) <= -20.f);
            const bool bWildThreat = Other->bWild && !bWild &&
                Trust.FindRef(Other->AgentId) < 10.f && Other->Trust.FindRef(AgentId) < 10.f;
            const bool bHunting = bWild && Trust.FindRef(Other->AgentId) < 10.f;
            if ((bKnownThreat || bWildThreat || bHunting) && Distance < BestDistance)
            {
                Enemy = Other;
                BestDistance = Distance;
            }
        }
        if (Enemy)
        {
            HostileId = Enemy->AgentId;
            CombatTarget = Enemy;
        }
    }

    if (Enemy)
    {
        const float Distance = FVector::Dist2D(GetActorLocation(), Enemy->GetActorLocation());
        if (Distance > 2800.f || (bWild &&
            FVector::Dist2D(GetActorLocation(), WildHome(AgentId)) > 2300.f))
        {
            HostileId = -1;
            CombatTarget.Reset();
            Enemy = nullptr;
            BrainClock = 0.f;
        }
        else
        {
            CombatTarget = Enemy;
            const bool bOutmatched = Enemy->BodySize > BodySize * 1.45f ||
                Health < MaxHealth() * .28f;
            if (bOutmatched)
            {
                FVector Away = (GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
                if (Away.IsNearlyZero()) Away = GetActorRightVector();
                Destination = W->SurfacePoint(GetActorLocation() + Away * 950.f);
                Destination.X = FMath::Clamp(Destination.X, -10000.f, 10000.f);
                Destination.Y = FMath::Clamp(Destination.Y, -10000.f, 10000.f);
                ActionLabel = TEXT("避开更强的威胁");
                MoveToward(Destination, 100.f);
                if (Distance < Enemy->Reach() + 80.f && ActionCooldown <= 0.f)
                {
                    Dodge();
                    ActionCooldown = 3.f;
                }
                return;
            }
            ActionLabel = TEXT("反击与争夺");
            const float StrikeDistance = Reach() + Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() * .7f;
            if (Distance <= StrikeDistance)
            {
                GetCharacterMovement()->StopMovementImmediately();
                SetActorRotation(FRotator(0.f,
                    (Enemy->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
                if (AttackTimer <= 0.f)
                    Attack(AgentId % 2 != 0 && Enemy->BodySize >= BodySize * .8f);
            }
            else
            {
                Destination = Enemy->GetActorLocation();
                MoveToward(Destination, FMath::Max(60.f, StrikeDistance * .8f));
            }
            return;
        }
    }

    // A model response cannot freeze the world, but its addressee waits while replying.
    if (W->Dialogue && W->Dialogue->bBusy && W->Dialogue->Selected.Get() == this)
    {
        ActionLabel = TEXT("思考你的提议");
        GetCharacterMovement()->StopMovementImmediately();
        return;
    }

    if (DonationPledge > .01f && !bWild)
    {
        if (W->bRestored)
        {
            DonationPledge = 0.f;
            ActionLabel = TEXT("母树已复苏");
        }
        else if (Food >= FMath::Min(3.f, DonationPledge))
        {
            Destination = W->MotherPosition + FVector(
                FMath::Cos(AgentId * 2.f) * 300.f, FMath::Sin(AgentId * 2.f) * 300.f, 0.f);
            ActionLabel = TEXT("带着承诺的养分前往母树");
            if (FVector::Dist2D(GetActorLocation(), W->MotherPosition) < 850.f)
            {
                GetCharacterMovement()->StopMovementImmediately();
                if (ActionCooldown <= 0.f)
                {
                    const float Before = Food;
                    if (W->Donate(this, FMath::Min(DonationPledge, Food)))
                    {
                        DonationPledge = FMath::Max(0.f, DonationPledge - (Before - Food));
                        Say(DonationPledge <= .01f
                            ? TEXT("答应的养分已经送到了。")
                            : TEXT("这一份先交给母树，剩下的我继续寻找。"));
                    }
                    ActionCooldown = 2.f;
                    BrainClock = 0.f;
                }
            }
            else MoveToward(Destination, 150.f);
            return;
        }
        // Insufficient carried food: keep the remaining promise and gather actual stock.
    }

    if (FollowId >= 0 && DonationPledge <= .01f)
    {
        auto* Leader = W->FindFolk(FollowId);
        if (!Alive(Leader) || Leader == this)
        {
            FollowId = -1;
            W->Record(DisplayName + TEXT(" 失去了同行目标，开始重新寻找落脚点。"));
        }
        else
        {
            const float Separation = 170.f + 65.f * (BodySize + Leader->BodySize);
            Destination = Leader->GetActorLocation() - Leader->GetActorForwardVector() * Separation
                + Leader->GetActorRightVector() * (AgentId % 2 == 0 ? 90.f : -90.f);
            ActionLabel = TEXT("与 ") + Leader->DisplayName + TEXT(" 同行");
            MoveToward(Destination, 100.f);
            return;
        }
    }

    if (BrainClock > 0.f)
    {
        MoveToward(Destination, 130.f + GetCapsuleComponent()->GetScaledCapsuleRadius());
        return;
    }
    BrainClock = .65f + AgentId * .07f;

    // Loot is a transfer from a real corpse inventory, never newly minted kill rewards.
    for (AVerdantCharacter* Other : W->Folk)
    {
        if (!IsValid(Other) || Other == this || !Other->bDead || Other->Food <= .01f)
            continue;
        if (FVector::Dist2D(GetActorLocation(), Other->GetActorLocation()) < Reach() + 120.f)
        {
            const float Taken = Other->Food;
            Food += Taken;
            Other->Food = 0.f;
            W->Record(FString::Printf(TEXT("%s 取走了倒下生物留下的 %.1f 份养分。"), *DisplayName, Taken));
            break;
        }
    }

    const float DesiredSize = AgentId == 1 ? .92f : AgentId == 2 ? 1.45f : 2.4f;
    const float UnpromisedFood = FMath::Max(0.f, Food - DonationPledge);
    if (BodySize < DesiredSize && Nutrition < 3.f && UnpromisedFood >= 5.f)
        Eat(FMath::Min(3.f, UnpromisedFood));

    if (bWild)
    {
        // Wildlife lives in distinct wilderness pockets rather than camping on the spawn.
        const FVector Anchor = WildHome(AgentId);
        const float Phase = FMath::FloorToFloat(Now / 12.f) * 1.7f + AgentId * 1.3f;
        Destination = W->SurfacePoint(Anchor + FVector(FMath::Cos(Phase) * 520.f,
            FMath::Sin(Phase) * 520.f, 0.f));
        ActionLabel = TEXT("在荒地觅食");
        const int32 Nearby = W->NearestGrove(GetActorLocation(), 650.f);
        if (Nearby >= 0 && Food < 18.f && ActionCooldown <= 0.f && W->CanHarvest(Nearby, this))
        {
            W->Harvest(Nearby, this, 3.f);
            ActionCooldown = 5.f;
        }
        MoveToward(Destination, 150.f);
        return;
    }

    // Miya voluntarily offers small surpluses. Negotiated pledges use the same executor.
    if (AgentId == 1 && Food >= 24.f && DonationPledge <= .01f && !W->bRestored && Now >= ReconsiderAt)
    {
        DonationPledge = 3.f;
        ReconsiderAt = Now + 35.f;
        Say(TEXT("我存够了口粮，准备给母树带去三份养分。"));
        W->Record(DisplayName + TEXT(" 决定把三份实际储粮带往母树。"));
        BrainClock = 0.f;
        return;
    }

    int32 Chosen = INDEX_NONE;
    const bool bAccessibleHome = W->Groves.IsValidIndex(HomeGrove) && W->CanHarvest(HomeGrove, this);
    // Commit to the chosen trip until arrival. Another resident harvesting a few
    // fruit must not reverse a traveller every decision tick; lost permission can.
    const bool bStillTravelling = bAccessibleHome &&
        FVector::Dist2D(GetActorLocation(), W->Groves[HomeGrove].Position) >= 620.f;
    if (bAccessibleHome && (bStillTravelling || W->Groves[HomeGrove].Food >= 3.f))
    {
        Chosen = HomeGrove;
    }
    else
    {
        float BestScore = -BIG_NUMBER;
        for (int32 I = 0; I < W->Groves.Num(); ++I)
        {
            if (!W->CanHarvest(I, this))
                continue;
            const FVerdantGrove& Grove = W->Groves[I];
            float Score = Grove.Food * 1.5f + Grove.Fertility * 8.f
                - FVector::Dist2D(GetActorLocation(), Grove.Position) * .003f;
            if (I == HomeGrove) Score += 3.f;
            for (AVerdantCharacter* Other : W->Folk)
            {
                if (Alive(Other) && Other != this && Other->BodySize > BodySize * 1.3f &&
                    (Other->bWild || Other->HostileId == AgentId) &&
                    FVector::Dist2D(Other->GetActorLocation(), Grove.Position) < 850.f)
                    Score -= 30.f;
            }
            if (Score > BestScore)
            {
                Chosen = I;
                BestScore = Score;
            }
        }
    }

    if (Chosen == INDEX_NONE)
    {
        Destination = W->MotherPosition + FVector(AgentId * 170.f, -850.f, 0.f);
        ActionLabel = TEXT("寻找新的采集许可");
        MoveToward(Destination, 180.f);
        return;
    }

    if (HomeGrove != Chosen)
    {
        HomeGrove = Chosen;
        W->Record(FString::Printf(TEXT("%s 改变采集路线，前往%s。"),
            *DisplayName, *W->Groves[Chosen].Name));
        Say(TEXT("这里不好继续采集了，我去另一片果林看看。"));
    }
    FVerdantGrove& Grove = W->Groves[Chosen];
    const float Angle = AgentId * 2.2f;
    Destination = Grove.Position + FVector(FMath::Cos(Angle) * 230.f, FMath::Sin(Angle) * 230.f, 0.f);
    const float Distance = FVector::Dist2D(GetActorLocation(), Grove.Position);
    ActionLabel = DonationPledge > .01f ? TEXT("为履行供奉承诺采集") : TEXT("前往 ") + Grove.Name;

    if (Distance < 620.f)
    {
        if (Grove.ControllerId < 0 && Food >= 3.f)
            W->Claim(Chosen, this);
        if (Food < 28.f && ActionCooldown <= 0.f)
        {
            W->Harvest(Chosen, this, 5.f);
            ActionCooldown = 3.5f + AgentId * .2f;
        }
        ActionLabel = Food >= 28.f ? TEXT("守着储粮，观察来往的人") : TEXT("采集与储备");
    }
    MoveToward(Destination, 120.f + GetCapsuleComponent()->GetScaledCapsuleRadius());
}
