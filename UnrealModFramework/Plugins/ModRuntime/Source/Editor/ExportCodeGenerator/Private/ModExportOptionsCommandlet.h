// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Commandlets/Commandlet.h"

#include "ModExportOptionsCommandlet.generated.h"

/// Exports resolved shared metadata choices without reading Blueprint assets.
UCLASS(MinimalAPI)
class UModExportOptionsCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	/// Initializes a noninteractive editor commandlet.
	UModExportOptionsCommandlet();

	/// Writes the shared option catalog; returns nonzero on failure.
	virtual int32 Main(const FString& Params) override;
};
