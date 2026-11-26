// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModInstance.h"
#include "UMFDevSDKAPI.h"

#include "SharedCallerInstance.generated.h"

UCLASS(Blueprintable)
class USharedCallerInstance : public UModInstance
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Shared Runtime Test")
	float ReadHealth(const FUMFDevSDKPlayerContext& PlayerContext);
};
