// Copyright (c) 2026 Nelaric Contributors
#include "GameCharacter.h"

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

	const USkeletalMesh* CurrentMesh = MeshComponent->GetSkeletalMeshAsset();
	if (CurrentMesh && CurrentMesh->GetSkeleton() != NewSkeleton)
	{
		return false;
	}

	MeshComponent->SetSkeletalMeshAsset(NewMesh);
	return true;
}
