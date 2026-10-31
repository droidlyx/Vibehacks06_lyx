#include "VerdantTitanPresentation.h"
#include "VerdantCharacter.h"
#include "VerdantWorld.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

namespace VerdantTitanPresentation
{
	namespace
	{
		const FString Cat = TEXT("/Game/Characters/NPCs/Clifftop/Creatures/Jellybean_Cat/");
		const FString Penguin = TEXT("/Game/Characters/NPCs/Arctic/Penguin/Rigged/Small/");
		const FString Ferret = TEXT("/Game/Characters/NPCs/Desert/Creatures/Desert_Ferret/");

		template <class T>
		T* Asset(const FString& Package)
		{
			FString Name;
			Package.Split(TEXT("/"), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			return LoadObject<T>(nullptr, *(Package + TEXT(".") + Name));
		}

		bool IsPenguin(const AVerdantCharacter* C)
		{
			return !C->ModelPath.IsEmpty()
				       ? C->ModelPath.Contains(TEXT("Penguin"))
				       : C->AgentId == 1 || C->AgentId == 3;
		}

		bool IsFerret(const AVerdantCharacter* C)
		{
			return !C->ModelPath.IsEmpty()
				       ? (C->ModelPath.Contains(TEXT("Ferret")) || C->ModelPath.Contains(TEXT("Ferrret")))
				       : C->AgentId == 4 || C->AgentId == 6;
		}
	}

	float BaseHeight(const AVerdantCharacter* C)
	{
		// Population model bounds are normalized to the same physical height basis.
		if (!C->ModelPath.IsEmpty()) return 180.f;
		return IsFerret(C) && C->BodyParts.IsEmpty() ? 110.f : 180.f;
	}

