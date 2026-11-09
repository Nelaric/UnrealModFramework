// Copyright (c) 2026 Nelaric Contributors
/** @file ModInstanceSubSystem.h */
#pragma once

#include "ModInstance.h"

#include "ModInstanceSubSystem.generated.h"

/// An independently authored global service for the game session.
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DontUseGenericSpawnObject))
class MODLOADER_API UModInstanceSubSystem : public UModInstance
{
	GENERATED_BODY()
};
