// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Commandlets/Commandlet.h"
#include "BenchmarkAssets.generated.h"

UCLASS(MinimalAPI)
class UBenchmarkAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
