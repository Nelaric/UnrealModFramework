// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "BenchmarkCalls.h"
#include "BenchmarkFixtures.h"
#include "Engine/LevelStreaming.h"
#include "BenchmarkSession.generated.h"

class UModInstance;

UCLASS(MinimalAPI)
class UBenchmarkCallbacks : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void Complete(int32 Linkage)
	{
		bDone = true;
	}

	UFUNCTION()
	void AssetLoaded(UObject* Loaded)
	{
		bSuccess = IsValid(Loaded);
	}

	UFUNCTION()
	void ClassLoaded(TSubclassOf<UObject> Loaded)
	{
		bSuccess = Loaded != nullptr;
	}

	UFUNCTION()
	void SaveComplete(USaveGame* Object, bool bSucceeded)
	{
		bDone = true;
		bSuccess = bSucceeded && IsValid(Object);
	}

	void TraceComplete(const FTraceHandle&, FTraceDatum& Datum)
	{
		bDone = true;
		bSuccess = !Datum.OutHits.IsEmpty();
	}

	void OverlapComplete(const FTraceHandle&, FOverlapDatum& Datum)
	{
		bDone = true;
		bSuccess = !Datum.OutOverlaps.IsEmpty();
	}

	bool bDone = false;
	bool bSuccess = true;
};

namespace Benchmark
{
enum class EPath : uint8
{
	NativeCpp,
	NativeBlueprint,
	ModCpp,
	ModBlueprint,
	Count,
};

constexpr bool IsCpp(EPath Path)
{
	return Path == EPath::NativeCpp || Path == EPath::ModCpp;
}

class FSession : private FCallContext
{
public:
	FSession(UWorld& InWorld, FString InReport);

	bool Initialize();
	bool Tick();
	bool Finish();
	const FString& GetFailure() const
	{
		return Failure;
	}

private:
	static constexpr int32 Samples = 1000;
	static constexpr int32 PathCount = static_cast<int32>(EPath::Count);
	static constexpr int32 CaseCount = 200;

	void BeginCase();
	bool SetUp(int32 Id);
	bool TearDown(int32 Id);
	bool DispatchModCpp(int32 Id, uint64& Elapsed);
	bool DispatchBlueprint(int32 Id, uint64& Elapsed);
	bool SetArguments(UFunction& Function, uint8* Buffer, int32 Id);
	UObject& Receiver() const;
	FBlueprintSample& BlueprintSample() const;
	UObject* ObservedObject() const;
	bool AfterDispatch(int32 Id);
	bool IsPending(int32 Id) const;
	bool Validate(int32 Id) const;
	void Fail(const FString& Reason);
	bool PrepareMod();
	bool ReadyForNavigation();
	bool WriteReport() const;

	FString Report;
	FString Failure;
	ABenchmarkBlueprintDriver* Driver = nullptr;
	ULevelStreaming* Streaming = nullptr;
	TWeakObjectPtr<UModInstance> Mod;
	UFunction* ModNativeCall = nullptr;
	FBlueprintSample* ModSample = nullptr;
	TStrongObjectPtr<UBenchmarkCallbacks> CallbackTarget;
	TStrongObjectPtr<UObject> Proxy;
	const FPrimaryAssetId Primary{TEXT("BenchmarkAsset"), TEXT("BenchmarkAsset")};

	bool bPending = false;
	bool bFailed = false;
	bool bModRequired = false;
	bool bNavigationReady = false;
	EPath ActivePath = EPath::NativeCpp;
	int32 Id = 1;
	int32 Iteration = 0;
	int32 PathIndex = 0;
	int32 WaitFrames = 0;
	int32 ReadyFrames = 0;
	uint64 Cycles[CaseCount][PathCount]{};
	int32 Counts[CaseCount][PathCount]{};
	bool Available[CaseCount][PathCount]{};
	EPath Paths[PathCount]{};
	int32 NumPaths = 0;
};
} // namespace Benchmark
