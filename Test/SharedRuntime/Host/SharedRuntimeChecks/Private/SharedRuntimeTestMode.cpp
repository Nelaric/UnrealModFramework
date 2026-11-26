// Copyright (c) 2026 Nelaric Contributors
#include "SharedRuntimeTestMode.h"
#include "GameCharacter.h"

ASharedRuntimeTestMode::ASharedRuntimeTestMode()
{
	DefaultPawnClass = AGameCharacter::StaticClass();
}
