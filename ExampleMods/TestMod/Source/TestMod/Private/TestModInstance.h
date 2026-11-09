// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModInstance.h"

#include "TestModInstance.generated.h"

UCLASS(Blueprintable)
class UTestModInstance : public UModInstance
{
	GENERATED_BODY()

protected:
	void OnModLoadCompleted(const FModLoadNotification& Result) override;
};
