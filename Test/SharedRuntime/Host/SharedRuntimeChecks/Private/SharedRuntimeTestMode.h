// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "GameFramework/GameModeBase.h"

#include "SharedRuntimeTestMode.generated.h"

// Exercises the ordinary ModLoader lifecycle with the sample game's exported pawn.
UCLASS()
class ASharedRuntimeTestMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ASharedRuntimeTestMode();
};
