#include "VerdantAudioComponent.h"
#include "VerdantCharacter.h"
#include "VerdantHUD.h"
#include "VerdantWorld.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "TimerManager.h"

namespace
{
	constexpr float TrackFadeSeconds = 2.f;
	constexpr float DialogueDuck = 0.501187f; // -6 dB relative to Music SoundClass.
	const TCHAR* RuntimeFolder = TEXT("/Game/Art/Audio/Runtime");
	const TCHAR* LicensedFolder = TEXT("/Game/Art/Audio/LocalLicensed");
	const FName ClothKeys[] = {TEXT("Cloth1"),TEXT("Cloth2"),TEXT("Cloth3"),TEXT("Cloth4")};
	const FName DirtKeys[] = {TEXT("Dirt1"),TEXT("Dirt2"),TEXT("Dirt3")};
	const FName StoneKeys[] = {TEXT("Stone1"),TEXT("Stone2"),TEXT("Stone3")};

	USoundConcurrency* MakeConcurrency(UObject* Outer, FName Name, int32 Count, bool bDistance)
	{
		auto* Result = NewObject<USoundConcurrency>(Outer, Name);
		Result->Concurrency.MaxCount = Count;
		Result->Concurrency.bLimitToOwner = false;
		Result->Concurrency.ResolutionRule = bDistance
			                                     ? EMaxConcurrentResolutionRule::StopFarthestThenOldest
			                                     : EMaxConcurrentResolutionRule::StopOldest;
		return Result;
	}
}

UVerdantAudioComponent::UVerdantAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

void UVerdantAudioComponent::LoadSound(FName Key, const TCHAR* Folder, const TCHAR* AssetName)
{
	// Null entries are intentional: a missing asset is attempted and reported once.
	if (Sounds.Contains(Key))return;
	const FString Path = FString::Printf(TEXT("%s/%s.%s"), Folder, AssetName, AssetName);
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path);
	Sounds.Add(Key, Sound);
	if (!Sound)UE_LOG(LogTemp, Warning, TEXT("VERDANT_AUDIO missing optional asset %s"), *Path);
}

void UVerdantAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	bAudioEnabled = GetWorld() && GetWorld()->IsGameWorld() && GetNetMode() != NM_DedicatedServer
		&& !FParse::Param(FCommandLine::Get(),TEXT("nosound"));
	if (!bAudioEnabled)
	{
		SetComponentTickEnabled(false);
		return;
	}

	LoadSound(TEXT("Music"), RuntimeFolder,TEXT("MUS_MosslitPaths"));
	LoadSound(TEXT("Forest"), LicensedFolder,TEXT("AMB_ForestDay"));
	LoadSound(TEXT("Open"), RuntimeFolder,TEXT("UI_JournalOpen"));
	LoadSound(TEXT("Close"), RuntimeFolder,TEXT("UI_JournalClose"));
	LoadSound(TEXT("Leather"), RuntimeFolder,TEXT("SFX_LeatherPick"));
	LoadSound(TEXT("Tick"), LicensedFolder,TEXT("UI_Tick"));
	LoadSound(TEXT("Confirm"), LicensedFolder,TEXT("UI_Select"));
	for (int32 I = 0; I < 4; ++I)
		LoadSound(ClothKeys[I], RuntimeFolder,
		          *FString::Printf(TEXT("SFX_Cloth_%02d"), I + 1));
	for (int32 I = 0; I < 3; ++I)
	{
		LoadSound(DirtKeys[I], LicensedFolder, *FString::Printf(TEXT("SFX_DirtStep_%02d"), I + 1));
		LoadSound(StoneKeys[I], LicensedFolder, *FString::Printf(TEXT("SFX_StoneStep_%02d"), I + 1));
	}

	NearAttenuation = NewObject<USoundAttenuation>(this,TEXT("FoleyDistance"));
	NearAttenuation->Attenuation.bAttenuate = true;
	NearAttenuation->Attenuation.bSpatialize = true;
	NearAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	NearAttenuation->Attenuation.AttenuationShapeExtents = FVector(120.f);
	NearAttenuation->Attenuation.FalloffDistance = 1800.f;
	FootstepConcurrency = MakeConcurrency(this,TEXT("FootstepsBounded"), 6, true);
	FoleyConcurrency = MakeConcurrency(this,TEXT("FoleyBounded"), 4, true);
	UIConcurrency = MakeConcurrency(this,TEXT("UIBounded"), 2, false);

	if (USoundBase* Music = Sounds.FindRef(TEXT("Music")))
	{
		MusicPlayer = UGameplayStatics::CreateSound2D(this, Music, 1.f, 1.f, 0.f, nullptr, false, false);
		if (MusicPlayer)
		{
			MusicPlayer->bIsUISound = false;
			MusicPlayer->OnAudioFinishedNative.AddUObject(this, &UVerdantAudioComponent::MusicFinished);
			MusicPlayer->OnAudioPlaybackPercentNative.AddUObject(this, &UVerdantAudioComponent::MusicProgress);
			MusicDuration = Music->GetDuration();
			StartMusic();
		}
	}
	if (USoundBase* Forest = Sounds.FindRef(TEXT("Forest")))
	{
		AmbiencePlayer = UGameplayStatics::CreateSound2D(this, Forest, 1.f, 1.f, 0.f, nullptr, false, false);
		if (AmbiencePlayer)
		{
			AmbiencePlayer->bIsUISound = false;
			AmbiencePlayer->FadeIn(3.f, 1.f);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("VERDANT_AUDIO initialized music=%d ambience=%d cached_assets=%d"),
	       IsValid(MusicPlayer), IsValid(AmbiencePlayer), Sounds.Num());
}

