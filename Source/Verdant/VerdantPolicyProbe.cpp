// Observe the unassisted world (default 90s; configurable duration and sampling). The open variant has
// exactly one intervention: the current highland owner opens access at t ~= 2s.
// No actors are frozen, moved, healed or supplied with resources by this probe.
#include "VerdantWorld.h"
#include "VerdantCharacter.h"
#include "VerdantDialogue.h"
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
    // Probe-only process-local state is reset when another world owns the probe.
    struct FPolicyMotionWatch
    {
        FVector Origin = FVector::ZeroVector;
        float Since = -1.f;
        bool bReported = false;
    };
    struct FPolicyObservation
    {
        uint32 WorldId = 0;
        float Duration = 90.f;
        float Interval = 1.f;
        int64 InvariantFrames = 0;
        TSet<FString> FailureKeys;
        TArray<TSharedPtr<FJsonValue>> Failures;
        TArray<TSharedPtr<FJsonValue>> MovementSuspects;
        TMap<int32, FPolicyMotionWatch> Motion;
    };
    FPolicyObservation PolicyObservation;

    // Keep invalid observations explicit and JSON-readable; never replace NaN
    // with a plausible zero. An invariant failure still produces a usable report.
    void PolicyNumber(const TSharedPtr<FJsonObject>& Object, const FString& Key, double Value)
    {
        if (FMath::IsFinite(Value)) Object->SetNumberField(Key, Value);
        else Object->SetStringField(Key, FMath::IsNaN(Value) ? TEXT("NaN") : Value > 0 ? TEXT("Infinity") : TEXT("-Infinity"));
    }

    TSharedPtr<FJsonObject> PolicyPosition(const FVector& Position)
    {
        auto Value = MakeShared<FJsonObject>();
        PolicyNumber(Value, TEXT("x"), Position.X);
        PolicyNumber(Value, TEXT("y"), Position.Y);
        PolicyNumber(Value, TEXT("z"), Position.Z);
        return Value;
    }

    TSharedPtr<FJsonObject> PolicyHarvestLedger(const TMap<FString, float>& Ledger)
    {
        auto Value = MakeShared<FJsonObject>();
        for (const auto& Pair : Ledger)
            PolicyNumber(Value, Pair.Key, Pair.Value);
        return Value;
    }
}

