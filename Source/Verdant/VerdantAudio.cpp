#include "VerdantWorld.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
void AVerdantWorld::PlayCue(FName Cue,FVector Position,float BodySize) {
 if(!GetWorld()||FParse::Param(FCommandLine::Get(),TEXT("nosound")))return;
 USoundBase* Sound=CueCache.FindRef(Cue);
 if(!Sound) {
  const FString Name=TEXT("AFX_")+Cue.ToString();
  Sound=LoadObject<USoundBase>(nullptr,*FString::Printf(TEXT("/Game/Art/Audio/%s.%s"),*Name,*Name));
  if(!Sound)return;
  CueCache.Add(Cue,Sound);
 }
 if(!CueAttenuation) {
  CueAttenuation=NewObject<USoundAttenuation>(this,TEXT("CueDistance"));
  CueAttenuation->Attenuation.bAttenuate=true;
  CueAttenuation->Attenuation.bSpatialize=true;
  CueAttenuation->Attenuation.AttenuationShape=EAttenuationShape::Sphere;
  CueAttenuation->Attenuation.AttenuationShapeExtents=FVector(180.f);
  CueAttenuation->Attenuation.FalloffDistance=2600.f;
 }
 if(!CueConcurrency) {
  CueConcurrency=NewObject<USoundConcurrency>(this,TEXT("BoundedFeedback"));
  CueConcurrency->Concurrency.MaxCount=4;
  CueConcurrency->Concurrency.bLimitToOwner=false;
  CueConcurrency->Concurrency.ResolutionRule=EMaxConcurrentResolutionRule::StopFarthestThenOldest;
 }
 // Private deterministic variation does not consume gameplay RNG or alter policy replay.
 const float Variation=1.f+.025f*FMath::Sin(Elapsed*7.f+float(GetTypeHash(Cue)%17));
 const float Pitch=FMath::Clamp(FMath::Pow(FMath::Max(.35f,BodySize),-.20f)*Variation,.7f,1.3f);
 UGameplayStatics::PlaySoundAtLocation(this,Sound,Position,FRotator::ZeroRotator,.85f,Pitch,0.f,CueAttenuation,CueConcurrency,this);
}
