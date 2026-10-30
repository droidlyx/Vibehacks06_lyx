// Observe the unassisted world for 90 simulated seconds. The open variant has
// exactly one intervention: the current highland owner opens access at t ~= 2s.
// No actors are frozen, moved, healed or supplied with resources by this probe.
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Engine/World.h"

namespace
{
    TSharedPtr<FJsonObject> PolicyPosition(const FVector& Position)
    {
        auto Value = MakeShared<FJsonObject>();
        Value->SetNumberField(TEXT("x"), Position.X);
        Value->SetNumberField(TEXT("y"), Position.Y);
        Value->SetNumberField(TEXT("z"), Position.Z);
        return Value;
    }

    TSharedPtr<FJsonObject> PolicyHarvestLedger(const TMap<FString, float>& Ledger)
    {
        auto Value = MakeShared<FJsonObject>();
        for (const auto& Pair : Ledger)
            Value->SetNumberField(Pair.Key, Pair.Value);
        return Value;
    }
}

void AVerdantWorld::TickPolicyProbe(float Dt)
{
    if (bPolicyProbeFinished ||
        !FParse::Param(FCommandLine::Get(), TEXT("VerdantPolicyProbe")))
        return;

    const bool bOpenVariant = FParse::Param(FCommandLine::Get(), TEXT("VerdantPolicyOpen"));
    const float PreviousElapsed = PolicyProbeElapsed;
    PolicyProbeElapsed += FMath::Max(0.f, Dt);

    if (bOpenVariant && PreviousElapsed < 2.f && PolicyProbeElapsed >= 2.f)
    {
        PolicyProbeInterventionAt = PolicyProbeElapsed;
        bPolicyProbeInterventionApplied = SetAccess(2, FindFolk(3), true);
        UE_LOG(LogTemp, Display, TEXT("VERDANT_POLICY_INTERVENTION elapsed=%.3f applied=%d"),
            PolicyProbeElapsed, bPolicyProbeInterventionApplied ? 1 : 0);
    }

    const bool bComplete = PolicyProbeElapsed >= 90.f;
    if (PolicyProbeElapsed >= PolicyProbeNextSample || bComplete)
    {
        auto Sample = MakeShared<FJsonObject>();
        Sample->SetNumberField(TEXT("elapsed_seconds"), PolicyProbeElapsed);
        Sample->SetNumberField(TEXT("world_time_seconds"), GetWorld()->GetTimeSeconds());
        Sample->SetNumberField(TEXT("tree_nutrition"), TreeNutrition);
        Sample->SetNumberField(TEXT("tree_goal"), TreeGoal);
        Sample->SetBoolField(TEXT("tree_restored"), bRestored);
        Sample->SetBoolField(TEXT("opening_intervention_applied"), bPolicyProbeInterventionApplied);
        Sample->SetObjectField(TEXT("harvest_ledger_net_food"), PolicyHarvestLedger(HarvestLedger));

        TArray<TSharedPtr<FJsonValue>> Residents;
        for (AVerdantCharacter* Who : Folk)
        {
            if (!IsValid(Who))
                continue;
            auto Resident = MakeShared<FJsonObject>();
            Resident->SetNumberField(TEXT("agent_id"), Who->AgentId);
            Resident->SetStringField(TEXT("name"), Who->DisplayName);
            Resident->SetBoolField(TEXT("wild"), Who->bWild);
            Resident->SetBoolField(TEXT("dead"), Who->bDead);
            Resident->SetObjectField(TEXT("position_cm"), PolicyPosition(Who->GetActorLocation()));
            Resident->SetNumberField(TEXT("speed_cm_per_second"), Who->GetVelocity().Size2D());
            Resident->SetNumberField(TEXT("home_grove"), Who->HomeGrove);
            Resident->SetNumberField(TEXT("food"), Who->Food);
            Resident->SetNumberField(TEXT("nutrition_reserve"), Who->Nutrition);
            Resident->SetNumberField(TEXT("body_size"), Who->BodySize);
            Resident->SetNumberField(TEXT("height_m"), Who->BodySize * 1.8f);
            Resident->SetNumberField(TEXT("health"), Who->Health);
            Resident->SetNumberField(TEXT("max_health"), Who->MaxHealth());
            Resident->SetNumberField(TEXT("donation_pledge"), Who->DonationPledge);
            Resident->SetNumberField(TEXT("follow_id"), Who->FollowId);
            Resident->SetNumberField(TEXT("hostile_id"), Who->HostileId);
            Resident->SetStringField(TEXT("action"), Who->ActionLabel);
            Resident->SetObjectField(TEXT("destination_cm"), PolicyPosition(Who->Destination));
            Residents.Add(MakeShared<FJsonValueObject>(Resident));
        }
        Sample->SetArrayField(TEXT("actors"), Residents);

        TArray<TSharedPtr<FJsonValue>> Places;
        for (int32 I = 0; I < Groves.Num(); ++I)
        {
            const FVerdantGrove& Grove = Groves[I];
            auto Place = MakeShared<FJsonObject>();
            Place->SetNumberField(TEXT("grove_index"), I);
            Place->SetStringField(TEXT("name"), Grove.Name);
            Place->SetObjectField(TEXT("position_cm"), PolicyPosition(Grove.Position));
            Place->SetNumberField(TEXT("food"), Grove.Food);
            Place->SetNumberField(TEXT("fertility"), Grove.Fertility);
            Place->SetNumberField(TEXT("controller_id"), Grove.ControllerId);
            Place->SetBoolField(TEXT("open"), Grove.bOpen);
            Place->SetNumberField(TEXT("share"), Grove.Share);
            TArray<TSharedPtr<FJsonValue>> Permitted;
            for (int32 Id : Grove.Permitted)
                Permitted.Add(MakeShared<FJsonValueNumber>(Id));
            Place->SetArrayField(TEXT("permitted_actor_ids"), Permitted);
            TArray<TSharedPtr<FJsonValue>> Present;
            for (AVerdantCharacter* Who : Folk)
                if (IsValid(Who) && !Who->bDead &&
                    FVector::Dist2D(Who->GetActorLocation(), Grove.Position) <= 700.f)
                    Present.Add(MakeShared<FJsonValueNumber>(Who->AgentId));
            Place->SetArrayField(TEXT("living_actor_ids_within_700cm"), Present);
            Places.Add(MakeShared<FJsonValueObject>(Place));
        }
        Sample->SetArrayField(TEXT("groves"), Places);

        FString Serialized;
        const auto Writer = TJsonWriterFactory<>::Create(&Serialized);
        if (FJsonSerializer::Serialize(Sample, Writer))
            PolicyProbeSamples.Add(Serialized);
        // Record actual observations, not fabricated catch-up samples after a slow frame.
        PolicyProbeNextSample = FMath::FloorToFloat(PolicyProbeElapsed) + 1.f;
    }

    if (!bComplete)
        return;
    bPolicyProbeFinished = true;

    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schema_version"), 1);
    Report->SetStringField(TEXT("probe"), TEXT("natural_npc_policy_comparison"));
    Report->SetStringField(TEXT("variant"), bOpenVariant ? TEXT("open") : TEXT("closed"));
    Report->SetStringField(TEXT("captured_at_utc"), FDateTime::UtcNow().ToIso8601());
    Report->SetNumberField(TEXT("duration_seconds"), PolicyProbeElapsed);
    Report->SetNumberField(TEXT("sample_interval_seconds"), 1.f);
    Report->SetBoolField(TEXT("actors_frozen"), false);
    Report->SetBoolField(TEXT("actors_teleported"), false);
    Report->SetBoolField(TEXT("resources_injected"), false);
    Report->SetStringField(TEXT("scope"),
        TEXT("Natural NPC simulation. The player remains idle; this is not a human playthrough."));

    auto Intervention = MakeShared<FJsonObject>();
    Intervention->SetStringField(TEXT("action"), bOpenVariant ? TEXT("SetAccess(2,FindFolk(3),true)") : TEXT("none"));
    Intervention->SetNumberField(TEXT("scheduled_seconds"), bOpenVariant ? 2.f : -1.f);
    Intervention->SetNumberField(TEXT("actual_seconds"), PolicyProbeInterventionAt);
    Intervention->SetBoolField(TEXT("applied"), bPolicyProbeInterventionApplied);
    Report->SetObjectField(TEXT("external_intervention"), Intervention);

    TArray<TSharedPtr<FJsonValue>> Samples;
    int32 EncodingErrors = 0;
    for (const FString& Serialized : PolicyProbeSamples)
    {
        TSharedPtr<FJsonObject> Sample;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Serialized), Sample) && Sample.IsValid())
            Samples.Add(MakeShared<FJsonValueObject>(Sample));
        else
            ++EncodingErrors;
    }
    Report->SetArrayField(TEXT("samples"), Samples);
    Report->SetNumberField(TEXT("sample_count"), Samples.Num());
    Report->SetNumberField(TEXT("sample_encoding_errors"), EncodingErrors);
    Report->SetObjectField(TEXT("final_harvest_ledger_net_food"), PolicyHarvestLedger(HarvestLedger));
    Report->SetStringField(TEXT("final_world_summary"), WorldSummary());
    TArray<TSharedPtr<FJsonValue>> RecordedEvents;
    for (const FString& Event : Events)
        RecordedEvents.Add(MakeShared<FJsonValueString>(Event));
    Report->SetArrayField(TEXT("recent_world_events"), RecordedEvents);

    auto Observed = MakeShared<FJsonObject>();
    int32 LivingMainResidents = 0;
    float NonOwnerHighlandHarvest = 0.f;
    for (AVerdantCharacter* Who : Folk)
    {
        if (!IsValid(Who))
            continue;
        if (Who->AgentId > 0 && !Who->bWild && !Who->bDead)
            ++LivingMainResidents;
        if (Who->AgentId > 0 && Who->AgentId != 3)
            NonOwnerHighlandHarvest += HarvestLedger.FindRef(FString::Printf(TEXT("%d:2"), Who->AgentId));
    }
    Observed->SetNumberField(TEXT("living_main_residents"), LivingMainResidents);
    Observed->SetNumberField(TEXT("non_owner_npc_highland_net_harvest"), NonOwnerHighlandHarvest);
    Observed->SetNumberField(TEXT("tree_nutrition"), TreeNutrition);
    Report->SetObjectField(TEXT("observed_metrics"), Observed);

    FString OutputJson;
    const auto Writer = TJsonWriterFactory<>::Create(&OutputJson);
    const bool bEncoded = FJsonSerializer::Serialize(Report, Writer);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = Directory / (bOpenVariant ? TEXT("policy-open.json") : TEXT("policy-closed.json"));
    const bool bSaved = bEncoded && FFileHelper::SaveStringToFile(
        OutputJson, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    const bool bValidCapture = bSaved && EncodingErrors == 0 && !Samples.IsEmpty() &&
        (!bOpenVariant || bPolicyProbeInterventionApplied);
    UE_LOG(LogTemp, Display, TEXT("VERDANT_POLICY_CAPTURE variant=%s samples=%d saved=%d capture_valid=%d"),
        bOpenVariant ? TEXT("open") : TEXT("closed"), Samples.Num(), bSaved ? 1 : 0, bValidCapture ? 1 : 0);
    // Capture success is not an assertion that the economy is fun or policies differ.
    FPlatformMisc::RequestExitWithStatus(false, bValidCapture ? 0 : 1);
}
