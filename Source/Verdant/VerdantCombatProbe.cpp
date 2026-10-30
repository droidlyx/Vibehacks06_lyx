// Explicit combat fixtures. This is engine-input automation, not a normal-resource playthrough.
#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantHUD.h"
#include "VerdantDialogue.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    struct FVerdantCombatFixture
    {
        double BeganAt = 0;
        float PhaseAt = 0;
        FVector Origin = FVector::ZeroVector;
        FVector StrafeOrigin = FVector::ZeroVector;
        float TargetHealth = 0;
        float PlayerHealth = 0;
        float BoundaryFood = 0;
        float BoundaryHealth = 0;
        float MaxYawError = 0;
        float MaxAttackPoseError = 0;
        int32 AttackPoseSamples = 0;
        float RecoveryAtAttempt = 0;
        int32 CameraIndex = 0;
        FString Shot;
        TArray<FString> Shots;
        TArray<TSharedPtr<FJsonValue>> Framing;
        bool bAttackSeen = false;
        bool bImpactCaptured = false;
        bool bDodgeAttempted = false;
        bool bRestartRequested = false;
        bool bRestartObserved = false;
        bool bRestartChecksDone = false;
        bool bFinished = false;
        uint32 OldWorldId = 0;
        uint32 OldPawnId = 0;
        double RestartWallTime = 0;
        float PriorElapsed = 0;
        TArray<FString> SavedChecks;
        int32 SavedFailures = 0;
    };
    // Only consulted by the explicit -VerdantCombatProbe path. Survives OpenLevel
    // so a real R key can prove a new world and pawn were created.
    FVerdantCombatFixture CombatFixture;
    const FVector CombatAnchor(-500.f, -2500.f, 0.f);
}

