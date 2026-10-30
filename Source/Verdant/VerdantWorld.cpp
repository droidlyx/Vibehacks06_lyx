#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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
    const TCHAR* Names[] = { TEXT("苔石果林"), TEXT("赤土果林"), TEXT("高地果林") };
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
        Grove.Food = I == 2 ? 38.f : 32.f;
        Grove.Fertility = 1.f;
        Grove.bOpen = I != 2;
        Grove.Share = I == 1 ? .2f : 0.f;
        Groves.Add(Grove);
    }
    MotherPosition = SurfacePoint(FVector(0.f, 3600.f, 0.f));
    BuildLandscape();
    BuildScenery();
    SpawnResidents();

    if (auto* Player = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
    {
        Folk.AddUnique(Player);
        Player->AgentId = 0;
        Player->UpdateAppearance();
    }
    Record(TEXT("生长季开始。三片果林由不同居民管理，母树等待 100 份养分。"));
    Record(TEXT("米芽开放采集；陶恩收取两成分成；岚石暂不允许外人进入高地果林。"));
    Notify(TEXT("WASD 移动 · E 采集 · Q 吸收养分 · T 交谈 · F 分享或供奉"));
    UpdateGroveVisuals();
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
    struct FResidentSpec
    {
        int32 Id;
        const TCHAR* Name;
        const TCHAR* Purpose;
        FVector Position;
        float Size;
        int32 Home;
        int32 Outfit;
        FLinearColor Color;
        bool bWild;
        float StartingFood;
    };
    const FResidentSpec Specs[] = {
        {1, TEXT("米芽"), TEXT("保护小居民的采食机会；希望大家合作复苏母树。"),
            FVector(-4250.f,-1750.f,0.f), .6f, 0, 1, FLinearColor(.35f,.69f,.43f), false, 8.f},
        {2, TEXT("陶恩"), TEXT("经营赤土果林，愿意用采集许可换取合理分成。"),
            FVector(4500.f,-1200.f,0.f), 1.f, 1, 0, FLinearColor(.75f,.39f,.22f), false, 12.f},
        {3, TEXT("岚石"), TEXT("守住高地的粮食；尊重实力，也会考虑可靠的交换与承诺。"),
            FVector(850.f,6350.f,0.f), 2.f, 2, 2, FLinearColor(.39f,.49f,.73f), false, 16.f},
        {4, TEXT("荒枝游民"), TEXT("寻找食物，驱赶靠近的陌生生物。"),
            FVector(-1800.f,-300.f,0.f), .8f, 0, 2, FLinearColor(.58f,.44f,.25f), true, 7.f},
        {5, TEXT("赤背掠夺者"), TEXT("沿荒地觅食，避开比自己强大的威胁。"),
            FVector(2800.f,3000.f,0.f), 1.25f, 1, 2, FLinearColor(.66f,.28f,.22f), true, 10.f},
        {6, TEXT("古岩守卫"), TEXT("在北侧荒地游荡，保卫身边的食物。"),
            FVector(-3300.f,6200.f,0.f), 2.1f, 2, 2, FLinearColor(.43f,.43f,.35f), true, 12.f}
    };

    for (const FResidentSpec& Spec : Specs)
    {
        // Character::SetSize anchors feet from the constructor's 90 cm half-height.
        const FVector SpawnPosition = SurfacePoint(Spec.Position, 95.f);
        const FTransform Transform(FRotator(0.f, Spec.Id * 47.f, 0.f), SpawnPosition);
        auto* Resident = GetWorld()->SpawnActorDeferred<AVerdantCharacter>(
            AVerdantCharacter::StaticClass(), Transform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Resident)
            continue;

        Resident->AgentId = Spec.Id;
        Resident->DisplayName = Spec.Name;
        Resident->Motivation = Spec.Purpose;
        Resident->BodySize = Spec.Size;
        Resident->HomeGrove = Spec.Home;
        Resident->Outfit = Spec.Outfit;
        Resident->BodyColor = Spec.Color;
        Resident->bWild = Spec.bWild;
        Resident->Food = Spec.StartingFood;
        Resident->SharePledge = Spec.Id == 2 ? .2f : 0.f;
        Resident->BrainClock = Spec.Id * .13f;
        Resident->Destination = SpawnPosition;
        Resident->ActionLabel = Spec.bWild ? TEXT("在荒地觅食") : TEXT("观察果林");
        Folk.Add(Resident);
        UGameplayStatics::FinishSpawningActor(Resident, Transform);
        Resident->GetCharacterMovement()->bRunPhysicsWithNoController = true;
        Resident->UpdateAppearance();
    }
}

