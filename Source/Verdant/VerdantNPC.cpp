#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "VerdantFoodPickup.h"
#include "VerdantDialogue.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

namespace
{
	bool Alive(const AVerdantCharacter* Who)
	{
		return IsValid(Who) && !Who->bDead;
	}

	constexpr float NPCCombatMaxDistance = 2800.f;
	constexpr float NPCRetreatSizeRatio = 1.45f;
	constexpr float NPCRetreatHealthFraction = .28f;

	bool OutmatchedForCombat(const AVerdantCharacter* Who, const AVerdantCharacter* Target)
	{
		return Target->BodySize > Who->BodySize * NPCRetreatSizeRatio
			|| Who->Health < Who->MaxHealth() * NPCRetreatHealthFraction;
	}

	bool BeyondCombatDuty(const AVerdantCharacter* Who, const AVerdantCharacter* Target,
	                      const AVerdantWorld* World, const AVerdantCharacter* Leader = nullptr)
	{
		const FVector Anchor = Leader ? Leader->GetActorLocation() : Who->bWild ? Who->HomePosition
			: World->Groves.IsValidIndex(Who->HomeGrove) ? World->Groves[Who->HomeGrove].Position
			: World->MotherPosition;
		const float Leash = Leader ? 1400.f : Who->bWild ? 2300.f : 1800.f;
		const float NearbyReach = FMath::Max(350.f,
			Who->Reach() + Target->GetCapsuleComponent()->GetScaledCapsuleRadius() + 120.f);
		return FVector::Dist2D(Who->GetActorLocation(), Anchor) > Leash
			&& FVector::Dist2D(Who->GetActorLocation(), Target->GetActorLocation()) > NearbyReach;
	}

	// Read collision triangles directly: a smoothed vertex normal can hide an
	// unwalkable face, and evaluating the fractal field for every edge is costly.
	struct FWalkingQuery
	{
		AVerdantWorld* Valley;
		AVerdantCharacter* Walker;
		const FProcMeshSection* Section;
		FTransform GroundTransform;
		FCollisionQueryParams Obstacles;
		FCollisionShape Capsule;
		int32 Row = 0;
		float SpacingX = 0.f, SpacingY = 0.f, HalfHeight, FloorZ;
		mutable int32 SweepsRemaining = MAX_int32;
		mutable bool bSweepBudgetExhausted = false;

