// Copyright (c) 2026 Nelaric Contributors
/** @file BenchmarkFixtures.h */
#pragma once

#include "BenchmarkBlueprintSample.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SaveGame.h"
#include "BenchmarkFixtures.generated.h"

UCLASS(MinimalAPI, Blueprintable)
class ABenchmarkBlueprintDriver : public AActor
{
	GENERATED_BODY()

public:
	/// Starts timing the Blueprint operation.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void BeginSample();

	/// Records the time spent in the Blueprint operation.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void EndSample();

	/// Marks the timed Blueprint operation as complete.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void CompleteSample();

	/// Records the object returned by the Blueprint operation.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void ObserveObject(UObject* Value);

	/// Provides a callback for timer operations.
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	void TimerCallback()
	{
	}

	Benchmark::FBlueprintSample Sample;
};

UCLASS(MinimalAPI)
class ABenchmarkCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/// Configures the pawn used for movement and navigation samples.
	ABenchmarkCharacter();
};

UCLASS(MinimalAPI)
class ABenchmarkGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/// Selects the benchmark pawn and HUD classes.
	ABenchmarkGameMode();
};

UCLASS(MinimalAPI)
class UBenchmarkAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/// Returns the identifier used by the asset manager samples.
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("BenchmarkAsset"), GetFName());
	}
};

UCLASS(MinimalAPI)
class UBenchmarkSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	TArray<uint8> Payload;
};
