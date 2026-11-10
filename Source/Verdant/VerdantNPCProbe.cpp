// Explicit deterministic NPC fixtures: grants, placement and frozen bystanders are disclosed.
// The normal input journey is the separate evidence for an unmodified resource playthrough.
#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    struct FVerdantNPCFixture
    {
        double BeganAt = 0;
        float PhaseAt = 0;
        bool bFinished = false;
        bool bNeutralAggro = false;
        bool bDistantAid = false;
        bool bEnemyFrozen = false;
        bool bCooldownReacquired = false;
        bool bRemoteDonation = false;
        float EnemyHealth = 0;
        float PlayerHealth = 0;
        float BuddyHealth = 0;
        float HealthBefore = 0;
        float FoodBefore = 0;
        float TreeBefore = 0;
        float LedgerBefore = 0;
        float AidDamage = 0;
        float AllyDamage = 0;
        float RetreatSeparation = 0;
        float HealingCost = 0;
        float MaxLateral = 0;
        float DonationDelta = 0;
        float DonationDistance = -1;
        int32 MaxRecovery = 0;
        bool bBlockedAttack = false;
        float ReturnStartDistance = 0;
        float WildAidDamage = 0;
        float ReturnDistance = 0;
        float CareFoodBefore = 0;
        float CareLedgerBefore = 0;
        float CareFertilityBefore = 0;
        float CarePaid = 0;
        float CareCooldown = 0;
        float HarvestBefore = 0;
        FVector Origin = FVector::ZeroVector;
        FVector ChaseRay = FVector(3200.f, -1100.f, 0.f).GetSafeNormal2D();
        TArray<TWeakObjectPtr<AStaticMeshActor>> Walls;
    };
    FVerdantNPCFixture NPCFixture;
    const FVector NPCCombatAnchor(-500.f, -2500.f, 0.f);
}

