#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "VerdantFoodPickup.h"
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
		MoveSampleAt = 0.f;
		MoveRecoveryCount = 0;
		DetourUntil = 0.f;
		return;
	}
	// Sample actual progress only across a continuous trip. Dialogues, attacks,
	// arrival and large goal changes do not accumulate false stuck time.
	if (MoveSampleAt <= 0.f || Now - MoveSampleAt > 4.f || AttackTimer > 0
		|| FVector::Dist2D(Goal, MoveSampleGoal) > 1600.f)
	{
		MoveSampleAt = Now;
		MoveSamplePosition = Here;
		MoveSampleGoal = Goal;
		MoveRecoveryCount = 0;
		DetourUntil = 0.f;
	}
	else if (Now - MoveSampleAt >= 2.f)
	{
		const float Moved = FVector::Dist2D(Here, MoveSamplePosition);
		const bool MovingGoal = FVector::Dist2D(Goal, MoveSampleGoal) > 300.f;
		const float Advance = FVector::Dist2D(MoveSamplePosition, MoveSampleGoal) - FVector::Dist2D(Here, Goal);
		if (Moved < 45.f || (!MovingGoal && Advance < 35.f))
		{
			++MoveRecoveryCount;
			DetourUntil = 0.f;
			BrainClock = 0.f;
			if (MoveRecoveryCount == 1)
			{
				Say(TEXT("这边受阻，我试着绕开。"));
				W->Record(DisplayName + TEXT(" 遇到阻挡，开始寻找绕行路线。"));
			}
			if (MoveRecoveryCount >= 3 && HostileId < 0 && !bWild && FollowId < 0 && DonationPledge <= .001f
				&& W->Groves.IsValidIndex(HomeGrove))
			{
				AvoidGrove = HomeGrove;
				AvoidGroveUntil = Now + 8.f;
				W->Record(DisplayName + TEXT(" 暂时绕开受阻的果林，寻找其他采集地点。"));
				GetCharacterMovement()->StopMovementImmediately();
				MoveSampleAt = 0.f;
				return;
			}
		}
		else MoveRecoveryCount = 0;
		MoveSampleAt = Now;
		MoveSamplePosition = Here;
		MoveSampleGoal = Goal;
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
	// An uphill floor may intersect this horizontal probe; let CharacterMovement
	// climb it instead of treating every terrain hit as a wall.
	if (GetWorld()->SweepSingleByChannel(Obstacle, Here, Here + Direction * ProbeDistance,
	                                     FQuat::Identity, ECC_WorldStatic, Probe,
	                                     Params) && !GetCharacterMovement()->IsWalkable(Obstacle))
	{
		const float Preferred = ((AgentId + MoveRecoveryCount) % 2 == 0) ? 1.f : -1.f;
		const float Step = FMath::Clamp(Radius * 5.f, 240.f, 550.f);
		const FVector ToGoal = (Goal - Here).GetSafeNormal2D();
		bool Found = false;
		// A short committed waypoint prevents choosing opposite sides every frame.
		for (float Angle : {65.f, 90.f, 125.f, -65.f, -90.f, -125.f, 175.f})
		{
			const FVector CandidateDirection = ToGoal.RotateAngleAxis(Angle * Preferred, FVector::UpVector);
			FVector Candidate = W->SurfacePoint(Here + CandidateDirection * Step,
			                                    GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const float MaxGrade = FMath::Tan(FMath::DegreesToRadians(
				FMath::Clamp(GetCharacterMovement()->GetWalkableFloorAngle(), 0.f, 89.f)));
			if (FMath::Abs(Candidate.Z - Here.Z) > Step * MaxGrade) continue;
			FHitResult Hit;
			if (!GetWorld()->SweepSingleByChannel(Hit, Here, Candidate, FQuat::Identity, ECC_WorldStatic, Probe,
			                                      Params))
			{
				DetourPoint = Candidate;
				DetourUntil = Now + 1.4f;
				Direction = (Candidate - Here).GetSafeNormal2D();
				Found = true;
				break;
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
	// Temporary arrival display: keep each species available to inspect instead
	// of letting its ordinary harvesting/hunting immediately scatter the group.
	// A strike or an agreed movement task releases the original NPC permanently.
	if (ActorHasTag(TEXT("VerdantSpawnPreview")))
	{
		if (HostileId < 0 && FollowId < 0 && DonationPledge <= .001f
			&& FVector::DistSquared2D(Destination, GetActorLocation()) < FMath::Square(100.f))
		{
			bSprinting = false;
			GetCharacterMovement()->StopMovementImmediately();
			ActionLabel = TEXT("在出发地等候");
			return;
		}
		Tags.Remove(TEXT("VerdantSpawnPreview"));
	}
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
		|| (Enemy->bWild && FriendlyEscort(Enemy) && Enemy->HostileId != AgentId && Trust.FindRef(Enemy->AgentId) >=
			0.f))
	{
		if (HostileId >= 0) ++IntentRevision;
		HostileId = -1;
		CombatTarget.Reset();
		Enemy = nullptr;
	}
	if (!Enemy && bProtectLeader && Leader && FVector::Dist2D(GetActorLocation(), Leader->GetActorLocation()) < 1800.f)
	{
		float Best = 1600.f;
		for (AVerdantCharacter* Other : W->Folk)
		{
			if (!Alive(Other) || Other == this || SameParty(Other) || Other->HostileId != Leader->AgentId) continue;
			if (Other->AgentId == AvoidThreatId && Now < AvoidThreatUntil) continue;
			const float Distance = FVector::Dist2D(GetActorLocation(), Other->GetActorLocation());
			if (Distance >= Best || FVector::Dist2D(Other->GetActorLocation(), Leader->GetActorLocation()) > 1200.f)
				continue;
			Enemy = Other;
			Best = Distance;
		}
		if (Enemy)
		{
			++IntentRevision;
			HostileId = Enemy->AgentId;
			CombatTarget = Enemy;
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
			{
				Enemy = Other;
				BestDistance = Distance;
			}
		}
		if (Enemy)
		{
			++IntentRevision;
			HostileId = Enemy->AgentId;
			CombatTarget = Enemy;
		}
	}
	if (Enemy)
	{
		const float Distance = FVector::Dist2D(GetActorLocation(), Enemy->GetActorLocation());
		const bool Outmatched = Enemy->BodySize > BodySize * 1.45f || Health < MaxHealth() * .28f;
		const FVector Anchor = Leader
			                       ? Leader->GetActorLocation()
			                       : bWild
			                       ? HomePosition
			                       : W->Groves.IsValidIndex(HomeGrove)
			                       ? W->Groves[HomeGrove].Position
			                       : W->MotherPosition;
		const float Leash = Leader ? 1400.f : bWild ? 2300.f : 1800.f;
		const bool TooFarFromDuty = FVector::Dist2D(GetActorLocation(), Anchor) > Leash
			&& Distance > FMath::Max(350.f, Reach() + Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + 120.f);
		const bool SafeRetreat = Outmatched && Distance > FMath::Max(1150.f, Enemy->Reach() + 500.f);
		if (Distance > 2800.f || TooFarFromDuty || SafeRetreat)
		{
			AvoidThreatId = Enemy->AgentId;
			AvoidThreatUntil = Now + 8.f;
			W->Record(DisplayName + (SafeRetreat ? TEXT(" 与强敌拉开距离，继续原来的行程。") : TEXT(" 停止追逐，回到原来的行程。")));
			++IntentRevision;
			HostileId = -1;
			CombatTarget.Reset();
			Enemy = nullptr;
			BrainClock = 0.f;
			MoveSampleAt = 0.f;
			DetourUntil = 0.f;
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
			{
				Dodge();
				ActionCooldown = 3.f;
			}
			else if (Health < MaxHealth() * .45f && Distance > Enemy->Reach() + 170.f && ActionCooldown <= 0.f)
			{
				const float Spare = Food - DonationPledge - 3.f;
				if (Spare >= 3.f && Eat(3.f))
				{
					ActionCooldown = 2.5f;
					Say(TEXT("我先退开，用自己的口粮恢复一下。"));
				}
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
					AvoidThreatId = Enemy->AgentId;
					AvoidThreatUntil = Now + 8.f;
					++IntentRevision;
					HostileId = -1;
					CombatTarget.Reset();
					AttackTimer = 0.f;
					bHitApplied = true;
					BrainClock = 0.f;
					MoveSampleAt = 0.f;
					MoveRecoveryCount = 0;
					DetourUntil = 0.f;
					GetCharacterMovement()->StopMovementImmediately();
					W->Record(DisplayName + TEXT(" 无法接近对手，暂时停止追逐，回到原来的行程。"));
				}
			};
			if (Distance <= StrikeDistance && ClearStrike)
			{
				const FVector ToEnemy = (Enemy->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
				if (Leader && FVector::Dist2D(GetActorLocation(), Leader->GetActorLocation()) < Distance
					&& FVector::DotProduct(ToEnemy, (Leader->GetActorLocation() - GetActorLocation()).GetSafeNormal2D())
					> .9f)
				{
					const FVector Side = FVector::CrossProduct(FVector::UpVector, ToEnemy);
					MoveDuringCombat(W->SurfacePoint(Enemy->GetActorLocation() + Side * (AgentId % 2 ? 1.f : -1.f)
						                 * FMath::Max(160.f, Reach() * .8f)), 65.f);
					return;
				}
				GetCharacterMovement()->StopMovementImmediately();
				MoveSampleAt = 0.f;
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

	// Network requests never stop the current rule-driven action.
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
			++IntentRevision;
			FollowId = -1;
			bProtectLeader = false;
			W->Record(DisplayName + TEXT(" 失去了同行目标，开始重新寻找落脚点。"));
		}
		else
		{
			const float Separation = 170.f + 65.f * (BodySize + FollowLeader->BodySize);
			Destination = FollowLeader->GetActorLocation() - FollowLeader->GetActorForwardVector() * Separation
				+ FollowLeader->GetActorRightVector() * (AgentId % 2 == 0 ? 90.f : -90.f);
			ActionLabel = (bProtectLeader ? TEXT("保护 ") : TEXT("跟随 ")) + FollowLeader->DisplayName;
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

    // Corpse transfers use exactly the same source-tier/capacity rules as the player.
    for (AVerdantCharacter* Other : W->Folk)
    {
        if (!IsValid(Other) || Other == this || Other->AgentId == 0 || !Other->bDead || Other->Food <= .001f) continue;
        if (FVector::Dist2D(GetActorLocation(), Other->GetActorLocation()) < Reach() + 120.f)
            if (W->LootCorpse(Other, this, 5.f) > .001f) break;
    }

    const float TargetStock = FoodTier() == 4 ? 160.f : FoodTier() == 3 ? 90.f : FoodTier() == 2 ? 45.f : 20.f;
    if (Food < TargetStock || DonationPledge > .001f)
    {
        if (AVerdantFoodPickup* Pickup = W->FindFood(this, 6000.f))
        {
            Destination = Pickup->GetActorLocation();
            ActionLabel = DonationPledge > .001f ? TEXT("为供奉寻找食物") : TEXT("寻找可获取的散落食物");
            if (FVector::Dist2D(GetActorLocation(), Destination) < FMath::Max(240.f, 250.f * BodySize))
            {
                if (ActionCooldown <= 0.f)
                {
                    W->CollectFood(this, FMath::Max(240.f, 250.f * BodySize));
                    ActionCooldown = 3.5f + (AgentId % 5) * .2f;
                }
            }
            else MoveToward(Destination, 120.f + GetCapsuleComponent()->GetScaledCapsuleRadius());
            return;
        }
    }

    // Preserve the modest ecological action, funded only by the NPC's actual spare inventory.
    const int32 NearbyGrove = W->NearestGrove(GetActorLocation(), 650.f);
    if (!bWild && NearbyGrove != INDEX_NONE && DonationPledge <= .001f && Now >= NextTendAt
        && ActionCooldown <= 0.f && W->Groves[NearbyGrove].Fertility < .55f
        && Food >= W->NegotiationReserve(this) + W->TendCost(NearbyGrove)
        && W->Tend(NearbyGrove, this))
    {
        NextTendAt = Now + 60.f;
        ActionCooldown = 1.5f;
        ActionLabel = TEXT("用余粮照料土地");
        GetCharacterMovement()->StopMovementImmediately();
        return;
    }

    // Low-frequency local wandering is deterministic and independent of dialogue availability.
    const float Phase = FMath::FloorToFloat(Now / 18.f) * 1.7f + AgentId * 1.3f;
    const FVector Anchor = HomePosition.IsNearlyZero() ? WildHome(AgentId) : HomePosition;
    Destination = W->SurfacePoint(Anchor + FVector(FMath::Cos(Phase), FMath::Sin(Phase), 0.f) * 320.f);
    ActionLabel = Food >= TargetStock ? TEXT("带着储粮，观察附近") : TEXT("等待附近食物，再作打算");
    MoveToward(Destination, 150.f);
}
