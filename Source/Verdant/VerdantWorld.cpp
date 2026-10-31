#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "VerdantPopulation.h"
#include "VerdantFoodPickup.h"
#include "VerdantEconomyRules.h"
#include "VerdantAudioComponent.h"
#include "VerdantGroundCoverComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	constexpr float GroveInteractionRadius = 700.f;
	constexpr float GroveCapacity = 48.f;

	bool IsLiving(const AVerdantCharacter* Who)
	{
		return IsValid(Who) && !Who->bDead;
	}
}

AVerdantWorld::AVerdantWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ValleyGround"));
	SetRootComponent(Ground);
	Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Ground->bUseAsyncCooking = true;
	Dialogue = CreateDefaultSubobject<UVerdantDialogue>(TEXT("WorldDialogue"));
	Population = CreateDefaultSubobject<UVerdantPopulation>(TEXT("Population"));
}

void AVerdantWorld::BeginPlay()
{
	Super::BeginPlay();

	Groves.Reset();
	const FVector Centers[] = {
		FVector(-4500.f, -1500.f, 0.f),
		FVector(4200.f, -1000.f, 0.f),
		FVector(500.f, 6500.f, 0.f)
	};
	const TCHAR* Names[] = {TEXT("苔石果林"), TEXT("赤土果林"), TEXT("高地果林")};
	const FLinearColor Colors[] = {
		FLinearColor(.30f, .68f, .43f), FLinearColor(.84f, .43f, .23f),
		FLinearColor(.43f, .56f, .83f)
	};
	for (int32 I = 0; I < 3; ++I)
	{
		FVerdantGrove Grove;
		Grove.Name = Names[I];
		Grove.Position = SurfacePoint(Centers[I]);
		Grove.Color = Colors[I];
		Grove.ControllerId = I + 1;
		Grove.Food = I == 0 ? 42.f : I == 1 ? 8.f : 48.f;
		Grove.Fertility = I == 1 ? .35f : 1.f;
		Grove.bOpen = I != 2;
		Grove.Share = I == 1 ? .2f : 0.f;
		Groves.Add(Grove);
	}
	MotherPosition = SurfacePoint(FVector(0.f, 3600.f, 0.f));
	BuildLandscape();
	BuildScenery();
	SpawnResidents();
	SpawnFoodField();

	if (auto* Player = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		Folk.AddUnique(Player);
		Player->AgentId = 0;
		Player->UpdateAppearance();
	}
	BeginSeasonRecord();
	Record(TEXT("生长季开始。20位居民分布在山谷，四档散落食物等待探索。母树等待100份养分。"));
	Record(TEXT("米芽开放采集；陶恩收取两成分成；岚石暂不允许外人进入高地果林。"));
	Notify(TEXT("WASD 移动 · E 吸收附近食物 · 每10秒自动成长 · T 附近交谈 · F 分享或供奉"));
	UpdateGroveVisuals();
	// Detailed groundcover is a bounded world-cell cache, not player-relative scaling.
	auto* GroundCover = NewObject<UVerdantGroundCoverComponent>(this, TEXT("ValleyGroundCover"));
	AddInstanceComponent(GroundCover);
	GroundCover->RegisterComponent();
	// Instance-owned audio stops with this world and is recreated on a new journey.
	auto* Audio = NewObject<UVerdantAudioComponent>(this, TEXT("ValleyAudio"));
	AddInstanceComponent(Audio);
	Audio->RegisterComponent();
}

void AVerdantWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += FMath::Max(0.f, DeltaSeconds);

	// GameMode may create the world before the player pawn is possessed.
	if (auto* Player = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		if (!Folk.Contains(Player))
		{
			Player->AgentId = 0;
			Folk.Add(Player);
			Player->UpdateAppearance();
		}
	}
	TickEconomy(DeltaSeconds);
	TickPolicyProbe(DeltaSeconds);
	if (GetWorld()->GetTimeSeconds() > NoticeUntil)
		Notice.Empty();
}

AVerdantWorld* AVerdantWorld::Find(const UObject* Context)
{
	if (!Context || !Context->GetWorld())
		return nullptr;
	for (TActorIterator<AVerdantWorld> It(Context->GetWorld()); It; ++It)
		return *It;
	return nullptr;
}

void AVerdantWorld::SpawnResidents()
{
    if (Population) Population->SpawnResidents();
}

