// Copyright (c) 2026 Nelaric Contributors
#include "ModLoader.h"

#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformProperties.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "ModPackageArchive.h"
#include "ModPackageMount.h"
#include "Modules/ModuleManager.h"
#include "Templates/UnrealTemplate.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ModLoaderPIE.h"
#include "Templates/Atomic.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogModLoader, Log, All);

namespace Mod::Loader
{
namespace
{
using Private::Fail;
using Private::FPreparedPackage;
using Private::PrepareArchive;

constexpr TCHAR ConfigSection[] = TEXT("ModLoader");
constexpr int32 MaxCandidates = 128;
constexpr int32 MaxDirectoryEntries = 4096;

struct FDeferredCompletion
{
	ELoadOrigin Origin = ELoadOrigin::Manual;
	FString Path;
	TArray<FLoadResult> Results;
	uint64 PIESession = 0;
};

struct FMountedRecord
{
	FMountedPackage Package;
#if WITH_EDITOR
	Private::FMountedResources PIEResources;
#endif
};
} // namespace

IModLoaderModule& IModLoaderModule::Get()
{
	return FModuleManager::LoadModuleChecked<IModLoaderModule>(TEXT("ModLoader"));
}

class FModLoaderModule final : public IModLoaderModule
{
public:
	void StartupModule() override
	{
		CompletionTicker =
		    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FModLoaderModule::TickCompletions));
		if (FPlatformProperties::RequiresCookedData() && !IsRunningCommandlet())
		{
			StartupHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FModLoaderModule::LoadAtStartup);
		}
#if WITH_EDITOR
		else if (GIsEditor && !IsRunningCommandlet())
		{
			BeginPIEHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FModLoaderModule::BeginPIE);
			EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FModLoaderModule::EndPIE);
			ShutdownPIEHandle = FEditorDelegates::ShutdownPIE.AddRaw(this, &FModLoaderModule::ShutdownPIE);
		}
#endif
	}

	void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.Remove(StartupHandle);
#if WITH_EDITOR
		FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		FEditorDelegates::ShutdownPIE.Remove(ShutdownPIEHandle);
#endif
		FTSTicker::RemoveTicker(CompletionTicker);
		// Engine-owned mounts and shaders live until process exit.
		Mounted.Reset();
	}

	bool SupportsDynamicReloading() override
	{
		return false;
	}

	FLoadResult LoadPackage(const FString& Path, const FLoadPolicy& Policy) override
	{
		FLoadResult Result = LoadPackageImpl(Path, Policy);
		PublishCompletion(ELoadOrigin::Manual, Result.Path, MakeArrayView(&Result, 1));
		return Result;
	}

	TArray<FLoadResult> LoadDirectory(const FString& Directory, const FLoadPolicy& Policy) override
	{
		const FString Path = FPaths::ConvertRelativePathToFull(Directory);
		TArray<FLoadResult> Results = LoadDirectoryImpl(Path, Policy);
		PublishCompletion(ELoadOrigin::Manual, Path, Results);
		return Results;
	}

	TArray<FMountedPackage> GetMountedPackages() const override
	{
		TArray<FMountedPackage> Result;
		if (IsInGameThread())
		{
			Result.Reserve(Mounted.Num());
			for (const TPair<FString, FMountedRecord>& Entry : Mounted)
			{
				Result.Add(Entry.Value.Package);
			}
			Result.Sort([](const FMountedPackage& Left, const FMountedPackage& Right)
			            { return Left.ModId < Right.ModId; });
		}
		return Result;
	}

#if WITH_EDITOR
	bool IsPIEActive() const
	{
		return ActivePIESession.Load() != 0;
	}

	bool IsCurrentPIECompletion() const
	{
		return DispatchingPIESession != 0 && DispatchingPIESession == ActivePIESession.Load();
	}

	TArray<FLoadResult> GetPIEStartupResults() const
	{
		return PIEStartupResults;
	}
#endif

