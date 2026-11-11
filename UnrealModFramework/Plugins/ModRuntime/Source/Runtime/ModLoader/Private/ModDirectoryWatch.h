// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "Async/Future.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "ModDirectoryWatcher.h"
#include "ModPackageArchive.h"

namespace Mod::Loader::Private
{
struct FWatchedPackage
{
	FLoadResult Result;
	FPreparedPackage Prepared;
};

struct FWatchedChanges
{
	TMap<FString, FString> RemovedSources;
	TArray<FWatchedPackage> Packages;
};

class FModDirectoryWatch
{
public:
	void Start(FString InDirectory, const FLoadPolicy& InPolicy);
	void RecordStartupPaths(TConstArrayView<FString> Paths);
	void RequestRescan();
	void Stop();
	void Shutdown();
	FWatchedChanges Tick(TFunctionRef<TMap<FString, FString>()> GetMountedSources);
	const FString& GetDirectory() const
	{
		return Directory;
	}

private:
	struct FObservation
	{
		FString Path;
		FFileStatData Stat;
		int32 AttemptsLeft;
	};

	struct FScan
	{
		TArray<FObservation> Observations;
		FWatchedChanges Changes;
		bool bNeedsAnotherScan = false;
	};

	static FScan ScanDirectory(const FString& Path, const FLoadPolicy& Policy, TConstArrayView<FObservation> Previous,
	                           const IO::FDirectoryChanges& Changes, const TMap<FString, FString>& MountedSources,
	                           const TAtomic<bool>& Canceled);
	void TryRegister(double Now);

	FString Directory;
	FLoadPolicy Policy;
	IO::FDirectoryWatcher Watcher;
	IO::FDirectoryChanges PendingChanges;
	TArray<FObservation> Observations;
	TFuture<FScan> Scan;
	TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> Cancellation;
	double NextReadAt = 0.0;
	double NextScanAt = 0.0;
	double NextRegistrationAt = 0.0;
};
} // namespace Mod::Loader::Private