	bool Load(AVerdantCharacter* C)
	{
		if (!C->ModelPath.IsEmpty())
		{
			auto* Mesh = Asset<USkeletalMesh>(C->ModelPath);
			if (!Mesh)
			{
				UE_LOG(LogTemp, Error, TEXT("VERDANT_RESIDENT_MESH_MISSING type=%s path=%s"), *C->CharacterTypeId,
				       *C->ModelPath);
				return false;
			}
			C->GetMesh()->SetSkeletalMeshAsset(Mesh);
			// Some original Titan meshes contain empty slots. Supply component-only
			// overrides for those slots; never rewrite the mesh asset or replace its
			// authored, non-null materials. The ferret's empty mouth/eye slot stays dark.
			for (int32 Slot = 0; Slot < Mesh->GetMaterials().Num(); ++Slot)
			{
				if (Mesh->GetMaterials()[Slot].MaterialInterface) continue;
				UMaterialInterface* Fallback = nullptr;
				if (auto* W = C->Valley())
				{
					const FLinearColor Color = IsFerret(C) && Slot == 1
						                           ? FLinearColor(.035f, .025f, .018f)
						                           : C->BodyColor;
					Fallback = W->ColoredMaterial(Color);
				}
				if (!Fallback)
					Fallback = LoadObject<UMaterialInterface>(nullptr,
					                                          TEXT(
						                                          "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
				if (!Fallback) Fallback = UMaterial::GetDefaultMaterial(MD_Surface);
				C->GetMesh()->SetMaterial(Slot, Fallback);
				UE_LOG(LogTemp, Display, TEXT("VERDANT_RESIDENT_MATERIAL_FALLBACK type=%s slot=%d material=%s"),
				       *C->CharacterTypeId, Slot, *GetPathNameSafe(Fallback));
			}
			C->IdleAnimation = C->IdlePath.IsEmpty() ? nullptr : Asset<UAnimSequence>(C->IdlePath);
			C->WalkAnimation = C->WalkPath.IsEmpty() ? nullptr : Asset<UAnimSequence>(C->WalkPath);
			C->RunAnimation = C->WalkAnimation;
			C->GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			C->GetMesh()->SetVisibility(true);
			return true;
		}
		auto* Mesh = Asset<USkeletalMesh>(IsPenguin(C)
			                                  ? Penguin + TEXT("T-Pose_weight_color")
			                                  : IsFerret(C)
			                                  ? Ferret + TEXT("SKM_DesertFerrret")
			                                  : Cat + TEXT("SKM_Jellybean"));
		if (!Mesh) return false;
		C->GetMesh()->SetSkeletalMeshAsset(Mesh);
		const FString Motion = TEXT("/Game/Art/Creatures/Animations/");
		C->IdleAnimation = Asset<UAnimSequence>(IsPenguin(C)
			                                        ? Penguin + TEXT("Idle")
			                                        : Motion + (IsFerret(C)
				                                                    ? TEXT("A_Ferret_Idle")
				                                                    : TEXT("A_Jellybean_Idle")));
		C->WalkAnimation = Asset<UAnimSequence>(IsPenguin(C)
			                                        ? Penguin + TEXT("AS_Penguin_Small_Walk")
			                                        : Motion + (IsFerret(C)
				                                                    ? TEXT("A_Ferret_Walk")
				                                                    : TEXT("A_Jellybean_Walk")));
		C->RunAnimation = C->WalkAnimation;
		// Species keep their own rig. Combat is a simple mesh pose driven by the
		// same authoritative clock as damage, never a human clip on an animal rig.
		return true;
	}

	void Paint(AVerdantCharacter* C)
	{
		// Preserve the twenty authored Titan materials; role identity is not a recolour.
		if (!C->ModelPath.IsEmpty()) return;
		if (auto* W = C->Valley())
		{
			// Jellybean's original glossy body is replaced by matte role colour.
			// Preserve its separate face/whisker material and all penguin/ferret textures.
			if (!IsPenguin(C) && !IsFerret(C))
				C->GetMesh()->SetMaterial(0, W->ColoredMaterial(C->BodyColor));
			if (IsFerret(C)) C->GetMesh()->SetMaterial(1, W->ColoredMaterial(FLinearColor(.035f, .025f, .018f)));
		}
	}

	void Update(AVerdantCharacter* C)
	{
		auto* Component = C->GetMesh();
		auto* Mesh = Component->GetSkeletalMeshAsset();
		if (!Mesh) return;
		const bool Action = C->bDead || C->AttackTimer > 0 || C->HurtTimer > 0 || C->DodgeTimer > 0;
		UAnimSequence* Clip = !Action && C->GetVelocity().Size2D() > 15.f
			                      ? C->WalkAnimation.Get()
			                      : C->IdleAnimation.Get();
		auto* Instance = Component->GetSingleNodeInstance();
		if (Clip && (!Instance || Instance->GetCurrentAsset() != Clip)) Component->PlayAnimation(Clip, true);
		Component->SetPlayRate(Action ? 0.f : Clip == C->WalkAnimation ? (C->bSprinting ? 1.5f : 1.f) : 1.f);
		if (Action) Component->SetPosition(0.f, false);

		const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
		const float HalfHeight = BaseHeight(C) * .5f;
		const float Scale = BaseHeight(C) / FMath::Max(1.f, static_cast<float>(Bounds.BoxExtent.Z * 2.f)) * C->BodySize;
		const float Floor = -HalfHeight * C->BodySize;
		float Forward = 0.f, Lift = 0.f, Pitch = 0.f, Roll = 0.f;
		FVector Squash(1.f);
		if (C->bDead)
		{
			Roll = 85.f;
			Squash = FVector(1.f, 1.f, .65f);
		}
		else if (C->DodgeTimer > 0)
		{
			const float Phase = 1.f - C->DodgeTimer / C->DodgeActiveDuration;
			Pitch = -360.f * Phase;
			Lift = FMath::Sin(Phase * PI) * 18.f;
		}
		else if (C->HurtTimer > 0 && !(C->bHeavyAttack && C->AttackTimer > 0))
		{
			const float Recoil = FMath::Sin(C->HurtTimer / .3f * PI);
			Pitch = 18.f * Recoil;
			Forward = -10.f * Recoil;
		}
		else if (C->AttackTimer > 0)
		{
			const float Phase = FMath::Clamp(1.f - C->AttackTimer / C->AttackDuration, 0.f, 1.f);
			const float Contact = Phase < .6f ? FMath::Pow(Phase / .6f, 3.f) : 1.f - (Phase - .6f) / .4f;
			Forward = Contact * (C->bHeavyAttack ? 35.f : 26.f);
			Pitch = -Contact * (C->bHeavyAttack ? 24.f : 14.f);
			if (C->bHeavyAttack)
			{
				const float Windup = Phase < .6f ? FMath::Sin(Phase / .6f * PI) : 0.f;
				Squash = FVector(1.f + .12f * Windup, 1.f + .12f * Windup, 1.f - .18f * Windup);
			}
		}
		// Rotate around the visual centre, then ground the mesh by its imported bounds.
		// All selected rigs face +Y (verified using head/foot/toe bind positions).
		const FQuat Rotation = FRotator(Pitch, 0.f, Roll).Quaternion()
			* FRotator(0.f, -90.f, 0.f).Quaternion();
		FVector Center(Forward * C->BodySize, 0.f, Floor + HalfHeight * C->BodySize * Squash.Z + Lift * C->BodySize);
		if (C->bDead)
		{
			const FVector Extent = Bounds.BoxExtent * Squash * Scale;
			const float RestHeight = FMath::Abs(Rotation.RotateVector(FVector(Extent.X, 0, 0)).Z)
				+ FMath::Abs(Rotation.RotateVector(FVector(0, Extent.Y, 0)).Z)
				+ FMath::Abs(Rotation.RotateVector(FVector(0, 0, Extent.Z)).Z);
			Center.Z = Floor + RestHeight;
		}
		Component->SetRelativeScale3D(Squash * Scale);
		Component->SetRelativeRotation(Rotation);
		// The ferret's long cosmetic tail must not shift its gameplay centre away
		// from its torso. Its rig origin lies between pelvis and front shoulders.
		const FVector Anchor = IsFerret(C) ? FVector(0, 0, Bounds.Origin.Z) : Bounds.Origin;
		Component->SetRelativeLocation(Center - Rotation.RotateVector(Anchor * Squash * Scale));
	}
}