void UVerdantAudioComponent::StartMusic()
{
	if (bEnding || !bAudioEnabled || !MusicPlayer || !GetWorld() || GetWorld()->bIsTearingDown)return;
	MusicAge = 0.f;
	// Track envelope and dialogue duck are multiplied independently in Tick.
	MusicPlayer->SetVolumeMultiplier(.0001f);
	MusicPlayer->Play();
}

void UVerdantAudioComponent::MusicFinished(UAudioComponent* Component)
{
	if (bEnding || !bAudioEnabled || Component != MusicPlayer || !GetWorld() || GetWorld()->bIsTearingDown)return;
	// A full composition repeats with soft boundaries; it is not a seamless stem.
	GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this]() { StartMusic(); }));
}

void UVerdantAudioComponent::MusicProgress(const UAudioComponent* Component, const USoundWave*, float Percent)
{
	if (Component == MusicPlayer && MusicDuration > 0.f && FMath::IsFinite(Percent))
		MusicAge = FMath::Max(MusicAge, FMath::Clamp(Percent, 0.f, 1.f) * MusicDuration);
}

void UVerdantAudioComponent::SetDialogueActive(bool bActive)
{
	bDialogueActive = bActive;
}

void UVerdantAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bAudioEnabled || bEnding || !GetWorld() || GetWorld()->bIsTearingDown)return;
	const float Dt = FMath::Max(0.f, DeltaTime);
	if (auto* PC = UGameplayStatics::GetPlayerController(this, 0))
		if (auto* HUD = Cast<AVerdantHUD>(PC->GetHUD()))SetDialogueActive(HUD->bConversationOpen);
	const float Target = bDialogueActive ? DialogueDuck : 1.f;
	const float Time = bDialogueActive ? .25f : 1.f;
	MusicDuck = FMath::FInterpConstantTo(MusicDuck, Target, Dt, (1.f - DialogueDuck) / Time);
	if (MusicPlayer && MusicPlayer->IsPlaying())
	{
		MusicAge += Dt;
		float Envelope = FMath::Clamp(MusicAge / TrackFadeSeconds, 0.f, 1.f);
		if (MusicDuration > TrackFadeSeconds * 2.f)
			Envelope = FMath::Min(Envelope, FMath::Clamp((MusicDuration - MusicAge) / TrackFadeSeconds, 0.f, 1.f));
		// SoundClass supplies baseline gain. No duplicate -16 dB here.
		MusicPlayer->SetVolumeMultiplier(FMath::Max(.0001f, Envelope * MusicDuck));
	}
	UpdateFootsteps(Dt);
}

void UVerdantAudioComponent::PlaySpatial(FName Key, const FVector& Position, float Gain, float Pitch,
                                         USoundConcurrency* Concurrency)
{
	if (!bAudioEnabled || bEnding || !GetWorld() || GetWorld()->bIsTearingDown)return;
	if (USoundBase* Sound = Sounds.FindRef(Key))
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Position, FRotator::ZeroRotator, Gain, Pitch, 0.f,
		                                      NearAttenuation, Concurrency, GetOwner());
}

bool UVerdantAudioComponent::PlayEvent(FName Cue, FVector Position, float BodySize)
{
	if (!bAudioEnabled || bEnding || !GetWorld() || GetWorld()->bIsTearingDown)return false;
	if (Cue == TEXT("Swing") || Cue == TEXT("ClothDodge"))
	{
		const int32 Index = FoleyVariant++ % 4;
		const float Pitch = FMath::Clamp(FMath::Pow(FMath::Max(.35f, BodySize), -.12f), .8f, 1.15f);
		PlaySpatial(ClothKeys[Index], Position, Cue == TEXT("Swing") ? .4f : .7f, Pitch, FoleyConcurrency);
		return Cue == TEXT("ClothDodge");
	}
	if (Cue == TEXT("Pickup") || Cue == TEXT("Tend"))
	{
		PlaySpatial(TEXT("Leather"), Position, Cue == TEXT("Pickup") ? .3f : .55f, 1.f, FoleyConcurrency);
		return Cue == TEXT("Tend");
	}
	return false;
}

