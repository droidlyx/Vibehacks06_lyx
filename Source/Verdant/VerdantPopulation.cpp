#include "VerdantPopulation.h"
#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "VerdantDialogue.h"
#include "VerdantEconomyRules.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "ProceduralMeshComponent.h"

namespace
{
	struct FResidentProfile
	{
		const TCHAR* Type;
		const TCHAR* Name;
		const TCHAR* Motivation;
		const TCHAR* Persona;
		const TCHAR* Tags;
		const TCHAR* Mesh;
		const TCHAR* Idle;
		const TCHAR* Walk;
	};

	struct FRelationRule
	{
		const TCHAR* Id;
		const TCHAR* Label;
		const TCHAR* SourceTypes;
		const TCHAR* TargetTypes;
		bool Symmetric;
		float Weight;
		const TCHAR* Excludes;
		int32 MaxA;
		int32 MaxB;
	};

#include "VerdantPopulationProfiles.inl"
#include "VerdantPopulationRelations.inl"

	bool MatchesType(const FString& Type, const TCHAR* Allowed)
	{
		if (FString(Allowed) == TEXT("*")) return true;
		return (TEXT(",") + FString(Allowed) + TEXT(",")).Contains(TEXT(",") + Type + TEXT(","));
	}

	bool PopulationActorAlive(const AVerdantCharacter* Person) { return IsValid(Person) && !Person->bDead; }

	constexpr float SpawnFootClearance = 5.f;
	constexpr float SpawnMinimumNormalZ = .819152f; // At most 35 degrees, below the movement limit.
	const FVector ArrivalPoint(-1000.f, -5800.f, 0.f);

	bool ReadTerrainFloor(AVerdantWorld* W, const FVector& XY, FHitResult& Hit)
	{
		if (!W || !W->Ground || !W->GetWorld()) return false;
		const FVector Estimate = W->SurfacePoint(XY);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VerdantNPCSpawnFloor), true);
		return W->GetWorld()->LineTraceSingleByChannel(Hit, Estimate + FVector(0, 0, 20000.f),
		                                               Estimate - FVector(0, 0, 20000.f), ECC_Visibility, Query)
			&& Hit.bBlockingHit && Hit.GetComponent() == W->Ground;
	}

	bool ClearCapsule(AVerdantWorld* W, const FVector& Foot, float Radius, float HalfHeight,
	                  const AActor* Ignore = nullptr)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VerdantNPCSpawnSpace), false);
		if (Ignore) Query.AddIgnoredActor(Ignore);
		return !W->GetWorld()->OverlapBlockingTestByChannel(
			Foot + FVector(0, 0, HalfHeight + SpawnFootClearance), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), Query);
	}

	bool ChooseGround(AVerdantWorld* W, const FVector& Anchor, float Radius, float HalfHeight,
	                  float InitialRadius, float InitialHalfHeight, bool KeepAwayFromArrival,
	                  const TArray<FVector>& Occupied, FRandomStream& Random, FVector& OutGround)
	{
		// Try nearby positions first, then expand. A low relative score never makes an invalid spot legal.
		constexpr float SearchRanges[] = {900.f, 1800.f, 3600.f, 6000.f};
		for (int32 Ring = 0; Ring < UE_ARRAY_COUNT(SearchRanges); ++Ring)
		{
			bool Found = false;
			float BestScore = BIG_NUMBER;
			for (int32 Attempt = 0; Attempt < 64; ++Attempt)
			{
				const float Angle = Random.FRandRange(0.f, 2.f * PI);
				const float Distance = Ring == 0 && Attempt == 0
					                       ? 0.f
					                       : Random.FRandRange(Ring == 0 ? 120.f : SearchRanges[Ring - 1],
					                                           SearchRanges[Ring]);
				const FVector XY = Anchor + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Distance;
				if (FMath::Abs(XY.X) > 11000.f || FMath::Abs(XY.Y) > 11000.f) continue;
				const float ArrivalClearance = KeepAwayFromArrival ? 2200.f : 450.f;
				if (FVector::DistSquared2D(XY, ArrivalPoint) < FMath::Square(ArrivalClearance)) continue;
				bool Crowded = false;
				for (const FVector& Existing : Occupied)
					if (FVector::DistSquared2D(XY, Existing) < FMath::Square(Radius + 260.f))
					{
						Crowded = true;
						break;
					}
				if (Crowded) continue;
				FHitResult Floor;
				if (!ReadTerrainFloor(W, XY, Floor) || Floor.ImpactNormal.Z < SpawnMinimumNormalZ) continue;
				const FVector Point = Floor.ImpactPoint;
				const float SupportRadius = FMath::Max(10.f, Radius * .75f);
				const float MaxGroundVariation = FMath::Max(4.f, FMath::Min(30.f, Radius * .65f));
				float Roughness = 0.f;
				bool Supported = true;
				for (const FVector& Offset : {
					     FVector(SupportRadius, 0, 0), FVector(-SupportRadius, 0, 0),
					     FVector(0, SupportRadius, 0), FVector(0, -SupportRadius, 0)
				     })
				{
					FHitResult Support;
					if (!ReadTerrainFloor(W, Point + Offset, Support) || Support.ImpactNormal.Z < SpawnMinimumNormalZ)
					{
						Supported = false;
						break;
					}
					Roughness = FMath::Max(Roughness, float(FMath::Abs(Support.ImpactPoint.Z - Point.Z)));
				}
				if (!Supported || Roughness > MaxGroundVariation) continue;
				// Check the final body and the constructor capsule used during deferred spawning.
				// No owner is ignored: terrain, the world's tree/rock components and prior residents all count.
				if (!ClearCapsule(W, Point, Radius, HalfHeight)
					|| !ClearCapsule(W, Point, InitialRadius, InitialHalfHeight))
					continue;
				const float Score = Roughness + Distance * .025f;
				if (Score < BestScore)
				{
					OutGround = Point;
					BestScore = Score;
					Found = true;
				}
			}
			if (Found) return true;
		}
		return false;
	}
}

