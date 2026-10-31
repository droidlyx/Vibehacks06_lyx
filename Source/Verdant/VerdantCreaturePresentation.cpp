#include "VerdantCreaturePresentation.h"
#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
	enum ESeedPart
	{
		Seed, Belly, Stem, LeafLeft, LeafRight, EyeLeft, EyeRight,
		PupilLeft, PupilRight, FootLeft, FootRight, PartCount
	};

	struct FSeedPart
	{
		const TCHAR* Name;
		FVector Position;
		FVector Scale;
		FRotator Rotation;
	};

	// Engine sphere diameter is 100 cm. Rest pose: feet at z=0, crown ~180 cm,
	// body diameter 64 cm. Leaves are soft silhouette accents, not colliders.
	const FSeedPart Parts[] = {
		{TEXT("SeedBody"), FVector(0, 0, 78), FVector(.64, .60, 1.24), FRotator::ZeroRotator},
		{TEXT("SeedBelly"), FVector(23, 0, 72), FVector(.18, .45, .54), FRotator::ZeroRotator},
		{TEXT("SeedStem"), FVector(0, 0, 145), FVector(.08, .08, .28), FRotator(0, 0, -8)},
		{TEXT("SeedLeafLeft"), FVector(0, -12, 160), FVector(.13, .29, .50), FRotator(0, 0, -35)},
		{TEXT("SeedLeafRight"), FVector(0, 12, 160), FVector(.13, .29, .50), FRotator(0, 0, 35)},
		{TEXT("SeedEyeLeft"), FVector(27, -12, 104), FVector(.09, .13, .18), FRotator::ZeroRotator},
		{TEXT("SeedEyeRight"), FVector(27, 12, 104), FVector(.09, .13, .18), FRotator::ZeroRotator},
		{TEXT("SeedPupilLeft"), FVector(31, -12, 104), FVector(.045, .065, .10), FRotator::ZeroRotator},
		{TEXT("SeedPupilRight"), FVector(31, 12, 104), FVector(.045, .065, .10), FRotator::ZeroRotator},
		{TEXT("SeedFootLeft"), FVector(5, -17, 9), FVector(.34, .27, .18), FRotator::ZeroRotator},
		{TEXT("SeedFootRight"), FVector(5, 17, 9), FVector(.34, .27, .18), FRotator::ZeroRotator}
	};
	static_assert(UE_ARRAY_COUNT(Parts) == PartCount);

	float Smooth(const float Value)
	{
		const float T = FMath::Clamp(Value, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
}

void VerdantCreaturePresentation::BuildHero(AVerdantCharacter* Character)
{
	if (!Character || !Character->FallbackBody || !Character->BodyParts.IsEmpty()) return;
	auto* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Sphere) return;
	for (const FSeedPart& Spec : Parts)
	{
		auto* Part = NewObject<UStaticMeshComponent>(Character, Spec.Name);
		Character->AddInstanceComponent(Part);
		Part->SetupAttachment(Character->FallbackBody);
		Part->SetStaticMesh(Sphere);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetRelativeLocation(Spec.Position);
		Part->SetRelativeScale3D(Spec.Scale);
		Part->SetRelativeRotation(Spec.Rotation);
		Part->RegisterComponent();
		Character->BodyParts.Add(Part);
	}
	PaintHero(Character);
}

void VerdantCreaturePresentation::PaintHero(AVerdantCharacter* Character)
{
	if (!Character || Character->BodyParts.Num() != PartCount) return;
	auto* World = Character->Valley();
	if (!World) return;
	const FLinearColor Shell = FMath::Lerp(Character->BodyColor, FLinearColor(.76f, .68f, .40f), .35f);
	for (int32 Index = 0; Index < PartCount; ++Index)
	{
		FLinearColor Color = Index == Seed ? Shell : Character->BodyColor;
		if (Index == Belly) Color = FLinearColor(.85f, .72f, .42f);
		else if (Index >= Stem && Index <= LeafRight) Color = FLinearColor(.24f, .43f, .16f);
		else if (Index == EyeLeft || Index == EyeRight) Color = FLinearColor(.94f, .89f, .70f);
		else if (Index == PupilLeft || Index == PupilRight) Color = FLinearColor(.025f, .045f, .028f);
		else if (Index == FootLeft || Index == FootRight) Color = FLinearColor(.30f, .20f, .09f);
		if (auto* Part = Character->BodyParts[Index].Get()) Part->SetMaterial(0, World->ColoredMaterial(Color));
	}
}