void AVerdantWorld::TickEconomy(float Dt)
{
	EconomyClock += FMath::Max(0.f, Dt);
	if (EconomyClock < .5f)
		return;
	const float Step = FMath::Min(EconomyClock, 2.f);
	EconomyClock = 0.f;
	TickFoodField();

	for (FVerdantGrove& Grove : Groves)
	{
		if (Grove.ControllerId >= 0 && !IsLiving(FindFolk(Grove.ControllerId)))
		{
			Grove.ControllerId = -1;
			Grove.bOpen = true;
			Grove.Share = 0.f;
			Grove.Permitted.Reset();
			Record(Grove.Name + TEXT(" 的管理者已倒下，居民现在可以自由进入。"));
		}
		// A depleted grove slowly recovers; no essential resource can disappear forever.
		// Grove food is the sum of real ground pickups; no parallel invisible resource pool.
		if (Grove.Food >= 12.f)
			Grove.Fertility = FMath::Min(1.f, Grove.Fertility + .004f * Step);
		else if (Grove.Food < 3.f)
			Grove.Fertility = FMath::Max(.2f, Grove.Fertility - .001f * Step);
	}
	UpdateGroveVisuals();
}

void AVerdantWorld::Record(const FString& Message)
{
	const int32 Seconds = FMath::Max(0, FMath::FloorToInt(Elapsed));
	Events.Add(FString::Printf(TEXT("%02d:%02d  %s"), Seconds / 60, Seconds % 60, *Message));
	if (Events.Num() > 80)
		Events.RemoveAt(0, Events.Num() - 80, EAllowShrinking::No);
	UE_LOG(LogTemp, Display, TEXT("[VerdantWorld] %s"), *Message);
}

void AVerdantWorld::Notify(const FString& Message)
{
	Notice = Message;
	NoticeUntil = GetWorld()->GetTimeSeconds() + 5.f;
}

int32 AVerdantWorld::NearestGrove(const FVector& At, float MaxDistance) const
{
	int32 Best = INDEX_NONE;
	float BestDistance = MaxDistance;
	for (int32 I = 0; I < Groves.Num(); ++I)
	{
		const float Distance = FVector::Dist2D(At, Groves[I].Position);
		if (Distance <= BestDistance)
		{
			Best = I;
			BestDistance = Distance;
		}
	}
	return Best;
}

AVerdantCharacter* AVerdantWorld::FindFolk(int32 Id) const
{
	for (AVerdantCharacter* Who : Folk)
		if (IsValid(Who) && Who->AgentId == Id)
			return Who;
	return nullptr;
}

bool AVerdantWorld::CanHarvest(int32 GroveIndex, const AVerdantCharacter* Who) const
{
	if (!Groves.IsValidIndex(GroveIndex) || !IsLiving(Who))
		return false;
	const FVerdantGrove& Grove = Groves[GroveIndex];
	if (Grove.ControllerId < 0 || !IsLiving(FindFolk(Grove.ControllerId)))
		return true;
	return Grove.ControllerId == Who->AgentId || Grove.bOpen || Grove.Permitted.Contains(Who->AgentId);
}

float AVerdantWorld::Harvest(int32 GroveIndex, AVerdantCharacter* Who, float Amount)
{
    if (!CanHarvest(GroveIndex, Who) || !FMath::IsFinite(Amount) || Amount <= 0.f) return 0.f;
    AVerdantFoodPickup* Best = nullptr;
    float Distance = FMath::Square(FMath::Max(180.f, Who->Reach()+130.f));
    for (AVerdantFoodPickup* P : FoodPickups)
    {
        if (!IsValid(P) || P->GroveIndex != GroveIndex || P->Remaining <= .001f || P->FoodTier > Who->FoodTier()) continue;
        const float D = FVector::DistSquared2D(P->GetActorLocation(), Who->GetActorLocation());
        if (D < Distance) { Distance = D; Best = P; }
    }
    return Best ? Best->Take(Who, Amount) : 0.f;
}