void AVerdantWorld::TickPolicyProbe(float Dt)
{
    if (bPolicyProbeFinished ||
        !FParse::Param(FCommandLine::Get(), TEXT("VerdantPolicyProbe")))
        return;

    const bool bOpenVariant = FParse::Param(FCommandLine::Get(), TEXT("VerdantPolicyOpen"));
    auto& Observation = PolicyObservation;
    const bool bNewObservation = Observation.WorldId != GetUniqueID();
    if (bNewObservation)
    {
        Observation = FPolicyObservation();
        Observation.WorldId = GetUniqueID();
    }
    auto Failure = [&](const FString& Key, const FString& Detail, double Actual)
    {
        if (Observation.FailureKeys.Contains(Key)) return;
        Observation.FailureKeys.Add(Key);
        auto Problem = MakeShared<FJsonObject>();
        Problem->SetStringField(TEXT("key"), Key); Problem->SetStringField(TEXT("detail"), Detail);
        PolicyNumber(Problem, TEXT("elapsed_seconds"), PolicyProbeElapsed);
        PolicyNumber(Problem, TEXT("actual"), Actual);
        Observation.Failures.Add(MakeShared<FJsonValueObject>(Problem));
        UE_LOG(LogTemp, Error, TEXT("VERDANT_POLICY_INVARIANT %s: %s actual=%g elapsed=%.3f"),
            *Key, *Detail, Actual, PolicyProbeElapsed);
    };
    if (bNewObservation)
    {
        float Requested = 0.f;
        if (FParse::Value(FCommandLine::Get(), TEXT("VerdantPolicyDuration="), Requested))
        {
            if (FMath::IsFinite(Requested) && Requested > 0.f) Observation.Duration = Requested;
            else Failure(TEXT("config.duration"), TEXT("Duration must be a positive finite number of simulated seconds."), Requested);
        }
        if (FParse::Value(FCommandLine::Get(), TEXT("VerdantPolicySampleInterval="), Requested))
        {
            if (FMath::IsFinite(Requested) && Requested > 0.f) Observation.Interval = Requested;
            else Failure(TEXT("config.sample_interval"), TEXT("Sample interval must be a positive finite number of seconds."), Requested);
        }
        UE_LOG(LogTemp, Display, TEXT("VERDANT_POLICY_CONFIG variant=%s duration=%.3f interval=%.3f"),
            bOpenVariant ? TEXT("open") : TEXT("closed"), Observation.Duration, Observation.Interval);
    }
    const float PreviousElapsed = PolicyProbeElapsed;
    if (!FMath::IsFinite(Dt)) Failure(TEXT("world.delta_time"), TEXT("Simulation delta must be finite."), Dt);
    else PolicyProbeElapsed += FMath::Max(0.f, Dt);

    // Validate every real tick, independently of the requested sample interval.
    // Recent deaths may still own a grove until the next economy tick; resolution
    // to a registered actor is sufficient here, not that actor being alive.
    ++Observation.InvariantFrames;
    auto Finite = [&](float Value, const FString& Key)
    {
        if (!FMath::IsFinite(Value)) { Failure(Key, TEXT("Value must be finite."), Value); return false; }
        return true;
    };
    auto Nonnegative = [&](float Value, const FString& Key)
    {
        if (Finite(Value, Key) && Value < -.001f) Failure(Key, TEXT("Resource value must not be negative (0.001 float tolerance)."), Value);
    };
    Nonnegative(TreeNutrition, TEXT("world.tree_nutrition"));
    if (Finite(TreeGoal, TEXT("world.tree_goal")) && TreeGoal <= 0.f)
        Failure(TEXT("world.tree_goal"), TEXT("Tree goal must be positive."), TreeGoal);
    if (FMath::IsFinite(TreeNutrition) && FMath::IsFinite(TreeGoal) && TreeNutrition > TreeGoal + .001f)
        Failure(TEXT("world.tree_overflow"), TEXT("Tree nutrition must not exceed its goal."), TreeNutrition);
    for (AVerdantCharacter* Who : Folk)
    {
        if (!IsValid(Who)) continue;
        const FString Prefix = FString::Printf(TEXT("actor.%d."), Who->AgentId);
        Nonnegative(Who->Food, Prefix + TEXT("food"));
        Nonnegative(Who->Nutrition, Prefix + TEXT("nutrition"));
        Nonnegative(Who->DonationPledge, Prefix + TEXT("donation_pledge"));
        Finite(Who->Health, Prefix + TEXT("health"));
        if (Finite(Who->BodySize, Prefix + TEXT("body_size")) && (Who->BodySize < .35f - .0001f || Who->BodySize > 2.5f + .0001f))
            Failure(Prefix + TEXT("body_size"), TEXT("Body size must stay in [0.35, 2.5]."), Who->BodySize);
        const FVector Position = Who->GetActorLocation();
        if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) || !FMath::IsFinite(Position.Z))
            Failure(Prefix + TEXT("position"), TEXT("Actor position must be finite; exact components appear in its sample."), Position.Size());
    }
    for (int32 I = 0; I < Groves.Num(); ++I)
    {
        const auto& Grove = Groves[I];
        const FString Prefix = FString::Printf(TEXT("grove.%d."), I);
        Nonnegative(Grove.Food, Prefix + TEXT("food"));
        if (Finite(Grove.Fertility, Prefix + TEXT("fertility")) && (Grove.Fertility < -.001f || Grove.Fertility > 1.f + .001f))
            Failure(Prefix + TEXT("fertility"), TEXT("Fertility must stay in [0, 1]."), Grove.Fertility);
        if (Grove.ControllerId < -1 || (Grove.ControllerId >= 0 && !IsValid(FindFolk(Grove.ControllerId))))
            Failure(Prefix + TEXT("controller_id"), TEXT("Controller must be -1 or resolve to a registered actor; a recently dead actor is allowed."), Grove.ControllerId);
    }

    if (bOpenVariant && PreviousElapsed < 2.f && PolicyProbeElapsed >= 2.f)
    {
        PolicyProbeInterventionAt = PolicyProbeElapsed;
        bPolicyProbeInterventionApplied = SetAccess(2, FindFolk(3), true);
        UE_LOG(LogTemp, Display, TEXT("VERDANT_POLICY_INTERVENTION elapsed=%.3f applied=%d"),
            PolicyProbeElapsed, bPolicyProbeInterventionApplied ? 1 : 0);
    }

    bool bComplete = PolicyProbeElapsed >= Observation.Duration || !Observation.Failures.IsEmpty();
    if (PolicyProbeElapsed >= PolicyProbeNextSample || bComplete)
    {
        auto Sample = MakeShared<FJsonObject>();
        PolicyNumber(Sample, TEXT("elapsed_seconds"), PolicyProbeElapsed);
        PolicyNumber(Sample, TEXT("world_time_seconds"), GetWorld()->GetTimeSeconds());
        PolicyNumber(Sample, TEXT("tree_nutrition"), TreeNutrition);
        PolicyNumber(Sample, TEXT("tree_goal"), TreeGoal);
        Sample->SetBoolField(TEXT("tree_restored"), bRestored);
        Sample->SetBoolField(TEXT("opening_intervention_applied"), bPolicyProbeInterventionApplied);
        Sample->SetObjectField(TEXT("harvest_ledger_net_food"), PolicyHarvestLedger(HarvestLedger));

        TArray<TSharedPtr<FJsonValue>> Residents;
        TArray<TSharedPtr<FJsonValue>> StationarySuspects;
        for (AVerdantCharacter* Who : Folk)
        {
            if (!IsValid(Who))
                continue;
            auto Resident = MakeShared<FJsonObject>();
            PolicyNumber(Resident, TEXT("agent_id"), Who->AgentId);
            Resident->SetStringField(TEXT("name"), Who->DisplayName);
            Resident->SetBoolField(TEXT("wild"), Who->bWild);
            Resident->SetBoolField(TEXT("dead"), Who->bDead);
            Resident->SetObjectField(TEXT("position_cm"), PolicyPosition(Who->GetActorLocation()));
            PolicyNumber(Resident, TEXT("speed_cm_per_second"), Who->GetVelocity().Size2D());
            PolicyNumber(Resident, TEXT("home_grove"), Who->HomeGrove);
            PolicyNumber(Resident, TEXT("food"), Who->Food);
            PolicyNumber(Resident, TEXT("nutrition_reserve"), Who->Nutrition);
            PolicyNumber(Resident, TEXT("body_size"), Who->BodySize);
            PolicyNumber(Resident, TEXT("height_m"), Who->BodySize * 1.8f);
            PolicyNumber(Resident, TEXT("health"), Who->Health);
            PolicyNumber(Resident, TEXT("max_health"), Who->MaxHealth());
            PolicyNumber(Resident, TEXT("donation_pledge"), Who->DonationPledge);
            PolicyNumber(Resident, TEXT("follow_id"), Who->FollowId);
            PolicyNumber(Resident, TEXT("hostile_id"), Who->HostileId);
            Resident->SetStringField(TEXT("action"), Who->ActionLabel);
            Resident->SetObjectField(TEXT("destination_cm"), PolicyPosition(Who->Destination));
            PolicyNumber(Resident, TEXT("next_tend_at_world_seconds"), Who->NextTendAt);
            PolicyNumber(Resident, TEXT("move_recovery_count"), Who->MoveRecoveryCount);
            PolicyNumber(Resident, TEXT("avoid_threat_id"), Who->AvoidThreatId);
            PolicyNumber(Resident, TEXT("avoid_threat_until_world_seconds"), Who->AvoidThreatUntil);
            const bool bReplying = Dialogue && Dialogue->bBusy && Dialogue->RequestNPC.Get() == Who;
            const bool bBusyOrWaiting = Who->HostileId >= 0 || Who->AttackTimer > 0.f || Who->HurtTimer > 0.f
                || Who->DodgeTimer > 0.f || bReplying || Who->ActionLabel.Contains(TEXT("守着"))
                || Who->ActionLabel.Contains(TEXT("观察")) || Who->ActionLabel.Contains(TEXT("思考"));
            const float GoalDistance = FVector::Dist2D(Who->GetActorLocation(), Who->Destination);
            auto& Watch = Observation.Motion.FindOrAdd(Who->AgentId);
            const bool bTravelIntent = Who->AgentId > 0 && !Who->bDead && !bBusyOrWaiting
                && FMath::IsFinite(GoalDistance) && GoalDistance > 650.f;
            if (!bTravelIntent) Watch = FPolicyMotionWatch();
            else if (Watch.Since < 0.f || FVector::Dist2D(Who->GetActorLocation(), Watch.Origin) > 60.f)
            {
                Watch.Origin = Who->GetActorLocation(); Watch.Since = PolicyProbeElapsed; Watch.bReported = false;
            }
            else if (PolicyProbeElapsed - Watch.Since >= 30.f)
            {
                StationarySuspects.Add(MakeShared<FJsonValueNumber>(Who->AgentId));
                if (!Watch.bReported)
                {
                    auto Suspect = MakeShared<FJsonObject>();
                    PolicyNumber(Suspect, TEXT("agent_id"), Who->AgentId);
                    PolicyNumber(Suspect, TEXT("elapsed_seconds"), PolicyProbeElapsed);
                    PolicyNumber(Suspect, TEXT("stationary_observation_seconds"), PolicyProbeElapsed - Watch.Since);
                    PolicyNumber(Suspect, TEXT("goal_distance_cm"), GoalDistance);
                    Suspect->SetStringField(TEXT("action"), Who->ActionLabel);
                    Suspect->SetObjectField(TEXT("position_cm"), PolicyPosition(Who->GetActorLocation()));
                    Suspect->SetObjectField(TEXT("destination_cm"), PolicyPosition(Who->Destination));
                    Suspect->SetStringField(TEXT("interpretation"), TEXT("Review suspect only: far travel intent and sampled positions within 60cm for >=30s; not proof of a bug."));
                    Observation.MovementSuspects.Add(MakeShared<FJsonValueObject>(Suspect));
                    Watch.bReported = true;
                }
            }
            Resident->SetBoolField(TEXT("awaiting_model_reply"), bReplying);
            Resident->SetBoolField(TEXT("travel_stationary_suspect"), bTravelIntent && Watch.bReported);
            Residents.Add(MakeShared<FJsonValueObject>(Resident));
        }
        Sample->SetArrayField(TEXT("actors"), Residents);
        Sample->SetArrayField(TEXT("stationary_travel_suspect_actor_ids"), StationarySuspects);

        TArray<TSharedPtr<FJsonValue>> Places;
        for (int32 I = 0; I < Groves.Num(); ++I)
        {
            const FVerdantGrove& Grove = Groves[I];
            auto Place = MakeShared<FJsonObject>();
            PolicyNumber(Place, TEXT("grove_index"), I);
            Place->SetStringField(TEXT("name"), Grove.Name);
            Place->SetObjectField(TEXT("position_cm"), PolicyPosition(Grove.Position));
            PolicyNumber(Place, TEXT("food"), Grove.Food);
            PolicyNumber(Place, TEXT("fertility"), Grove.Fertility);
            PolicyNumber(Place, TEXT("controller_id"), Grove.ControllerId);
            Place->SetBoolField(TEXT("open"), Grove.bOpen);
            PolicyNumber(Place, TEXT("share"), Grove.Share);
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
        if (FJsonSerializer::Serialize(Sample, Writer)) PolicyProbeSamples.Add(Serialized);
        else
        {
            Failure(TEXT("capture.sample_encoding"), TEXT("An actual observed sample could not be encoded."), PolicyProbeElapsed);
            bComplete = true;
        }
        // Record actual observations, not fabricated catch-up samples after a slow frame.
        PolicyProbeNextSample = (FMath::FloorToFloat(PolicyProbeElapsed / Observation.Interval) + 1.f) * Observation.Interval;
    }

    if (!bComplete)
        return;
    bPolicyProbeFinished = true;

    auto Report = MakeShared<FJsonObject>();
    PolicyNumber(Report, TEXT("schema_version"), 2);
    Report->SetStringField(TEXT("probe"), TEXT("natural_npc_policy_comparison"));
    Report->SetStringField(TEXT("variant"), bOpenVariant ? TEXT("open") : TEXT("closed"));
    Report->SetStringField(TEXT("captured_at_utc"), FDateTime::UtcNow().ToIso8601());
    PolicyNumber(Report, TEXT("duration_seconds"), PolicyProbeElapsed);
    PolicyNumber(Report, TEXT("requested_duration_seconds"), Observation.Duration);
    PolicyNumber(Report, TEXT("sample_interval_seconds"), Observation.Interval);
    PolicyNumber(Report, TEXT("invariant_frames_checked"), Observation.InvariantFrames);
    Report->SetBoolField(TEXT("actors_frozen"), false);
    Report->SetBoolField(TEXT("actors_teleported"), false);
    Report->SetBoolField(TEXT("resources_injected"), false);
    Report->SetStringField(TEXT("scope"),
        TEXT("Natural NPC simulation. The player remains idle; this is not a human playthrough."));

    auto Intervention = MakeShared<FJsonObject>();
    Intervention->SetStringField(TEXT("action"), bOpenVariant ? TEXT("SetAccess(2,FindFolk(3),true)") : TEXT("none"));
    PolicyNumber(Intervention, TEXT("scheduled_seconds"), bOpenVariant ? 2.f : -1.f);
    PolicyNumber(Intervention, TEXT("actual_seconds"), PolicyProbeInterventionAt);
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
    if (EncodingErrors > 0) Failure(TEXT("capture.sample_decoding"), TEXT("Stored observations could not be decoded."), EncodingErrors);
    if (Samples.IsEmpty()) Failure(TEXT("capture.no_samples"), TEXT("No actual observations were recorded."), 0.f);
    if (bOpenVariant && !bPolicyProbeInterventionApplied)
        Failure(TEXT("capture.open_intervention"), TEXT("The requested single opening intervention was not applied."), PolicyProbeInterventionAt);
    PolicyNumber(Report, TEXT("failures"), Observation.Failures.Num());
    Report->SetArrayField(TEXT("failure_details"), Observation.Failures);
    PolicyNumber(Report, TEXT("movement_suspect_count"), Observation.MovementSuspects.Num());
    Report->SetArrayField(TEXT("movement_suspects"), Observation.MovementSuspects);
    Report->SetStringField(TEXT("movement_suspect_scope"), TEXT("Sampling heuristic only; resting, replying, combat and near-destination actors are excluded. Suspects do not fail numeric validation."));
    Report->SetArrayField(TEXT("samples"), Samples);
    PolicyNumber(Report, TEXT("sample_count"), Samples.Num());
    PolicyNumber(Report, TEXT("sample_encoding_errors"), EncodingErrors);
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
    PolicyNumber(Observed, TEXT("living_main_residents"), LivingMainResidents);
    PolicyNumber(Observed, TEXT("non_owner_npc_highland_net_harvest"), NonOwnerHighlandHarvest);
    PolicyNumber(Observed, TEXT("tree_nutrition"), TreeNutrition);
    Report->SetObjectField(TEXT("observed_metrics"), Observed);

    FString OutputJson;
    const auto Writer = TJsonWriterFactory<>::Create(&OutputJson);
    const bool bEncoded = FJsonSerializer::Serialize(Report, Writer);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = Directory / (bOpenVariant ? TEXT("policy-open.json") : TEXT("policy-closed.json"));
    const bool bSaved = bEncoded && FFileHelper::SaveStringToFile(
        OutputJson, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    const bool bValidCapture = bSaved && Observation.Failures.IsEmpty();
    UE_LOG(LogTemp, Display, TEXT("VERDANT_POLICY_CAPTURE variant=%s samples=%d saved=%d capture_valid=%d failures=%d suspects=%d"),
        bOpenVariant ? TEXT("open") : TEXT("closed"), Samples.Num(), bSaved ? 1 : 0, bValidCapture ? 1 : 0,
        Observation.Failures.Num(), Observation.MovementSuspects.Num());
    // Capture success is not an assertion that the economy is fun or policies differ.
    FPlatformMisc::RequestExitWithStatus(false, bValidCapture ? 0 : 1);
}
