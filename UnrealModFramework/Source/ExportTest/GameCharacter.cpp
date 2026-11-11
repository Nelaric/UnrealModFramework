// Copyright (c) 2026 Nelaric Contributors
#include "GameCharacter.h"

#include "Animation/AnimClassInterface.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"

float AGameCharacter::AddHealth(float Amount)
{
	if (FMath::IsFinite(Amount) && Amount > 0.0f)
	{
		Health = FMath::Clamp(Health + Amount, 0.0f, FMath::Max(MaxHealth, 0.0f));
	}
	return Health;
}

bool AGameCharacter::SetMesh(USkeletalMesh* NewMesh)
{
	if (!IsInGameThread() || !IsValid(NewMesh))
	{
		return false;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	const USkeleton* NewSkeleton = NewMesh->GetSkeleton();
	if (!IsValid(MeshComponent) || !NewSkeleton)
	{
		return false;
	}

	if (MeshComponent->GetAnimationMode() == EAnimationMode::AnimationBlueprint)
	{
		const IAnimClassInterface* AnimClassInterface =
		    IAnimClassInterface::GetFromClass(MeshComponent->GetAnimClass());
		if (AnimClassInterface)
		{
			const USkeleton* TargetSkeleton = AnimClassInterface->GetTargetSkeleton();
			// Animation Blueprint templates use the new mesh's skeleton.
			const USkeleton* AnimSkeleton = TargetSkeleton ? TargetSkeleton : NewSkeleton;
			if (!AnimSkeleton->IsCompatibleMesh(NewMesh, false))
			{
				return false;
			}
		}
	}

	if (MeshComponent->GetSkeletalMeshAsset() != NewMesh)
	{
		if (!bHasOriginalMesh)
		{
			OriginalMesh = MeshComponent->GetSkeletalMeshAsset();
			bHasOriginalMesh = true;
		}
		MeshComponent->SetSkeletalMeshAsset(NewMesh);
	}
	return true;
}

bool AGameCharacter::RestoreMesh()
{
	if (!IsInGameThread() || !IsValid(GetMesh()))
	{
		return false;
	}

	if (bHasOriginalMesh)
	{
		GetMesh()->SetSkeletalMeshAsset(OriginalMesh);
		OriginalMesh = nullptr;
		bHasOriginalMesh = false;
	}
	return true;
}