bool AVerdantWorld::Donate(AVerdantCharacter* Who, float Amount)
{
	if (!IsLiving(Who) || !FMath::IsFinite(Amount) || Amount <= 0.f)
		return false;
	if (FVector::Dist2D(Who->GetActorLocation(), MotherPosition) > 1000.f)
		return false;
	if (bRestored)
	{
		if (Who->AgentId == 0)
			Notify(TEXT("母树已经复苏。山谷仍在运转，你可以继续影响它的未来。"));
		return false;
	}
	const float Paid = FMath::Min3(Amount, Who->Food, FMath::Max(0.f, TreeGoal - TreeNutrition));
	if (Paid <= .001f)
	{
		if (Who->AgentId == 0)
			Notify(TEXT("没有可供奉的养分。采集、交换或战斗都能改变资源归属。"));
		return false;
	}
	Who->Food -= Paid;
	TreeNutrition += Paid;
	DonationLedger.FindOrAdd(Who->AgentId) += Paid;
	DonationNames.Add(Who->AgentId, Who->DisplayName);
	Who->LastDonationTime = GetWorld()->GetTimeSeconds();
	Who->LastDonatedAmount = Paid;
	Record(FString::Printf(TEXT("%s 向母树供奉 %.1f 份养分（%.0f / %.0f）。"),
	                       *Who->DisplayName, Paid, TreeNutrition, TreeGoal));
	if (Who->AgentId == 0)
		Notify(FString::Printf(TEXT("母树获得 %.1f 份养分。复苏进度 %.0f / %.0f。"), Paid, TreeNutrition, TreeGoal));
	if (TreeNutrition >= TreeGoal - .001f)
	{
		TreeNutrition = TreeGoal;
		bRestored = true;
		FinalizeSeasonRecord();
		Record(TEXT("母树重新结果，新的生长季开始。世界将继续保留居民、领地和资源的真实状态。"));
		Record(SeasonCoreSummary.Replace(TEXT("\n"), TEXT("；")));
		Notify(TEXT("母树复苏！观察三片果林：你们的行动已经改变了山谷。"));
	}
	PlayCue(bRestored ? TEXT("Restore") : TEXT("Offer"), MotherPosition, 1.f);
	UpdateGroveVisuals();
	return true;
}

bool AVerdantWorld::Transfer(AVerdantCharacter* From, AVerdantCharacter* To, float Amount)
{
	if (!IsLiving(From) || !IsLiving(To) || From == To || !FMath::IsFinite(Amount) || Amount <= 0.f)
		return false;
	if (FVector::Dist(From->GetActorLocation(), To->GetActorLocation()) > 1100.f)
		return false;
	const float Paid = VerdantEconomy::TransferAmount(From->Food, To->FreeFoodCapacity(), Amount, To->FoodTier(), From->FoodTier());
	if (Paid <= .001f)
		return false;
	From->Food -= Paid;
	To->Food += Paid;
	TransferLedger.FindOrAdd(FString::Printf(TEXT("%d:%d"), From->AgentId, To->AgentId)) += Paid;
	if (From->AgentId == 0 || To->AgentId == 0) PlayCue(TEXT("Pickup"), To->GetActorLocation(), To->BodySize);
	Record(FString::Printf(TEXT("%s 把 %.1f 份养分交给了 %s。"), *From->DisplayName, Paid, *To->DisplayName));
	return true;
}

bool AVerdantWorld::Claim(int32 GroveIndex, AVerdantCharacter* Who)
{
	if (!Groves.IsValidIndex(GroveIndex) || !IsLiving(Who))
		return false;
	FVerdantGrove& Grove = Groves[GroveIndex];
	if (FVector::Dist2D(Who->GetActorLocation(), Grove.Position) > GroveInteractionRadius)
		return false;
	if (Grove.ControllerId == Who->AgentId)
		return true;
	if (IsLiving(FindFolk(Grove.ControllerId)))
	{
		if (Who->AgentId == 0)
			Notify(TEXT("这里已有管理者。可先协商采集权，或通过战斗改变控制关系。"));
		return false;
	}
	Grove.ControllerId = Who->AgentId;
	Grove.bOpen = true;
	Grove.Share = 0.f;
	Grove.Permitted.Reset();
	Who->HomeGrove = GroveIndex;
	Record(FString::Printf(TEXT("%s 开始管理%s，并开放采集。"), *Who->DisplayName, *Grove.Name));
	if (Who->AgentId == 0)
		Notify(TEXT("已接管果林，目前免费开放。按 G 切换免费、分成或关闭。"));
	return true;
}

float AVerdantWorld::TendCost(int32 GroveIndex) const
{
	if (!Groves.IsValidIndex(GroveIndex)) return 0.f;
	return FMath::Clamp((1.f - Groves[GroveIndex].Fertility) / .03f, 0.f, 5.f);
}

