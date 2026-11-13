// Copyright (c) 2026 Nelaric Contributors
/** @file ModPackageCommandlet.h */
#pragma once

#include "Commandlets/Commandlet.h"

#include "ModPackageCommandlet.generated.h"

/// Headless Mod packaging.
UCLASS(MinimalAPI)
class UModPackageCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	/// Sets commandlet defaults.
	UModPackageCommandlet();

public:
	int32 Main(const FString& Params) override;
};
