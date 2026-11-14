// Copyright (c) 2026 Nelaric Contributors
#include "ModLoader.h"

#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformProperties.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "ModDirectoryWatch.h"
#include "ModPackageArchive.h"
#include "ModPackageMount.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleManifest.h"
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
	TSharedPtr<const FMountedPackage> Lease;
	Private::FMountedResources Resources;
	FString SourcePath;
	bool bUnloadNotified = false;
	bool bSourceRemoved = false;
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
			WorldTearDownHandle =
			    FWorldDelegates::OnWorldBeginTearDown.AddRaw(this, &FModLoaderModule::BeginGameShutdown);
		}
#if WITH_EDITOR
		else if (GIsEditor && !IsRunningCommandlet())
		{
#if PLATFORM_WINDOWS
			FString Backend;
			if (GConfig && GConfig->GetString(ConfigSection, TEXT("Backend"), Backend, GGameIni) &&
			    Backend == TEXT("Pak"))
			{
				Private::InitializePIEPakPlatform();
			}
#endif
			BeginPIEHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FModLoaderModule::BeginPIE);
			PrePIEEndedHandle = FEditorDelegates::PrePIEEnded.AddRaw(this, &FModLoaderModule::PrePIEEnded);
			EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FModLoaderModule::EndPIE);
			ShutdownPIEHandle = FEditorDelegates::ShutdownPIE.AddRaw(this, &FModLoaderModule::ShutdownPIE);
		}
#endif
	}

	void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.Remove(StartupHandle);
		FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDownHandle);
