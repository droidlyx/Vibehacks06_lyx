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
    if (bDead || !GetCharacterMovement()) return;
    auto* W = Valley();
    if (!W) return;
    const float Now = GetWorld()->GetTimeSeconds();
    const FVector Here = GetActorLocation();
    if (FVector::DistSquared2D(Here, Goal) <= FMath::Square(Acceptance))
    {
        GetCharacterMovement()->StopMovementImmediately();
        MoveSampleAt = 0.f; MoveRecoveryCount = 0; DetourUntil = 0.f;
        return;
    }
    // Sample actual progress only across a continuous trip. Dialogues, attacks,
    // arrival and large goal changes do not accumulate false stuck time.
    if (MoveSampleAt <= 0.f || Now - MoveSampleAt > 4.f || AttackTimer > 0
        || FVector::Dist2D(Goal, MoveSampleGoal) > 1600.f)
    {
        MoveSampleAt = Now; MoveSamplePosition = Here; MoveSampleGoal = Goal;
        MoveRecoveryCount = 0; DetourUntil = 0.f;
    }
    else if (Now - MoveSampleAt >= 2.f)
    {
        const float Moved = FVector::Dist2D(Here, MoveSamplePosition);
        const bool MovingGoal = FVector::Dist2D(Goal, MoveSampleGoal) > 300.f;
        const float Advance = FVector::Dist2D(MoveSamplePosition, MoveSampleGoal) - FVector::Dist2D(Here, Goal);
        if (Moved < 45.f || (!MovingGoal && Advance < 35.f))
        {
            ++MoveRecoveryCount;
            DetourUntil = 0.f; BrainClock = 0.f;
            if (MoveRecoveryCount == 1)
            {
                Say(TEXT("这边受阻，我试着绕开。"));
                W->Record(DisplayName + TEXT(" 遇到阻挡，开始寻找绕行路线。"));
            }
            if (MoveRecoveryCount >= 3 && HostileId < 0 && !bWild && FollowId < 0 && DonationPledge <= .001f
                && W->Groves.IsValidIndex(HomeGrove))
            {
                AvoidGrove = HomeGrove; AvoidGroveUntil = Now + 8.f;
                W->Record(DisplayName + TEXT(" 暂时绕开受阻的果林，寻找其他采集地点。"));
                GetCharacterMovement()->StopMovementImmediately();
                MoveSampleAt = 0.f;
                return;
            }
        }
        else MoveRecoveryCount = 0;
        MoveSampleAt = Now; MoveSamplePosition = Here; MoveSampleGoal = Goal;
    }
    const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(VerdantNPCMove), false, this);
    for (AVerdantCharacter* Other : W->Folk) if (IsValid(Other) && Other != this) Params.AddIgnoredActor(Other);
    const float ProbeDistance = FMath::Max(110.f, Radius * 2.5f);
    const FCollisionShape Probe = FCollisionShape::MakeSphere(FMath::Max(18.f, Radius * .75f));
    if (DetourUntil > Now && FVector::Dist2D(Here, DetourPoint) < FMath::Max(60.f, Radius)) DetourUntil = 0.f;
    FVector MoveGoal = DetourUntil > Now ? DetourPoint : Goal;
    FVector Direction = (MoveGoal - Here).GetSafeNormal2D();
    FHitResult Obstacle;
    if (GetWorld()->SweepSingleByChannel(Obstacle, Here, Here + Direction * ProbeDistance,
        FQuat::Identity, ECC_WorldStatic, Probe, Params))
    {
        const float Preferred = ((AgentId + MoveRecoveryCount) % 2 == 0) ? 1.f : -1.f;
        const float Step = FMath::Clamp(Radius * 5.f, 240.f, 550.f);
        const FVector ToGoal = (Goal - Here).GetSafeNormal2D();
        bool Found = false;
        // A short committed waypoint prevents choosing opposite sides every frame.
        for (float Angle : { 65.f, 90.f, 125.f, -65.f, -90.f, -125.f, 175.f })
        {
            const FVector CandidateDirection = ToGoal.RotateAngleAxis(Angle * Preferred, FVector::UpVector);
            FVector Candidate = W->SurfacePoint(Here + CandidateDirection * Step,
                GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            if (FMath::Abs(Candidate.Z - Here.Z) > Step * .8f) continue;
            FHitResult Hit;
            if (!GetWorld()->SweepSingleByChannel(Hit, Here, Candidate, FQuat::Identity, ECC_WorldStatic, Probe, Params))
            {
                DetourPoint = Candidate; DetourUntil = Now + 1.4f;
                Direction = (Candidate - Here).GetSafeNormal2D(); Found = true; break;
            }
        }
        if (!Found)
        {
            GetCharacterMovement()->StopMovementImmediately();
            BrainClock = FMath::Min(BrainClock, .15f);
            return;
        }
    }
    for (AVerdantCharacter* Other : W->Folk)
    {
        if (!Alive(Other) || Other == this) continue;
        const FVector Difference = Here - Other->GetActorLocation();
        const float Clearance = Radius + Other->GetCapsuleComponent()->GetScaledCapsuleRadius() + 35.f;
        const float Distance = Difference.Size2D();
        if (Distance > 1.f && Distance < Clearance)
            Direction += Difference.GetSafeNormal2D() * (1.f - Distance / Clearance) * 1.4f;
    }
    if (DetourUntil > Now && !ActionLabel.EndsWith(TEXT(" · 绕路"))) ActionLabel += TEXT(" · 绕路");
    AddMovementInput(Direction.GetSafeNormal2D(), 1.f, true);
}

