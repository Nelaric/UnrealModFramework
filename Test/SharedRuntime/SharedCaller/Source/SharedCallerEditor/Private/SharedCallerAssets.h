// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Commandlets/Commandlet.h"

#include "SharedCallerAssets.generated.h"

/** Generates the cooked lifecycle entry point for the independent SharedCaller project. */
UCLASS(MinimalAPI)
class USharedCallerAssetsCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	int32 Main(const FString& Params) override;
};