#if WITH_EDITOR
		FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
		FEditorDelegates::PrePIEEnded.Remove(PrePIEEndedHandle);
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		FEditorDelegates::ShutdownPIE.Remove(ShutdownPIEHandle);
#endif
		FTSTicker::RemoveTicker(CompletionTicker);
		DirectoryWatch.Shutdown();
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
				if (!Entry.Value.bUnloadNotified && !Entry.Value.bSourceRemoved)
				{
					Result.Add(Entry.Value.Package);
				}
			}
			Result.Sort([](const FMountedPackage& Left, const FMountedPackage& Right)
			            { return Left.ModId < Right.ModId; });
		}
		return Result;
	}

	TSharedPtr<const FMountedPackage> PinMountedPackage(const FString& ModId) override
	{
		check(IsInGameThread());
		auto* Record = Mounted.Find(ModId);
		if (!Record || Record->bUnloadNotified || Record->bSourceRemoved)
		{
			return nullptr;
		}
		if (!Record->Lease)
		{
			Record->Lease = MakeShared<FMountedPackage>(Record->Package);
		}
		return Record->Lease;
	}

	TArray<FLoadResult> GetStartupResults() const override
	{
		check(IsInGameThread());
		return StartupResults;
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
			if (PrepareArchive(Policy, {Deadline}, Result, Prepared))
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

	TArray<FLoadResult> LoadDirectoryImpl(const FString& Directory, const FLoadPolicy& Policy, bool bStartup = false)
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
		if (!Private::FindArchives({Deadline}, Paths, ScanResult))
		{
			return {MoveTemp(ScanResult)};
		}
		if (bStartup)
		{
			DirectoryWatch.RecordStartupPaths(Paths);
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
			else if (PrepareArchive(Policy, {Deadline}, Result, Prepared[Index]))
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
		if (bLoading || bBroadcasting)
		{
			return true;
		}
		ReleaseRemovedPackages();
		TickWatchedDirectory();
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

	void TickWatchedDirectory()
	{
		if (bNeedsRestart)
		{
			DirectoryWatch.Stop();
			return;
		}
		Private::FWatchedChanges Changes = DirectoryWatch.Tick(
		    [this]
		    {
			    TMap<FString, FString> Sources;
			    for (const TPair<FString, FMountedRecord>& Entry : Mounted)
			    {
				    Sources.Add(Entry.Key, Entry.Value.SourcePath);
			    }
			    return Sources;
		    });
		TArray<FLoadResult> Results;
		{
			TGuardValue<bool> Loading(bLoading, true);
			UnloadRemovedSources(Changes.RemovedSources);
			for (Private::FWatchedPackage& Item : Changes.Packages)
			{
				if (DirectoryWatch.GetDirectory().IsEmpty())
				{
					break;
				}
				FLoadResult& Result = Item.Result;
				if (Item.Prepared.bSkipped || Mounted.Contains(Result.Package.ModId))
				{
					UE_LOG(LogModLoader, Verbose, TEXT("Skipped watched package '%s' for mounted mod %s."),
					       *FPaths::GetCleanFilename(Result.Path), *Result.Package.ModId);
					continue;
				}
				if (Result.IsSuccess())
				{
					Commit(Item.Prepared, Result);
				}
				Results.Add(MoveTemp(Result));
			}
		}
		if (!Results.IsEmpty())
		{
			LogLoadResults(Results);
			const FString Directory = DirectoryWatch.GetDirectory();
			PublishCompletion(ELoadOrigin::DirectoryWatch, Directory, Results);
		}
	}

	void NotifyBeforeUnload(FMountedRecord& Record)
	{
		if (!Record.bUnloadNotified)
		{
			Record.bUnloadNotified = true;
			BroadcastBeforeUnload(Record.Package);
		}
	}

	bool Unmount(FMountedRecord& Record)
	{
		if (Private::UnmountPackage(Record.Package, Record.Resources))
		{
			UE_LOG(LogModLoader, Display, TEXT("Unmounted mod %s %s."), *Record.Package.ModId, *Record.Package.Version);
			return true;
		}
		bNeedsRestart = true;
		UE_LOG(LogModLoader, Error, TEXT("Could not unmount mod %s. Restart before loading more mods."),
		       *Record.Package.ModId);
		return false;
	}

	void UnloadRemovedSources(const TMap<FString, FString>& RemovedSources)
	{
		IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
		for (const TPair<FString, FString>& Entry : RemovedSources)
		{
			FMountedRecord* Record = Mounted.Find(Entry.Key);
			if (Record && FPaths::IsSamePath(Record->SourcePath, Entry.Value) && !Files.FileExists(*Entry.Value))
			{
				if (!Record->bSourceRemoved)
				{
					Record->bSourceRemoved = true;
					if (Record->Lease && !Record->Lease.IsUnique())
					{
						UE_LOG(LogModLoader, Display, TEXT("Mod %s remains mounted until active World leases end."),
						       *Entry.Key);
					}
				}
			}
		}
		ReleaseRemovedPackages();
	}

	void ReleaseRemovedPackages()
	{
		TGuardValue<bool> Loading(bLoading, true);
		TArray<FString> Unloading;
		for (auto& [ModId, Record] : Mounted)
		{
			if (Record.bSourceRemoved && (!Record.Lease || Record.Lease.IsUnique()))
			{
				NotifyBeforeUnload(Record);
				Unloading.Add(ModId);
			}
		}
		for (const FString& ModId : Unloading)
		{
			FMountedRecord& Record = Mounted.FindChecked(ModId);
			FString Reason;
			if (!Private::ReleaseModPackages(Record.Package.PackageNames, Reason))
			{
				UE_LOG(LogModLoader, Warning, TEXT("Mod %s is still mounted. %s"), *ModId, *Reason);
				Record.bSourceRemoved = false;
			}
			else if (Unmount(Record))
			{
				Mounted.Remove(ModId);
				DirectoryWatch.RequestRescan();
			}
		}
	}

	void NotifySessionEnding()
	{
		DirectoryWatch.Stop();
		TGuardValue<bool> Loading(bLoading, true);
		for (TPair<FString, FMountedRecord>& Entry : Mounted)
		{
			NotifyBeforeUnload(Entry.Value);
		}
	}

	void BeginGameShutdown(UWorld* World)
	{
		if (IsEngineExitRequested() && World->IsGameWorld())
		{
			NotifySessionEnding();
		}
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

	bool CheckNativeCode(const Private::FNativeModuleSet& Code, FLoadResult& Result) const
	{
		if (Code.Modules.IsEmpty())
		{
			return true;
		}
		if (const FString* Resident = ResidentNativeArchives.Find(Result.Package.ModId))
		{
			return *Resident == Result.Package.ArchiveSha256 ||
			       Fail(Result, ELoadError::RestartRequired, TEXT("Restart before replacing a resident UE module."));
		}
		FModuleManifest HostManifest;
		FModuleManifest ModManifest;
		const FString ExpectedManifest = FModuleManifest::GetFileName(Code.Directory, true);
		if (FPaths::GetCleanFilename(ExpectedManifest) != Code.ManifestName ||
		    !FModuleManifest::TryRead(FModuleManifest::GetFileName(FPlatformProcess::GetModulesDirectory(), false),
		                              HostManifest) ||
		    !FModuleManifest::TryRead(ExpectedManifest, ModManifest) || Code.BuildId != HostManifest.BuildId ||
		    Code.BuildId != ModManifest.BuildId || Code.Modules.Num() != ModManifest.ModuleNameToFileName.Num())
		{
			return Fail(Result, ELoadError::Incompatible,
			            TEXT("The UE module manifest does not match the host build."));
		}
		const FModuleManager& Manager = FModuleManager::Get();
		for (const Private::FNativeModule& Module : Code.Modules)
		{
			const FString* FileName = ModManifest.ModuleNameToFileName.Find(Module.Name);
			if (!FileName || *FileName != Module.FileName)
			{
				return Fail(Result, ELoadError::InvalidArchive,
				            TEXT("A UE module is missing from the binary manifest."));
			}
			if (Manager.IsModuleLoaded(FName(*Module.Name)) || Manager.ModuleExists(*Module.Name))
			{
				return Fail(
				    Result, ELoadError::Conflict,
				    FString::Printf(TEXT("UE module '%s' is already supplied by another package."), *Module.Name));
			}
		}
		return true;
	}

	bool LoadNativeCode(const Private::FNativeModuleSet& Code, FLoadResult& Result)
	{
		if (Code.Modules.IsEmpty() || ResidentNativeArchives.Contains(Result.Package.ModId))
		{
			return true;
		}
		FModuleManager& Manager = FModuleManager::Get();
		Manager.AddBinariesDirectory(*Code.Directory, true);
		for (const Private::FNativeModule& Module : Code.Modules)
		{
			FString ModuleFile;
			if (!Manager.ModuleExists(*Module.Name, &ModuleFile) ||
			    !FPaths::IsSamePath(ModuleFile, FPaths::Combine(Code.Directory, Module.FileName)))
			{
				return Fail(Result, ELoadError::RestartRequired,
				            FString::Printf(TEXT("UE did not discover '%s' at the verified DLL path."), *Module.Name));
			}
		}
		for (const Private::FNativeModule& Module : Code.Modules)
		{
			EModuleLoadResult Failure = EModuleLoadResult::Success;
			if (!Manager.LoadModuleWithFailureReason(FName(*Module.Name), Failure, ELoadModuleFlags::LogFailures))
			{
				return Fail(Result, ELoadError::RestartRequired,
				            FString::Printf(TEXT("Could not load UE module '%s' (module error %d)."), *Module.Name,
				                            static_cast<int32>(Failure)));
			}
			UE_LOG(LogModLoader, Display, TEXT("Loaded UE module %s for mod %s."), *Module.Name, *Result.Package.ModId);
		}
		ResidentNativeArchives.Add(Result.Package.ModId, Result.Package.ArchiveSha256);
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
			if (Existing->bUnloadNotified)
			{
				Fail(Result, ELoadError::Busy, TEXT("This mod is awaiting unload."));
			}
			else if (Existing->Package.ArchiveSha256 == Result.Package.ArchiveSha256)
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
#if WITH_EDITOR
		if (!FPlatformProperties::RequiresCookedData() && !IsPIEActive())
		{
			Fail(Result, ELoadError::BackendUnavailable, TEXT("The PIE session ended before the mod could mount."));
			return;
		}
#endif
		if (!CheckNativeCode(Prepared.NativeCode, Result))
		{
			return;
		}
		if (Private::MountPrepared(Prepared, Result, Record.Resources,
		                           [this, &Prepared, &Result] { return LoadNativeCode(Prepared.NativeCode, Result); }))
		{
			Record.Package = Result.Package;
			Record.SourcePath = Result.Path;
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
		GConfig->GetBool(ConfigSection, TEXT("bAllowNativeModules"), Policy.bAllowNativeModules, GGameIni);
		bool bEnforcePackageAllowlist = false;
		if (GConfig->GetBool(ConfigSection, TEXT("bEnforcePackageAllowlist"), bEnforcePackageAllowlist, GGameIni))
		{
			Policy.bEnforcePackageAllowlist = bEnforcePackageAllowlist;
		}
		GConfig->GetFloat(ConfigSection, TEXT("PreparationTimeoutSeconds"), Policy.PreparationTimeoutSeconds, GGameIni);
		const FString BinariesDirectory =
		    FPlatformProperties::RequiresCookedData()
		        ? FPaths::GetPath(FPlatformProcess::ExecutablePath())
		        : FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(BinariesDirectory, TEXT("Mods")));
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
		if (Results.IsEmpty() && GConfig->GetBoolOrDefault(ConfigSection, TEXT("bWatchForChanges"), true, GGameIni))
		{
			FLoadResult WatchResult;
			if (CanLoad(Policy, WatchResult))
			{
				DirectoryWatch.Start(Directory, Policy);
			}
		}
		if (Results.IsEmpty() && IPlatformFile::GetPlatformPhysical().DirectoryExists(*Directory))
		{
			Results = LoadDirectoryImpl(Directory, Policy, true);
		}
		LogLoadResults(Results);
		StartupResults = Results;
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

	static void LogLoadResults(TConstArrayView<FLoadResult> Results)
	{
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
			DirectoryWatch.Stop();
			PIEStartupResults.Reset();
			StartupResults.Reset();
			bPIECleanupQueued = true;
		}
	}

	void PrePIEEnded(bool)
	{
		if (IsPIEActive())
		{
			NotifySessionEnding();
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
		for (const auto& [ModId, Record] : Mounted)
		{
			if (Record.Lease && !Record.Lease.IsUnique())
			{
				return false;
			}
		}
		bPIECleanupQueued = false;
		if (Mounted.IsEmpty())
		{
			return true;
		}
		TGuardValue<bool> Loading(bLoading, true);
		TArray<FName> PackageNames;
		for (TPair<FString, FMountedRecord>& Entry : Mounted)
		{
			NotifyBeforeUnload(Entry.Value);
			PackageNames.Append(Entry.Value.Package.PackageNames);
		}
		FString Reason;
		if (!Private::ReleaseModPackages(PackageNames, Reason))
		{
			UE_LOG(LogModLoader, Error, TEXT("Could not unload PIE mods. %s"), *Reason);
			return false;
		}
		for (auto Entry = Mounted.CreateIterator(); Entry; ++Entry)
		{
			if (Unmount(Entry.Value()))
			{
				Entry.RemoveCurrent();
			}
		}
		return !bNeedsRestart;
	}

	FDelegateHandle BeginPIEHandle;
	FDelegateHandle PrePIEEndedHandle;
	FDelegateHandle EndPIEHandle;
	FDelegateHandle ShutdownPIEHandle;
	TArray<FLoadResult> PIEStartupResults;
	TAtomic<uint64> ActivePIESession{0};
	uint64 LastPIESession = 0;
	uint64 DispatchingPIESession = 0;
	bool bPIECleanupQueued = false;
#endif

	FDelegateHandle StartupHandle;
	FDelegateHandle WorldTearDownHandle;
	FTSTicker::FDelegateHandle CompletionTicker;
	Private::FModDirectoryWatch DirectoryWatch;
	TArray<FLoadResult> StartupResults;
	TMap<FString, FMountedRecord> Mounted;
	// Registered UClasses can keep native function pointers after their content is unmounted.
	TMap<FString, FString> ResidentNativeArchives;
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
