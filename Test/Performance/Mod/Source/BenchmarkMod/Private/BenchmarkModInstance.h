// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "BenchmarkBlueprintSample.h"
#include "ModInstance.h"
#include "BenchmarkModInstance.generated.h"

UCLASS(MinimalAPI, Blueprintable, meta = (ShowWorldContextPin))
class UBenchmarkModInstance : public UModInstance
{
	GENERATED_BODY()

public:
	UFUNCTION()
	int64 GetSampleAddress();

	UFUNCTION()
	int64 MeasureCpp(int32 Case, int64 ContextAddress);

	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void BeginSample();

	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void EndSample();

	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void CompleteSample();

	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void ObserveObject(UObject* Value);

	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void TimerCallback()
	{
	}

private:
	Benchmark::FBlueprintSample Sample;
};
