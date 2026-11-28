// Copyright (c) 2026 Nelaric Contributors
/** @file BenchmarkModInstance.h */
#pragma once

#include "BenchmarkBlueprintSample.h"
#include "ModInstance.h"
#include "BenchmarkModInstance.generated.h"

/// Runs the benchmark Blueprint graphs loaded from the Mod package.
UCLASS(MinimalAPI, Blueprintable, meta = (ShowWorldContextPin))
class UBenchmarkModInstance : public UModInstance
{
	GENERATED_BODY()

public:
	/// Returns the current Blueprint measurement state.
	Benchmark::FBlueprintSample& GetSample()
	{
		return Sample;
	}

	/// Starts timing one Blueprint call.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void BeginSample();

	/// Stores the elapsed cycles for the current call.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void EndSample();

	/// Counts one Blueprint completion.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void CompleteSample();

	/// Records the object returned by the operation.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void ObserveObject(UObject* Value);

	/// Receives the timer event used by the benchmark graph.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void TimerCallback()
	{
	}

private:
	Benchmark::FBlueprintSample Sample;
};