		FWalkingQuery(AVerdantWorld* InValley, AVerdantCharacter* InWalker)
			: Valley(InValley), Walker(InWalker), Section(nullptr),
			  Obstacles(SCENE_QUERY_STAT(VerdantNPCWalking), false, InWalker)
		{
			HalfHeight = Walker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float Radius = Walker->GetCapsuleComponent()->GetScaledCapsuleRadius();
			Capsule = FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - .5f), HalfHeight - .5f);
			FloorZ = Walker->GetCharacterMovement()->GetWalkableFloorZ();
			if (Valley->Ground)
			{
				Section = Valley->Ground->GetProcMeshSection(0);
				GroundTransform = Valley->Ground->GetComponentTransform();
				// Ground is checked face by face below. Sweeping a level capsule
				// through a rising floor would incorrectly call every hill a wall.
				Obstacles.AddIgnoredComponent(Valley->Ground.Get());
			}
			if (Section && Section->ProcVertexBuffer.Num() >= 4)
			{
				Row = FMath::RoundToInt(FMath::Sqrt(float(Section->ProcVertexBuffer.Num())));
				if (Row * Row != Section->ProcVertexBuffer.Num()) Row = 0;
				if (Row >= 2)
				{
					const FVector Origin = Section->ProcVertexBuffer[0].Position;
					SpacingX = Section->ProcVertexBuffer[1].Position.X - Origin.X;
					SpacingY = Section->ProcVertexBuffer[Row].Position.Y - Origin.Y;
				}
			}
			for (AVerdantCharacter* Other : Valley->Folk)
				if (IsValid(Other)) Obstacles.AddIgnoredActor(Other);
		}

		bool Ground(const FVector& At, FVector& Surface, FVector& Normal) const
		{
			if (Row < 2 || SpacingX <= SMALL_NUMBER || SpacingY <= SMALL_NUMBER) return false;
			const FVector Origin = Section->ProcVertexBuffer[0].Position;
			const FVector RayStart = GroundTransform.InverseTransformPosition(At);
			const FVector RayDirection = GroundTransform.InverseTransformVector(FVector::UpVector);
			FVector Point = RayStart;
			for (int32 Iteration = 0; Iteration < 5; ++Iteration)
			{
				const float X = float((Point.X - Origin.X) / SpacingX);
				const float Y = float((Point.Y - Origin.Y) / SpacingY);
				if (X < 0 || Y < 0 || X >= Row - 1 || Y >= Row - 1) return false;
				const int32 IX = FMath::FloorToInt(X), IY = FMath::FloorToInt(Y);
				const int32 A = IY * Row + IX;
				const bool Upper = X - IX >= Y - IY;
				const FVector VA = Section->ProcVertexBuffer[A].Position;
				const FVector VB = Section->ProcVertexBuffer[A + (Upper ? 1 : Row)].Position;
				const FVector VD = Section->ProcVertexBuffer[A + Row + 1].Position;
				FVector Plane = FVector::CrossProduct(VB - VA, VD - VA);
				const float Denominator = FVector::DotProduct(Plane, RayDirection);
				if (FMath::Abs(Denominator) < SMALL_NUMBER) return false;
				const FVector Hit = RayStart + RayDirection * (FVector::DotProduct(Plane, VA - RayStart) / Denominator);
				if (FVector::DistSquared2D(Hit, Point) < .0001f)
				{
					const FVector Scale = GroundTransform.GetScale3D();
					if (FMath::Abs(Scale.X * Scale.Y * Scale.Z) < SMALL_NUMBER) return false;
					Normal = GroundTransform.TransformVectorNoScale(Plane / Scale).GetSafeNormal();
					if (Normal.Z < 0.f) Normal = -Normal;
					Surface = GroundTransform.TransformPosition(Hit);
					return !Surface.ContainsNaN();
				}
				Point = Hit;
			}
			return false;
		}

		bool Segment(const FVector& From, const FVector& To) const
		{
			// Half a terrain cell catches intermediate ridges, not just endpoint grade.
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(From, To) / 25.f));
			FVector Previous = From;
			for (int32 I = 0; I <= Steps; ++I)
			{
				FVector Surface, Normal;
				if (!Ground(FMath::Lerp(From, To, float(I) / Steps), Surface, Normal)) return false;
				if (I > 0 && Normal.Z + .001f < FloorZ) return false;
				const FVector Center = Surface + FVector(0, 0, HalfHeight + 2.f);
				FHitResult Hit;
				if (SweepsRemaining <= 0)
				{
					bSweepBudgetExhausted = true;
					return false;
				}
				--SweepsRemaining;
				if (Valley->GetWorld()->SweepSingleByChannel(Hit, Previous, Center, FQuat::Identity,
				                                             ECC_Pawn, Capsule, Obstacles))
					return false;
				Previous = Center;
			}
			return true;
		}
	};

	bool FindLocalRoute(const FWalkingQuery& SourceQuery, const FVector& Start, const FVector& Goal,
	                    bool AvoidForward, TArray<FVector>& Path)
	{
		// A bounded, on-demand search; ordinary clear walking never builds a grid.
		// A private query copy also caps physics work, regardless of terrain shape.
		const FWalkingQuery Query(SourceQuery);
		Query.SweepsRemaining = 384;
		constexpr int32 Radius = 7, Side = Radius * 2 + 1, NodeCount = Side * Side;
		const float Step = FMath::Clamp(Query.Walker->GetCapsuleComponent()->GetScaledCapsuleRadius() * 3.f,
		                                100.f, 200.f);
		struct FNode
		{
			FVector Position = FVector::ZeroVector;
			float Cost = BIG_NUMBER;
			int32 Parent = INDEX_NONE;
			bool bSampled = false, bWalkable = false, bClosed = false;
		};
		TArray<FNode> Nodes;
		Nodes.SetNum(NodeCount);
		auto Sample = [&](int32 Index) -> FNode&
		{
			FNode& N = Nodes[Index];
			if (!N.bSampled)
			{
				FVector Normal;
				N.bSampled = true;
				N.bWalkable = Query.Ground(Start + FVector((Index % Side - Radius) * Step,
				                                           (Index / Side - Radius) * Step, 0), N.Position, Normal)
					&& Normal.Z + .001f >= Query.FloorZ;
				N.Position.Z += Query.HalfHeight + 2.f;
			}
			return N;
		};
		const int32 First = Radius * Side + Radius;
		FNode& Origin = Sample(First);
		Origin.Position = Start;
		Origin.Cost = 0.f;
		TArray<int32> Open;
		Open.Add(First);
		int32 Best = First;
		float BestDistance = FVector::Dist2D(Start, Goal);
		for (int32 Expanded = 0; Expanded < 96 && !Open.IsEmpty() && !Query.bSweepBudgetExhausted; ++Expanded)
		{
			int32 Slot = 0;
			float Score = BIG_NUMBER;
			for (int32 I = 0; I < Open.Num(); ++I)
			{
				const FNode& N = Nodes[Open[I]];
				const float Candidate = N.Cost + FVector::Dist2D(N.Position, Goal);
				if (Candidate < Score)
				{
					Score = Candidate;
					Slot = I;
				}
			}
			const int32 Current = Open[Slot];
			Open.RemoveAtSwap(Slot);
			FNode& N = Nodes[Current];
			N.bClosed = true;
			const float Distance = FVector::Dist2D(N.Position, Goal);
			if (Distance < BestDistance)
			{
				Best = Current;
				BestDistance = Distance;
			}
			if (Current != First && Distance < Step && Query.Segment(N.Position, Goal))
			{
				Best = Current;
				BestDistance = Distance;
				break;
			}
			const int32 X = Current % Side, Y = Current / Side;
			if (Current != First && (X == 0 || X == Side - 1 || Y == 0 || Y == Side - 1)
				&& Distance + Step < FVector::Dist2D(Start, Goal))
				break;
			for (int32 DY = -1; DY <= 1; ++DY)
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					if ((!DX && !DY) || X + DX < 0 || X + DX >= Side || Y + DY < 0 || Y + DY >= Side) continue;
					const int32 Next = (Y + DY) * Side + X + DX;
					FNode& Other = Sample(Next);
					if (Current == First && AvoidForward && FVector::DotProduct(
						(Other.Position - Start).GetSafeNormal2D(), (Goal - Start).GetSafeNormal2D()) > .5f)
						continue;
					const float Cost = N.Cost + FVector::Dist(N.Position, Other.Position);
					if (Other.bClosed || !Other.bWalkable || Cost >= Other.Cost || !Query.Segment(
						N.Position, Other.Position))
						continue;
					Other.Cost = Cost;
					Other.Parent = Current;
					Open.AddUnique(Next);
				}
		}
		Path.Reset();
		if (Query.bSweepBudgetExhausted) return false;
		if (Best == First || BestDistance + 20.f >= FVector::Dist2D(Start, Goal)) return false;
		for (int32 I = Best; I != First && I != INDEX_NONE; I = Nodes[I].Parent) Path.Insert(Nodes[I].Position, 0);
		return !Path.IsEmpty();
	}

	void ResetFoodRoute(AVerdantCharacter* Who)
	{
		Who->FoodTarget.Reset();
		Who->FoodCollectionFailures = 0;
		Who->MoveSampleAt = 0.f;
		Who->MoveRecoveryCount = 0;
		Who->MoveSteeringAt = 0.f;
		Who->LocalMovePath.Reset();
		Who->BrainClock = 0.f;
	}
}