void AVerdantWorld::TickEconomy(float Dt)
{
    EconomyClock += FMath::Max(0.f, Dt);
    if (EconomyClock < .5f)
        return;
    const float Step = FMath::Min(EconomyClock, 2.f);
    EconomyClock = 0.f;

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
        const float Regen = (.18f + .62f * Grove.Fertility) * (bRestored ? 1.2f : 1.f);
        Grove.Food = FMath::Min(GroveCapacity, Grove.Food + Regen * Step);
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
    if (!Groves.IsValidIndex(GroveIndex) || !IsLiving(Who) || !FMath::IsFinite(Amount) || Amount <= 0.f)
        return 0.f;
    FVerdantGrove& Grove = Groves[GroveIndex];
    if (FVector::Dist2D(Who->GetActorLocation(), Grove.Position) > GroveInteractionRadius)
        return 0.f;
    if (!CanHarvest(GroveIndex, Who))
    {
        if (Who->AgentId == 0)
        {
            const auto* GroveOwner = FindFolk(Grove.ControllerId);
            Notify(FString::Printf(TEXT("%s 暂未允许你采集。可以交谈争取许可，或挑战管理者。"),
                GroveOwner ? *GroveOwner->DisplayName : TEXT("这里的管理者")));
        }
        return 0.f;
    }
    if (Grove.Food < .5f)
    {
        if (Who->AgentId == 0)
            Notify(TEXT("这里的果实暂时采空了。可以去另一片果林，或等待少量再生。"));
        return 0.f;
    }

    const float Taken = FMath::Min(Amount, Grove.Food);
    auto* GroveOwner = FindFolk(Grove.ControllerId);
    const float Tax = IsLiving(GroveOwner) && GroveOwner != Who
        ? Taken * FMath::Clamp(Grove.Share, 0.f, .9f) : 0.f;
    Grove.Food -= Taken;
    Who->Food += Taken - Tax;
    if (Tax > 0.f)
        GroveOwner->Food += Tax;
    if (Grove.Food < 5.f)
        Grove.Fertility = FMath::Max(.2f, Grove.Fertility - Taken * .003f);

    if (Tax > .01f)
        Record(FString::Printf(TEXT("%s 在%s采得 %.1f 份，按约定交给%s %.1f 份。"),
            *Who->DisplayName, *Grove.Name, Taken, *GroveOwner->DisplayName, Tax));
    else if (Who->AgentId == 0 || Taken >= 4.f)
        Record(FString::Printf(TEXT("%s 在%s采得 %.1f 份养分。"), *Who->DisplayName, *Grove.Name, Taken));
    if (Who->AgentId == 0)
        Notify(FString::Printf(TEXT("获得 %.1f 份养分。Q 吸收成长；F 分享或供奉。"), Taken - Tax));
    return Taken - Tax;
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
    Record(FString::Printf(TEXT("%s 向母树供奉 %.1f 份养分（%.0f / %.0f）。"),
        *Who->DisplayName, Paid, TreeNutrition, TreeGoal));
    if (Who->AgentId == 0)
        Notify(FString::Printf(TEXT("母树获得 %.1f 份养分。复苏进度 %.0f / %.0f。"), Paid, TreeNutrition, TreeGoal));
    if (TreeNutrition >= TreeGoal - .001f)
    {
        TreeNutrition = TreeGoal;
        bRestored = true;
        Record(TEXT("母树重新结果，新的生长季开始。世界将继续保留居民、领地和资源的真实状态。"));
        Record(WorldSummary());
        Notify(TEXT("母树复苏！观察三片果林：你们的行动已经改变了山谷。"));
    }
    UpdateGroveVisuals();
    return true;
}

bool AVerdantWorld::Transfer(AVerdantCharacter* From, AVerdantCharacter* To, float Amount)
{
    if (!IsLiving(From) || !IsLiving(To) || From == To || !FMath::IsFinite(Amount) || Amount <= 0.f)
        return false;
    if (FVector::Dist2D(From->GetActorLocation(), To->GetActorLocation()) > 1100.f)
        return false;
    const float Paid = FMath::Min(Amount, From->Food);
    if (Paid <= .001f)
        return false;
    From->Food -= Paid;
    To->Food += Paid;
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
        Notify(TEXT("已接管果林，目前开放采集。再次按 G 可切换开放与关闭。"));
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
        Notify(bOpen ? TEXT("果林已开放，居民可以进入采集。") : TEXT("果林已关闭，其他居民会寻找新的采集地点。"));
    return true;
}

FString AVerdantWorld::WorldSummary() const
{
    int32 ResidentsAlive = 0;
    int32 PlayerGroves = 0;
    int32 OpenGroves = 0;
    float Fertility = 0.f;
    for (AVerdantCharacter* Who : Folk)
        if (IsLiving(Who) && Who->AgentId != 0 && !Who->bWild)
            ++ResidentsAlive;
    for (const FVerdantGrove& Grove : Groves)
    {
        PlayerGroves += Grove.ControllerId == 0 ? 1 : 0;
        OpenGroves += Grove.bOpen || !IsLiving(FindFolk(Grove.ControllerId)) ? 1 : 0;
        Fertility += Grove.Fertility;
    }
    FString State = TEXT("分立的山谷");
    if (ResidentsAlive == 0)
        State = TEXT("寂静的山谷");
    else if (PlayerGroves >= 2)
        State = TEXT("旅人的领地");
    else if (OpenGroves >= 2 && ResidentsAlive >= 2)
        State = TEXT("开放的山谷");

    FString Summary = FString::Printf(TEXT("%s · 居民存活 %d/3 · 开放果林 %d/%d · 平均生机 %.0f%%"),
        *State, ResidentsAlive, OpenGroves, Groves.Num(),
        Groves.IsEmpty() ? 0.f : Fertility * 100.f / Groves.Num());
    for (const FVerdantGrove& Grove : Groves)
    {
        const auto* GroveOwner = FindFolk(Grove.ControllerId);
        Summary += FString::Printf(TEXT("\n%s：%s，储粮 %.0f，%s"),
            *Grove.Name, IsLiving(GroveOwner) ? *GroveOwner->DisplayName : TEXT("无人管理"),
            Grove.Food, Grove.bOpen || !IsLiving(GroveOwner) ? TEXT("开放") : TEXT("受限"));
    }
    return Summary;
}