void AVerdantGameMode::TickNPCProbe(float Dt)
{
    auto& F = NPCFixture;
    if (F.bFinished) return;
    if (F.BeganAt == 0) F.BeganAt = FPlatformTime::Seconds();
    SmokeTime += Dt;
    auto* W = AVerdantWorld::Find(this);
    auto* P = W ? W->FindFolk(0) : nullptr;
    auto* Miya = W ? W->FindFolk(1) : nullptr;
    auto* Trader = W ? W->FindFolk(2) : nullptr;
    auto* Keeper = W ? W->FindFolk(3) : nullptr;
    auto* Wild = W ? W->FindFolk(4) : nullptr;
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Validation");
    auto Check = [this](bool Pass, const FString& Label)
    {
        const FString Line = (Pass ? TEXT("PASS ") : TEXT("FAIL ")) + Label;
        SmokeChecks.Add(Line); if (!Pass) ++SmokeFailures;
        UE_LOG(LogTemp, Display, TEXT("VERDANT_NPC_CHECK %s"), *Line);
    };
    auto Freeze = [](AVerdantCharacter* Who)
    {
        Who->SetActorTickEnabled(false);
        Who->ConsumeMovementInputVector();
        Who->GetCharacterMovement()->ClearAccumulatedForces();
        Who->GetCharacterMovement()->StopMovementImmediately();
        Who->GetCharacterMovement()->DisableMovement();
    };
    auto Place = [W](AVerdantCharacter* Who, const FVector& Position)
    {
        Who->ConsumeMovementInputVector();
        Who->GetCharacterMovement()->ClearAccumulatedForces();
        Who->GetCharacterMovement()->StopMovementImmediately();
        Who->SetActorLocation(W->SurfacePoint(Position, Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), false);
        Who->GetCharacterMovement()->bForceNextFloorCheck = true;
    };
    auto Enable = [](AVerdantCharacter* Who)
    {
        Who->SetActorTickEnabled(true);
        Who->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Who->GetCharacterMovement()->bForceNextFloorCheck = true;
    };
    auto Reset = [&](AVerdantCharacter* Who, float Size, const FVector& Position)
    {
        Freeze(Who);
        Who->bDead = false; Who->bSprinting = false; Who->bHarvestHeld = false;
        Who->Nutrition = 0; Who->Food = 0; Who->DonationPledge = 0; Who->SharePledge = 0;
        Who->AttackTimer = 0; Who->HurtTimer = 0; Who->DodgeTimer = 0; Who->DodgeRecoveryTimer = 0;
        Who->QueuedAttackType = -1; Who->AttackInputBuffer = 0; Who->bHitApplied = true;
        Who->ActionCooldown = 0; Who->FollowId = -1; Who->HostileId = -1; Who->CombatTarget.Reset();
        Who->BrainClock = 0; Who->ReconsiderAt = 100000.f; Who->NextTendAt = 100000.f; Who->Trust.Empty();
        Who->AvoidThreatId = -1; Who->AvoidThreatUntil = 0;
        Who->MoveSampleAt = 0; Who->MoveRecoveryCount = 0; Who->DetourUntil = 0;
        Who->AvoidGrove = -1; Who->AvoidGroveUntil = 0;
        Who->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        // Resize out of geometry, then place on the real generated terrain.
        Who->SetActorLocation(Position + FVector(0.f, 0.f, 5000.f), false);
        Who->SetSize(Size); Who->Health = Who->MaxHealth();
        Place(Who, Position); Who->Destination = Who->GetActorLocation();
    };
    auto ResetAll = [&]()
    {
        for (AVerdantCharacter* Who : W->Folk)
            if (IsValid(Who)) Reset(Who, 1.f, FVector(-9300.f + Who->AgentId * 650.f, -9300.f, 0.f));
    };
    auto Next = [this, &F](int32 Stage) { SmokeStage = Stage; F.PhaseAt = SmokeTime; };
    auto RemoveWalls = [&F]()
    {
        for (auto& Wall : F.Walls) if (Wall.IsValid()) Wall->Destroy();
        F.Walls.Empty();
    };
    auto Finish = [&]()
    {
        RemoveWalls();
        IFileManager::Get().MakeDirectory(*Dir, true);
        SmokeChecks.Add(FString::Printf(TEXT("Failures: %d"), SmokeFailures));
        FFileHelper::SaveStringArrayToFile(SmokeChecks, *(Dir / TEXT("npc-probe-results.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        auto Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("kind"), TEXT("explicit-npc-behavior-fixtures"));
        Report->SetBoolField(TEXT("physical_desktop_input"), false);
        Report->SetBoolField(TEXT("fixture_placement_grants_and_frozen_bystanders"), true);
        Report->SetBoolField(TEXT("movement_and_donation_use_normal_gameplay"), true);
        Report->SetNumberField(TEXT("failures"), SmokeFailures);
        Report->SetNumberField(TEXT("elapsed_seconds"), SmokeTime);
        Report->SetNumberField(TEXT("aid_enemy_damage"), F.AidDamage);
        Report->SetNumberField(TEXT("aid_friendly_damage_after_enemy_frozen"), F.AllyDamage);
        Report->SetNumberField(TEXT("retreat_max_separation_cm"), F.RetreatSeparation);
        Report->SetNumberField(TEXT("healing_food_cost"), F.HealingCost);
        Report->SetNumberField(TEXT("max_stuck_recoveries"), F.MaxRecovery);
        Report->SetNumberField(TEXT("detour_lateral_distance_cm"), F.MaxLateral);
        Report->SetNumberField(TEXT("actual_donation_delta"), F.DonationDelta);
        Report->SetNumberField(TEXT("actual_donation_distance_cm"), F.DonationDistance);
        Report->SetNumberField(TEXT("travelling_wild_companion_damage"), F.WildAidDamage);
        Report->SetNumberField(TEXT("companion_return_distance_cm"), F.ReturnDistance);
        Report->SetNumberField(TEXT("npc_care_food_paid"), F.CarePaid);
        Report->SetNumberField(TEXT("npc_care_cooldown_seconds"), F.CareCooldown);
        Report->SetBoolField(TEXT("fixture_economy_frozen_for_care_isolation"), true);
        TArray<TSharedPtr<FJsonValue>> Checks;
        for (const FString& Line : SmokeChecks) Checks.Add(MakeShared<FJsonValueString>(Line));
        Report->SetArrayField(TEXT("checks"), Checks);
        FString Json; auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Report, Writer);
        FFileHelper::SaveStringToFile(Json, *(Dir / TEXT("npc-probe.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("VERDANT_NPC_PROBE_COMPLETE failures=%d"), SmokeFailures);
        F.bFinished = true;
        FPlatformMisc::RequestExitWithStatus(false, SmokeFailures ? 1 : 0);
    };
    if (FPlatformTime::Seconds() - F.BeganAt > 95 || SmokeTime > 90.f)
    { Check(false, TEXT("NPC fixtures complete within their bounded deadline")); Finish(); return; }
    if (!W || !P || !Miya || !Trader || !Keeper || !Wild || W->Groves.Num() < 3) return;
    const float Elapsed = SmokeTime - F.PhaseAt;

    if (SmokeStage == 0 && SmokeTime > 2.f)
    {
        ResetAll();
        Reset(P, 1.1f, NPCCombatAnchor);
        Reset(Miya, 1.f, NPCCombatAnchor + FVector(-280.f, 220.f, 0.f));
        Reset(Wild, 1.f, NPCCombatAnchor + FVector(-280.f, -170.f, 0.f));
        Wild->FollowId = 0; Wild->Trust.Add(0, 12.f);
        Enable(Miya); Enable(Wild); Next(1);
    }
    else if (SmokeStage == 1)
    {
        F.bNeutralAggro |= Miya->HostileId == Wild->AgentId || Wild->HostileId == Miya->AgentId;
        if (Elapsed < 1.3f) return;
        Check(!F.bNeutralAggro, TEXT("Escorted wildlife and neutral resident do not initiate mutual combat"));
        Check(Wild->FollowId == 0, TEXT("Escorted wildlife keeps its peaceful follow agreement"));
        ResetAll();
        Reset(P, 1.1f, NPCCombatAnchor);
        Reset(Keeper, 1.5f, NPCCombatAnchor + FVector(-700.f, -80.f, 0.f));
        Reset(Trader, 1.f, NPCCombatAnchor + FVector(120.f, 150.f, 0.f));
        Reset(Wild, 1.f, NPCCombatAnchor + FVector(170.f, 0.f, 0.f));
        Keeper->FollowId = 0; Keeper->Trust.Add(0, 12.f);
        Trader->FollowId = 0; Trader->Trust.Add(0, 12.f);
        Wild->HostileId = 0; Wild->CombatTarget = P;
        Wild->SetActorRotation((P->GetActorLocation() - Wild->GetActorLocation()).Rotation());
        F.EnemyHealth = Wild->Health; F.PlayerHealth = P->Health; F.BuddyHealth = Trader->Health;
        Enable(Keeper); Enable(Wild); Next(2);
    }
    else if (SmokeStage == 2)
    {
        if (Keeper->HostileId == Wild->AgentId && FVector::Dist2D(Keeper->GetActorLocation(), Wild->GetActorLocation()) > 650.f)
            F.bDistantAid = true;
        if (!F.bEnemyFrozen && P->Health < F.PlayerHealth - .01f)
        {
            Freeze(Wild); Wild->AttackTimer = 0; Wild->bHitApplied = true;
            F.bEnemyFrozen = true; F.PlayerHealth = P->Health; F.BuddyHealth = Trader->Health;
        }
        F.AidDamage = F.EnemyHealth - Wild->Health;
        if (Elapsed < 8.f && !(F.bEnemyFrozen && F.AidDamage > .1f)) return;
        Check(F.bDistantAid, TEXT("Follower acquires its leader's attacker beyond ordinary proximity aggro"));
        Check(F.bEnemyFrozen, TEXT("Aggressor actually strikes the leader before its attacks are isolated"));
        Check(F.AidDamage > .1f, TEXT("Follower travels and deals real damage to the leader's attacker"));
        F.AllyDamage = FMath::Max(0.f, F.PlayerHealth - P->Health) + FMath::Max(0.f, F.BuddyHealth - Trader->Health);
        Check(F.bEnemyFrozen && F.AllyDamage < .01f, TEXT("Autonomous aid does not hit its leader or another follower"));
        Check(Keeper->FollowId == 0, TEXT("Successful aid preserves the follower's agreement"));
        Freeze(Keeper);
        const float KeeperHealth = Keeper->Health;
        Keeper->ReceiveStrike(Wild, 1.f);
        Check(Keeper->Health < KeeperHealth && Keeper->FollowId == 0, TEXT("An actual external strike preserves the companion's follow plan"));
        Keeper->ReceiveStrike(P, 1.f);
        Check(Keeper->FollowId < 0 && Keeper->HostileId == 0, TEXT("An actual attack by the leader breaks the follow agreement"));
        ResetAll();
        F.Origin = W->Groves[0].Position;
        Reset(Miya, .6f, F.Origin);
        Reset(Wild, 2.3f, F.Origin + FVector(850.f, 0.f, 0.f));
        Miya->HomeGrove = 0; Miya->Food = 14.f; Miya->DonationPledge = 5.f;
        Miya->Health = Miya->MaxHealth() * .2f;
        Miya->ReceiveStrike(Wild, 1.f);
        F.HealthBefore = Miya->Health; F.FoodBefore = Miya->Food;
        F.RetreatSeparation = FVector::Dist2D(Miya->GetActorLocation(), Wild->GetActorLocation());
        Enable(Miya); Next(3);
    }
    else if (SmokeStage == 3)
    {
        F.RetreatSeparation = FMath::Max(F.RetreatSeparation, float(FVector::Dist2D(Miya->GetActorLocation(), Wild->GetActorLocation())));
        if (Elapsed < 2.4f) return;
        Check(F.RetreatSeparation > 1000.f, TEXT("Injured small resident physically separates from a stronger attacker"));
        F.HealingCost = F.FoodBefore - Miya->Food;
        Check(F.HealingCost >= 3.f - .01f && Miya->Health > F.HealthBefore, TEXT("Safe retreat restores health by consuming actual carried rations"));
        Check(Miya->Food >= Miya->DonationPledge + 3.f - .01f && FMath::IsNearlyEqual(Miya->DonationPledge, 5.f), TEXT("Emergency ration use preserves promised food and a remaining personal reserve"));
        ResetAll();
        const FVector Home = W->Groves[1].Position;
        Reset(Trader, 1.2f, Home + F.ChaseRay * 2100.f);
        Reset(Wild, 1.f, Home + F.ChaseRay * 2750.f);
        Trader->HomeGrove = 1; Trader->ReceiveStrike(Wild, 1.f);
        Enable(Trader); Next(4);
    }
    else if (SmokeStage == 4 && Elapsed > .9f)
    {
        Check(Trader->HostileId < 0 && Trader->AvoidThreatId == Wild->AgentId && Trader->AvoidThreatUntil > GetWorld()->GetTimeSeconds(), TEXT("Resident abandons a distant pursuit outside its home area"));
        Place(Wild, Trader->GetActorLocation() + F.ChaseRay * 300.f);
        Next(5);
    }
    else if (SmokeStage == 5)
    {
        F.bCooldownReacquired |= Trader->HostileId == Wild->AgentId;
        if (Elapsed < .4f) return;
        Check(!F.bCooldownReacquired, TEXT("Pursuit cooldown prevents immediate proximity reacquisition"));
        Place(Wild, Trader->GetActorLocation() + F.ChaseRay * 180.f);
        Trader->ReceiveStrike(Wild, 2.f);
        F.EnemyHealth = Wild->Health;
        Check(Trader->HostileId == Wild->AgentId, TEXT("A new actual strike bypasses search cooldown and immediately marks its attacker"));
        Next(6);
    }
    else if (SmokeStage == 6)
    {
        if (Elapsed < 3.f && Wild->Health >= F.EnemyHealth - .1f) return;
        Check(Wild->Health < F.EnemyHealth - .1f, TEXT("Resident actually retaliates after receiving a fresh strike during cooldown"));
        ResetAll();
        F.Origin = W->MotherPosition + FVector(0.f, -1800.f, 0.f);
        Reset(Trader, 1.f, F.Origin); Trader->HomeGrove = 1;
        Trader->Food = 20.f; Trader->DonationPledge = 5.f;
        F.FoodBefore = Trader->Food; F.TreeBefore = W->TreeNutrition;
        F.LedgerBefore = W->DonationLedger.FindRef(Trader->AgentId);
        auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        const FVector Offsets[] = { FVector(0.f, 240.f, 0.f), FVector(-240.f, 0.f, 0.f), FVector(240.f, 0.f, 0.f), FVector(0.f, -240.f, 0.f) };
        const FVector Scales[] = { FVector(6.f, .6f, 3.5f), FVector(.6f, 6.f, 3.5f), FVector(.6f, 6.f, 3.5f), FVector(6.f, .6f, 3.5f) };
        for (int32 I = 0; I < 4; ++I)
        {
            auto* Wall = GetWorld()->SpawnActor<AStaticMeshActor>();
            if (!Wall) continue;
            Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Wall->SetActorScale3D(Scales[I]);
            Wall->SetActorLocation(W->SurfacePoint(F.Origin + Offsets[I], 175.f));
            F.Walls.Add(Wall);
        }
        Check(Cube && F.Walls.Num() == 4, TEXT("Physical obstruction fixture creates four collidable walls"));
        Enable(Trader); Next(7);
    }
    else if (SmokeStage == 7)
    {
        F.MaxRecovery = FMath::Max(F.MaxRecovery, Trader->MoveRecoveryCount);
        if (Elapsed < 5.3f) return;
        Check(F.MaxRecovery > 0, TEXT("Physical inability to progress triggers movement recovery"));
        Check(FMath::IsNearlyEqual(W->TreeNutrition, F.TreeBefore) && FMath::IsNearlyEqual(Trader->Food, F.FoodBefore)
            && FMath::IsNearlyEqual(Trader->DonationPledge, 5.f), TEXT("Blocked courier retains actual inventory and promise without remote donation"));
        // Open the cage's sides and rear. The finite front wall still requires a real detour.
        for (int32 I = 1; I < F.Walls.Num(); ++I) if (F.Walls[I].IsValid()) F.Walls[I]->Destroy();
        Next(8);
    }
    else if (SmokeStage == 8)
    {
        F.MaxRecovery = FMath::Max(F.MaxRecovery, Trader->MoveRecoveryCount);
        F.MaxLateral = FMath::Max(F.MaxLateral, float(FMath::Abs(Trader->GetActorLocation().X - F.Origin.X)));
        const float MotherDistance = FVector::Dist2D(Trader->GetActorLocation(), W->MotherPosition);
        F.DonationDelta = W->TreeNutrition - F.TreeBefore;
        if (F.DonationDelta > .01f && F.DonationDistance < 0.f) F.DonationDistance = MotherDistance;
        if (F.DonationDelta > .01f && MotherDistance > 1005.f) F.bRemoteDonation = true;
        if (Elapsed < 18.f && F.DonationDelta < 4.99f) return;
        Check(F.MaxLateral > 290.f, TEXT("Courier physically travels around the finite front obstacle"));
        Check(!F.bRemoteDonation && F.DonationDistance >= 0.f && F.DonationDistance <= 1005.f, TEXT("Restoration changes only after courier reaches the mother tree"));
        Check(FMath::IsNearlyEqual(F.DonationDelta, 5.f, .01f) && Trader->DonationPledge <= .001f, TEXT("Recovered courier completes its original five-food promise"));
        Check(FMath::IsNearlyEqual(F.FoodBefore - Trader->Food, 5.f, .01f)
            && FMath::IsNearlyEqual(W->DonationLedger.FindRef(Trader->AgentId) - F.LedgerBefore, 5.f, .01f), TEXT("Delivery deducts real inventory and records the same amount in the contribution ledger"));
        RemoveWalls();
        ResetAll();
        // Far from wildlife #4's birthplace: its duty is now its leader.
        F.Origin = FVector(5000.f, -3000.f, 0.f);
        Reset(P, 1.2f, F.Origin);
        Reset(Wild, 1.2f, F.Origin + FVector(-400.f, -80.f, 0.f));
        Reset(Trader, 1.f, F.Origin + FVector(500.f, 0.f, 0.f));
        Wild->FollowId = 0; Wild->Trust.Add(0, 12.f);
        Trader->HostileId = 0; Trader->CombatTarget = P;
        F.EnemyHealth = Trader->Health;
        Enable(Wild); Next(9);
    }
    else if (SmokeStage == 9)
    {
        F.WildAidDamage = F.EnemyHealth - Trader->Health;
        if (Elapsed < 8.f && F.WildAidDamage < .1f) return;
        Check(F.WildAidDamage > .1f && Wild->FollowId == 0, TEXT("Wild companion away from its birthplace actually defends its present leader"));
        // End the threat, then move the leader to make physical regrouping measurable.
        Trader->ReceiveStrike(P, Trader->Health + 1.f);
        Place(P, F.Origin - FVector(1200.f, 0.f, 0.f));
        F.ReturnStartDistance = FVector::Dist2D(Wild->GetActorLocation(), P->GetActorLocation());
        Next(10);
    }
    else if (SmokeStage == 10)
    {
        F.ReturnDistance = FVector::Dist2D(Wild->GetActorLocation(), P->GetActorLocation());
        if (Elapsed < 5.f && F.ReturnDistance > 600.f) return;
        Check(Wild->HostileId < 0 && Wild->FollowId == 0 && F.ReturnDistance < 650.f
            && F.ReturnStartDistance - F.ReturnDistance > 300.f, TEXT("After the threat dies, the same companion really travels back to its leader"));
        ResetAll();
        Reset(Trader, 1.45f, W->Groves[1].Position + FVector(180.f, 0.f, 0.f));
        Reset(Keeper, 2.f, W->Groves[1].Position + FVector(-500.f, 0.f, 0.f));
        W->Groves[1].ControllerId = Trader->AgentId; W->Groves[1].Food = 30.f;
        Trader->HomeGrove = 1; Trader->FollowId = Keeper->AgentId; Trader->Trust.Add(Keeper->AgentId, 12.f);
        F.HarvestBefore = W->HarvestLedger.FindRef(TEXT("2:1"));
        Keeper->ReceiveStrike(P, Keeper->Health + 1.f);
        Enable(Trader); Next(11);
    }
    else if (SmokeStage == 11 && Elapsed > 2.f)
    {
        Check(Trader->FollowId < 0 && Trader->HostileId < 0, TEXT("A resident releases a dead leader and leaves escort state"));
        Check(W->HarvestLedger.FindRef(TEXT("2:1")) > F.HarvestBefore && Trader->Food > 0.f, TEXT("After losing a leader, the resident resumes actual local harvesting"));
        ResetAll();
        F.Origin = NPCCombatAnchor;
        Reset(P, 1.2f, F.Origin - FVector(700.f, 0.f, 0.f));
        Reset(Trader, 1.45f, F.Origin);
        Reset(Wild, 1.f, F.Origin + FVector(240.f, 0.f, 0.f));
        Trader->HomeGrove = 1; Trader->FollowId = 0; Trader->Trust.Add(0, 12.f);
        Wild->HostileId = 0; Wild->CombatTarget = P;
        F.EnemyHealth = Wild->Health;
        auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        const FVector Offsets[] = { FVector(120.f, 0.f, 0.f), FVector(-120.f, 0.f, 0.f), FVector(0.f, 120.f, 0.f), FVector(0.f, -120.f, 0.f) };
        const FVector Scales[] = { FVector(.4f, 4.f, 4.f), FVector(.4f, 4.f, 4.f), FVector(4.f, .4f, 4.f), FVector(4.f, .4f, 4.f) };
        for (int32 I = 0; I < 4; ++I)
        {
            auto* Wall = GetWorld()->SpawnActor<AStaticMeshActor>();
            if (!Wall) continue;
            Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Wall->SetActorScale3D(Scales[I]);
            Wall->SetActorLocation(W->SurfacePoint(F.Origin + Offsets[I], 200.f));
            F.Walls.Add(Wall);
        }
        Check(Cube && F.Walls.Num() == 4, TEXT("Combat obstruction encloses a resident with a close enemy beyond its wall"));
        Enable(Trader); Next(12);
    }
    else if (SmokeStage == 12)
    {
        F.bBlockedAttack |= Trader->AttackTimer > 0.f;
        if (Elapsed < 9.f && Trader->AvoidThreatId != Wild->AgentId) return;
        Check(!F.bBlockedAttack && FMath::IsNearlyEqual(Wild->Health, F.EnemyHealth), TEXT("A close but occluded enemy does not cause endless attacks through a wall"));
        Check(Trader->HostileId < 0 && Trader->AvoidThreatId == Wild->AgentId && Trader->FollowId == 0, TEXT("Unreachable combat is abandoned within a bounded time while the escort plan survives"));
        Check(Trader->AvoidGrove < 0, TEXT("A failed combat route never marks the resident's fruit grove as blocked"));
        RemoveWalls();
        ResetAll();
        // Freeze natural regeneration only in these accounting fixtures. NPC decisions,
        // game time, terrain, movement and the real World::Tend transaction keep running.
        W->SetActorTickEnabled(false);
        Reset(Trader, 1.45f, W->Groves[1].Position);
        Trader->HomeGrove = 1; Trader->Food = 20.f; Trader->NextTendAt = 0.f;
        W->Groves[1].ControllerId = Trader->AgentId;
        W->Groves[1].Food = 10.f; W->Groves[1].Fertility = .3f;
        F.CareFoodBefore = Trader->Food; F.CareFertilityBefore = W->Groves[1].Fertility;
        F.CareLedgerBefore = W->TendLedger.FindRef(Trader->AgentId);
        Enable(Trader); Next(13);
    }
    else if (SmokeStage == 13)
    {
        F.CarePaid = W->TendLedger.FindRef(Trader->AgentId) - F.CareLedgerBefore;
        if (Elapsed < 3.f && F.CarePaid < .1f) return;
        Check(FMath::IsNearlyEqual(F.CarePaid, 5.f, .01f)
            && FMath::IsNearlyEqual(F.CareFoodBefore - Trader->Food, F.CarePaid, .01f)
            && FMath::IsNearlyEqual(W->Groves[1].Fertility - F.CareFertilityBefore, .15f, .002f), TEXT("Autonomous care pays five actual food, records it and restores fifteen fertility points"));
        F.CareCooldown = Trader->NextTendAt - GetWorld()->GetTimeSeconds();
        Check(Trader->Food >= W->NegotiationReserve(Trader) - .01f && F.CareCooldown > 59.f, TEXT("Care preserves personal rations and schedules a separate sixty-second cooldown"));
        // Low fertility and enough food remain eligible; only the independent timer should prevent repeat care.
        Trader->Food = 30.f; W->Groves[1].Fertility = .3f;
        Trader->ActionCooldown = 0.f; Trader->BrainClock = 0.f;
        F.CareLedgerBefore = W->TendLedger.FindRef(Trader->AgentId);
        Next(14);
    }
    else if (SmokeStage == 14 && Elapsed > 3.f)
    {
        Check(FMath::IsNearlyEqual(W->TendLedger.FindRef(Trader->AgentId), F.CareLedgerBefore)
            && FMath::IsNearlyEqual(Trader->Food, 30.f), TEXT("Eligible low-fertility grove cannot consume care food again early in the sixty-second cooldown"));
        Reset(Trader, 1.45f, W->Groves[1].Position);
        Trader->HomeGrove = 1; Trader->Food = W->NegotiationReserve(Trader) + 4.f; Trader->NextTendAt = 0.f;
        F.CareFoodBefore = Trader->Food;
        F.HarvestBefore = W->HarvestLedger.FindRef(TEXT("2:1"));
        Enable(Trader); Next(15);
    }
    else if (SmokeStage == 15 && Elapsed > .3f)
    {
        Check(FMath::IsNearlyEqual(W->TendLedger.FindRef(Trader->AgentId), F.CareLedgerBefore)
            && FMath::IsNearlyEqual(Trader->Food, F.CareFoodBefore + W->HarvestLedger.FindRef(TEXT("2:1")) - F.HarvestBefore, .01f),
            TEXT("A resident with insufficient surplus harvests instead of spending its reserved rations on care"));
        Reset(Trader, 1.45f, W->Groves[1].Position);
        Trader->HomeGrove = 1; Trader->Food = 20.f; Trader->DonationPledge = 5.f; Trader->NextTendAt = 0.f;
        Enable(Trader); Next(16);
    }
    else if (SmokeStage == 16 && Elapsed > 1.2f)
    {
        Check(FMath::IsNearlyEqual(W->TendLedger.FindRef(Trader->AgentId), F.CareLedgerBefore)
            && FMath::IsNearlyEqual(Trader->Food, 20.f) && FMath::IsNearlyEqual(Trader->DonationPledge, 5.f), TEXT("A pending delivery keeps its real food and is not diverted into autonomous care"));
        W->SetActorTickEnabled(true);
        Finish();
    }
}
