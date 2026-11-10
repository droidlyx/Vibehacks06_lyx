#include "VerdantWorld.h"
#include "VerdantAudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AVerdantWorld::PlayCue(FName Cue, FVector Position, float BodySize)
{
	if (!GetWorld() || GetWorld()->bIsTearingDown || FParse::Param(FCommandLine::Get(),TEXT("nosound")))return;
	if (auto* Audio = FindComponentByClass<UVerdantAudioComponent>())
		if (Audio->PlayEvent(Cue, Position, BodySize))return;
	USoundBase* Sound = CueCache.FindRef(Cue);
	if (!CueCache.Contains(Cue))
	{
		const FString Name = TEXT("AFX_") + Cue.ToString();
		Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Art/Audio/%s.%s"), *Name, *Name));
		CueCache.Add(Cue, Sound);
		if (!Sound)UE_LOG(LogTemp, Warning, TEXT("VERDANT_AUDIO missing original cue %s"), *Name);
	}
	if (!Sound)return;
	if (!CueAttenuation)
	{
		CueAttenuation = NewObject<USoundAttenuation>(this,TEXT("CueDistance"));
		CueAttenuation->Attenuation.bAttenuate = true;
		CueAttenuation->Attenuation.bSpatialize = true;
		CueAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		CueAttenuation->Attenuation.AttenuationShapeExtents = FVector(180.f);
		CueAttenuation->Attenuation.FalloffDistance = 2600.f;
	}
	if (!CueConcurrency)
	{
		CueConcurrency = NewObject<USoundConcurrency>(this,TEXT("BoundedFeedback"));
		CueConcurrency->Concurrency.MaxCount = 4;
		CueConcurrency->Concurrency.bLimitToOwner = false;
		CueConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
	}
	// Private deterministic variation does not consume gameplay RNG or alter policy replay.
	const float Variation = 1.f + .025f * FMath::Sin(Elapsed * 7.f + float(GetTypeHash(Cue) % 17));
	const bool Physical = Cue == TEXT("Swing") || Cue == TEXT("Hit");
	const float Pitch = Physical
		                    ? FMath::Clamp(FMath::Pow(FMath::Max(.35f, BodySize), -.20f) * Variation, .7f, 1.3f)
		                    : 1.f;
	UGameplayStatics::PlaySoundAtLocation(this, Sound, Position, FRotator::ZeroRotator, .85f, Pitch, 0.f,
	                                      CueAttenuation, CueConcurrency, this);
}

void AVerdantWorld::PlayUISound(FName Cue)
{
	if (auto* Audio = FindComponentByClass<UVerdantAudioComponent>())Audio->PlayUI(Cue);
}
