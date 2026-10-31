#include "VerdantPopulation.h"
#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "VerdantEconomyRules.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"

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

	bool Alive(const AVerdantCharacter* Person) { return IsValid(Person) && !Person->bDead; }

	FVector ChooseGround(AVerdantWorld* W, FVector Anchor, float Radius, const TArray<FVector>& Occupied,
	                     FRandomStream& Random)
	{
		FVector Best = W->SurfacePoint(Anchor);
		float BestScore = BIG_NUMBER;
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const float Angle = Random.FRandRange(0.f, 2.f * PI);
			const float Distance = Attempt == 0 ? 0.f : Random.FRandRange(120.f, 900.f);
			const FVector Point = W->SurfacePoint(Anchor + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Distance);
			bool Crowded = false;
			for (const FVector& Existing : Occupied)
				if (FVector::DistSquared2D(Point, Existing) < FMath::Square(Radius + 260.f))
				{
					Crowded = true;
					break;
				}
			if (Crowded) continue;
			float Roughness = 0.f;
			for (const FVector& Offset : {
				     FVector(Radius, 0, 0), FVector(-Radius, 0, 0), FVector(0, Radius, 0), FVector(0, -Radius, 0)
			     })
				Roughness = FMath::Max(Roughness, float(FMath::Abs(W->SurfacePoint(Point + Offset).Z - Point.Z)));
			const float Score = Roughness + Distance * .025f;
			if (Score < BestScore)
			{
				Best = Point;
				BestScore = Score;
			}
		}
		return Best;
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
	if (!W || bSpawned || W->Groves.Num() < 3) return;
	bSpawned = true;
	GConfig->GetInt(TEXT("Verdant.Population"), TEXT("Seed"), WorldSeed, GGameIni);
	FRandomStream Random(WorldSeed);
	const int32 Tiers[] = {1, 2, 4, 1, 3, 1, 2, 3, 2, 4, 3, 2, 3, 4, 4, 1, 4, 1, 3, 2};
	static_assert(UE_ARRAY_COUNT(Tiers) == UE_ARRAY_COUNT(Profiles));
	TArray<FVector> Occupied;
	const FVector Arrival(-1000, -5800, 0);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Profiles); ++Index)
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
		const FVector Ground = ChooseGround(W, Anchor, FMath::Max(80.f, Size * 45.f), Occupied, Random);
		Occupied.Add(Ground);
		// Initial capsule is 90cm high; SetSize preserves its foot point.
		const FTransform Transform(FRotator(0, Random.FRandRange(0.f, 360.f), 0), Ground + FVector(0, 0, 95.f));
		auto* N = GetWorld()->SpawnActorDeferred<AVerdantCharacter>(AVerdantCharacter::StaticClass(), Transform,
		                                                            W, nullptr,
		                                                            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!N) continue;
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
		N->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		N->UpdateAppearance();
		UE_LOG(LogTemp, Display, TEXT("VERDANT_RESIDENT id=%d type=%s tier=%d height_cm=%.1f model=%s"),
		       Id, *N->CharacterTypeId, N->FoodTier(), N->HeightCm(), *N->ModelPath);
	}
	GenerateRelations(W);
	W->Record(TEXT("二十位不同性格、不同体型的居民来到山谷；他们的旧识、合作与分歧已经登记。"));
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
		if (!Alive(From) || !Alive(To) || !MatchesType(From->CharacterTypeId, Rule.SourceTypes) || !MatchesType(
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
				if (!Alive(A) || !Alive(B) || A->AgentId <= 0 || B->AgentId <= 0 || A == B) continue;
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
	UE_LOG(LogTemp, Display, TEXT("VERDANT_RELATIONS seed=%d residents=%d edges=%d"), WorldSeed, 20, Relations.Num());
}

FString UVerdantPopulation::ContextFor(const AVerdantCharacter* Who) const
{
	const auto* W = Cast<AVerdantWorld>(GetOwner());
	if (!W || !Alive(Who))return FString();
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
		if (FVector::Dist(Who->GetActorLocation(), Other->GetActorLocation()) <= VerdantEconomy::HearingRadiusCm)
			Result += Other->bDead ? TEXT("对方已倒下。\n") : FString::Printf(TEXT("对方在附近，当前行动=%s。\n"), *Other->ActionLabel);
		else Result += TEXT("对方不在附近，你不知道其此刻的库存和行为。\n");
	}
	return Result + RequestSummary(Who);
}

FString UVerdantPopulation::RequestSummary(const AVerdantCharacter* Who) const
{
	const auto* W = Cast<AVerdantWorld>(GetOwner());
	const auto* Donor = W ? W->FindFolk(2) : nullptr;
	if (!Alive(Who) || !IsValid(Donor) || !(Who->AgentId == 1 || Who->AgentId == 2 || Who->AgentId == 3 || Who->AgentId
		== 5))
		return FString();
	if (Who != Donor && FVector::Dist(Who->GetActorLocation(), Donor->GetActorLocation()) >
		VerdantEconomy::HearingRadiusCm)
		return TEXT("你只知道陶恩与米芽是旧识，和岚石有过交往；陶恩此刻的送粮计划未知，不能声称已经送达。\n");
	if (SharedRequestState == TEXT("fulfilled"))return TEXT("共同事件：陶恩已实际送达本次约定养分，不能重复索要或奖励。\n");
	if (SharedRequestState == TEXT("failed"))return TEXT("共同事件：陶恩已经死亡，本次保护送达失败。不能把别人捡粮供奉称为救活他。\n");
	if (W->bRestored)return TEXT("母树已经复苏，这次送粮不再必要。\n");
	if (!Alive(Donor))return FString();
	if (SharedRequestState == TEXT("active") || SharedRequestState == TEXT("blocked"))
		return FString::Printf(TEXT("共同事件：陶恩承诺实际送达%.1f份，目前实送%.1f；状态=%s。可以商量保护或让真实威胁退让，不能假报完成。\n"),
		                       DeliveryAmount, FMath::Max(0.f, W->DonationLedger.FindRef(2) - DeliveryBaseline),
		                       *SharedRequestState);
	const float Available = W->MaxNegotiatedDonation(Donor);
	if (Available <= .01f)return TEXT("陶恩目前没有满足真实协商条件的可供奉额度，不要虚构一批待送粮。可以先商量实际互助。\n");
	bool Threat = false;
	for (AVerdantCharacter* Other : W->Folk)
		if (Alive(Other) && Other != Donor && Other->HostileId == 2 &&
			FVector::Dist2D(Other->GetActorLocation(), Donor->GetActorLocation()) < 2000.f)
			Threat = true;
	return FString::Printf(TEXT("可商量的共同事件：陶恩可亲自送最多%.1f份真实余粮到母树。%s接受donate才建立计划，保护目标可以是陶恩(ID2)。\n"),
	                       Available, Threat ? TEXT("附近有正在威胁他的对手，可以请求保护或停战。") : TEXT("目前没有看到对他的直接威胁，不能硬说道路被堵。"));
}

void UVerdantPopulation::ObserveDelivery(AVerdantCharacter* Donor)
{
	auto* W = Cast<AVerdantWorld>(GetOwner());
	if (!W || !Alive(Donor) || Donor->AgentId != 2 || W->bRestored || Donor->DonationPledge <= .001f
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
	if (!Alive(Donor) || Donor->LifeRevision != DeliveryLifeRevision)
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
