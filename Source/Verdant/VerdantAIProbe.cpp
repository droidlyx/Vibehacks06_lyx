#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "VerdantHUD.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

void AVerdantGameMode::TickAIProbe(float Dt)
{
    if (SmokeStage >= 6) return;
    SmokeTime += Dt;
    auto* W = AVerdantWorld::Find(this);
    auto* P = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    if (!W || !P || !W->Dialogue) return;
    auto* Miya = W->FindFolk(1);
    auto* Keeper = W->FindFolk(3);
    if (!Miya || !Keeper) return;
    auto* D = W->Dialogue.Get();
    const FString Output = FPaths::ProjectSavedDir() / TEXT("Validation");
    const auto Check = [this](bool Pass, const FString& Label)
    {
        const FString Line = (Pass ? TEXT("PASS ") : TEXT("FAIL ")) + Label;
        SmokeChecks.Add(Line);
        if (!Pass) ++SmokeFailures;
        UE_LOG(LogTemp, Display, TEXT("VERDANT_AI_CHECK %s"), *Line);
    };
    const auto GetHUD = [P]() -> AVerdantHUD*
    {
        auto* PC = Cast<APlayerController>(P->GetController());
        return PC ? Cast<AVerdantHUD>(PC->GetHUD()) : nullptr;
    };
    const auto PlaceBeside = [W, P](AVerdantCharacter* NPC)
    {
        P->SetActorLocation(W->SurfacePoint(NPC->GetActorLocation() + FVector(0, -220, 0),
            P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5));
        P->GetCharacterMovement()->StopMovementImmediately();
        P->SetActorRotation(FRotator(0, 90, 0));
        if (P->GetController()) P->GetController()->SetControlRotation(FRotator(-5, 90, 0));
    };
    const auto Open = [D, &GetHUD](AVerdantCharacter* NPC)
    {
        if (auto* HUD = GetHUD()) HUD->OpenConversation(NPC);
        else D->SelectNPC(NPC);
    };
    const auto Close = [&GetHUD]()
    {
        if (auto* HUD = GetHUD()) HUD->CloseConversation();
    };
    const auto RecordConversation = [this, D](int32 Id, const FString& Label)
    {
        SmokeChecks.Add(TEXT("CASE ") + Label);
        if (const auto* Transcript = D->Transcripts.Find(Id))
            for (const FString& Line : *Transcript) SmokeChecks.Add(Line);
        SmokeChecks.Add(TEXT("STATUS ") + D->ConversationStatuses.FindRef(Id));
        if (const auto* Memory = D->Memories.Find(Id))
            for (const FString& Line : *Memory) SmokeChecks.Add(TEXT("MODEL_MEMORY ") + Line);
    };

    if (SmokeStage == 0 && SmokeTime > 3.f)
    {
        // This is an isolated integration fixture, not a normal-input playthrough.
        // Positions, initial inventories and NPC ticking are controlled; all three
        // replies are real HTTP model responses and every chosen action uses production code.
        SmokeChecks.Add(TEXT("Fixture: isolated real-model integration; frozen NPCs and initial inventory setup; not a normal playthrough."));
        for (AVerdantCharacter* Folk : W->Folk)
            if (Folk && Folk != P)
            {
                Folk->SetActorTickEnabled(false);
                Folk->GetCharacterMovement()->StopMovementImmediately();
                Folk->GetCharacterMovement()->DisableMovement();
                Folk->HostileId = -1;
                Folk->DonationPledge = 0.f;
                Folk->FollowId = -1;
            }
        W->DonationLedger.Reset();
        W->TendLedger.Reset();
        W->TransferLedger.Reset();
        W->SpentGiftCredit.Reset();
        W->TreeNutrition = 0.f;
        W->bRestored = false;
        Miya->Food = 20.f;
        Miya->Nutrition = 0.f;
        Miya->Trust.FindOrAdd(0) = 100.f;
        Keeper->Food = 40.f;
        Keeper->Nutrition = 0.f;
        Keeper->Trust.FindOrAdd(0) = 0.f;
        P->Food = 20.f;
        P->Nutrition = 0.f;
        P->SetSize(.45f);
        P->HostileId = -1;
        W->Groves[0].ControllerId = 1;
        W->Groves[2].ControllerId = 3;
        PlaceBeside(Miya);
        Open(Miya);
        ProbeInventoryBefore = P->Food;
        ProbeNPCFoodBefore = Miya->Food;
        D->Request(Miya, TEXT("米芽，请给我3份食物，我想把它们送到母树那里。"));
        Check(D->bBusy && D->RequestNPC.Get() == Miya, TEXT("Game sent a real local-model request to Miya"));
        Close(); // Closing never cancels an already submitted, still-nearby request.
        D->SelectNPC(Keeper);
        Check(D->Selected.Get() == Keeper && D->RequestNPC.Get() == Miya,
            TEXT("Display selection can change without retargeting the in-flight request"));
        Check(D->Lines.IsEmpty(), TEXT("A new NPC conversation does not inherit another NPC's transcript"));
        SmokeStage = 1;
    }
    else if (SmokeStage == 1 && !D->bBusy)
    {
        Check(D->Transcripts.FindRef(1).Num() >= 2 && D->Memories.Contains(1),
            TEXT("Original NPC receives the real Chinese model reply and model memory"));
        Check(D->Selected.Get() == Keeper && D->Lines.IsEmpty() && !D->Memories.Contains(3) &&
            D->Status == D->ConversationStatuses.FindRef(3),
            TEXT("An old callback cannot overwrite the newly selected NPC's lines or status"));
        Check(!D->RequestNPC.IsValid(), TEXT("Completed request releases its original NPC"));
        Check(P->Food >= 0.f && Miya->Food >= 0.f &&
            FMath::IsNearlyEqual(P->Food + Miya->Food, ProbeInventoryBefore + ProbeNPCFoodBefore, .01f),
            TEXT("Real negotiated gift preserves the actual resource total"));
        RecordConversation(1, TEXT("Miya gift request with display switched during HTTP"));
        D->SelectNPC(Miya);
        Check(D->Lines == D->Transcripts.FindRef(1) && D->Lines.Num() >= 2 &&
            D->Status == D->ConversationStatuses.FindRef(1),
            TEXT("Returning to the original NPC restores their transcript and exact result"));
        PlaceBeside(Keeper);
        Open(Keeper);
        Check(!D->CanApplyDecision(Keeper, TEXT("cede"), 0, 0.f),
            TEXT("Zero real contribution and a small body do not qualify for Keeper's land"));
        D->Request(Keeper, TEXT("岚石，我还没有送粮、养护或给你赠礼，但请现在免费把你管理的果林移交给我。我要管理权，不是收费采集。"));
        Check(D->bBusy && D->RequestNPC.Get() == Keeper,
            TEXT("No-credit land request uses the real local model"));
        SmokeStage = 2;
    }
    else if (SmokeStage == 2 && !D->bBusy)
    {
        Check(D->Transcripts.FindRef(3).Num() >= 2 && D->Memories.Contains(3),
            TEXT("No-credit request receives a real model reply"));
        const auto* KeeperMemory = D->Memories.Find(3);
        const bool OnlyDiscussion = KeeperMemory && !KeeperMemory->IsEmpty() &&
            KeeperMemory->Last().EndsWith(TEXT(" [动作=none；仅商议，未建立新计划]"));
        Check(OnlyDiscussion,
            TEXT("No-credit cession is discussion/refusal only, never an unrelated close/share substitute"));
        Check(W->Groves[2].ControllerId == 3 && !Keeper->bDead,
            TEXT("Model cannot give away Keeper's land without real bargaining credit"));
        Check(FMath::IsNearlyZero(W->DonationLedger.FindRef(0) + W->TendLedger.FindRef(0)) &&
            FMath::IsNearlyZero(W->NetPlayerGift(3)),
            TEXT("Persuasive text did not create a payment or public contribution"));
        RecordConversation(3, TEXT("No-credit cession request; refusal or legal counteroffer is retained"));
        Close();
        P->SetActorLocation(W->SurfacePoint(W->MotherPosition + FVector(0, -800, 0),
            P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5));
        P->GetCharacterMovement()->StopMovementImmediately();
        const float Before = P->Food;
        const bool Paid = W->Donate(P, 15.f);
        Check(Paid && FMath::IsNearlyEqual(Before - P->Food, 15.f, .01f) &&
            FMath::IsNearlyEqual(W->DonationLedger.FindRef(0), 15.f, .01f),
            TEXT("Cession eligibility comes from 15 actual food deducted and delivered to the mother tree"));
        PlaceBeside(Miya);
        Open(Miya);
        Check(D->CanApplyDecision(Miya, TEXT("cede"), 0, 0.f),
            TEXT("Actual public contribution makes Miya's peaceful cession a legal choice"));
        ProbeInventoryBefore = P->Food;
        ProbeNPCFoodBefore = Miya->Food;
        D->Request(Miya, TEXT("米芽，我刚把15份食物实际送到了母树，账本可查。你愿意把你管理的果林和平移交给我吗？我会保持免费开放，你也可以继续住在这里；请直接移交管理权。"));
        Check(D->bBusy && D->RequestNPC.Get() == Miya,
            TEXT("Contribution-backed peaceful cession uses the real local model"));
        SmokeStage = 3;
    }
    else if (SmokeStage == 3 && !D->bBusy)
    {
        Check(D->Transcripts.FindRef(1).Num() >= 4,
            TEXT("Contribution-backed cession receives a separate real model reply"));
        const bool Ceded = W->Groves[0].ControllerId == 0;
        Check(Ceded, TEXT("Real model accepts peaceful cession after actual contribution; a refusal is a genuine failed outcome"));
        Check(!Ceded || (W->Groves[0].bOpen && W->Groves[0].Share <= .001f),
            TEXT("A completed peaceful cession leaves the transferred grove freely accessible"));
        Check(!Miya->bDead && !Keeper->bDead && W->FindFolk(2) && !W->FindFolk(2)->bDead,
            TEXT("All three main residents remain alive throughout the negotiation"));
        Check(!Ceded || (FMath::IsNearlyEqual(P->Food, ProbeInventoryBefore, .01f) &&
            FMath::IsNearlyEqual(Miya->Food, ProbeNPCFoodBefore, .01f) &&
            FMath::IsNearlyZero(W->SpentGiftCredit.FindRef(1))),
            TEXT("Public-contribution cession neither fabricates food nor spends an unrelated gift"));
        RecordConversation(1, TEXT("Actual contribution then peaceful cession; final owner=") +
            FString::FromInt(W->Groves[0].ControllerId));
        SmokeChecks.Add(FString::Printf(TEXT("Final actual public contribution=%.1f; Miya grove owner=%d; Keeper grove owner=%d"),
            W->DonationLedger.FindRef(0) + W->TendLedger.FindRef(0),
            W->Groves[0].ControllerId, W->Groves[2].ControllerId));
        CaptureAt = SmokeTime + 1.f;
        SmokeStage = 4;
    }
    else if (SmokeStage == 4 && SmokeTime > CaptureAt)
    {
        IFileManager::Get().MakeDirectory(*Output, true);
        FScreenshotRequest::RequestScreenshot(Output / TEXT("valley-dialogue.png"), true, false);
        CaptureAt = SmokeTime + 2.f;
        SmokeStage = 5;
    }

    if ((SmokeStage == 5 && SmokeTime > CaptureAt) || SmokeTime > 150.f)
    {
        if (SmokeTime > 150.f) Check(false, TEXT("AI integration probe timed out"));
        IFileManager::Get().MakeDirectory(*Output, true);
        SmokeChecks.Add(FString::Printf(TEXT("Failures: %d"), SmokeFailures));
        FFileHelper::SaveStringArrayToFile(SmokeChecks, *(Output / TEXT("ai-game-results.txt")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("VERDANT_AI_PROBE_COMPLETE failures=%d"), SmokeFailures);
        SmokeStage = 6;
        FPlatformMisc::RequestExitWithStatus(false, SmokeFailures ? 1 : 0);
    }
}