bool AVerdantCharacter::CanStartDialogueAttack(const AVerdantCharacter* Target, FString* Reason) const
{
	const auto Reject = [Reason](const TCHAR* Message)
	{
		if (Reason)*Reason = Message;
		return false;
	};
	if (Reason)Reason->Empty();
	const auto* World = Valley();
	if (!World || !Alive(this) || !Alive(Target) || Target == this)
		return Reject(TEXT("当前没有可攻击的有效目标。"));
	if (Target->IsRespawnProtected())return Reject(TEXT("目标仍处于复活保护中。"));
	if (FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) > NPCCombatMaxDistance)
		return Reject(TEXT("目标超出当前可追击的28米范围。"));
	if (OutmatchedForCombat(this, Target))
		return Reject(TEXT("当前体力或双方体型差距会使我撤退，无法接受这次攻击计划。"));
	// An explicit attack clears follow/protect, so evaluate the same home leash
	// that the combat brain will use afterwards, without the previous leader.
	if (BeyondCombatDuty(this, Target, World))
		return Reject(TEXT("我已远离原来的活动区域，无法继续追击这个目标。"));
	return true;
}

bool AVerdantCharacter::IsWithinHomeRange(const FVector& Position, float Inset) const
{
	if (Position.ContainsNaN() || HomePosition.ContainsNaN() || !FMath::IsFinite(HomeRangeRadius)) return false;
	const float Radius = FMath::Max(0.f, HomeRangeRadius - FMath::Max(0.f, Inset));
	return FVector::DistSquared2D(HomePosition, Position) <= FMath::Square(Radius);
}