void UVerdantAudioComponent::PlayUI(FName Cue)
{
	if (!bAudioEnabled || bEnding || !GetWorld() || GetWorld()->bIsTearingDown)return;
	if (Cue == TEXT("JournalOpen"))Cue = TEXT("Open");
	if (Cue == TEXT("JournalClose"))Cue = TEXT("Close");
	if (USoundBase* Sound = Sounds.FindRef(Cue))
		UGameplayStatics::PlaySound2D(this, Sound, 1.f, 1.f, 0.f, UIConcurrency, GetOwner(), true);
}

void UVerdantAudioComponent::UpdateFootsteps(float DeltaTime)
{
	auto* Valley = Cast<AVerdantWorld>(GetOwner());
	auto* Player = Cast<AVerdantCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (!Valley || !Player)return;
	TSet<int32> Present;
	for (AVerdantCharacter* Character : Valley->Folk)
	{
		if (!IsValid(Character) || Character->bDead || Character->bWild)continue;
		if (FVector::DistSquared2D(Character->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(2500.f))
			continue;
		Present.Add(Character->AgentId);
		const FVector Position = Character->GetActorLocation();
		auto* Old = Footsteps.Find(Character->AgentId);
		if (!Old || Old->Character.Get() != Character)
		{
			FFootstepState Initial;
			Initial.Character = Character;
			Initial.PreviousPosition = Position;
			Footsteps.Add(Character->AgentId, Initial);
			continue;
		}
		FFootstepState& State = *Old;
		const float Travel = FVector::Dist2D(Position, State.PreviousPosition);
		State.PreviousPosition = Position;
		State.SinceStep += DeltaTime;
		const auto* Move = Character->GetCharacterMovement();
		const float Speed = Character->GetVelocity().Size2D();
		const float Stride = FMath::Clamp(170.f * Character->BodySize * (Character->bSprinting ? 1.3f : 1.f), 65.f,
		                                  400.f);
		// Ground travel only: don't turn falling, dodging, teleports or scale changes into steps.
		if (!Move || !Move->IsMovingOnGround() || Speed < 20.f || Character->DodgeTimer > 0.f
			|| (Character == Player && bDialogueActive) || Travel > FMath::Max(
				600.f, Speed * DeltaTime * 4.f + Stride * 3.f))
		{
			State.Travel = 0.f;
			continue;
		}
		State.Travel += Travel;
		if (State.Travel < Stride || State.SinceStep < .15f)continue;
		State.Travel = FMath::Fmod(State.Travel, Stride);
		State.SinceStep = 0.f;
		const FHitResult& Floor = Move->CurrentFloor.HitResult;
		const UPrimitiveComponent* Surface = Floor.GetComponent();
		const AActor* FloorActor = Floor.GetActor();
		const bool Stone = (Surface && (Surface->ComponentHasTag(TEXT("AudioSurface.Stone")) || Surface->
				ComponentHasTag(TEXT("Stone"))))
			|| (FloorActor && FloorActor->ActorHasTag(TEXT("AudioSurface.Stone")));
		const int32 Index = State.Variant++ % 3;
		const FVector FootPosition = Floor.bBlockingHit
			                             ? FVector(Floor.ImpactPoint)
			                             : Position - FVector(
				                             0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const float Pitch = FMath::Clamp(
			FMath::Pow(FMath::Max(.35f, Character->BodySize), -.14f) * (1.f + (Index - 1) * .018f), .7f, 1.2f);
		PlaySpatial(Stone ? StoneKeys[Index] : DirtKeys[Index], FootPosition, Character == Player ? .7f : .5f, Pitch,
		            FootstepConcurrency);
	}
	for (auto It = Footsteps.CreateIterator(); It; ++It)
		if (!Present.Contains(It.Key()) || !It.Value().Character.IsValid())It.RemoveCurrent();
}

void UVerdantAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	if (MusicPlayer)
	{
		MusicPlayer->OnAudioFinishedNative.RemoveAll(this);
		MusicPlayer->OnAudioPlaybackPercentNative.RemoveAll(this);
		MusicPlayer->Stop();
		MusicPlayer->DestroyComponent();
		MusicPlayer = nullptr;
	}
	if (AmbiencePlayer)
	{
		AmbiencePlayer->Stop();
		AmbiencePlayer->DestroyComponent();
		AmbiencePlayer = nullptr;
	}
	Footsteps.Reset();
	Sounds.Reset();
	Super::EndPlay(Reason);
}
