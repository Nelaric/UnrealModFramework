// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Commandlets/Commandlet.h"

#include "ModExportBlueprintsCommandlet.generated.h"

/// Exports reflected API signatures from configured host Blueprint assets.
UCLASS(MinimalAPI)
class UModExportBlueprintsCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	UModExportBlueprintsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
