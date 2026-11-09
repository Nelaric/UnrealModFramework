// Copyright (c) 2026 Nelaric Contributors
#include "GameCharacter.h"

float AGameCharacter::AddHealth(float Amount)
{
	if (FMath::IsFinite(Amount) && Amount > 0.0f)
	{
		Health = FMath::Clamp(Health + Amount, 0.0f, FMath::Max(MaxHealth, 0.0f));
	}
	return Health;
}
