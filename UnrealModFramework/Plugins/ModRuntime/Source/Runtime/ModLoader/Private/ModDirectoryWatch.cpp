// Copyright (c) 2026 Nelaric Contributors
#include "ModDirectoryWatch.h"

#include "Algo/Find.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogModDirectoryWatch, Log, All);

namespace Mod::Loader::Private
{
namespace
{
constexpr double ReadInterval = 0.1;
constexpr double SettleInterval = 0.5;
constexpr double RegistrationInterval = 2.0;
constexpr int32 PreparationAttempts = 3;
constexpr int32 MaxCandidates = 128;
constexpr int32 MaxDirectoryEntries = 4096;

bool IsModArchive(const FString& Path)
{
	return FPaths::GetExtension(Path).Equals(TEXT("mod"), ESearchCase::IgnoreCase);
}

bool SameFileState(const FFileStatData& Left, const FFileStatData& Right)
{
	return Right.bIsValid && !Right.bIsDirectory && Left.FileSize == Right.FileSize &&
	       Left.ModificationTime == Right.ModificationTime && Left.CreationTime == Right.CreationTime;
}

bool WasChanged(const IO::FDirectoryChanges& Changes, const FString& Path)
{
	return Changes.bRescan || Changes.Names.Contains(FPaths::GetCleanFilename(Path));
}
} // namespace

bool FindArchives(FPreparationBudget Budget, TArray<FString>& Paths, FLoadResult& Result)
{
	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	int32 Visited = 0;
	bool bLimitReached = false;
	const bool bScanned =
	    Files.IsSymlink(*Result.Path) != ESymlinkResult::Symlink &&
	    Files.IterateDirectory(*Result.Path,
	                           [&Paths, &Visited, &bLimitReached, Budget](const TCHAR* Path, bool bIsDirectory)
	                           {
		                           bLimitReached = ++Visited > MaxDirectoryEntries;
		                           if (bLimitReached || Budget.IsExpired())
		                           {
			                           return false;
		                           }
		                           if (!bIsDirectory && IsModArchive(Path))
		                           {
			                           Paths.Add(Path);
			                           bLimitReached = Paths.Num() > MaxCandidates;
		                           }
		                           return !bLimitReached;
	                           });
	if (!bScanned)
	{
		if (bLimitReached)
		{
			return Fail(Result, ELoadError::LimitExceeded, TEXT("The mod directory exceeds the scan limit."));
		}
		if (Budget.IsExpired())
		{
			return Fail(Result, ELoadError::TimedOut, TEXT("The mod directory scan timed out."));
		}
		return Fail(Result, ELoadError::InputUnavailable, TEXT("The physical mod directory could not be scanned."));
	}
	Paths.Sort([](const FString& Left, const FString& Right)
	           { return Left.Compare(Right, ESearchCase::CaseSensitive) < 0; });
	return true;
}

void FModDirectoryWatch::Start(FString InDirectory, const FLoadPolicy& InPolicy)
{
	Stop();
	Directory = MoveTemp(InDirectory);
	Policy = InPolicy;
	const double Now = FPlatformTime::Seconds();
	NextReadAt = Now;
	TryRegister(Now);
}

void FModDirectoryWatch::Stop()
{
	Watcher.Stop();
	if (Cancellation)
	{
		Cancellation->Store(true);
	}
	Directory.Reset();
	PendingChanges = {};
	Observations.Reset();
}

void FModDirectoryWatch::RecordStartupPaths(TConstArrayView<FString> Paths)
{
	if (Directory.IsEmpty())
	{
		return;
	}
	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	Observations.Reserve(Paths.Num());
	for (const FString& Path : Paths)
	{
		const FFileStatData Stat = Files.GetStatData(*Path);
		if (Stat.bIsValid && !Stat.bIsDirectory)
		{
			Observations.Add({Path, Stat, 0});
		}
	}
}

void FModDirectoryWatch::RequestRescan()
{
	PendingChanges.bRescan = true;
	NextScanAt = FMath::Min(NextScanAt, FPlatformTime::Seconds() + SettleInterval);
}

void FModDirectoryWatch::Shutdown()
{
	Stop();
	if (Scan.IsValid())
	{
		Scan.Wait();
		Scan.Reset();
	}
}

void FModDirectoryWatch::TryRegister(double Now)
{
	NextRegistrationAt = Now + RegistrationInterval;
	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	if (Files.IsSymlink(*Directory) != ESymlinkResult::Symlink && Files.CreateDirectoryTree(*Directory) &&
	    Watcher.Start(Directory))
	{
		UE_LOG(LogModDirectoryWatch, Display, TEXT("Watching mod directory '%s'."), *Directory);
		NextScanAt = Now + SettleInterval;
	}
}

FWatchedChanges FModDirectoryWatch::Tick(TFunctionRef<TMap<FString, FString>()> GetMountedSources)
{
	const double Now = FPlatformTime::Seconds();
	if (!Directory.IsEmpty() && Now >= NextReadAt)
	{
		NextReadAt = Now + ReadInterval;
		IO::FDirectoryChanges Changes = Watcher.ReadChanges();
		for (const FString& Name : Changes.Names)
		{
			if (IsModArchive(Name))
			{
				PendingChanges.Names.Add(Name);
			}
		}
		PendingChanges.bRescan |= Changes.bRescan || PendingChanges.Names.Num() > MaxCandidates;
		if (PendingChanges.bRescan)
		{
			PendingChanges.Names.Reset();
		}
		if (PendingChanges.bRescan || !PendingChanges.Names.IsEmpty())
		{
			NextScanAt = FMath::Min(NextScanAt, Now + SettleInterval);
		}
		if (!Watcher.IsActive() && Now >= NextRegistrationAt)
		{
			TryRegister(Now);
		}
	}

	if (Scan.IsReady())
	{
		FScan Completed = Scan.Consume();
		if (!Cancellation->Load())
		{
			Observations = MoveTemp(Completed.Observations);
			if (Completed.bNeedsAnotherScan)
			{
				NextScanAt = FMath::Min(NextScanAt, Now + SettleInterval);
			}
			Completed.Changes.Packages.RemoveAll([this](const FWatchedPackage& Item)
			                                     { return WasChanged(PendingChanges, Item.Result.Path); });
			return MoveTemp(Completed.Changes);
		}
	}

	if (!Directory.IsEmpty() && Watcher.IsActive() && !Scan.IsValid() && Now >= NextScanAt)
	{
		NextScanAt = TNumericLimits<double>::Max();
		Cancellation = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
		IO::FDirectoryChanges Changes = MoveTemp(PendingChanges);
		PendingChanges = {};
		Scan = Async(EAsyncExecution::ThreadPool,
		             [Path = Directory, Policy = Policy, Previous = Observations, Changes = MoveTemp(Changes),
		              MountedSources = GetMountedSources(), Canceled = Cancellation]
		             { return ScanDirectory(Path, Policy, Previous, Changes, MountedSources, *Canceled); });
	}
	return {};
}

FModDirectoryWatch::FScan FModDirectoryWatch::ScanDirectory(const FString& Path, const FLoadPolicy& Policy,
                                                            TConstArrayView<FObservation> Previous,
                                                            const IO::FDirectoryChanges& Changes,
                                                            const TMap<FString, FString>& MountedSources,
                                                            const TAtomic<bool>& Canceled)
{
	FScan Result;
	const FPreparationBudget Budget{FPlatformTime::Seconds() + Policy.PreparationTimeoutSeconds, &Canceled};
	TArray<FString> Paths;
	FLoadResult ScanResult;
	ScanResult.Path = Path;
	if (!FindArchives(Budget, Paths, ScanResult))
	{
		Result.Observations.Append(Previous.GetData(), Previous.Num());
		Result.Changes.Packages.Add({MoveTemp(ScanResult), {}});
		return Result;
	}

	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	TSet<FString> MountedIds;
	for (const TPair<FString, FString>& Entry : MountedSources)
	{
		if (FPaths::IsSamePath(Path, FPaths::GetPath(Entry.Value)) && !Files.FileExists(*Entry.Value))
		{
			Result.Changes.RemovedSources.Add(Entry.Key, Entry.Value);
		}
		else
		{
			MountedIds.Add(Entry.Key);
		}
	}
	for (FString& Archive : Paths)
	{
		const FFileStatData Stat = Files.GetStatData(*Archive);
		if (!Stat.bIsValid || Stat.bIsDirectory)
		{
			continue;
		}
		const FObservation* Old =
		    Algo::FindByPredicate(Previous, [&Archive](const FObservation& Entry)
		                          { return Entry.Path.Equals(Archive, ESearchCase::CaseSensitive); });
		FObservation& Current = Result.Observations.Add_GetRef({MoveTemp(Archive), Stat, PreparationAttempts});
		if (!Old || !SameFileState(Old->Stat, Stat) || WasChanged(Changes, Current.Path))
		{
			Result.bNeedsAnotherScan = true;
			continue;
		}

		Current.AttemptsLeft = Old->AttemptsLeft;
		if (Current.AttemptsLeft == 0)
		{
			continue;
		}
		FWatchedPackage Item;
		Item.Result.Path = Current.Path;
		if (Budget.IsExpired())
		{
			Fail(Item.Result, ELoadError::TimedOut, TEXT("Mod package preparation timed out."));
		}
		else
		{
			PrepareArchive(Policy, Budget, Item.Result, Item.Prepared, &MountedIds);
		}

		const FFileStatData After = Files.GetStatData(*Current.Path);
		if (!SameFileState(Stat, After))
		{
			Result.bNeedsAnotherScan = true;
			continue;
		}
		const ELoadError Error = Item.Result.Error;
		if (--Current.AttemptsLeft > 0 &&
		    (Error == ELoadError::InputUnavailable || Error == ELoadError::InvalidArchive ||
		     Error == ELoadError::IntegrityFailure))
		{
			Result.bNeedsAnotherScan = true;
			continue;
		}
		Current.AttemptsLeft = 0;
		Result.Changes.Packages.Add(MoveTemp(Item));
	}
	return Result;
}
} // namespace Mod::Loader::Private