void AVerdantCharacter::SetHomeRangeCenter(const FVector& NewCenter)
{
	if (NewCenter.ContainsNaN()) return;
	// Explicit relocation only. Following, defending and delivering never call this.
	HomePosition = NewCenter;
	ResetFoodRoute(this);
	AvoidFoodUntil.Reset();
	bReturningHome = true;
	Destination = HomePosition;
	MoveSteeringDirection = FVector::ZeroVector;
	DetourUntil = 0.f;
}

void AVerdantCharacter::MoveToward(const FVector& Goal, float Acceptance)
{
	if (bDead || !GetCharacterMovement()) return;
	if (IsDialogueReplyActive())
	{
		UpdateDialogueReplyPose();
		return;
	}
	auto* W = Valley();
	if (!W) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Here = GetActorLocation();
	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	if (MoveIntentRevision < 0) MovePathSearchAt = Now + static_cast<float>(AgentId % 20) * .06f;
	if (MoveIntentRevision != IntentRevision || FVector::DistSquared2D(LocalMoveGoal, Goal) > FMath::Square(160.f)
		|| !FMath::IsNearlyEqual(MoveAcceptance, Acceptance, 1.f))
	{
		LocalMovePath.Reset();
		LocalMoveGoal = Goal;
		MoveIntentRevision = IntentRevision;
		MoveAcceptance = Acceptance;
		MoveSteeringAt = 0.f;
		MoveSampleAt = 0.f;
		MoveRecoveryCount = 0;
	}
	if (Now < MoveSteeringAt)
	{
		if (!MoveSteeringDirection.IsNearlyZero()) AddMovementInput(MoveSteeringDirection, MoveSteeringScale, true);
		else GetCharacterMovement()->StopMovementImmediately();
		return;
	}
	MoveSteeringAt = Now + .12f;
	MoveSteeringDirection = FVector::ZeroVector;
	const FWalkingQuery Query(W, this);
	// Arrival must have a traversable connection, not merely matching XY on
	// opposite sides of a ridge. Failed food interactions have their own retry limit.
	if (FVector::DistSquared2D(Here, Goal) <= FMath::Square(Acceptance) && Query.Segment(Here, Goal))
	{
		GetCharacterMovement()->StopMovementImmediately();
		MoveSampleAt = 0.f;
		MoveRecoveryCount = 0;
		LocalMovePath.Reset();
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
		if (Moved < 25.f || (!MovingGoal && Advance < 20.f && LocalMovePath.IsEmpty()))
		{
			++MoveRecoveryCount;
			DetourUntil = 0.f;
			LocalMovePath.Reset();
			BrainClock = 0.f;
			if (MoveRecoveryCount == 1)
			{
				Say(TEXT("这边受阻，我试着绕开。"));
				W->Record(DisplayName + TEXT(" 遇到阻挡，开始寻找绕行路线。"));
			}
		}
		else MoveRecoveryCount = 0;
		MoveSampleAt = Now;
		MoveSamplePosition = Here;
		MoveSampleGoal = Goal;
	}
	while (!LocalMovePath.IsEmpty() && FVector::Dist2D(Here, LocalMovePath[0]) < FMath::Max(35.f, Radius * .65f))
		LocalMovePath.RemoveAt(0, 1, EAllowShrinking::No);
	FVector MoveGoal = LocalMovePath.IsEmpty() ? Goal : LocalMovePath[0];
	auto ClearAhead = [&](const FVector& Target)
	{
		const float Distance = FMath::Min(float(FVector::Dist2D(Here, Target)), FMath::Max(100.f, Radius * 3.f));
		return Query.Segment(Here, Here + (Target - Here).GetSafeNormal2D() * Distance);
	};
	if (!ClearAhead(MoveGoal) || (MoveRecoveryCount > 0 && LocalMovePath.IsEmpty()))
	{
		LocalMovePath.Reset();
		if (Now >= MovePathSearchAt)
		{
			MovePathSearchAt = Now + 1.25f + (AgentId % 5) * .07f;
			FindLocalRoute(Query, Here, Goal, MoveRecoveryCount > 0, LocalMovePath);
		}
		if (LocalMovePath.IsEmpty())
		{
			GetCharacterMovement()->StopMovementImmediately();
			return;
		}
		MoveGoal = LocalMovePath[0];
	}
	FVector Direction = (MoveGoal - Here).GetSafeNormal2D();
	FVector Separated = Direction;
	for (AVerdantCharacter* Other : W->Folk)
	{
		if (!Alive(Other) || Other == this) continue;
		const FVector Difference = Here - Other->GetActorLocation();
		const float Clearance = Radius + Other->GetCapsuleComponent()->GetScaledCapsuleRadius() + 35.f;
		const float Distance = Difference.Size2D();
		if (Distance > 1.f && Distance < Clearance)
			Separated += Difference.GetSafeNormal2D() * (1.f - Distance / Clearance) * 1.4f;
	}
	// Separation must not steer a valid route back into a rock or steep face.
	if (!Separated.Equals(Direction, .01f) && ClearAhead(Here + Separated.GetSafeNormal2D() * 120.f))
		Direction = Separated.GetSafeNormal2D();
	DetourUntil = LocalMovePath.IsEmpty() ? 0.f : Now + .3f;
	DetourPoint = MoveGoal;
	if (!LocalMovePath.IsEmpty() && !ActionLabel.EndsWith(TEXT(" · 绕路"))) ActionLabel += TEXT(" · 绕路");
	MoveSteeringDirection = Direction;
	MoveSteeringScale = FMath::Clamp(float(FVector::Dist2D(Here, MoveGoal)) / 120.f, .25f, 1.f);
	AddMovementInput(MoveSteeringDirection, MoveSteeringScale, true);
}