void VerdantCreaturePresentation::UpdateHero(AVerdantCharacter* Character)
{
	if (!Character || !Character->GetWorld() || Character->BodyParts.Num() != PartCount) return;
	const float Time = Character->GetWorld()->GetTimeSeconds();
	const float Speed = Character->GetVelocity().Size2D();
	const float Moving = FMath::Clamp(Speed / FMath::Max(50.f, 260.f * Character->BodySize), 0.f, 1.f);
	const float Step = FMath::Sin(Time * (Character->bSprinting ? 13.f : 9.f));
	FVector Offset(0, 0, 1.f + FMath::Sin(Time * 1.8f) + 3.f * FMath::Abs(Step) * Moving);
	FVector Stretch(1.f);
	FRotator Rotation(0, 0, Step * 3.f * Moving);
	bool bFeetMoving = true;
	const bool HeavyCommitted = Character->bHeavyAttack && Character->AttackTimer > 0;

	if (Character->bDead)
	{
		Rotation = FRotator(0, 0, 82);
		Offset = FVector(0, 0, -43);
		Stretch = FVector(1.f, 1.f, .88f);
		bFeetMoving = false;
	}
	else if (Character->DodgeTimer > 0)
	{
		const float Phase = FMath::Clamp(1.f - Character->DodgeTimer / AVerdantCharacter::DodgeActiveDuration, 0.f,
		                                 1.f);
		Rotation = FRotator(-360.f * Smooth(Phase), 0, 0);
		Offset = FVector(0, 0, 48.f * FMath::Sin(PI * Phase));
		Stretch = FVector(1.04f, 1.04f, .9f);
		bFeetMoving = false;
	}
	else if (Character->HurtTimer > 0 && !HeavyCommitted)
	{
		const float Hit = FMath::Clamp(Character->HurtTimer / .3f, 0.f, 1.f);
		Rotation = FRotator(17.f * Hit, 0, 0);
		Offset = FVector(-10.f * Hit, 0, -4.f * Hit);
		Stretch = FVector(1.f + .1f * Hit, 1.f + .1f * Hit, 1.f - .12f * Hit);
		bFeetMoving = false;
	}
	else if (Character->AttackTimer > 0)
	{
		const float Phase = FMath::Clamp(1.f - Character->AttackTimer / FMath::Max(.01f, Character->AttackDuration),
		                                 0.f, 1.f);
		bFeetMoving = false;
		// Both contacts peak at 60%, exactly where the authoritative ApplyStrike
		// runs. Only the visible body moves; no teleport, damage or extra collision.
		if (!Character->bHeavyAttack)
		{
			const float Windup = Smooth(Phase / .40f);
			const float Contact = Phase < .60f ? Smooth((Phase - .40f) / .20f) : 1.f - Smooth((Phase - .60f) / .40f);
			const float Ready = Phase < .40f ? Windup : 1.f - Contact;
			Offset = FVector(42.f * Contact - 8.f * Ready * (Phase < .60f ? 1.f : 0.f), 0, 3.f * Contact);
			Rotation = FRotator(-24.f * Contact + 7.f * Ready * (Phase < .60f ? 1.f : 0.f), 0, 0);
			Stretch = FVector(1.f + .10f * Contact, 1.f, 1.f - .08f * Contact);
		}
		else
		{
			if (Phase < .32f)
			{
				const float Crouch = Smooth(Phase / .32f);
				Stretch = FVector(1.f + .16f * Crouch, 1.f + .16f * Crouch, 1.f - .24f * Crouch);
				Offset = FVector(-9.f * Crouch, 0, -17.f * Crouch);
				Rotation = FRotator(10.f * Crouch, 0, 0);
			}
			else if (Phase < .46f)
			{
				const float Rise = Smooth((Phase - .32f) / .14f);
				Stretch = FMath::Lerp(FVector(1.16f, 1.16f, .76f), FVector(.93f, .93f, 1.1f), Rise);
				Offset = FMath::Lerp(FVector(-9, 0, -17), FVector(18, 0, 45), Rise);
				Rotation = FRotator(FMath::Lerp(10.f, -12.f, Rise), 0, 0);
			}
			else if (Phase < .60f)
			{
				const float Fall = Smooth((Phase - .46f) / .14f);
				Stretch = FMath::Lerp(FVector(.93f, .93f, 1.1f), FVector(1.23f, 1.18f, .65f), Fall);
				Offset = FMath::Lerp(FVector(18, 0, 45), FVector(42, 0, -22), Fall);
				Rotation = FRotator(FMath::Lerp(-12.f, -24.f, Fall), 0, 0);
			}
			else
			{
				const float Settle = Smooth((Phase - .60f) / .40f);
				Stretch = FMath::Lerp(FVector(1.23f, 1.18f, .65f), FVector(1.f), Settle);
				Offset = FVector(42, 0, -22) * (1.f - Settle);
				Rotation = FRotator(-24.f * (1.f - Settle), 0, 0);
			}
		}
	}

	const FVector Pivot(0, 0, 76);
	const FQuat Turn = Rotation.Quaternion();
	const bool Blink = Character->bDead || FMath::Fmod(Time, 4.6f) < .12f;
	for (int32 Index = 0; Index < PartCount; ++Index)
	{
		auto* Part = Character->BodyParts[Index].Get();
		if (!Part) continue;
		FVector Position = Parts[Index].Position;
		FVector Scale = Parts[Index].Scale;
		FRotator LocalRotation = Parts[Index].Rotation;
		if (Index == LeafLeft || Index == LeafRight)
			LocalRotation.Roll += FMath::Sin(Time * 3.f + Index) * 5.f + Rotation.Pitch * .25f;
		if (bFeetMoving && (Index == FootLeft || Index == FootRight))
		{
			const float FootStep = Step * (Index == FootLeft ? 1.f : -1.f) * Moving;
			Position.X += FootStep * 10.f;
			Position.Z += FMath::Max(0.f, FootStep) * 8.f;
		}
		if (Blink && Index >= EyeLeft && Index <= PupilRight) Scale.Z *= .12f;
		Part->SetRelativeLocation(Pivot + Offset + Turn.RotateVector((Position - Pivot) * Stretch));
		Part->SetRelativeScale3D(Scale * Stretch);
		Part->SetRelativeRotation(Turn * LocalRotation.Quaternion());
	}
}