bool AVerdantWorld::Tend(int32 GroveIndex, AVerdantCharacter* Who)
{
	if (!Groves.IsValidIndex(GroveIndex) || !IsLiving(Who)) return false;
	auto& Grove = Groves[GroveIndex];
	if (FVector::Dist2D(Who->GetActorLocation(), Grove.Position) > GroveInteractionRadius || !
		CanHarvest(GroveIndex, Who)) return false;
	const float Cost = TendCost(GroveIndex);
	if (Cost < .01f)
	{
		if (Who->AgentId == 0) Notify(TEXT("果林生机充足，不需要额外养护。"));
		return false;
	}
	if (Who->Food + .001f < Cost)
	{
		if (Who->AgentId == 0) Notify(FString::Printf(TEXT("养护需要 %.1f 份食物，作为新的土壤养分。"), Cost));
		return false;
	}
	Who->Food = FMath::Max(0.f, Who->Food - Cost);
	Grove.Fertility = FMath::Min(1.f, Grove.Fertility + Cost * .03f);
	TendLedger.FindOrAdd(Who->AgentId) += Cost;
	Record(FString::Printf(TEXT("%s 把 %.1f 份食物还给%s的土地，生机提升至 %.0f%%。"), *Who->DisplayName, Cost, *Grove.Name,
	                       Grove.Fertility * 100.f));
	if (Who->AgentId == 0) Notify(TEXT("果林恢复得更快了。这份真实帮助也会被居民记住。"));
	PlayCue(TEXT("Tend"), Grove.Position, Who->BodySize);
	UpdateGroveVisuals();
	return true;
}

bool AVerdantWorld::TransferControl(int32 GroveIndex, AVerdantCharacter* From, AVerdantCharacter* To)
{
	if (!Groves.IsValidIndex(GroveIndex) || !IsLiving(From) || !IsLiving(To) || From == To) return false;
	auto& Grove = Groves[GroveIndex];
	if (Grove.ControllerId != From->AgentId || FVector::Dist(From->GetActorLocation(), To->GetActorLocation()) > 750.f
		* FMath::Max(1.f, To->BodySize)) return false;
	Grove.ControllerId = To->AgentId;
	Grove.bOpen = true;
	Grove.Share = 0.f;
	Grove.Permitted.Reset();
	To->HomeGrove = GroveIndex;
	Record(FString::Printf(TEXT("%s 将%s的管理权交给%s，居民仍可自由采集。"), *From->DisplayName, *Grove.Name, *To->DisplayName));
	UpdateGroveVisuals();
	return true;
}

bool AVerdantWorld::SetAccess(int32 GroveIndex, AVerdantCharacter* Who, bool bOpen, float Share)
{
	if (!Groves.IsValidIndex(GroveIndex) || !IsLiving(Who) || !FMath::IsFinite(Share))
		return false;
	FVerdantGrove& Grove = Groves[GroveIndex];
	if (Grove.ControllerId != Who->AgentId)
		return false;
	Grove.bOpen = bOpen;
	Grove.Share = FMath::Clamp(Share, 0.f, .9f);
	Who->SharePledge = Grove.Share;
	Record(FString::Printf(TEXT("%s 将%s设为%s，采集分成为 %.0f%%。"),
	                       *Who->DisplayName, *Grove.Name, bOpen ? TEXT("开放") : TEXT("凭许可进入"), Grove.Share * 100.f));
	if (Who->AgentId == 0)
		Notify(!bOpen
			       ? TEXT("果林已关闭，其他居民会寻找新的采集地点。")
			       : Grove.Share > .001f
			       ? TEXT("果林开放并收取三成分成；居民采集时会实际交粮。")
			       : TEXT("果林免费开放，居民可以自由采集。"));
	return true;
}

FString AVerdantWorld::WorldSummary() const
{
	const FVerdantSeasonSnapshot Current = CaptureSeasonSnapshot();
	FString Summary = FString::Printf(TEXT("当前：%s · 居民 %d/%d · 野生 %d/%d\n免税开放 %d/%d · 你管理 %d处 · 生机 %.0f%%"),
	                                  *Current.StateLabel, Current.ResidentsAlive, Current.ResidentsTotal,
	                                  Current.WildAlive, Current.WildTotal, Current.FreeGroves, Current.Groves.Num(),
	                                  Current.PlayerGroves, Current.AverageFertility * 100.f);
	for (const FVerdantSeasonGrove& Grove : Current.Groves)
	{
		const FString Access = !Grove.bOpen
			                       ? TEXT("限采")
			                       : Grove.Share > .001f
			                       ? FString::Printf(TEXT("开放/分成%.0f%%"), Grove.Share * 100.f)
			                       : TEXT("免费开放");
		Summary += FString::Printf(TEXT("\n%s：%s · %s · 粮%.0f"),
		                           *Grove.Name, *Grove.ControllerName, *Access, Grove.Food);
	}
	return Summary;
}
