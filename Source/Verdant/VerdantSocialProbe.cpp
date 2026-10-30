// Synthetic social-policy fixtures. These intentionally configure balances and
// positions to verify authoritative limits; they are NOT a normal-play journey.
#include "VerdantGameMode.h"
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

void AVerdantGameMode::TickSocialProbe(float Dt)
{
    SmokeTime += Dt;
    if (SmokeStage != 0 || SmokeTime < 2.f) return;
    auto* W = AVerdantWorld::Find(this);
    auto* P = W ? W->FindFolk(0) : nullptr;
    if (!W || !P || !W->Dialogue || W->Groves.Num() != 3)
    {
        if (SmokeTime > 15.f) FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    auto* Miya = W->FindFolk(1);
    auto* Trader = W->FindFolk(2);
    auto* Keeper = W->FindFolk(3);
    auto* Wild = W->FindFolk(4);
    if (!Miya || !Trader || !Keeper || !Wild) return;
    SmokeStage = 1;
    const auto Check = [this](bool Pass, const FString& Label)
    {
        const FString Line = (Pass ? TEXT("PASS ") : TEXT("FAIL ")) + Label;
        SmokeChecks.Add(Line);
        if (!Pass) ++SmokeFailures;
        UE_LOG(LogTemp, Display, TEXT("VERDANT_SOCIAL %s"), *Line);
    };
    const auto Near = [P](AVerdantCharacter* NPC)
    {
        P->SetActorLocation(NPC->GetActorLocation() + FVector(210.f, 0.f, 0.f), false);
    };
    const auto Reset = [W,P]()
    {
        W->TransferLedger.Reset(); W->SpentGiftCredit.Reset();
        W->DonationLedger.Reset(); W->TendLedger.Reset(); W->DonationNames.Reset();
        W->TreeNutrition = 0.f; W->bRestored = false;
        P->SetSize(.45f); P->Food = 100.f; P->Nutrition = 0.f; P->DonationPledge = 0.f;
        for (AVerdantCharacter* N : W->Folk)
        {
            if (!IsValid(N)) continue;
            N->bDead = false; N->HostileId = -1; N->CombatTarget.Reset();
            N->FollowId = -1; N->DonationPledge = 0.f;
            N->Trust.Reset();
            if (N != P) N->Food = 40.f;
        }
        for (int32 I = 0; I < W->Groves.Num(); ++I)
        {
            auto& G = W->Groves[I];
            G.ControllerId = I + 1; G.bOpen = I != 2; G.Share = I == 1 ? .2f : 0.f;
            G.Permitted.Reset(); G.Food = 20.f; G.Fertility = I == 1 ? .35f : 1.f;
        }
        W->FindFolk(1)->HomeGrove = 0;
        W->FindFolk(2)->HomeGrove = 1;
        W->FindFolk(3)->HomeGrove = 2;
    };

    Reset();
    Check(W->NegotiationReserve(Miya) == 6.f && W->NegotiationReserve(Trader) == 12.f &&
        W->NegotiationReserve(Keeper) == 18.f, TEXT("Residents retain distinct real food reserves"));
    Near(Trader);
    Check(W->MaxNegotiatedDonation(Trader) == 0.f &&
        !W->Dialogue->CanApplyDecision(Trader, TEXT("open"), 0, 0.f),
        TEXT("Unproven requests cannot obtain a trader donation or free opening"));
    P->DonationPledge = 100.f;
    Check(W->MaxNegotiatedDonation(Trader) == 0.f &&
        !W->CanNegotiatePolicy(Trader, TEXT("cede"), 1),
        TEXT("Unfulfilled promises are not public contribution evidence"));
    P->DonationPledge = 0.f;

    Near(Miya);
    const float GiftTotal = P->Food + Miya->Food;
    Check(FMath::IsNearlyEqual(W->MaxNegotiatedGive(Miya, P), 3.f),
        TEXT("Miya offers only one bounded net introductory gift"));
    const bool Gift = W->Dialogue->ApplyDecision(Miya, TEXT("give"), 0, 3.f);
    Check(Gift && FMath::IsNearlyEqual(P->Food + Miya->Food, GiftTotal, .001f) &&
        W->MaxNegotiatedGive(Miya, P) <= .001f &&
        !W->Dialogue->ApplyDecision(Miya, TEXT("give"), 0, .01f),
        TEXT("A repeated request cannot regenerate the introductory gift"));

    Reset(); Near(Trader);
    Check(W->Transfer(P, Trader, 5.f) &&
        W->Dialogue->ApplyDecision(Trader, TEXT("give"), 0, 5.f) &&
        FMath::IsNearlyZero(W->NetPlayerGift(2), .001f) &&
        !W->CanNegotiatePolicy(Trader, TEXT("open"), 1),
        TEXT("A real gift/refund round trip earns no reusable policy credit"));
    Check(W->Transfer(P, Trader, 8.f), TEXT("Policy payment comes from actual player inventory"));
    float Cost = -1.f;
    Check(W->CanNegotiatePolicy(Trader, TEXT("open"), 1, &Cost) && FMath::IsNearlyEqual(Cost, 8.f),
        TEXT("An unused net gift can fund the trader's free-opening concession"));
    const float FoodBeforePolicy = P->Food + Trader->Food;
    const bool Opened = W->Dialogue->ApplyDecision(Trader, TEXT("open"), 0, 0.f);
    Check(Opened && W->Groves[1].bOpen && W->Groves[1].Share == 0.f &&
        FMath::IsNearlyEqual(W->SpentGiftCredit.FindRef(2), 8.f) &&
        FMath::IsNearlyEqual(P->Food + Trader->Food, FoodBeforePolicy, .001f),
        TEXT("Opening consumes bargaining credit once without charging food twice"));
    const float SpentBeforeRepeat = W->SpentGiftCredit.FindRef(2);
    Check(W->Dialogue->ApplyDecision(Trader, TEXT("open"), 0, 0.f) &&
        W->SpentGiftCredit.FindRef(2) == SpentBeforeRepeat,
        TEXT("Repeating an already-free opening never consumes credit again"));
    Check(W->MaxNegotiatedGive(Trader, P) <= .001f &&
        !W->Dialogue->ApplyDecision(Trader, TEXT("give"), 0, 8.f),
        TEXT("Already-spent policy payment cannot be refunded into another free concession"));

    Reset();
    P->SetActorLocation(W->SurfacePoint(W->Groves[1].Position, 100.f), false);
    const float BeforeTendFood = P->Food;
    const float BeforeTendFertility = W->Groves[1].Fertility;
    Check(W->Tend(1, P) && P->Food < BeforeTendFood &&
        W->Groves[1].Fertility > BeforeTendFertility &&
        FMath::IsNearlyEqual(W->TendLedger.FindRef(0), BeforeTendFood - P->Food, .001f),
        TEXT("Tending spends real food, improves fertility and records only the payment"));
    Check(W->Tend(1, P) && W->Tend(1, P), TEXT("Additional tending uses additional actual food"));
    Near(Trader); Cost = -1.f;
    Check(W->DonationLedger.FindRef(0) == 0.f && W->TendLedger.FindRef(0) >= 14.99f &&
        W->CanNegotiatePolicy(Trader, TEXT("open"), 1, &Cost) && Cost == 0.f &&
        FMath::IsNearlyEqual(W->MaxNegotiatedDonation(Trader), 7.5f, .001f),
        TEXT("Actual tending is recognized like actual public donation, without spending gifts"));
    P->SetActorLocation(W->SurfacePoint(W->Groves[1].Position, 100.f), false);
    W->Groves[1].Fertility = 1.f;
    const float FullFood = P->Food, FullLedger = W->TendLedger.FindRef(0);
    Check(!W->Tend(1, P) && P->Food == FullFood && W->TendLedger.FindRef(0) == FullLedger,
        TEXT("A fully fertile grove cannot charge food or mint tending recognition"));
    W->Groves[2].Fertility = .5f;
    P->SetActorLocation(W->SurfacePoint(W->Groves[2].Position, 100.f), false);
    Check(!W->Tend(2, P) && P->Food == FullFood && W->Groves[2].Fertility == .5f,
        TEXT("A closed grove rejects unauthorized tending without side effects"));

    Reset();
    P->SetActorLocation(W->MotherPosition + FVector(400.f, 0.f, 100.f), false);
    Check(W->Donate(P, 15.f), TEXT("Public contribution is backed by an actual Mother Tree payment"));
    Near(Trader);
    Check(!W->Dialogue->ApplyDecision(Trader, TEXT("donate"), 0, 20.f) &&
        W->Dialogue->ApplyDecision(Trader, TEXT("donate"), 0, 7.5f),
        TEXT("The trader must counteroffer within the earned matching budget"));
    Trader->Food = 13.f; Trader->DonationPledge = 1.f;
    Check(W->MaxNegotiatedGive(Trader, P) == 0.f,
        TEXT("Pending delivery plus reserve cannot be given away"));
    Trader->Food = 40.f; Trader->DonationPledge = 0.f;
    Trader->SetActorLocation(W->MotherPosition + FVector(350.f, 0.f, 100.f), false);
    Check(W->Donate(Trader, 7.5f) && W->MaxNegotiatedDonation(Trader) <= .001f,
        TEXT("Completed NPC deliveries consume their matching budget and cannot repeat for free"));

    Reset(); Near(Trader);
    Check(W->Transfer(P, Trader, 15.f), TEXT("A peaceful cession can use a real net payment"));
    const float GroveFood = W->Groves[1].Food;
    const bool Ceded = W->Dialogue->ApplyDecision(Trader, TEXT("cede"), 0, 0.f);
    Check(Ceded && W->Groves[1].ControllerId == 0 && !Trader->bDead && !P->bDead &&
        W->Groves[1].bOpen && W->Groves[1].Share == 0.f && W->Groves[1].Food == GroveFood &&
        FMath::IsNearlyEqual(W->SpentGiftCredit.FindRef(2), 15.f),
        TEXT("A living keeper transfers actual ownership without killing or fabricating stock"));
    const float CessionSpent = W->SpentGiftCredit.FindRef(2);
    Check(!W->Dialogue->ApplyDecision(Trader, TEXT("cede"), 0, 0.f) &&
        W->SpentGiftCredit.FindRef(2) == CessionSpent,
        TEXT("Replayed cession from a former owner is rejected without consuming credit"));

    Reset();
    P->SetActorLocation(W->MotherPosition + FVector(400.f, 0.f, 100.f), false);
    Check(W->Donate(P, 30.f), TEXT("Real public help can support a peaceful territory route"));
    Near(Miya); const bool FirstCession = W->Dialogue->ApplyDecision(Miya, TEXT("cede"), 0, 0.f);
    Near(Trader); const bool SecondCession = W->Dialogue->ApplyDecision(Trader, TEXT("cede"), 0, 0.f);
    const auto PeacefulSeason = W->CaptureSeasonSnapshot();
    Check(FirstCession && SecondCession && PeacefulSeason.PlayerGroves == 2 &&
        PeacefulSeason.ResidentsAlive == 3 && W->SpentGiftCredit.IsEmpty(),
        TEXT("Two live cessions create player territory using deeds, without forced killing"));

    Reset(); Near(Keeper);
    Check(!W->CanNegotiateFollow(Keeper, P), TEXT("The largest keeper cannot be summoned by an unproven opening request"));
    P->SetActorLocation(W->MotherPosition + FVector(400.f, 0.f, 100.f), false);
    Check(W->Donate(P, 20.f) && W->CanNegotiateFollow(Keeper, P),
        TEXT("Actual shared help can make peaceful following negotiable"));
    Reset(); Near(Wild);
    Check(!W->CanNegotiateFollow(Wild, P), TEXT("A weak unproven player cannot freely recruit a wilderness fighter"));
    P->SetSize(1.f);
    Check(W->CanNegotiateFollow(Wild, P), TEXT("A clear real size advantage can support wilderness following"));

    Reset();
    P->SetActorLocation(W->MotherPosition + FVector(400.f, 0.f, 100.f), false);
    const bool FollowHelp = W->Donate(P, 20.f);
    Near(Miya);
    const bool FirstLink = W->Dialogue->ApplyDecision(Miya, TEXT("follow"), 2, 0.f);
    Near(Trader);
    Check(FollowHelp && FirstLink && !W->Dialogue->ApplyDecision(Trader, TEXT("follow"), 1, 0.f) &&
        Miya->FollowId == 2 && Trader->FollowId == -1,
        TEXT("Actual follow agreements reject a two-person cycle without changing the rejected leader"));

    const bool SecondLink = W->Dialogue->ApplyDecision(Trader, TEXT("follow"), 3, 0.f);
    Near(Keeper);
    const bool RejectLongCycle = !W->Dialogue->ApplyDecision(Keeper, TEXT("follow"), 1, 0.f);
    Trader->FollowId = 1; // Deliberately inject an existing dirty cycle for this boundary fixture.
    const bool RejectDirtyCycle = !W->CanNegotiateFollow(Keeper, Miya);
    Check(SecondLink && RejectLongCycle && RejectDirtyCycle && Keeper->FollowId == -1,
        TEXT("Following rejects both a multi-layer cycle and an already-corrupt target chain"));

    Miya->FollowId = -1; Trader->FollowId = 3; Keeper->FollowId = -1;
    Near(Miya);
    const bool Acyclic = W->Dialogue->ApplyDecision(Miya, TEXT("follow"), 2, 0.f);
    Keeper->bDead = true;
    const bool DeadEnd = W->CanNegotiateFollow(Miya, Trader);
    Keeper->bDead = false;
    Trader->FollowId = 9999;
    const bool MissingEnd = W->CanNegotiateFollow(Miya, Trader);
    Check(Acyclic && DeadEnd && MissingEnd && Miya->FollowId == 2,
        TEXT("A normal acyclic chain is allowed and dead or missing downstream leaders terminate the chain"));

    auto* Dialogue = W->Dialogue.Get();
    Dialogue->SelectNPC(Miya);
    Dialogue->AppendNPCLine(1, TEXT("Visible conversation history is independent of model memory."));
    const TArray<FString> VisibleBefore = Dialogue->Lines;
    const TArray<FString> TranscriptBefore = Dialogue->Transcripts.FindRef(1);
    const float ActualDonationBeforeMemory = W->DonationLedger.FindRef(0);
    const float ActualTendingBeforeMemory = W->TendLedger.FindRef(0);
    Dialogue->Memories.Remove(1);
    for (int32 Round = 0; Round < 24; ++Round)
        Dialogue->RememberExchange(1, FString::Printf(TEXT("Short question %d"), Round),
            FString::Printf(TEXT("Short reply %d [action=none; verified]"), Round));
    Check(Dialogue->Memories.FindRef(1).Num() == 16,
        TEXT("Many short exchanges retain at most sixteen complete memory lines"));

    bool BoundedEveryRound = true;
    FString LatestPlayer, LatestNPC;
    for (int32 Round = 0; Round < 24; ++Round)
    {
        LatestPlayer = FString::ChrN(480, TCHAR(0x8bdd)) + FString::Printf(TEXT(" [round=%d]"), Round);
        LatestNPC = Miya->DisplayName + TEXT("：") + FString::ChrN(180, TCHAR(0x7b54)) +
            FString::Printf(TEXT(" [round=%d] [动作=none；仅商议，未建立新计划]"), Round);
        Dialogue->RememberExchange(1, LatestPlayer, LatestNPC);
        const auto& CurrentMemory = Dialogue->Memories.FindChecked(1);
        int32 CurrentCharacters = 0;
        for (const FString& Line : CurrentMemory) CurrentCharacters += Line.Len() + 1;
        BoundedEveryRound &= CurrentMemory.Num() <= 16 && CurrentMemory.Num() % 2 == 0 &&
            CurrentCharacters <= 2000;
    }
    const auto& FinalMemory = Dialogue->Memories.FindChecked(1);
    int32 MemoryCharacters = 0;
    for (const FString& Line : FinalMemory) MemoryCharacters += Line.Len() + 1;
    Check(BoundedEveryRound, TEXT("Every long exchange remains within 2000 characters and sixteen paired lines"));
    Check(FinalMemory.Num() >= 2 && FinalMemory[FinalMemory.Num() - 2] == TEXT("玩家说：") + LatestPlayer &&
        FinalMemory.Last() == LatestNPC &&
        FinalMemory.Last().EndsWith(TEXT("[动作=none；仅商议，未建立新计划]")),
        TEXT("The latest complete player/NPC exchange and authoritative action result survive truncation"));
    const FString BoundedState = Dialogue->BuildState(Miya);
    Check(BoundedState.Contains(W->NegotiationSummary(Miya)) &&
        BoundedState.Contains(TEXT("当前真正合法的动作列表")) &&
        W->DonationLedger.FindRef(0) == ActualDonationBeforeMemory &&
        W->TendLedger.FindRef(0) == ActualTendingBeforeMemory &&
        Dialogue->Lines == VisibleBefore && Dialogue->Transcripts.FindRef(1) == TranscriptBefore,
        TEXT("Memory trimming preserves real world conditions, payment ledgers and visible conversation history"));

    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Validation")), true);
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("synthetic-authoritative-social-fixtures"));
    Result->SetBoolField(TEXT("normal_playthrough"), false);
    Result->SetBoolField(TEXT("runtime_model_called"), false);
    Result->SetNumberField(TEXT("failures"), SmokeFailures);
    Result->SetNumberField(TEXT("conversation_memory_chars"), MemoryCharacters);
    Result->SetNumberField(TEXT("conversation_memory_lines"), FinalMemory.Num());
    Result->SetNumberField(TEXT("world_state_chars_after_long_conversation"), BoundedState.Len());
    Result->SetNumberField(TEXT("long_conversation_fixture_rounds"), 24);
    TArray<TSharedPtr<FJsonValue>> Checks;
    for (const FString& Line : SmokeChecks) Checks.Add(MakeShared<FJsonValueString>(Line));
    Result->SetArrayField(TEXT("checks"), Checks);
    FString Json; const auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Result, Writer);
    const bool Saved = FFileHelper::SaveStringToFile(Json,
        *(FPaths::ProjectSavedDir() / TEXT("Validation/social-probe.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp, Display, TEXT("VERDANT_SOCIAL_COMPLETE checks=%d failures=%d saved=%d"),
        SmokeChecks.Num(), SmokeFailures, Saved ? 1 : 0);
    FPlatformMisc::RequestExitWithStatus(false, SmokeFailures || !Saved ? 1 : 0);
}
