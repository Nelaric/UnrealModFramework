// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Commandlets/Commandlet.h"
#include "BenchmarkModAssets.generated.h"

UCLASS(MinimalAPI)
class UBenchmarkModAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
