// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "GameFramework/GameModeBase.h"

#include "GameExtensionMode.generated.h"

UCLASS(MinimalAPI)
class AGameExtensionMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGameExtensionMode();
	virtual void StartPlay() override;
};