private:
	FLoadResult LoadPackageImpl(const FString& Path, const FLoadPolicy& Policy)
	{
		FLoadResult Result;
		Result.Path = FPaths::ConvertRelativePathToFull(Path);
		if (CanLoad(Policy, Result))
		{
			TGuardValue<bool> Loading(bLoading, true);
			FPreparedPackage Prepared;
			const double Deadline = FPlatformTime::Seconds() + Policy.PreparationTimeoutSeconds;
			if (PrepareArchive(Policy, Deadline, Result, Prepared))
			{
				if (FPlatformTime::Seconds() >= Deadline)
				{
					Fail(Result, ELoadError::TimedOut, TEXT("Mod package preparation timed out."));
				}
				else
				{
					Commit(Prepared, Result);
				}
			}
		}
		return Result;
	}

	TArray<FLoadResult> LoadDirectoryImpl(const FString& Directory, const FLoadPolicy& Policy)
	{
		FLoadResult ScanResult;
		ScanResult.Path = Directory;
		if (!CanLoad(Policy, ScanResult))
		{
			return {MoveTemp(ScanResult)};
		}
		TGuardValue<bool> Loading(bLoading, true);
		TArray<FString> Paths;
		const double Deadline = FPlatformTime::Seconds() + Policy.PreparationTimeoutSeconds;
		if (!FindArchives(Deadline, Paths, ScanResult))
		{
			return {MoveTemp(ScanResult)};
		}
		TArray<FLoadResult> Results;
		TArray<FPreparedPackage> Prepared;
		Prepared.SetNum(Paths.Num());
		TMap<FString, FString> Digests;
		TSet<FString> ConflictingIds;
		for (int32 Index = 0; Index < Paths.Num(); ++Index)
		{
			FLoadResult& Result = Results.AddDefaulted_GetRef();
			Result.Path = MoveTemp(Paths[Index]);
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Fail(Result, ELoadError::TimedOut, TEXT("Mod package preparation timed out."));
			}
			else if (PrepareArchive(Policy, Deadline, Result, Prepared[Index]))
			{
				const FString& ModId = Result.Package.ModId;
				const FString& Digest = Result.Package.ArchiveSha256;
				const FString& First = Digests.FindOrAdd(ModId, Digest);
				if (First != Digest)
				{
					ConflictingIds.Add(ModId);
				}
			}
		}
		const bool bPreparationTimedOut = FPlatformTime::Seconds() >= Deadline;
		for (int32 Index = 0; Index < Results.Num(); ++Index)
		{
			FLoadResult& Result = Results[Index];
			if (!Result.IsSuccess())
			{
				continue;
			}
			if (bPreparationTimedOut)
			{
				Fail(Result, ELoadError::TimedOut,
				     TEXT("The scan timed out before the package set could be resolved."));
			}
			else if (ConflictingIds.Contains(Result.Package.ModId))
			{
				Fail(Result, ELoadError::Conflict, TEXT("More than one eligible archive declares this mod ID."));
			}
			else
			{
				Commit(Prepared[Index], Result);
			}
		}
		return Results;
	}

	void PublishCompletion(ELoadOrigin Origin, const FString& Path, TConstArrayView<FLoadResult> Results)
	{
#if WITH_EDITOR
		const uint64 PIESession = ActivePIESession.Load();
#else
		constexpr uint64 PIESession = 0;
#endif
		if (!IsInGameThread() || bLoading || bBroadcasting)
		{
			DeferredCompletions.Enqueue(
			    {Origin, Path, TArray<FLoadResult>(Results.GetData(), Results.Num()), PIESession});
			return;
		}
		DispatchCompletion(Origin, Path, Results, PIESession);
	}

	void DispatchCompletion(ELoadOrigin Origin, const FString& Path, TConstArrayView<FLoadResult> Results,
	                        [[maybe_unused]] uint64 PIESession)
	{
		TGuardValue<bool> Broadcasting(bBroadcasting, true);
#if WITH_EDITOR
		TGuardValue<uint64> Session(DispatchingPIESession, PIESession);
#endif
		BroadcastLoadCompleted(Origin, Path, Results);
	}

	bool TickCompletions(float)
	{
#if WITH_EDITOR
		if (bPIECleanupQueued && GEditor && !GEditor->IsPlayingSessionInEditor())
		{
			CleanupPIE();
		}
#endif
		if (bLoading || bBroadcasting || DeferredCompletions.IsEmpty())
		{
			return true;
		}
		TArray<FDeferredCompletion> Pending;
		FDeferredCompletion Completion;
		while (Pending.Num() < MaxCandidates && DeferredCompletions.Dequeue(Completion))
		{
			Pending.Add(MoveTemp(Completion));
		}
		for (const FDeferredCompletion& Item : Pending)
		{
			DispatchCompletion(Item.Origin, Item.Path, Item.Results, Item.PIESession);
		}
		return true;
	}

	bool CanLoad(const FLoadPolicy& Policy, FLoadResult& Result) const
	{
		if (!IsInGameThread())
		{
			return Fail(Result, ELoadError::WrongThread, TEXT("Call the mod loader on the game thread."));
		}
		if (bLoading)
		{
			return Fail(Result, ELoadError::Busy, TEXT("A mod load is already in progress."));
		}
		if (bNeedsRestart)
		{
			return Fail(Result, ELoadError::RestartRequired, TEXT("Restart the game before mounting more mods."));
		}
		bool bPIE = false;
#if WITH_EDITOR
		bPIE = IsPIEActive();
#endif
		return Private::ValidatePolicy(Policy, Result) && Private::CheckBackend(Policy.Backend, bPIE, Result);
	}

	static bool FindArchives(double Deadline, TArray<FString>& Paths, FLoadResult& Result)
	{
		IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
		int32 Visited = 0;
		bool bLimitReached = false;
		const bool bScanned =
		    Files.IsSymlink(*Result.Path) != ESymlinkResult::Symlink &&
		    Files.IterateDirectory(*Result.Path,
		                           [&Paths, &Visited, &bLimitReached, Deadline](const TCHAR* Path, bool bIsDirectory)
		                           {
			                           if (++Visited > MaxDirectoryEntries || FPlatformTime::Seconds() >= Deadline)
			                           {
				                           bLimitReached = Visited > MaxDirectoryEntries;
				                           return false;
			                           }
			                           if (!bIsDirectory &&
			                               FPaths::GetExtension(Path).Equals(TEXT("mod"), ESearchCase::IgnoreCase))
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
			if (FPlatformTime::Seconds() >= Deadline)
			{
				return Fail(Result, ELoadError::TimedOut, TEXT("The mod directory scan timed out."));
			}
			return Fail(Result, ELoadError::InputUnavailable, TEXT("The physical mod directory could not be scanned."));
		}
		Paths.Sort([](const FString& Left, const FString& Right)
		           { return Left.Compare(Right, ESearchCase::CaseSensitive) < 0; });
		return true;
	}

	void Commit(const FPreparedPackage& Prepared, FLoadResult& Result)
	{
		if (bNeedsRestart)
		{
			Fail(Result, ELoadError::RestartRequired, TEXT("Restart the game before mounting more mods."));
			return;
		}
		if (const FMountedRecord* Existing = Mounted.Find(Result.Package.ModId))
		{
			if (Existing->Package.ArchiveSha256 == Result.Package.ArchiveSha256)
			{
				Result.Package = Existing->Package;
				Result.bAlreadyMounted = true;
			}
			else
			{
				Fail(Result, ELoadError::Conflict, TEXT("A different archive with this mod ID is already mounted."));
			}
			return;
		}
		FMountedRecord Record;
		Private::FMountedResources* Resources = nullptr;
#if WITH_EDITOR
		if (!FPlatformProperties::RequiresCookedData())
		{
			if (!IsPIEActive())
			{
				Fail(Result, ELoadError::BackendUnavailable, TEXT("The PIE session ended before the mod could mount."));
				return;
			}
			Resources = &Record.PIEResources;
		}
#endif
		if (Private::MountPrepared(Prepared, Result, Resources))
		{
			Record.Package = Result.Package;
			Mounted.Add(Result.Package.ModId, MoveTemp(Record));
		}
		else if (Result.Error == ELoadError::RestartRequired)
		{
			bNeedsRestart = true;
		}
	}

	void LoadAtStartup()
	{
		if (!GConfig || !GConfig->GetBoolOrDefault(ConfigSection, TEXT("bLoadAtStartup"), true, GGameIni))
		{
			return;
		}
		FLoadPolicy Policy;
		GConfig->GetString(ConfigSection, TEXT("GameId"), Policy.GameId, GGameIni);
		GConfig->GetString(ConfigSection, TEXT("BaseReleaseVersion"), Policy.BaseReleaseVersion, GGameIni);
		GConfig->GetString(ConfigSection, TEXT("BaseRegistrySha256"), Policy.BaseRegistrySha256, GGameIni);
		GConfig->GetArray(ConfigSection, TEXT("TrustedPackageSha256"), Policy.TrustedPackageSha256, GGameIni);
		bool bEnforcePackageAllowlist = false;
		if (GConfig->GetBool(ConfigSection, TEXT("bEnforcePackageAllowlist"), bEnforcePackageAllowlist, GGameIni))
		{
			Policy.bEnforcePackageAllowlist = bEnforcePackageAllowlist;
		}
		GConfig->GetFloat(ConfigSection, TEXT("PreparationTimeoutSeconds"), Policy.PreparationTimeoutSeconds, GGameIni);
		const FString Directory =
		    FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Mods")));
		TArray<FLoadResult> Results;
		FString Backend;
		if (GConfig->GetString(ConfigSection, TEXT("Backend"), Backend, GGameIni))
		{
			if (Backend == TEXT("Pak"))
			{
				Policy.Backend = EBackend::Pak;
			}
			else if (Backend != TEXT("IoStore"))
			{
				FLoadResult& Result = Results.AddDefaulted_GetRef();
				Result.Path = Directory;
				Fail(Result, ELoadError::InvalidPolicy, TEXT("ModLoader.Backend must be Pak or IoStore."));
			}
		}
		if (Results.IsEmpty() && IPlatformFile::GetPlatformPhysical().DirectoryExists(*Directory))
		{
			Results = LoadDirectoryImpl(Directory, Policy);
		}
		for (const FLoadResult& Result : Results)
		{
			if (Result.IsSuccess())
			{
				UE_LOG(LogModLoader, Display, TEXT("Mounted mod %s %s with %d cooked packages."), *Result.Package.ModId,
				       *Result.Package.Version, Result.Package.PackageNames.Num());
			}
			else
			{
				UE_LOG(LogModLoader, Warning, TEXT("Mod load failed for '%s' (%d). %s"),
				       *FPaths::GetCleanFilename(Result.Path), static_cast<int32>(Result.Error), *Result.Detail);
			}
		}
#if WITH_EDITOR
		if (IsPIEActive())
		{
			PIEStartupResults = Results;
		}
#endif
		PublishCompletion(ELoadOrigin::Startup, Directory, Results);
		if (bNeedsRestart && FPlatformProperties::RequiresCookedData())
		{
			FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("ModLoader"));
		}
	}

