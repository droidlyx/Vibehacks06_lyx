#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VerdantAudioComponent.generated.h"

class AVerdantCharacter;
class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;
class USoundWave;

// Presentation only: never changes gameplay state or consumes its random stream.
UCLASS(ClassGroup=(Audio))
class VERDANT_API UVerdantAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVerdantAudioComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	// False allows the original project cue to play as well (e.g. Swing/Pickup).
	bool PlayEvent(FName Cue, FVector Position, float BodySize = 1.f);
	void PlayUI(FName Cue);
	void SetDialogueActive(bool bActive);

private:
	struct FFootstepState
	{
		TWeakObjectPtr<AVerdantCharacter> Character;
		FVector PreviousPosition = FVector::ZeroVector;
		float Travel = 0.f;
		float SinceStep = 0.f;
		int32 Variant = 0;
	};

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<USoundBase>> Sounds;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicPlayer;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbiencePlayer;
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> NearAttenuation;
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> FootstepConcurrency;
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> FoleyConcurrency;
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> UIConcurrency;
	TMap<int32, FFootstepState> Footsteps;
	int32 FoleyVariant = 0;
	bool bAudioEnabled = false;
	bool bEnding = false;
	bool bDialogueActive = false;
	float MusicAge = 0.f;
	float MusicDuration = 0.f;
	float MusicDuck = 1.f;

	void LoadSound(FName Key, const TCHAR* Folder, const TCHAR* AssetName);
	void StartMusic();
	void MusicFinished(UAudioComponent* Component);
	void MusicProgress(const UAudioComponent* Component, const USoundWave* Wave, float Percent);
	void UpdateFootsteps(float DeltaTime);
	void PlaySpatial(FName Key, const FVector& Position, float Gain, float Pitch, USoundConcurrency* Concurrency);
};
