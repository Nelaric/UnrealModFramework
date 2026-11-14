// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "HAL/PlatformTime.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace Benchmark
{
struct FBlueprintSample
{
	void Begin()
	{
		Started = FPlatformTime::Cycles64();
	}
	void End()
	{
		Cycles = FPlatformTime::Cycles64() - Started;
	}

	uint64 Started = 0;
	uint64 Cycles = 0;
	int32 Completed = 0;
	TWeakObjectPtr<UObject> Object;
};
} // namespace Benchmark