void AVerdantCharacter::TickNPC(float Dt)
{
    auto* W = Valley();
    if (!W || bDead || AgentId == 0)
        return;
    const float Now = GetWorld()->GetTimeSeconds();
    BrainClock = FMath::Max(0.f, BrainClock - Dt);

    bSprinting = false;
    auto* Leader = W->FindFolk(FollowId);
    if (!Alive(Leader) || Leader == this) Leader = nullptr;
    auto SameParty = [&](const AVerdantCharacter* Other)
    {
        return Other && ((Leader && (Other == Leader || Other->FollowId == FollowId))
            || (Other->FollowId == AgentId && Trust.FindRef(Other->AgentId) >= 0.f && Other->HostileId != AgentId));
    };
    auto FriendlyEscort = [&](const AVerdantCharacter* Other)
    {
        if (!Other || Other->FollowId < 0) return false;
        const auto* TheirLeader = W->FindFolk(Other->FollowId);
        return Alive(TheirLeader) && (TheirLeader == this ||
            (TheirLeader->HostileId != AgentId && Trust.FindRef(TheirLeader->AgentId) >= 0.f));
    };
    // An actual ReceiveStrike sets HostileId directly; search cooldowns never
    // suppress that immediate self-defence path.
    AVerdantCharacter* Enemy = W->FindFolk(HostileId);
    if (!Alive(Enemy) || Enemy == this || SameParty(Enemy)
        || (Enemy->bWild && FriendlyEscort(Enemy) && Enemy->HostileId != AgentId && Trust.FindRef(Enemy->AgentId) >= 0.f))
    {
        HostileId = -1; CombatTarget.Reset(); Enemy = nullptr;
    }
    if (!Enemy && Leader && FVector::Dist2D(GetActorLocation(), Leader->GetActorLocation()) < 1800.f)
    {
        float Best = 1600.f;
        for (AVerdantCharacter* Other : W->Folk)
        {
            if (!Alive(Other) || Other == this || SameParty(Other) || Other->HostileId != Leader->AgentId) continue;
            if (Other->AgentId == AvoidThreatId && Now < AvoidThreatUntil) continue;
            const float Distance = FVector::Dist2D(GetActorLocation(), Other->GetActorLocation());
            if (Distance >= Best || FVector::Dist2D(Other->GetActorLocation(), Leader->GetActorLocation()) > 1200.f) continue;
            Enemy = Other; Best = Distance;
        }
        if (Enemy)
        {
            HostileId = Enemy->AgentId; CombatTarget = Enemy;
            Say(TEXT("我来帮你挡住它。"));
            W->Record(FString::Printf(TEXT("%s 开始援护%s，对抗正在威胁同伴的%s。"),
                *DisplayName, *Leader->DisplayName, *Enemy->DisplayName));
        }
    }
    if (!Enemy)
    {
        float BestDistance = bWild ? 560.f : 620.f;
        for (AVerdantCharacter* Other : W->Folk)
        {
            if (!Alive(Other) || Other == this || SameParty(Other) || (bWild && Other->bWild)) continue;
            if (Other->AgentId == AvoidThreatId && Now < AvoidThreatUntil) continue;
            const float Distance = FVector::Dist2D(GetActorLocation(), Other->GetActorLocation());
            const bool KnownThreat = Other->HostileId == AgentId ||
                (Trust.Contains(Other->AgentId) && Trust.FindRef(Other->AgentId) <= -20.f);
            const bool WildThreat = Other->bWild && !bWild && !FriendlyEscort(Other)
                && Trust.FindRef(Other->AgentId) < 10.f && Other->Trust.FindRef(AgentId) < 10.f;
            const bool Hunting = bWild && FollowId < 0 && Trust.FindRef(Other->AgentId) < 10.f;
            if ((KnownThreat || WildThreat || Hunting) && Distance < BestDistance)
            { Enemy = Other; BestDistance = Distance; }
        }
        if (Enemy) { HostileId = Enemy->AgentId; CombatTarget = Enemy; }
    }
    if (Enemy)
    {
        const float Distance = FVector::Dist2D(GetActorLocation(), Enemy->GetActorLocation());
        const bool Outmatched = Enemy->BodySize > BodySize * 1.45f || Health < MaxHealth() * .28f;
        const FVector Anchor = Leader ? Leader->GetActorLocation() : bWild ? WildHome(AgentId)
            : W->Groves.IsValidIndex(HomeGrove) ? W->Groves[HomeGrove].Position : W->MotherPosition;
        const float Leash = Leader ? 1400.f : bWild ? 2300.f : 1800.f;
        const bool TooFarFromDuty = FVector::Dist2D(GetActorLocation(), Anchor) > Leash
            && Distance > FMath::Max(350.f, Reach() + Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + 120.f);
        const bool SafeRetreat = Outmatched && Distance > FMath::Max(1150.f, Enemy->Reach() + 500.f);
        if (Distance > 2800.f || TooFarFromDuty || SafeRetreat)
        {
            AvoidThreatId = Enemy->AgentId; AvoidThreatUntil = Now + 8.f;
            W->Record(DisplayName + (SafeRetreat ? TEXT(" 与强敌拉开距离，继续原来的行程。") : TEXT(" 停止追逐，回到原来的行程。")));
            HostileId = -1; CombatTarget.Reset(); Enemy = nullptr;
            BrainClock = 0.f; MoveSampleAt = 0.f; DetourUntil = 0.f;
        }
        else if (Outmatched)
        {
            bSprinting = true;
            FVector Away = (GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
            if (Away.IsNearlyZero()) Away = GetActorRightVector();
            Destination = GetActorLocation() + Away * 950.f;
            Destination.X = FMath::Clamp(Destination.X, -10000.f, 10000.f);
            Destination.Y = FMath::Clamp(Destination.Y, -10000.f, 10000.f);
            Destination = W->SurfacePoint(Destination);
            ActionLabel = Leader ? TEXT("援护受阻，先避开强敌") : TEXT("避开更强的威胁");
            MoveToward(Destination, 100.f);
            if (Distance < Enemy->Reach() + 80.f && ActionCooldown <= 0.f)
            { Dodge(); ActionCooldown = 3.f; }
            else if (Health < MaxHealth() * .45f && Distance > Enemy->Reach() + 170.f && ActionCooldown <= 0.f)
            {
                const float Spare = Food - DonationPledge - 3.f;
                if (Spare >= 3.f && Eat(3.f))
                { ActionCooldown = 2.5f; Say(TEXT("我先退开，用自己的口粮恢复一下。")); }
            }
            return;
        }
        else
        {
            CombatTarget = Enemy;
            const bool DefendingLeader = Leader && Enemy->HostileId == Leader->AgentId;
            ActionLabel = DefendingLeader ? TEXT("援护 ") + Leader->DisplayName : TEXT("反击与争夺");
            const float StrikeDistance = Reach() + Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() * .7f;
            FHitResult StrikeWall;
            FCollisionQueryParams StrikeQuery(SCENE_QUERY_STAT(VerdantNPCAttackSight), false, this);
            StrikeQuery.AddIgnoredActor(Enemy);
            // Use the same obstruction test as actual melee damage. Being close
            // through a rock is a routing problem, not a reason to swing forever.
            const bool ClearStrike = Distance > StrikeDistance || !GetWorld()->LineTraceSingleByChannel(
                StrikeWall, GetActorLocation(), Enemy->GetActorLocation(), ECC_Visibility, StrikeQuery);
            auto MoveDuringCombat = [&](const FVector& Goal, float Acceptance)
            {
                MoveToward(Goal, Acceptance);
                if (MoveRecoveryCount >= 3)
                {
                    AvoidThreatId = Enemy->AgentId; AvoidThreatUntil = Now + 8.f;
                    HostileId = -1; CombatTarget.Reset();
                    AttackTimer = 0.f; bHitApplied = true;
                    BrainClock = 0.f; MoveSampleAt = 0.f; MoveRecoveryCount = 0; DetourUntil = 0.f;
                    GetCharacterMovement()->StopMovementImmediately();
                    W->Record(DisplayName + TEXT(" 无法接近对手，暂时停止追逐，回到原来的行程。"));
                }
            };
            if (Distance <= StrikeDistance && ClearStrike)
            {
                const FVector ToEnemy = (Enemy->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
                if (Leader && FVector::Dist2D(GetActorLocation(), Leader->GetActorLocation()) < Distance
                    && FVector::DotProduct(ToEnemy, (Leader->GetActorLocation() - GetActorLocation()).GetSafeNormal2D()) > .9f)
                {
                    const FVector Side = FVector::CrossProduct(FVector::UpVector, ToEnemy);
                    MoveDuringCombat(W->SurfacePoint(Enemy->GetActorLocation() + Side * (AgentId % 2 ? 1.f : -1.f)
                        * FMath::Max(160.f, Reach() * .8f)), 65.f);
                    return;
                }
                GetCharacterMovement()->StopMovementImmediately(); MoveSampleAt = 0.f;
                SetActorRotation(FRotator(0.f, ToEnemy.Rotation().Yaw, 0.f));
                if (AttackTimer <= 0.f) Attack(AgentId % 2 != 0 && Enemy->BodySize >= BodySize * .8f);
            }
            else
            {
                Destination = Enemy->GetActorLocation();
                if (!ClearStrike) ActionLabel = TEXT("绕过阻挡接近对手");
                MoveDuringCombat(Destination, ClearStrike ? FMath::Max(60.f, StrikeDistance * .8f) : 35.f);
            }
            return;
        }
    }

    // A model response cannot freeze the world, but its addressee waits while replying.
    if (W->Dialogue && W->Dialogue->bBusy && W->Dialogue->RequestNPC.Get() == this)
    {
        ActionLabel = TEXT("思考你的提议");
        GetCharacterMovement()->StopMovementImmediately();
        return;
    }

    if (DonationPledge > .001f && !bWild)
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
                        Say(DonationPledge <= .001f
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

    if (FollowId >= 0 && DonationPledge <= .001f)
    {
        auto* FollowLeader = W->FindFolk(FollowId);
        if (!Alive(FollowLeader) || FollowLeader == this)
        {
            FollowId = -1;
            W->Record(DisplayName + TEXT(" 失去了同行目标，开始重新寻找落脚点。"));
        }
        else
        {
            const float Separation = 170.f + 65.f * (BodySize + FollowLeader->BodySize);
            Destination = FollowLeader->GetActorLocation() - FollowLeader->GetActorForwardVector() * Separation
                + FollowLeader->GetActorRightVector() * (AgentId % 2 == 0 ? 90.f : -90.f);
            ActionLabel = TEXT("与 ") + FollowLeader->DisplayName + TEXT(" 同行");
            bSprinting = FVector::Dist2D(GetActorLocation(), FollowLeader->GetActorLocation()) > 900.f;
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
    // Keep an actual carried reserve: growth spends only the surplus, so a
    // difficult fight still yields useful food after the wanderer has eaten.
    const float WildFoodReserve = !bWild ? 0.f : AgentId == 4 ? 16.f : AgentId == 5 ? 24.f : 36.f;
    const float RetainedFood = FMath::Max(WildFoodReserve, W->NegotiationReserve(this));
    const float UnpromisedFood = FMath::Max(0.f, Food - DonationPledge - RetainedFood);
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
    if (AgentId == 1 && Food >= 24.f && DonationPledge <= .001f && !W->bRestored && Now >= ReconsiderAt)
    {
        const float Promise = FMath::Min(3.f, W->MaxNegotiatedDonation(this));
        if (Promise > .001f)
        {
            DonationPledge = Promise;
            ReconsiderAt = Now + 35.f;
            Say(FString::Printf(TEXT("我留好了口粮，准备给母树带去 %.1f 份养分。"), Promise));
            W->Record(FString::Printf(TEXT("%s 决定把 %.1f 份实际储粮带往母树。"), *DisplayName, Promise));
            BrainClock = 0.f;
            return;
        }
    }

    int32 Chosen = INDEX_NONE;
    const bool bAccessibleHome = W->Groves.IsValidIndex(HomeGrove) && W->CanHarvest(HomeGrove, this)
        && !(HomeGrove == AvoidGrove && Now < AvoidGroveUntil);
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
            if (!W->CanHarvest(I, this) || (I == AvoidGrove && Now < AvoidGroveUntil))
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
    ActionLabel = DonationPledge > .001f ? TEXT("为履行供奉承诺采集") : TEXT("前往 ") + Grove.Name;

    if (Distance < 620.f)
    {
        if (Grove.ControllerId < 0 && Food >= 3.f)
            W->Claim(Chosen, this);
        // A modest, local ecological choice: keep food promised to others and
        // personal rations, and use the same real transaction as the player.
        const float CareCost = W->TendCost(Chosen);
        if (FollowId < 0 && DonationPledge <= .001f && Health >= MaxHealth() * .6f
            && Grove.Fertility < .55f && Now >= NextTendAt && ActionCooldown <= 0.f
            && CareCost > .001f && Food + .001f >= W->NegotiationReserve(this) + DonationPledge + CareCost
            && W->CanHarvest(Chosen, this) && W->Tend(Chosen, this))
        {
            NextTendAt = Now + 60.f;
            ActionCooldown = 1.5f;
            ActionLabel = TEXT("把余粮还给土地，照料果林");
            Say(TEXT("这片土地需要歇一歇，我用一些余粮照料它。"));
            GetCharacterMovement()->StopMovementImmediately();
            return;
        }
        if (Food < 28.f && ActionCooldown <= 0.f)
        {
            W->Harvest(Chosen, this, 5.f);
            ActionCooldown = 3.5f + AgentId * .2f;
        }
        ActionLabel = Food >= 28.f ? TEXT("守着储粮，观察来往的人") : TEXT("采集与储备");
    }
    MoveToward(Destination, 120.f + GetCapsuleComponent()->GetScaledCapsuleRadius());
}