UVerdantPopulation::UVerdantPopulation()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .5f;
}

UVerdantPopulation* UVerdantPopulation::Find(const UObject* Context)
{
	const auto* W = AVerdantWorld::Find(Context);
	return W ? W->FindComponentByClass<UVerdantPopulation>() : nullptr;
}

void UVerdantPopulation::SpawnResidents()
{
	auto* W = Cast<AVerdantWorld>(GetOwner());
	if (!W || bSpawnAttempted || W->Groves.Num() < 3) return;
	FHitResult ArrivalFloor;
	if (!ReadTerrainFloor(W, ArrivalPoint, ArrivalFloor))
	{
		if (!bWaitingForTerrainLogged)
		{
			UE_LOG(LogTemp, Display, TEXT("VERDANT_POPULATION_WAIT reason=terrain_not_queryable"));
			bWaitingForTerrainLogged = true;
		}
		return;
	}
	// Only the terrain-readiness probe is retried. A completed spawn attempt never creates duplicate IDs.
	bSpawnAttempted = true;
	GConfig->GetInt(TEXT("Verdant.Population"), TEXT("Seed"), WorldSeed, GGameIni);
	FRandomStream Random(WorldSeed);
	const int32 Tiers[] = {1, 2, 4, 1, 3, 1, 2, 3, 2, 4, 3, 2, 3, 4, 4, 1, 4, 1, 3, 2};
	static_assert(UE_ARRAY_COUNT(Tiers) == UE_ARRAY_COUNT(Profiles));
	constexpr int32 ExpectedCount = UE_ARRAY_COUNT(Tiers);
	TArray<FVector> Occupied;
	TArray<int32> FailedIds;
	int32 SpawnedCount = 0;
	const auto* DefaultCapsule = GetDefault<AVerdantCharacter>()->GetCapsuleComponent();
	const float InitialRadius = DefaultCapsule->GetUnscaledCapsuleRadius();
	const float InitialHalfHeight = DefaultCapsule->GetUnscaledCapsuleHalfHeight();
	const FVector Arrival(-1000, -5800, 0);
	for (int32 Index = 0; Index < ExpectedCount; ++Index)
	{
		const auto& Profile = Profiles[Index];
		const int32 Id = Index + 1;
		const int32 Tier = Tiers[Index];
		const float Height = Tier == 1
			                     ? Random.FRandRange(65.f, 110.f)
			                     : Tier == 2
			                     ? Random.FRandRange(135.f, 185.f)
			                     : Tier == 3
			                     ? Random.FRandRange(220.f, 295.f)
			                     : Random.FRandRange(335.f, 410.f);
		const float Size = Height / 180.f;
		FVector Anchor = Id <= 3 ? W->Groves[Id - 1].Position : W->Groves[(Id - 1) % 3].Position;
		if (Id == 1 || Id == 4 || Id == 6)
			Anchor = Arrival + FVector((Id == 1 ? -600.f : Id == 4 ? 500.f : 900.f), 950.f, 0);
		if (Id == 5) Anchor = W->Groves[1].Position + FVector(-200.f, 1450.f, 0);
		FVector Ground;
		const float FinalRadius = 32.f * Size;
		const float FinalHalfHeight = Height * .5f;
		const bool KeepAwayFromArrival = FString(Profile.Tags).Contains(TEXT("predator"));
		if (!ChooseGround(W, Anchor, FinalRadius, FinalHalfHeight, InitialRadius, InitialHalfHeight,
		                  KeepAwayFromArrival, Occupied, Random, Ground))
		{
			FailedIds.Add(Id);
			UE_LOG(LogTemp, Error, TEXT("VERDANT_RESIDENT_SPAWN_FAILED id=%d type=%s tier=%d reason=no_safe_ground"),
			       Id, Profile.Type, Tier);
			continue;
		}
		// SetSize preserves the constructor capsule's world-space foot point.
		const FTransform Transform(FRotator(0, Random.FRandRange(0.f, 360.f), 0),
		                           Ground + FVector(0, 0, InitialHalfHeight + SpawnFootClearance));
		auto* N = GetWorld()->SpawnActorDeferred<AVerdantCharacter>(AVerdantCharacter::StaticClass(), Transform,
		                                                            W, nullptr,
		                                                            ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
		if (!N)
		{
			FailedIds.Add(Id);
			UE_LOG(LogTemp, Error, TEXT("VERDANT_RESIDENT_SPAWN_FAILED id=%d type=%s reason=actor_spawn_rejected"), Id,
			       Profile.Type);
			continue;
		}
		N->AgentId = Id;
		N->CharacterTypeId = Profile.Type;
		N->DisplayName = Id == 1 ? TEXT("米芽") : Id == 2 ? TEXT("陶恩") : Id == 3 ? TEXT("岚石") : Profile.Name;
		N->Motivation = Profile.Motivation;
		N->Persona = Profile.Persona;
		FString(Profile.Tags).ParseIntoArray(N->SocialTags, TEXT(","), true);
		N->ModelPath = Profile.Mesh;
		N->IdlePath = Profile.Idle;
		N->WalkPath = Profile.Walk;
		N->BodySize = Size;
		N->HomePosition = Ground;
		N->HomeGrove = Id <= 3 ? Id - 1 : (Id - 1) % 3;
		N->Outfit = Index % 3;
		N->BodyColor = FLinearColor::MakeFromHSV8(uint8(Index * 37), 120, 190);
		N->bWild = N->SocialTags.Contains(TEXT("predator"));
		N->Food = Tier == 1 ? 8.f : Tier == 2 ? 18.f : Tier == 3 ? 38.f : 70.f;
		N->SharePledge = Id == 2 ? .2f : 0.f;
		N->Destination = Ground;
		N->ActionLabel = TEXT("观察附近的食物与居民");
		N->BrainClock = Index * .08f;
		W->Folk.Add(N);
		UGameplayStatics::FinishSpawningActor(N, Transform);
		if (!IsValid(N) || N->IsActorBeingDestroyed()
			|| !ClearCapsule(W, Ground, FinalRadius, FinalHalfHeight, N))
		{
			W->Folk.Remove(N);
			if (IsValid(N)) N->Destroy();
			FailedIds.Add(Id);
			UE_LOG(LogTemp, Error, TEXT("VERDANT_RESIDENT_SPAWN_FAILED id=%d type=%s reason=final_capsule_blocked"), Id,
			       Profile.Type);
			continue;
		}
		Occupied.Add(Ground);
		++SpawnedCount;
		N->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		N->UpdateAppearance();
		UE_LOG(LogTemp, Display, TEXT("VERDANT_RESIDENT id=%d type=%s tier=%d height_cm=%.1f model=%s"),
		       Id, *N->CharacterTypeId, N->FoodTier(), N->HeightCm(), *N->ModelPath);
	}
	GenerateRelations(W);
	bSpawned = SpawnedCount == ExpectedCount;
	UE_LOG(LogTemp, Display, TEXT("VERDANT_POPULATION_READY ready=%d expected=%d actual=%d"),
	       bSpawned ? 1 : 0, ExpectedCount, SpawnedCount);
	if (bSpawned)
		W->Record(TEXT("二十位不同性格、不同体型的居民来到山谷；他们的旧识、合作与分歧已经登记。"));
	else
	{
		FString Missing;
		for (const int32 Id : FailedIds)
			Missing += FString::Printf(
				TEXT("%s%d"), Missing.IsEmpty() ? TEXT("") : TEXT(","), Id);
		UE_LOG(LogTemp, Error, TEXT("VERDANT_POPULATION_INCOMPLETE expected=%d actual=%d missing_ids=%s"),
		       ExpectedCount, SpawnedCount, *Missing);
		W->Record(FString::Printf(TEXT("居民生成不完整：%d/20；未找到安全落点的ID：%s。"), SpawnedCount, *Missing));
	}
}

bool UVerdantPopulation::HasRelation(int32 A, int32 B, const FString& Kind) const
{
	return Relations.ContainsByPredicate([&](const FVerdantInstanceRelation& R)
	{
		return R.Kind == Kind && ((R.A == A && R.B == B) || (R.bSymmetric && R.A == B && R.B == A));
	});
}

void UVerdantPopulation::GenerateRelations(AVerdantWorld* W)
{
	Relations.Reset();
	TMap<int32, int32> Degree;
	auto Add = [&](int32 A, int32 B, const FRelationRule& Rule)
	{
		auto* From = W->FindFolk(A);
		auto* To = W->FindFolk(B);
		if (!PopulationActorAlive(From) || !PopulationActorAlive(To) || !MatchesType(
			From->CharacterTypeId, Rule.SourceTypes) || !MatchesType(
			To->CharacterTypeId, Rule.TargetTypes))
			return false;
		if (A == B || Degree.FindRef(A) >= 4 || Degree.FindRef(B) >= 4 || HasRelation(A, B, Rule.Id)) return false;
		int32 PairCount = 0, SourceCount = 0, TargetCount = 0;
		for (const auto& R : Relations)
		{
			if (R.Kind == Rule.Id)
			{
				if (R.A == A || (R.bSymmetric && R.B == A)) ++SourceCount;
				if (R.B == B || (R.bSymmetric && R.A == B)) ++TargetCount;
			}
			if ((R.A == A && R.B == B) || (R.A == B && R.B == A))
			{
				++PairCount;
				if (MatchesType(R.Kind, Rule.Excludes)) return false;
			}
		}
		const int32 SourceLimit = Rule.Symmetric ? FMath::Min(Rule.MaxA, Rule.MaxB) : Rule.MaxA;
		const int32 TargetLimit = Rule.Symmetric ? FMath::Min(Rule.MaxA, Rule.MaxB) : Rule.MaxB;
		if (PairCount >= 2 || SourceCount >= SourceLimit || TargetCount >= TargetLimit) return false;
		FVerdantInstanceRelation Relation;
		Relation.A = A;
		Relation.B = B;
		Relation.Kind = Rule.Id;
		Relation.Label = Rule.Label;
		Relation.bSymmetric = Rule.Symmetric;
		Relations.Add(Relation);
		++Degree.FindOrAdd(A);
		++Degree.FindOrAdd(B);
		const float Trust = Relation.Kind == TEXT("R10")
			                    ? -8.f
			                    : Relation.Kind == TEXT("R18") || Relation.Kind == TEXT("R21")
			                    ? -2.f
			                    : 12.f;
		From->Trust.FindOrAdd(B) = Trust;
		if (Rule.Symmetric) To->Trust.FindOrAdd(A) = Trust;
		return true;
	};
	// One connected triangle. Seeded backgrounds never create payment or completed aid.
	for (const auto& Rule : RelationRules)
	{
		if (FString(Rule.Id) == TEXT("R01"))
		{
			Add(1, 2, Rule);
			Add(2, 3, Rule);
		}
		if (FString(Rule.Id) == TEXT("R10")) Add(5, 2, Rule);
		if (FString(Rule.Id) == TEXT("R32")) Add(9, 2, Rule);
		if (FString(Rule.Id) == TEXT("R34")) Add(13, 12, Rule);
		if (FString(Rule.Id) == TEXT("R37")) Add(19, 15, Rule);
		if (FString(Rule.Id) == TEXT("R07")) Add(10, 4, Rule);
		if (FString(Rule.Id) == TEXT("R19")) Add(6, 17, Rule);
		if (FString(Rule.Id) == TEXT("R20")) Add(7, 18, Rule);
	}
	struct FCandidate
	{
		int32 A;
		int32 B;
		int32 Rule;
		float Priority;
	};
	TArray<FCandidate> Candidates;
	FRandomStream Random(WorldSeed + 811);
	for (int32 RuleIndex = 0; RuleIndex < UE_ARRAY_COUNT(RelationRules); ++RuleIndex)
	{
		const auto& Rule = RelationRules[RuleIndex];
		const FString Kind = Rule.Id;
		for (AVerdantCharacter* A : W->Folk)
			for (AVerdantCharacter* B : W->Folk)
			{
				if (!PopulationActorAlive(A) || !PopulationActorAlive(B) || A->AgentId <= 0 || B->AgentId <= 0 || A ==
					B)
					continue;
				if (Rule.Symmetric && A->AgentId > B->AgentId) continue;
				if (!MatchesType(A->CharacterTypeId, Rule.SourceTypes) || !MatchesType(
					B->CharacterTypeId, Rule.TargetTypes))
					continue;
				if (FVector::Dist2D(A->HomePosition, B->HomePosition) > 10000.f) continue;
				if (Kind == TEXT("R18") && A->HomeGrove != B->HomeGrove) continue;
				if ((Kind == TEXT("R07") || Kind == TEXT("R10")) && A->FoodTier() < B->FoodTier()) continue;
				const float Distance = FVector::Dist2D(A->HomePosition, B->HomePosition);
				Candidates.Add({
					A->AgentId, B->AgentId, RuleIndex,
					Random.FRand() / FMath::Max(1.f, Rule.Weight) + Distance * .000015f
				});
			}
	}
	Candidates.StableSort([](const FCandidate& A, const FCandidate& B) { return A.Priority < B.Priority; });
	for (const auto& Candidate : Candidates)
	{
		const auto& Rule = RelationRules[Candidate.Rule];
		bool ExistingPressure = false;
		if (FString(Rule.Id) == TEXT("R10"))
			for (const auto& R : Relations)
				if (R.Kind == TEXT("R10") && (R.A == Candidate.A || R.B == Candidate.B)) ExistingPressure = true;
		if (!ExistingPressure)Add(Candidate.A, Candidate.B, Rule);
	}
	int32 ResidentCount = 0;
	for (const AVerdantCharacter* Person : W->Folk)
		if (PopulationActorAlive(Person) && Person->AgentId > 0) ++ResidentCount;
	UE_LOG(LogTemp, Display, TEXT("VERDANT_RELATIONS seed=%d residents=%d edges=%d"), WorldSeed, ResidentCount,
	       Relations.Num());
}

FString UVerdantPopulation::ContextFor(const AVerdantCharacter* Who) const
{
	const auto* W = Cast<AVerdantWorld>(GetOwner());
	if (!W || !PopulationActorAlive(Who))return FString();
	const float HearingRadius =
		W->Dialogue ? W->Dialogue->GetHearingRadiusCm() : VerdantDialoguePolicy::DefaultRadiusCm;
	FString Result = TEXT("你的已知关系（开局背景不代表已支付食物、当前正在执行保护或已完成任务）：\n");
	for (const auto& R : Relations)
	{
		if (R.A != Who->AgentId && R.B != Who->AgentId)continue;
		const int32 OtherId = R.A == Who->AgentId ? R.B : R.A;
		const auto* Other = W->FindFolk(OtherId);
		const auto* A = W->FindFolk(R.A);
		const auto* B = W->FindFolk(R.B);
		if (!IsValid(Other) || !IsValid(A) || !IsValid(B))continue;
		Result += FString::Printf(TEXT("%s(%s)：A=%s(ID%d)，B=%s(ID%d)；你是%s。"),
		                          *R.Label, *R.Kind, *A->DisplayName, R.A, *B->DisplayName, R.B,
		                          R.A == Who->AgentId ? TEXT("A") : TEXT("B"));
		if (FVector::Dist(Who->GetActorLocation(), Other->GetActorLocation()) <= HearingRadius)
			Result += Other->bDead ? TEXT("对方已倒下。\n") : FString::Printf(TEXT("对方在附近，当前行动=%s。\n"), *Other->ActionLabel);
		else Result += TEXT("对方不在附近，你不知道其此刻的库存和行为。\n");
	}
	return Result + RequestSummary(Who);
}

FString UVerdantPopulation::RequestSummary(const AVerdantCharacter* Who) const
{
	const auto* W = Cast<AVerdantWorld>(GetOwner());
	const auto* Donor = W ? W->FindFolk(2) : nullptr;
	if (!PopulationActorAlive(Who) || !IsValid(Donor) || !(Who->AgentId == 1 || Who->AgentId == 2 || Who->AgentId == 3
		|| Who->AgentId
		== 5))
		return FString();
	const float HearingRadius =
		W->Dialogue ? W->Dialogue->GetHearingRadiusCm() : VerdantDialoguePolicy::DefaultRadiusCm;
	if (Who != Donor && FVector::Dist(Who->GetActorLocation(), Donor->GetActorLocation()) > HearingRadius)
		return TEXT("你只知道陶恩与米芽是旧识，和岚石有过交往；陶恩此刻的送粮计划未知，不能声称已经送达。\n");
	if (SharedRequestState == TEXT("fulfilled"))return TEXT("共同事件：陶恩已实际送达本次约定养分，不能重复索要或奖励。\n");
	if (SharedRequestState == TEXT("failed"))return TEXT("共同事件：陶恩已经死亡，本次保护送达失败。不能把别人捡粮供奉称为救活他。\n");
	if (W->bRestored)return TEXT("母树已经复苏，这次送粮不再必要。\n");
	if (!PopulationActorAlive(Donor))return FString();
	if (SharedRequestState == TEXT("active") || SharedRequestState == TEXT("blocked"))
		return FString::Printf(TEXT("共同事件：陶恩承诺实际送达%.1f份，目前实送%.1f；状态=%s。可以商量保护或让真实威胁退让，不能假报完成。\n"),
		                       DeliveryAmount, FMath::Max(0.f, W->DonationLedger.FindRef(2) - DeliveryBaseline),
		                       *SharedRequestState);
	const float Available = W->MaxNegotiatedDonation(Donor);
	if (Available <= .01f)return TEXT("陶恩目前没有满足真实协商条件的可供奉额度，不要虚构一批待送粮。可以先商量实际互助。\n");
	bool Threat = false;
	for (AVerdantCharacter* Other : W->Folk)
		if (PopulationActorAlive(Other) && Other != Donor && Other->HostileId == 2 &&
			FVector::Dist(Other->GetActorLocation(), Donor->GetActorLocation()) <= HearingRadius)
			Threat = true;
	return FString::Printf(TEXT("可商量的共同事件：陶恩可亲自送最多%.1f份真实余粮到母树。%s接受donate才建立计划，保护目标可以是陶恩(ID2)。\n"),
	                       Available, Threat ? TEXT("附近有正在威胁他的对手，可以请求保护或停战。") : TEXT("目前没有看到对他的直接威胁，不能硬说道路被堵。"));
}

void UVerdantPopulation::ObserveDelivery(AVerdantCharacter* Donor)
{
	auto* W = Cast<AVerdantWorld>(GetOwner());
	if (!W || !PopulationActorAlive(Donor) || Donor->AgentId != 2 || W->bRestored || Donor->DonationPledge <= .001f
		|| SharedRequestState == TEXT("fulfilled") || SharedRequestState == TEXT("failed") || SharedRequestState ==
		TEXT("obsolete"))
		return;
	if (SharedRequestState != TEXT("active") && SharedRequestState != TEXT("blocked"))
		DeliveryBaseline = W->
		                   DonationLedger.FindRef(2);
	DeliveryAmount = FMath::Max(0.f, W->DonationLedger.FindRef(2) - DeliveryBaseline) + Donor->DonationPledge;
	DeliveryLifeRevision = Donor->LifeRevision;
	SharedRequestState = TEXT("active");
	W->Record(TEXT("陶恩实际接受了送粮计划；以他的真实送达记录判断结果。"));
}

void UVerdantPopulation::TickComponent(float DeltaTime, ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bSpawned || !(SharedRequestState == TEXT("active") || SharedRequestState == TEXT("blocked")))return;
	EventClock += DeltaTime;
	if (EventClock < 1.f)return;
	EventClock = 0.f;
	auto* W = Cast<AVerdantWorld>(GetOwner());
	auto* Donor = W ? W->FindFolk(2) : nullptr;
	if (!W)return;
	if (!PopulationActorAlive(Donor) || Donor->LifeRevision != DeliveryLifeRevision)
	{
		SharedRequestState = TEXT("failed");
		W->Record(TEXT("陶恩的送粮约定因本人死亡而失败；世界仍继续运行。"));
		return;
	}
	const float Delivered = W->DonationLedger.FindRef(2) - DeliveryBaseline;
	if (Delivered + .001f >= DeliveryAmount)
	{
		SharedRequestState = TEXT("fulfilled");
		W->Record(TEXT("陶恩的约定已经实际送达，事件完成；没有生成额外粮食。"));
		return;
	}
	if (W->bRestored)
	{
		SharedRequestState = TEXT("obsolete");
		W->Record(TEXT("母树已经由实际供奉复苏，陶恩剩余的送粮约定不再必要。"));
		return;
	}
	SharedRequestState = Donor->DonationPledge <= .001f || Donor->Food <= .001f ? TEXT("blocked") : TEXT("active");
}