void AVerdantGameMode::TickCombatProbe(float Dt)
{
    auto& F = CombatFixture;
    if (F.bFinished) return;
    if (F.BeganAt == 0) F.BeganAt = FPlatformTime::Seconds();
    SmokeTime += Dt;
    auto* W = AVerdantWorld::Find(this);
    auto* P = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    auto* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
    auto* H = PC ? Cast<AVerdantHUD>(PC->GetHUD()) : nullptr;
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Validation");
    IFileManager::Get().MakeDirectory(*Dir, true);
    auto Check = [this](bool Pass, const FString& Label)
    {
        const FString Line = (Pass ? TEXT("PASS ") : TEXT("FAIL ")) + Label;
        SmokeChecks.Add(Line); if (!Pass) ++SmokeFailures;
        UE_LOG(LogTemp, Display, TEXT("VERDANT_COMBAT_CHECK %s"), *Line);
    };
    auto Key = [PC](FKey K, bool Down)
    {
        if (PC) PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    };
    auto Tap = [&Key](FKey K) { Key(K, true); Key(K, false); };
    auto ReleaseKeys = [&Key]()
    {
        for (FKey K : { EKeys::W, EKeys::A, EKeys::S, EKeys::D, EKeys::LeftShift,
            EKeys::LeftMouseButton, EKeys::SpaceBar, EKeys::T, EKeys::R }) Key(K, false);
    };
    auto Capture = [&F, &Dir](const FString& Name)
    {
        F.Shot = Dir / Name;
        // Delete only this probe's named artifact, preventing stale files from passing.
        IFileManager::Get().Delete(*F.Shot, false, true);
        FScreenshotRequest::RequestScreenshot(F.Shot, true, false);
        F.Shots.Add(F.Shot);
    };
    auto Finish = [&]()
    {
        ReleaseKeys();
        SmokeChecks.Add(FString::Printf(TEXT("Failures: %d"), SmokeFailures));
        FFileHelper::SaveStringArrayToFile(SmokeChecks, *(Dir / TEXT("combat-probe-results.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        auto Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("kind"), TEXT("explicit-combat-fixtures-with-engine-input"));
        Report->SetBoolField(TEXT("physical_desktop_input"), false);
        Report->SetBoolField(TEXT("teleports_size_health_grants_and_frozen_npcs"), true);
        Report->SetBoolField(TEXT("real_R_restart_observed"), F.bRestartObserved);
        Report->SetNumberField(TEXT("failures"), SmokeFailures);
        Report->SetNumberField(TEXT("elapsed_seconds"), (F.bRestartObserved ? F.PriorElapsed : 0.f) + SmokeTime);
        Report->SetNumberField(TEXT("max_attack_yaw_error_degrees"), F.MaxYawError);
        Report->SetNumberField(TEXT("max_attack_pose_error_seconds"), F.MaxAttackPoseError);
        Report->SetArrayField(TEXT("framing"), F.Framing);
        TArray<TSharedPtr<FJsonValue>> Checks, Images;
        for (const auto& Line : SmokeChecks) Checks.Add(MakeShared<FJsonValueString>(Line));
        for (const auto& Path : F.Shots) Images.Add(MakeShared<FJsonValueString>(Path));
        Report->SetArrayField(TEXT("checks"), Checks); Report->SetArrayField(TEXT("screenshots"), Images);
        FString Json; auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Report, Writer);
        FFileHelper::SaveStringToFile(Json, *(Dir / TEXT("combat-probe.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("VERDANT_COMBAT_PROBE_COMPLETE failures=%d"), SmokeFailures);
        F.bFinished = true;
        FPlatformMisc::RequestExitWithStatus(false, SmokeFailures ? 1 : 0);
    };
    if (FPlatformTime::Seconds() - F.BeganAt > 85)
    { Check(false, TEXT("Combat fixture completed before its wall-clock deadline")); Finish(); return; }

    if (F.bRestartRequested)
    {
        const bool NewWorld = GetWorld()->GetUniqueID() != F.OldWorldId;
        if (!NewWorld)
        {
            if (FPlatformTime::Seconds() - F.RestartWallTime > 12)
            { Check(false, TEXT("R input created a new world after defeat")); Finish(); }
            return;
        }
        if (!F.bRestartObserved)
        {
            F.bRestartObserved = true;
            SmokeChecks = F.SavedChecks; SmokeFailures = F.SavedFailures;
            SmokeTime = 0;
        }
        if (!W || !P || !PC || !H) return;
        if (!F.bRestartChecksDone && SmokeTime > 3.f)
        {
            Check(P->GetUniqueID() != F.OldPawnId && !P->bDead, TEXT("R input creates a fresh living player pawn"));
            Check(FMath::IsNearlyEqual(P->BodySize, .45f, .001f), TEXT("Restart resets continuous growth to the initial size"));
            Check(FMath::IsNearlyZero(W->TreeNutrition) && !W->bRestored, TEXT("Restart clears the prior world's restoration progress"));
            Check(W->Folk.Num() == 7 && W->Groves.Num() == 3, TEXT("Restart produces exactly one new resident population"));
            Check(!H->bConversationOpen && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored(), TEXT("Restart restores normal player input"));
            Capture(TEXT("combat-restarted.png"));
            F.bRestartChecksDone = true; F.PhaseAt = SmokeTime;
        }
        else if (F.bRestartChecksDone && SmokeTime - F.PhaseAt > .6f)
        {
            Check(IFileManager::Get().FileSize(*F.Shot) > 0, TEXT("Restarted world produced a rendered screenshot"));
            Finish();
        }
        return;
    }
    if (!W || !P || !PC || !H)
    {
        if (SmokeTime > 15) { Check(false, TEXT("World, player and HUD initialized")); Finish(); }
        return;
    }
    auto* Target = W->FindFolk(2);
    if (!Target) { Check(false, TEXT("Combat fixture target exists")); Finish(); return; }
    auto Next = [&](int32 Stage) { SmokeStage = Stage; F.PhaseAt = SmokeTime; };
    auto Place = [W](AVerdantCharacter* Who, FVector Where)
    {
        Who->GetCharacterMovement()->ClearAccumulatedForces();
        Who->GetCharacterMovement()->StopMovementImmediately();
        Who->SetActorLocation(W->SurfacePoint(Where, Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f));
        Who->GetCharacterMovement()->bForceNextFloorCheck = true;
    };
    auto ResetFixture = [&]()
    {
        ReleaseKeys(); if (H->bConversationOpen) H->CloseConversation();
        P->CancelForConversation(); P->DodgeRecoveryTimer = 0; P->HurtTimer = 0; P->Nutrition = 0;
        Target->CancelForConversation(); Target->DodgeRecoveryTimer = 0; Target->HurtTimer = 0; Target->Nutrition = 0;
        Place(Target, CombatAnchor + FVector(2500, 0, 0));
        Place(P, CombatAnchor); P->SetSize(1.f); Place(P, CombatAnchor);
        Target->SetSize(1.f); Place(Target, CombatAnchor + FVector(170, 0, 0));
        P->Health = P->MaxHealth(); Target->Health = Target->MaxHealth();
        P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        P->SetActorRotation(FRotator::ZeroRotator); PC->SetControlRotation(FRotator(-14, 0, 0));
        F.TargetHealth = Target->Health;
    };
    const float Phase = SmokeTime - F.PhaseAt;
    if (SmokeStage == 0 && SmokeTime > 3.f)
    {
        for (AVerdantCharacter* NPC : W->Folk) if (NPC && NPC != P)
        {
            NPC->SetActorTickEnabled(false);
            NPC->GetCharacterMovement()->ClearAccumulatedForces();
            NPC->GetCharacterMovement()->StopMovementImmediately();
            NPC->GetCharacterMovement()->DisableMovement();
        }
        Check(H->HasCanvasFont(),TEXT("Canvas labels have a live runtime Chinese font object"));
        ResetFixture(); F.Origin = P->GetActorLocation(); Key(EKeys::A, true); Next(1);
    }
    else if (SmokeStage == 1 && Phase > .22f)
    {
        Check(P->GetActorLocation().Y < F.Origin.Y - 8.f, TEXT("Held A input causes real lateral movement"));
        F.StrafeOrigin = P->GetActorLocation();
        Place(Target, F.StrafeOrigin + FVector(170, 0, 0));
        F.TargetHealth = Target->Health;
        Tap(EKeys::LeftMouseButton); Next(2);
    }
    else if (SmokeStage == 2)
    {
        if (P->AttackTimer > 0)
        {
            F.bAttackSeen = true;
            F.MaxYawError = FMath::Max(F.MaxYawError, FMath::Abs(FMath::FindDeltaAngleDegrees(0.f, float(P->GetActorRotation().Yaw))));
            if (P->AttackAnimation && P->GetMesh()->GetSingleNodeInstance()
                && P->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() == P->AttackAnimation.Get())
            {
                const float Expected = (1.f - P->AttackTimer / P->AttackDuration) * P->AttackAnimation->GetPlayLength();
                F.MaxAttackPoseError = FMath::Max(F.MaxAttackPoseError, FMath::Abs(P->GetMesh()->GetPosition() - Expected));
                ++F.AttackPoseSamples;
            }
        }
        if (Phase > .72f)
        {
            Key(EKeys::A, false);
            Check(F.bAttackSeen && F.MaxYawError < 3.f, TEXT("Attack facing remains camera-directed while A is held"));
            Check(P->GetActorLocation().Y < F.StrafeOrigin.Y - 12.f, TEXT("Attack permits lateral footwork without rotating the strike"));
            Check(Target->Health < F.TargetHealth, TEXT("The sideways attack actually damages the forward target"));
            Check(F.AttackPoseSamples >= 3 && F.MaxAttackPoseError < .08f, TEXT("Attack pose follows the authoritative hit clock"));
            ResetFixture(); Next(3);
        }
    }
    else if (SmokeStage == 3 && Phase > .25f)
    { Tap(EKeys::LeftMouseButton); Next(4); }
    else if (SmokeStage == 4 && Phase > .11f)
    {
        Check(P->AttackTimer > 0 && !P->bHitApplied, TEXT("Interruption fixture catches a real unlanded light attack"));
        F.PlayerHealth = P->Health; F.TargetHealth = Target->Health;
        P->ReceiveStrike(Target, 5.f);
        Check(P->Health < F.PlayerHealth && P->AttackTimer == 0, TEXT("Incoming strike cancels the unlanded light attack"));
        P->GetCharacterMovement()->ClearAccumulatedForces();
        P->GetCharacterMovement()->DisableMovement();
        Next(5);
    }
    else if (SmokeStage == 5)
    {
        // Hold both bodies in clear line-of-sight and melee range after knockback.
        // An uncancelled delayed hit would still connect, so unchanged health is meaningful.
        Place(P, CombatAnchor); Place(Target, CombatAnchor + FVector(170, 0, 0));
        P->SetActorRotation(FRotator::ZeroRotator);
        if (Phase > .8f)
        {
            FHitResult Block; FCollisionQueryParams Q(SCENE_QUERY_STAT(VerdantCombatFixture), false, P); Q.AddIgnoredActor(Target);
            const bool Clear = !GetWorld()->LineTraceSingleByChannel(Block, P->GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Q);
            Check(Clear && FVector::Dist2D(P->GetActorLocation(), Target->GetActorLocation()) < P->Reach() + Target->GetCapsuleComponent()->GetScaledCapsuleRadius(), TEXT("Ghost-hit test preserves range and line-of-sight after knockback"));
            Check(FMath::IsNearlyEqual(Target->Health, F.TargetHealth, .001f), TEXT("Interrupted light attack never applies delayed damage"));
            ResetFixture(); Place(Target, CombatAnchor + FVector(1200, 0, 0)); Next(6);
        }
    }
    else if (SmokeStage == 6 && Phase > .3f)
    {
        Check(P->GetCharacterMovement()->IsMovingOnGround(), TEXT("Dodge fixture is physically grounded"));
        F.Origin = P->GetActorLocation(); F.PlayerHealth = P->Health;
        Key(EKeys::W, true); Next(7);
    }
    else if (SmokeStage == 7 && Phase > .1f)
    { Tap(EKeys::SpaceBar); Key(EKeys::W, false); Next(8); }
    else if (SmokeStage == 8)
    {
        const float Elapsed = P->DodgeActiveDuration - P->DodgeTimer;
        if (P->DodgeTimer > 0 && Elapsed >= .10f && Elapsed < .28f)
        {
            Check(P->IsDodgeInvulnerable(), TEXT("Real Space input opens the early dodge invulnerability window"));
            Check(P->DodgeAnimation && P->GetMesh()->GetSingleNodeInstance()
                && P->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset() == P->DodgeAnimation.Get(), TEXT("Dodge plays the imported roll animation"));
            P->ReceiveStrike(Target, 7.f);
            Check(FMath::IsNearlyEqual(P->Health, F.PlayerHealth, .001f), TEXT("A strike during the roll window is actually avoided"));
            Next(9);
        }
        else if (Phase > .8f)
        { Check(false, TEXT("A real roll reached its early test window")); Next(9); }
    }
    else if (SmokeStage == 9)
    {
        const float Elapsed = P->DodgeActiveDuration - P->DodgeTimer;
        if (P->DodgeTimer > 0 && Elapsed >= .4f)
        {
            Check(!P->IsDodgeInvulnerable(), TEXT("Roll recovery has a vulnerable late phase"));
            Check(FVector::Dist2D(F.Origin, P->GetActorLocation()) > 65.f, TEXT("Roll animation is accompanied by collision-resolved movement"));
            P->ReceiveStrike(Target, 7.f);
            Check(P->Health < F.PlayerHealth, TEXT("A late-roll strike deals damage"));
            Next(10);
        }
        else if (Phase > .8f)
        { Check(false, TEXT("Roll reached its late vulnerable test window")); Next(10); }
    }
    else if (SmokeStage == 10)
    {
        if (P->HurtTimer <= 0 && P->DodgeRecoveryTimer > .08f && P->GetCharacterMovement()->IsMovingOnGround())
        {
            F.RecoveryAtAttempt = P->DodgeRecoveryTimer;
            Tap(EKeys::SpaceBar); Next(11);
        }
        else if (Phase > .8f)
        { Check(false, TEXT("Independent dodge recovery remains after stagger and landing")); Next(12); }
    }
    else if (SmokeStage == 11 && Phase > .06f)
    {
        Check(P->HurtTimer <= 0 && P->DodgeTimer <= 0 && F.RecoveryAtAttempt > .08f,
            TEXT("Space cannot chain a new roll during independent recovery"));
        Next(12);
    }
    else if (SmokeStage == 12)
    {
        if (!F.bDodgeAttempted && P->DodgeRecoveryTimer <= 0 && P->HurtTimer <= 0 && P->GetCharacterMovement()->IsMovingOnGround())
        { Tap(EKeys::SpaceBar); F.bDodgeAttempted = true; F.PhaseAt = SmokeTime; }
        else if (F.bDodgeAttempted && Phase > .09f)
        {
            Check(P->DodgeTimer > 0, TEXT("Space starts a new roll after recovery finishes"));
            ResetFixture(); Next(13);
        }
        else if (Phase > 2.f) { Check(false, TEXT("Recovery eventually allows the next roll")); ResetFixture(); Next(13); }
    }
    else if (SmokeStage == 13 && Phase > .25f)
    {
        F.TargetHealth = Target->Health; Key(EKeys::LeftShift, true); Tap(EKeys::LeftMouseButton); Next(14);
    }
    else if (SmokeStage == 14 && Phase > .1f)
    {
        Check(P->AttackTimer > 0 && !P->bHitApplied, TEXT("Conversation fixture begins during an unlanded attack"));
        Tap(EKeys::T); Key(EKeys::LeftShift, false); Next(15);
    }
    else if (SmokeStage == 15 && Phase > .1f)
    {
        Check(H->bConversationOpen && P->AttackTimer == 0 && !P->bSprinting, TEXT("T opens conversation and cancels the pending attack and sprint"));
        Next(16);
    }
    else if (SmokeStage == 16)
    {
        Place(P, CombatAnchor); Place(Target, CombatAnchor + FVector(170, 0, 0)); P->SetActorRotation(FRotator::ZeroRotator);
        if (Phase > .7f)
        {
            Check(FMath::IsNearlyEqual(Target->Health, F.TargetHealth, .001f), TEXT("Opening conversation cannot leave a delayed attack against its speaker"));
            F.PlayerHealth = P->Health; P->ReceiveStrike(Target, 4.f);
            Check(P->Health < F.PlayerHealth && !H->bConversationOpen && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored(), TEXT("Being hit interrupts conversation and restores movement and look input"));
            P->GetCharacterMovement()->ClearAccumulatedForces(); P->GetCharacterMovement()->StopMovementImmediately(); Next(17);
        }
    }
    else if (SmokeStage == 17 && Phase > .4f)
    { F.Origin = P->GetActorLocation(); Key(EKeys::W, true); Next(18); }
    else if (SmokeStage == 18 && Phase > .25f)
    {
        Key(EKeys::W, false);
        Check(P->GetActorLocation().X > F.Origin.X + 10.f, TEXT("Held W moves the player after conversation interruption"));
        P->CancelForConversation(); P->Nutrition = 0; P->HurtTimer = 0;
        ResetFixture(); Next(26);
    }
    else if (SmokeStage == 26 && Phase > .25f)
    { F.TargetHealth=Target->Health;Tap(EKeys::LeftMouseButton);Next(27); }
    else if (SmokeStage == 27)
    {
        Place(Target,CombatAnchor+FVector(170,0,0));
        if(P->AttackTimer>0&&P->AttackTimer<.16f){Tap(EKeys::RightMouseButton);Next(28);}
        else if(Phase>1.f){Check(false,TEXT("Late attack input reached the recovery window"));Next(28);}
    }
    else if (SmokeStage == 28)
    {
        Place(Target,CombatAnchor+FVector(170,0,0));
        if(P->bHeavyAttack&&P->AttackTimer>.55f){Check(true,TEXT("A late right-click starts one heavy follow-up after the light attack"));Capture(TEXT("combat-windup.png"));F.bImpactCaptured=false;Next(29);}
        else if(Phase>.65f){Check(false,TEXT("A late right-click starts one heavy follow-up after the light attack"));Next(29);}
    }
    else if (SmokeStage == 29)
    {
        Place(Target,CombatAnchor+FVector(170,0,0));
        if(Phase>.58f&&!F.bImpactCaptured){Capture(TEXT("combat-impact.png"));F.bImpactCaptured=true;}
        if(Phase>1.2f){
            Check(F.TargetHealth-Target->Health>=P->Strength()*2.7f,TEXT("Buffered light-to-heavy sequence causes two real hits"));
            Check(P->AttackTimer<=0&&P->QueuedAttackType<0,TEXT("One buffered click does not auto-repeat further attacks"));
            ResetFixture();Tap(EKeys::LeftMouseButton);Next(30);
        }
    }
    else if (SmokeStage == 30)
    {
        Place(Target,CombatAnchor+FVector(170,0,0));
        if(P->AttackTimer>0&&P->AttackTimer<.16f){Tap(EKeys::RightMouseButton);Tap(EKeys::T);F.TargetHealth=Target->Health;Next(31);}
        else if(Phase>1.f){Check(false,TEXT("Conversation cancellation fixture has a buffered follow-up"));Next(31);}
    }
    else if (SmokeStage == 31 && Phase > 1.f)
    {
        Check(H->bConversationOpen&&P->QueuedAttackType<0&&P->AttackTimer<=0&&FMath::IsNearlyEqual(Target->Health,F.TargetHealth,.01f),TEXT("Opening dialogue cancels a buffered follow-up without a delayed hit"));
        ResetFixture();Tap(EKeys::LeftMouseButton);Next(32);
    }
    else if (SmokeStage == 32)
    {
        Place(Target,CombatAnchor+FVector(170,0,0));
        if(P->AttackTimer>0&&P->AttackTimer<.16f){Tap(EKeys::RightMouseButton);F.TargetHealth=Target->Health;P->ReceiveStrike(Target,5.f);Next(33);}
        else if(Phase>1.f){Check(false,TEXT("Stagger cancellation fixture has a buffered follow-up"));Next(33);}
    }
    else if (SmokeStage == 33 && Phase > 1.f)
    {
        Check(P->QueuedAttackType<0&&P->AttackTimer<=0&&FMath::IsNearlyEqual(Target->Health,F.TargetHealth,.01f),TEXT("Being staggered cancels the buffered follow-up"));
        ResetFixture();Place(Target,CombatAnchor+FVector(2500,0,0));F.CameraIndex=0;Next(19);
    }
    else if (SmokeStage == 19)
    {
        if (W->Groves.IsEmpty() || !W->Dialogue)
        { Check(false, TEXT("Expanded HUD framing fixture has a grove and dialogue state")); Finish(); return; }
        const float Sizes[] = { .45f, 1.4f, 2.5f };
        const FVector FrameAnchor = W->Groves[0].Position + FVector(0, -180, 0);
        // Explicitly frame the taller HUD: a selected courier plus grove and
        // nearby-resident interaction lines. All NPC simulation stays frozen.
        for (AVerdantCharacter* Other : W->Folk)
            if (Other && Other != P && Other != Target && FVector::Dist2D(Other->GetActorLocation(), FrameAnchor) < 1800.f)
                Place(Other, W->Groves[0].Position + FVector(5000.f + Other->AgentId * 250.f, 3000, 0));
        Place(Target, FrameAnchor + FVector(2500, 0, 0));
        Place(P, FrameAnchor); P->SetSize(Sizes[F.CameraIndex]); Place(P, FrameAnchor);
        Place(Target, FrameAnchor + FVector(450, 0, 0));
        Target->DonationPledge = 5.f; Target->Food = 10.f; Target->LastDonationTime = -1.f;
        Target->SetActorTickEnabled(false); Target->GetCharacterMovement()->DisableMovement();
        W->Dialogue->SelectNPC(Target);
        P->GetCharacterMovement()->SetMovementMode(MOVE_Walking); P->Health = P->MaxHealth();
        P->SetActorRotation(FRotator(0, 78, 0)); PC->SetControlRotation(FRotator(-10, 78, 0));
        Check(FMath::IsNearlyEqual(P->BodySize, Sizes[F.CameraIndex], .001f), FString::Printf(TEXT("Open framing fixture permits body size %.2f"), Sizes[F.CameraIndex]));
        Next(20);
    }
    else if (SmokeStage == 20 && Phase > 1.5f)
    {
        int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height);
        const FBoxSphereBounds Bounds = P->GetMesh()->Bounds;
        const FVector Head = Bounds.Origin + FVector(0, 0, Bounds.BoxExtent.Z);
        const FVector Feet = Bounds.Origin - FVector(0, 0, Bounds.BoxExtent.Z);
        FVector2D HeadScreen = FVector2D::ZeroVector, FeetScreen = FVector2D::ZeroVector;
        const bool Projected = PC->ProjectWorldLocationToScreen(Head, HeadScreen) && PC->ProjectWorldLocationToScreen(Feet, FeetScreen);
        const bool Framed = Projected && Width > 0 && Height > 0 && HeadScreen.Y > 20 && FeetScreen.Y < Height - 190
            && HeadScreen.X > 20 && HeadScreen.X < Width - 20 && FeetScreen.X > 20 && FeetScreen.X < Width - 20;
        const bool ExpandedHUD = W->Dialogue && W->Dialogue->Selected.Get() == Target
            && Target->DonationPledge > 0 && Target->Food > 0 && !Target->bDead
            && W->NearestGrove(P->GetActorLocation(), 700.f) >= 0
            && P->NearestFolk(750.f * FMath::Max(1.f, P->BodySize)) == Target;
        Check(Framed && ExpandedHUD, FString::Printf(TEXT("Size %.2f head and feet clear 190px with delivery and two-line interaction HUD"), P->BodySize));
        auto Measurement = MakeShared<FJsonObject>(); Measurement->SetNumberField(TEXT("body_size"), P->BodySize);
        Measurement->SetNumberField(TEXT("width"), Width); Measurement->SetNumberField(TEXT("height"), Height);
        Measurement->SetNumberField(TEXT("head_x"), HeadScreen.X); Measurement->SetNumberField(TEXT("head_y"), HeadScreen.Y);
        Measurement->SetNumberField(TEXT("feet_x"), FeetScreen.X); Measurement->SetNumberField(TEXT("feet_y"), FeetScreen.Y);
        Measurement->SetNumberField(TEXT("arm_length_cm"), P->CameraArm->TargetArmLength);
        Measurement->SetNumberField(TEXT("bottom_safe_pixels"), 190);
        Measurement->SetBoolField(TEXT("expanded_delivery_and_interaction_hud"), ExpandedHUD);
        Measurement->SetBoolField(TEXT("passed"), Framed && ExpandedHUD); F.Framing.Add(MakeShared<FJsonValueObject>(Measurement));
        Capture(FString::Printf(TEXT("combat-size-%d.png"), F.CameraIndex)); Next(21);
    }
    else if (SmokeStage == 21 && Phase > .5f)
    {
        Check(IFileManager::Get().FileSize(*F.Shot) > 0, FString::Printf(TEXT("Size %d screenshot was rendered"), F.CameraIndex));
        ++F.CameraIndex;
        if (F.CameraIndex < 3) Next(19);
        else
        {
            // Independent boundary fixture starts only after all original framing shots.
            P->SetSize(2.495f); P->Nutrition = 0; P->Food = 10.f; P->Health = P->MaxHealth() - 30.f;
            F.BoundaryFood = P->Food;
            const bool Ate = P->Eat(5.f);
            F.BoundaryHealth = P->Health;
            Check(Ate && FMath::IsNearlyEqual(P->Food, F.BoundaryFood - 5.f, .001f), TEXT("Near-limit growth consumes five actual food units"));
            Next(24);
        }
    }
    else if (SmokeStage == 24 && Phase > 2.8f)
    {
        Check(FMath::IsNearlyEqual(P->BodySize, 2.5f, .00001f) && P->Nutrition <= .001f && P->Health > F.BoundaryHealth,
            TEXT("Growth crosses the old 2.49 boundary and finishes digesting at the 2.5 cap"));
        // Test capped healing separately so a failed crossing cannot hide its failure.
        P->SetSize(2.5f); P->Nutrition = 0; P->Food = 10.f; P->Health = P->MaxHealth() - 20.f;
        F.BoundaryFood = P->Food; F.BoundaryHealth = P->Health;
        const bool Ate = P->Eat(5.f);
        Check(Ate && FMath::IsNearlyEqual(P->Food, F.BoundaryFood - 5.f, .001f) && P->Health > F.BoundaryHealth,
            TEXT("An injured full-size player can spend carried food to restore health"));
        F.BoundaryHealth = P->Health;
        Next(25);
    }
    else if (SmokeStage == 25 && Phase > 2.8f)
    {
        Check(FMath::IsNearlyEqual(P->BodySize, 2.5f, .00001f) && P->Nutrition <= .001f && P->Health > F.BoundaryHealth,
            TEXT("At-cap nutrition empties through real-time digestion without additional growth"));
        P->Health = P->MaxHealth();
        F.BoundaryFood = P->Food;
        const float BeforeNutrition = P->Nutrition;
        const bool Ate = P->Eat(5.f);
        Check(!Ate && FMath::IsNearlyEqual(P->Food, F.BoundaryFood, .001f) && FMath::IsNearlyEqual(P->Nutrition, BeforeNutrition, .001f),
            TEXT("A healthy full-size player refuses extra food without consuming inventory"));
        W->TreeNutrition = 37.f; W->bRestored = false;
        P->ReceiveStrike(Target, P->MaxHealth() + 100.f); Next(22);
    }
    else if (SmokeStage == 22 && Phase > .35f)
    {
        Check(P->bDead && P->Health <= 0 && !H->bConversationOpen, TEXT("Defeat is visible without leaving a conversation input trap"));
        Capture(TEXT("combat-defeat.png")); Next(23);
    }
    else if (SmokeStage == 23 && Phase > .6f)
    {
        Check(IFileManager::Get().FileSize(*F.Shot) > 0, TEXT("Defeat screenshot was rendered before restart"));
        F.OldWorldId = GetWorld()->GetUniqueID(); F.OldPawnId = P->GetUniqueID();
        F.SavedChecks = SmokeChecks; F.SavedFailures = SmokeFailures; F.PriorElapsed = SmokeTime;
        F.RestartWallTime = FPlatformTime::Seconds(); F.bRestartRequested = true;
        Tap(EKeys::R);
    }
}
