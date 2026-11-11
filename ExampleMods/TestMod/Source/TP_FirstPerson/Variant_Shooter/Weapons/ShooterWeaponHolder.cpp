// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeaponHolder.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

IShooterWeaponHolder* IShooterWeaponHolder::Find(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}
	if (auto* Holder = Cast<IShooterWeaponHolder>(Actor))
	{
		return Holder;
	}
	const TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components)
	{
		if (IsValid(Component))
		{
			if (auto* Holder = Cast<IShooterWeaponHolder>(Component))
			{
				return Holder;
			}
		}
	}
	return nullptr;
}