void AVerdantCharacter::TickNPC(float Dt)
{
	auto* W = Valley();
	if (!W || bDead || AgentId == 0)
		return;
	if (IsDialogueReplyActive())
	{
		UpdateDialogueReplyPose();
		return;
	}
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
	// Explicit hostility (including an API decision or actual ReceiveStrike)
	// survives social heuristics; only invalid or protected targets cancel it here.
	AVerdantCharacter* Enemy = W->FindFolk(HostileId);
	if (!Alive(Enemy) || Enemy == this || Enemy->IsRespawnProtected())
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
			if (!Alive(Other) || Other == this || Other->IsRespawnProtected() || SameParty(Other) || Other->HostileId !=
				Leader->AgentId) continue;
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
		const bool HasCommittedPlan = FollowId >= 0 || DonationPledge > .001f || bReturningHome;
		for (AVerdantCharacter* Other : W->Folk)
		{
			if (!Alive(Other) || Other == this || Other->IsRespawnProtected() || SameParty(Other) || (bWild && Other->
				bWild)) continue;
			if (Other->AgentId == AvoidThreatId && Now < AvoidThreatUntil) continue;
			const float Distance = FVector::Dist2D(GetActorLocation(), Other->GetActorLocation());
			const bool KnownThreat = Other->HostileId == AgentId ||
				(Trust.Contains(Other->AgentId) && Trust.FindRef(Other->AgentId) <= -20.f);
			const bool WildThreat = Other->bWild && !bWild && !FriendlyEscort(Other)
				&& Trust.FindRef(Other->AgentId) < 10.f && Other->Trust.FindRef(AgentId) < 10.f;
			const bool Hunting = bWild && FollowId < 0 && Trust.FindRef(Other->AgentId) < 10.f;
			// Existing commitments override dislikes and opportunistic hunting.
			// A real attack remains a reason to defend oneself while travelling.
			if ((Other->HostileId == AgentId || (!HasCommittedPlan && (KnownThreat || WildThreat || Hunting)))
				&& Distance < BestDistance)
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
		const bool Outmatched = OutmatchedForCombat(this, Enemy);
		const bool TooFarFromDuty = BeyondCombatDuty(this, Enemy, W, Leader);
		const bool SafeRetreat = Outmatched && Distance > FMath::Max(1150.f, Enemy->Reach() + 500.f);
		if (Distance > NPCCombatMaxDistance || TooFarFromDuty || SafeRetreat)
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
	if (DonationPledge > .001f)
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

	// Combat, escorting and delivery return above. They may leave this circle,
	// but never overwrite it; ordinary life resumes in the original home range.
	if (!bReturningHome && !IsWithinHomeRange(GetActorLocation()))
	{
		if (FoodTarget.IsValid()) AvoidFoodUntil.Add(FoodTarget, Now + 25.f);
		ResetFoodRoute(this);
		bReturningHome = true;
	}
	if (bReturningHome)
	{
		// Re-enter well inside the boundary before foraging again, avoiding chatter
		// between a border food target and the return action after a short detour.
		if (IsWithinHomeRange(GetActorLocation(), HomeRangeRadius * .35f))
		{
			bReturningHome = false;
			ResetFoodRoute(this);
			Destination = GetActorLocation();
		}
		else
		{
			Destination = HomePosition;
			ActionLabel = TEXT("返回自己的活动范围");
			MoveToward(Destination, 150.f);
			return;
		}
	}

	const float TargetStock = FoodTier() == 4 ? 160.f : FoodTier() == 3 ? 90.f : FoodTier() == 2 ? 45.f : 20.f;
	bool NeedsFood = Food < TargetStock || DonationPledge > .001f;
	if (AVerdantFoodPickup* Current = FoodTarget.Get())
	{
		if (!NeedsFood || Current->Remaining <= .001f || Current->FoodTier > FoodTier()
			|| !IsWithinHomeRange(Current->GetActorLocation())
			|| (Current->GroveIndex != INDEX_NONE && !W->CanHarvest(Current->GroveIndex, this)))
			ResetFoodRoute(this);
	}
	const bool Reconsider = BrainClock <= 0.f;
	if (Reconsider)
	{
		BrainClock = .65f + AgentId * .07f;
		for (auto It = AvoidFoodUntil.CreateIterator(); It; ++It)
			if (!It.Key().IsValid() || It.Value() <= Now) It.RemoveCurrent();
		// Corpse transfers still use the same source-tier/capacity rules as the player.
		for (AVerdantCharacter* Other : W->Folk)
		{
			if (!IsValid(Other) || Other == this || Other->AgentId == 0 || !Other->bDead || Other->Food <= .001f)
				continue;
			if (IsWithinHomeRange(Other->GetActorLocation())
				&& FVector::Dist2D(GetActorLocation(), Other->GetActorLocation()) < Reach() + 120.f)
				if (W->LootCorpse(Other, this, 5.f) > .001f) break;
		}
		NeedsFood = Food < TargetStock || DonationPledge > .001f;
		if (NeedsFood && !FoodTarget.IsValid())
		{
			if (AVerdantFoodPickup* Pickup = W->FindFood(this, 6000.f, &AvoidFoodUntil))
			{
				ResetFoodRoute(this);
				FoodTarget = Pickup;
				FoodTargetDeadline = Now + 45.f + FVector::Dist2D(GetActorLocation(), Pickup->GetActorLocation()) /
					100.f;
				BrainClock = .65f + AgentId * .07f;
			}
		}
	}
	if (NeedsFood)
	{
		if (AVerdantFoodPickup* Pickup = FoodTarget.Get())
		{
			Destination = Pickup->GetActorLocation();
			ActionLabel = DonationPledge > .001f ? TEXT("为供奉寻找食物") : TEXT("寻找可获取的散落食物");
			const float DistanceSquared = FVector::DistSquared(GetActorLocation(), Destination);
			const float CollectionRange = FMath::Max(180.f, Reach() + 130.f);
			const bool WithinCollectionRange = DistanceSquared <= FMath::Square(CollectionRange);
			const bool CloseInXY = FVector::DistSquared2D(GetActorLocation(), Destination) <= FMath::Square(25.f);
			if (Now >= FoodAttemptAt && (WithinCollectionRange || CloseInXY))
			{
				// Try this exact target, so a nearer blocked pile cannot steal the attempt.
				// Matching XY with an unreachable height is also a failed attempt;
				// MoveToward may already be at its minimum arrival radius there.
				const float Taken = WithinCollectionRange ? Pickup->Take(this) : 0.f;
				if (Taken > .001f)
				{
					FoodAttemptAt = Now + 3.5f + (AgentId % 5) * .2f;
					FoodCollectionFailures = 0;
					MoveSampleAt = 0.f;
					MoveRecoveryCount = 0;
					MoveSteeringAt = 0.f;
					GetCharacterMovement()->StopMovementImmediately();
					if (!IsValid(Pickup) || Pickup->Remaining <= .001f || Food >= TargetStock) ResetFoodRoute(this);
					return;
				}
				++FoodCollectionFailures;
				FoodAttemptAt = Now + 1.5f;
				MoveSteeringAt = 0.f;
			}
			const bool StuckOnThisFood = MoveRecoveryCount >= 3
				&& FVector::DistSquared2D(MoveSampleGoal, Destination) < FMath::Square(50.f);
			if (StuckOnThisFood || FoodCollectionFailures >= 3 || Now >= FoodTargetDeadline)
			{
				AvoidFoodUntil.Add(FoodTarget, Now + 25.f + AgentId % 5);
				W->Record(DisplayName + TEXT(" 暂时放弃无法接近的食物，寻找另一处。"));
				ResetFoodRoute(this);
				Destination = GetActorLocation();
				GetCharacterMovement()->StopMovementImmediately();
				return;
			}
			const float NormalAcceptance = 120.f + GetCapsuleComponent()->GetScaledCapsuleRadius();
			const bool CloseButTooHigh = DistanceSquared > FMath::Square(CollectionRange)
				&& FVector::Dist2D(GetActorLocation(), Destination) <= NormalAcceptance + 25.f;
			MoveToward(Destination, FoodCollectionFailures > 0 || CloseButTooHigh ? 25.f : NormalAcceptance);
			return;
		}
	}
	if (!Reconsider)
	{
		if (IsWithinHomeRange(Destination))
		{
			MoveToward(Destination, 150.f);
			return;
		}
		// A finished special action can leave its old destination outside home.
		BrainClock = 0.f;
	}

	// Preserve the modest ecological action, funded only by the NPC's actual spare inventory.
	const int32 NearbyGrove = W->NearestGrove(GetActorLocation(), 650.f);
	if (!bWild && NearbyGrove != INDEX_NONE && DonationPledge <= .001f && Now >= NextTendAt
		&& ActionCooldown <= 0.f && W->Groves[NearbyGrove].Fertility < .55f
		&& IsWithinHomeRange(W->Groves[NearbyGrove].Position)
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
	const float WanderRadius = FMath::Max(0.f, HomeRangeRadius) * .35f;
	Destination = W->SurfacePoint(HomePosition + FVector(FMath::Cos(Phase), FMath::Sin(Phase), 0.f) * WanderRadius);
	ActionLabel = Food >= TargetStock ? TEXT("带着储粮，观察附近") : TEXT("等待附近食物，再作打算");
	MoveToward(Destination, 150.f);
}