#if WITH_EDITOR
	void BeginPIE(bool bSimulating)
	{
		if (bSimulating || IsPIEActive() || bLoading || bBroadcasting)
		{
			return;
		}
		if (bNeedsRestart)
		{
			UE_LOG(LogModLoader, Error, TEXT("Restart the editor before loading mods in PIE."));
			return;
		}
		if (!CleanupPIE())
		{
			return;
		}
		ActivePIESession.Store(++LastPIESession);
		LoadAtStartup();
	}

	void EndPIE(bool)
	{
		if (ActivePIESession.Exchange(0) != 0)
		{
			PIEStartupResults.Reset();
			bPIECleanupQueued = true;
		}
	}

	void ShutdownPIE(bool)
	{
		if (bPIECleanupQueued)
		{
			CleanupPIE();
		}
	}

	bool CleanupPIE()
	{
		if (bLoading || bBroadcasting)
		{
			return false;
		}
		bPIECleanupQueued = false;
		if (Mounted.IsEmpty())
		{
			return true;
		}
		TGuardValue<bool> Loading(bLoading, true);
		TArray<FName> PackageNames;
		for (const TPair<FString, FMountedRecord>& Entry : Mounted)
		{
			PackageNames.Append(Entry.Value.Package.PackageNames);
		}
		FString Reason;
		if (!Private::ReleasePIEPackages(PackageNames, Reason))
		{
			UE_LOG(LogModLoader, Error, TEXT("Could not unload PIE mods. %s"), *Reason);
			return false;
		}
		for (auto Entry = Mounted.CreateIterator(); Entry; ++Entry)
		{
			const FMountedRecord& Record = Entry.Value();
			if (Private::UnmountPIE(Record.Package, Record.PIEResources))
			{
				UE_LOG(LogModLoader, Display, TEXT("Unmounted PIE mod %s %s."), *Record.Package.ModId,
				       *Record.Package.Version);
				Entry.RemoveCurrent();
			}
			else
			{
				bNeedsRestart = true;
				UE_LOG(LogModLoader, Error, TEXT("Could not unmount PIE mod %s. Restart the editor."),
				       *Record.Package.ModId);
			}
		}
		return !bNeedsRestart;
	}

	FDelegateHandle BeginPIEHandle;
	FDelegateHandle EndPIEHandle;
	FDelegateHandle ShutdownPIEHandle;
	TArray<FLoadResult> PIEStartupResults;
	TAtomic<uint64> ActivePIESession{0};
	uint64 LastPIESession = 0;
	uint64 DispatchingPIESession = 0;
	bool bPIECleanupQueued = false;
#endif

	FDelegateHandle StartupHandle;
	FTSTicker::FDelegateHandle CompletionTicker;
	TMap<FString, FMountedRecord> Mounted;
	TQueue<FDeferredCompletion, EQueueMode::Mpsc> DeferredCompletions;
	bool bLoading = false;
	bool bBroadcasting = false;
	bool bNeedsRestart = false;
};

#if WITH_EDITOR
namespace Private
{
bool IsPIEActive()
{
	return static_cast<FModLoaderModule&>(IModLoaderModule::Get()).IsPIEActive();
}

bool IsCurrentPIECompletion()
{
	return static_cast<FModLoaderModule&>(IModLoaderModule::Get()).IsCurrentPIECompletion();
}

TArray<FLoadResult> GetPIEStartupResults()
{
	return static_cast<FModLoaderModule&>(IModLoaderModule::Get()).GetPIEStartupResults();
}
} // namespace Private
#endif
} // namespace Mod::Loader

IMPLEMENT_MODULE(Mod::Loader::FModLoaderModule, ModLoader);
