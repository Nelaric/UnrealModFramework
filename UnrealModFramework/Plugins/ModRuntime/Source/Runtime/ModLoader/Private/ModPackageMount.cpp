// Copyright (c) 2026 Nelaric Contributors
#include "ModPackageMount.h"

#include "Algo/AllOf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProperties.h"
#include "IO/IoContainerHeader.h"
#include "IO/IoDispatcher.h"
#include "IO/PackageStore.h"
#include "IPlatformFilePak.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Serialization/MemoryReader.h"
#include "ShaderCodeLibrary.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectHash.h"
#endif

#if WITH_EDITOR && PLATFORM_WINDOWS
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace Mod::Loader::Private
{
namespace
{
constexpr int32 MaxNativeFiles = 262144;
constexpr int64 MaxRegistrySize = 64 * 1024 * 1024;
constexpr uint64 MaxContainerHeaderSize = 32 * 1024 * 1024;

struct FContainerMount
{
	FString PakPath;
	FString VirtualMountPoint;
	FIoChunkId HeaderChunk = FIoChunkId::InvalidChunkId;
};

struct FMountPlan
{
	TArray<FContainerMount> Containers;
	TSet<FString> FilePaths;
	TSet<FName> Packages;
	TSet<FIoChunkId> IoChunks;
	FString RegistryPath;
	bool bHasShaderLibrary = false;
};

FPakPlatformFile* GetPakPlatform()
{
	return static_cast<FPakPlatformFile*>(FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")));
}

bool UnmountContainer(FPakPlatformFile& Platform, const FString& PakPath, FIoChunkId HeaderChunk)
{
	return Platform.Unmount(*PakPath) && (!HeaderChunk.IsValid() || !FIoDispatcher::Get().DoesChunkExist(HeaderChunk));
}

bool ReadNativeMountPoint(FString Path, FString& OutRelative)
{
	FPaths::NormalizeFilename(Path);
	// UAT stages mounted filenames relative to the build root.
	Path.RemoveFromStart(TEXT("../../../"), ESearchCase::CaseSensitive);
	Path.RemoveFromEnd(TEXT("/"), ESearchCase::CaseSensitive);
	if (!Path.IsEmpty() && !IsSafeRelativePath(Path))
	{
		return false;
	}
	OutRelative = MoveTemp(Path);
	return true;
}

bool RecordNativeFile(const FPreparedPackage& Package, const FString& Path, FMountPlan& Plan, FName& OutPackage)
{
	const bool bInContent = Path.StartsWith(Package.ContentRelativePath, ESearchCase::IgnoreCase);
	const FString PluginRegistry = Package.ContentRelativePath.LeftChop(8) + TEXT("AssetRegistry.bin");
	if (!IsSafeRelativePath(Path) || (!bInContent && !Path.Equals(PluginRegistry, ESearchCase::IgnoreCase)) ||
	    Plan.FilePaths.Num() >= MaxNativeFiles || Plan.FilePaths.Contains(Path.ToLower()))
	{
		return false;
	}
	const FString Extension = FPaths::GetExtension(Path).ToLower();
	if (Extension == TEXT("pak") || Extension == TEXT("utoc") || Extension == TEXT("ucas") ||
	    Extension == TEXT("sig") || Extension == TEXT("dll") || Extension == TEXT("so") || Extension == TEXT("dylib"))
	{
		return false;
	}
	Plan.FilePaths.Add(Path.ToLower());
	if (FPaths::GetCleanFilename(Path).Equals(TEXT("AssetRegistry.bin"), ESearchCase::IgnoreCase))
	{
		if (!Plan.RegistryPath.IsEmpty())
		{
			return false;
		}
		Plan.RegistryPath = FPaths::Combine(Package.StageRoot, Path);
	}
	if (Extension == TEXT("ushaderbytecode") || Extension == TEXT("ushadercode") || Extension == TEXT("metalmap"))
	{
		Plan.bHasShaderLibrary = true;
	}
	if (Extension == TEXT("uasset") || Extension == TEXT("umap"))
	{
		const FString LongName = TEXT("/") + Package.PluginName + TEXT("/") +
		                         FPaths::ChangeExtension(Path.Mid(Package.ContentRelativePath.Len()), TEXT(""));
		if (!FPackageName::IsValidTextForLongPackageName(LongName))
		{
			return false;
		}
		OutPackage = FName(*LongName);
		if (Plan.Packages.Contains(OutPackage))
		{
			return false;
		}
		Plan.Packages.Add(OutPackage);
	}
	return true;
}

bool InspectPak(const FPreparedPackage& Package, const FNativeContainer& Container, FPakPlatformFile& Platform,
                FMountPlan& Plan, FContainerMount& OutMount, FLoadResult& Result)
{
	const bool bSigned = FCoreDelegates::GetPakSigningKeysDelegate().IsBound();
	const FString Signature = FPaths::ChangeExtension(Container.PakPath, TEXT("sig"));
	if (bSigned && !IPlatformFile::GetPlatformPhysical().FileExists(*Signature))
	{
		return Fail(Result, ELoadError::InvalidContainer, TEXT("The host requires a native Pak signature."));
	}
	TRefCountPtr<FPakFile> Pak = MakeRefCount<FPakFile>(Platform.GetLowerLevel(), *Container.PakPath, bSigned, true);
	FString RelativeMount;
	if (!Pak->IsValid() || (bSigned && !Pak->PassedSignatureChecks()) || Pak->GetInfo().bEncryptedIndex ||
	    Pak->GetInfo().EncryptionKeyGuid.IsValid() || Pak->GetNumFiles() > MaxNativeFiles ||
	    Container.PakPath.EndsWith(TEXT("_P.pak"), ESearchCase::IgnoreCase) ||
	    !ReadNativeMountPoint(Pak->GetMountPoint(), RelativeMount))
	{
		return Fail(Result, ELoadError::InvalidContainer, TEXT("A Pak is invalid, encrypted, or uses a patch mount."));
	}
	for (FPakFile::FPakEntryIterator It(*Pak, true); It; ++It)
	{
		const FString* Name = It.TryGetFilename();
		FName PackageName;
		if (!Name || !IsSafeRelativePath(*Name) || It.Info().IsDeleteRecord() || It.Info().IsEncrypted() ||
		    !RecordNativeFile(Package, FPaths::Combine(RelativeMount, *Name), Plan, PackageName) ||
		    (Result.Package.Backend == EBackend::IoStore && !PackageName.IsNone()))
		{
			return Fail(Result, ELoadError::InvalidContainer,
			            TEXT("A Pak entry is ambiguous, encrypted, deleted, or outside the mod's cooked layout."));
		}
	}
	OutMount.PakPath = Container.PakPath;
	OutMount.VirtualMountPoint = FPaths::CreateStandardFilename(FPaths::Combine(Package.StageRoot, RelativeMount));
	return true;
}

bool InspectIoStore(const FPreparedPackage& Package, const FNativeContainer& Container, FMountPlan& Plan,
                    FContainerMount& OutMount, FLoadResult& Result)
{
	FIoStoreReader Reader;
	const FIoStatus OpenStatus = Reader.Initialize(FPaths::ChangeExtension(Container.TocPath, TEXT("")), {});
	FString RelativeMount;
	if (!OpenStatus.IsOk() || !Reader.GetContainerId().IsValid() || Reader.GetChunkCount() > MaxNativeFiles ||
	    !EnumHasAnyFlags(Reader.GetContainerFlags(), EIoContainerFlags::Indexed) ||
	    EnumHasAnyFlags(Reader.GetContainerFlags(), EIoContainerFlags::Encrypted | EIoContainerFlags::OnDemand) ||
	    !ReadNativeMountPoint(Reader.GetDirectoryIndexReader().GetMountPoint(), RelativeMount))
	{
		return Fail(Result, ELoadError::InvalidContainer,
		            TEXT("The IoStore table must be readable, indexed, and unencrypted."));
	}
	OutMount.HeaderChunk = CreateIoChunkId(Reader.GetContainerId().Value(), 0, EIoChunkType::ContainerHeader);
	TIoStatusOr<FIoStoreTocChunkInfo> HeaderInfo = Reader.GetChunkInfo(OutMount.HeaderChunk);
	if (!HeaderInfo.IsOk() || HeaderInfo.ValueOrDie().Size > MaxContainerHeaderSize)
	{
		return Fail(Result, ELoadError::InvalidContainer,
		            TEXT("The IoStore container header is missing or oversized."));
	}
	TIoStatusOr<FIoBuffer> HeaderData = Reader.Read(OutMount.HeaderChunk, FIoReadOptions());
	if (!HeaderData.IsOk())
	{
		return Fail(Result, ELoadError::InvalidContainer, TEXT("The IoStore container header could not be read."));
	}
	const FIoBuffer& Bytes = HeaderData.ValueOrDie();
	FMemoryReaderView HeaderReader(FMemoryView(Bytes.Data(), Bytes.DataSize()));
	FIoContainerHeader Header;
	HeaderReader << Header;
	if (HeaderReader.IsError() || Header.ContainerId != Reader.GetContainerId() ||
	    !Header.OptionalSegmentPackageIds.IsEmpty() || !Header.PackageRedirects.IsEmpty() ||
	    !Header.LocalizedPackages.IsEmpty() || Header.PackageIds.Num() > MaxNativeFiles)
	{
		return Fail(Result, ELoadError::InvalidContainer,
		            TEXT("The IoStore header has invalid package IDs, redirects, or optional packages."));
	}
	TSet<FPackageId> ListedIds;
	for (FPackageId Id : Header.PackageIds)
	{
		ListedIds.Add(Id);
	}
	TSet<FPackageId> Exports;
	Reader.EnumerateChunks(
	    [&Package, &Plan, &Result, &Exports, &ListedIds](FIoStoreTocChunkInfo&& Chunk)
	    {
		    if (Chunk.ChunkType == EIoChunkType::ShaderCode)
		    {
			    // Shader code chunks can be shared by content hash.
			    return true;
		    }
		    if (Plan.IoChunks.Num() >= MaxNativeFiles || Plan.IoChunks.Contains(Chunk.Id) ||
		        FIoDispatcher::Get().DoesChunkExist(Chunk.Id))
		    {
			    return Fail(Result, ELoadError::Conflict, TEXT("An IoStore chunk is already present."));
		    }
		    Plan.IoChunks.Add(Chunk.Id);
		    if (Chunk.ChunkType == EIoChunkType::ScriptObjects)
		    {
			    return Fail(Result, ELoadError::InvalidContainer, TEXT("Mods cannot replace the script object table."));
		    }
		    if (Chunk.ChunkType == EIoChunkType::ShaderCodeLibrary)
		    {
			    Plan.bHasShaderLibrary = true;
		    }
		    if (Chunk.ChunkType == EIoChunkType::ExportBundleData)
		    {
			    FName PackageName;
			    // The directory index returns the full staged filename, including its mount point.
			    Chunk.FileName.RemoveFromStart(TEXT("../../../"), ESearchCase::CaseSensitive);
			    if (!Chunk.bHasValidFileName || !RecordNativeFile(Package, Chunk.FileName, Plan, PackageName) ||
			        PackageName.IsNone())
			    {
				    return Fail(Result, ELoadError::InvalidContainer,
				                TEXT("An IoStore package is outside the mod root."));
			    }
			    const FPackageId Id = FPackageId::FromName(PackageName);
			    if (!ListedIds.Contains(Id) ||
			        Chunk.Id != CreateIoChunkId(Id.Value(), 0, EIoChunkType::ExportBundleData))
			    {
				    return Fail(Result, ELoadError::InvalidContainer,
				                TEXT("An IoStore export does not match its cooked package identity."));
			    }
			    Exports.Add(Id);
		    }
		    return true;
	    });
	if (!Result.IsSuccess())
	{
		return false;
	}
	if (ListedIds.Num() != Header.PackageIds.Num() || Exports.Num() != ListedIds.Num())
	{
		return Fail(Result, ELoadError::InvalidContainer,
		            TEXT("The IoStore package table disagrees with its directory."));
	}
	FPackageStore& Store = FPackageStore::Get();
	FPackageStoreReadScope ReadScope(Store);
	for (FPackageId Id : ListedIds)
	{
		FPackageStoreEntry Entry;
		const EPackageStoreEntryStatus Status = Store.GetPackageStoreEntry(Id, NAME_None, Entry);
		if (Status != EPackageStoreEntryStatus::Missing && Status != EPackageStoreEntryStatus::None)
		{
			return Fail(Result, ELoadError::Conflict, TEXT("An IoStore package identity already exists in the host."));
		}
	}
	return true;
}

bool LoadRegistry(const FMountPlan& Plan, FLoadResult& Result)
{
	if (Plan.RegistryPath.IsEmpty())
	{
		return true;
	}
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Plan.RegistryPath));
	FAssetRegistryState State;
	FAssetRegistryLoadOptions Options;
	Options.bLoadDependencies = false;
	Options.bLoadPackageData = false;
	if (!File || File->TotalSize() <= 0 || File->TotalSize() > MaxRegistrySize || !State.Load(*File, Options) ||
	    File->IsError() || State.GetNumAssets() > MaxNativeFiles)
	{
		return Fail(Result, ELoadError::MountFailed, TEXT("The cooked asset registry is invalid or oversized."));
	}
	bool bHasForeignAssets = false;
	State.EnumerateAllAssets([&Plan, &bHasForeignAssets](const FAssetData& Asset)
	                         { bHasForeignAssets |= !Plan.Packages.Contains(Asset.PackageName); });
	if (bHasForeignAssets)
	{
		return Fail(Result, ELoadError::MountFailed, TEXT("The cooked asset registry contains foreign assets."));
	}
	FAssetRegistryModule* Module = FModuleManager::LoadModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (!Module)
	{
		return Fail(Result, ELoadError::MountFailed, TEXT("The runtime asset registry is unavailable."));
	}
	Module->Get().AppendState(State);
	return true;
}

bool VerifyMountedPackages(const FMountPlan& Plan, FLoadResult& Result)
{
	if (Result.Package.Backend == EBackend::IoStore)
	{
		FPackageStore& Store = FPackageStore::Get();
		FPackageStoreReadScope ReadScope(Store);
		for (FName Name : Plan.Packages)
		{
			FPackageStoreEntry Entry;
			const FPackageId Id = FPackageId::FromName(Name);
			if (Store.GetPackageStoreEntry(Id, Name, Entry) != EPackageStoreEntryStatus::Ok ||
			    !FIoDispatcher::Get().DoesChunkExist(CreateIoChunkId(Id.Value(), 0, EIoChunkType::ExportBundleData)))
			{
				return Fail(Result, ELoadError::MountFailed,
				            FString::Printf(TEXT("The IoStore package is not registered: %s"), *Name.ToString()));
			}
		}
		return true;
	}
	for (FName Name : Plan.Packages)
	{
		if (!FPackageName::DoesPackageExist(Name.ToString()))
		{
			return Fail(Result, ELoadError::MountFailed,
			            FString::Printf(TEXT("The mounted package is not visible: %s"), *Name.ToString()));
		}
	}
	return true;
}
} // namespace

#if WITH_EDITOR && PLATFORM_WINDOWS
void InitializePIEPakPlatform()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	if (!GAllowCookedDataInEditorBuilds || GetPakPlatform() || FParse::Param(CommandLine, TEXT("NoPak")))
	{
		return;
	}

	FPlatformFileManager& Files = FPlatformFileManager::Get();
	IPlatformFile* PakFile = Files.GetPlatformFile(FPakPlatformFile::GetTypeName());
	if (PakFile && PakFile->Initialize(&Files.GetPlatformFile(), CommandLine))
	{
		PakFile->InitializeNewAsyncIO();
		Files.SetPlatformFile(*PakFile);
	}
}
#endif

bool CheckBackend(EBackend Backend, bool bPIE, FLoadResult& Result)
{
#if WITH_EDITOR
	if (bPIE)
	{
		if (!PLATFORM_WINDOWS || !GAllowCookedDataInEditorBuilds)
		{
			return Fail(
			    Result, ELoadError::BackendUnavailable,
			    TEXT("PIE .mod loading requires a Windows editor with Allow Cooked Content in the Editor enabled."));
		}
		if (!GetPakPlatform())
		{
			return Fail(Result, ELoadError::BackendUnavailable,
			            TEXT("Launch the editor with -UsePaks to load .mod in PIE."));
		}
	}
#endif
	if ((!bPIE && !FPlatformProperties::RequiresCookedData()) || !GetPakPlatform())
	{
		return Fail(Result, ELoadError::BackendUnavailable,
		            TEXT("Mod loading requires a cooked host with Pak support."));
	}
	const bool bUsesIoStore = FIoDispatcher::IsInitialized() && FPackageStore::Get().HasAnyBackendsMounted();
	if (bUsesIoStore != (Backend == EBackend::IoStore))
	{
		return Fail(Result, ELoadError::BackendUnavailable,
		            bPIE ? TEXT("The configured backend does not match the editor. Use -UsePaks for Pak or -UsePaks "
		                        "-UseIoStore for IoStore.")
		                 : TEXT("The configured backend does not match the cooked host."));
	}
	return true;
}

bool MountPrepared(const FPreparedPackage& Package, FLoadResult& Result, FMountedResources& Resources,
                   TFunctionRef<bool()> LoadCode)
{
	if (FPackageName::MountPointExists(Result.Package.PackageRoot))
	{
		return Fail(Result, ELoadError::Conflict, TEXT("The cooked package root is already registered."));
	}
	FPakPlatformFile* Platform = GetPakPlatform();
	if (!Platform)
	{
		return Fail(Result, ELoadError::BackendUnavailable, TEXT("The runtime Pak platform file is unavailable."));
	}
	FMountPlan Plan;
	for (const FNativeContainer& Container : Package.Containers)
	{
		FContainerMount& Mount = Plan.Containers.AddDefaulted_GetRef();
		if (!InspectPak(Package, Container, *Platform, Plan, Mount, Result) ||
		    (!Container.TocPath.IsEmpty() && !InspectIoStore(Package, Container, Plan, Mount, Result)))
		{
			return false;
		}
	}
	if (Plan.Packages.IsEmpty())
	{
		return Fail(Result, ELoadError::InvalidContainer, TEXT("The mod contains no cooked packages."));
	}
	for (FName Name : Plan.Packages)
	{
		if (FindPackage(nullptr, *Name.ToString()))
		{
			return Fail(Result, ELoadError::Conflict,
			            FString::Printf(TEXT("A package with the mod's name is already loaded: %s"), *Name.ToString()));
		}
	}
	for (const FString& ClassPath : Result.Package.LifecycleClasses)
	{
		if (!Plan.Packages.Contains(FName(*FPackageName::ObjectPathToPackageName(ClassPath))))
		{
			return Fail(Result, ELoadError::InvalidContainer,
			            FString::Printf(TEXT("A lifecycle Blueprint package was not cooked: %s"), *ClassPath));
		}
	}
	const FString ContentDirectory = FPaths::Combine(Package.StageRoot, Package.ContentRelativePath);
	int32 MountedCount = 0;
	bool bCommitted = false;
	bool bShaderOpened = false;
	FPackageName::RegisterMountPoint(Result.Package.PackageRoot, ContentDirectory);
	ON_SCOPE_EXIT
	{
		if (!bCommitted && Result.Error != ELoadError::RestartRequired)
		{
			if (bShaderOpened)
			{
				FShaderCodeLibrary::CloseLibrary(Package.PluginName);
			}
			bool bRolledBack = true;
			for (int32 Index = MountedCount - 1; Index >= 0; --Index)
			{
				const FContainerMount& Container = Plan.Containers[Index];
				bRolledBack = UnmountContainer(*Platform, Container.PakPath, Container.HeaderChunk) && bRolledBack;
			}
			if (bRolledBack)
			{
				FPackageName::UnRegisterMountPoint(Result.Package.PackageRoot, ContentDirectory);
			}
			else
			{
				Fail(Result, ELoadError::RestartRequired, TEXT("A partial container mount could not be rolled back."));
			}
		}
	};
	for (const FContainerMount& Container : Plan.Containers)
	{
		FPakMountArgs Args;
		Args.PakFilename = *Container.PakPath;
		Args.Path = *Container.VirtualMountPoint;
		Args.MountOptions.MountFlags = FPakMountOptions::EMountFlags::ReportDecryptionFailure;
		if (!Container.HeaderChunk.IsValid())
		{
			Args.MountOptions.MountFlags |= FPakMountOptions::EMountFlags::SkipContainerFile;
		}
		FIoStatus Status;
		if (!Platform->Mount(Args, &Status))
		{
			return Fail(Result, ELoadError::MountFailed, Status.ToString());
		}
		++MountedCount;
		if (Container.HeaderChunk.IsValid() && !FIoDispatcher::Get().DoesChunkExist(Container.HeaderChunk))
		{
			return Fail(Result, ELoadError::MountFailed, TEXT("The associated IoStore container is not visible."));
		}
	}
	if (Plan.bHasShaderLibrary && FApp::CanEverRender())
	{
		bShaderOpened = FShaderCodeLibrary::OpenLibrary(Package.PluginName, ContentDirectory);
		if (!bShaderOpened)
		{
			return Fail(Result, ELoadError::MountFailed, TEXT("The mod's cooked shader library could not be opened."));
		}
	}
	if (!VerifyMountedPackages(Plan, Result) || !LoadCode())
	{
		return false;
	}
	if (!LoadRegistry(Plan, Result))
	{
		if (!Package.NativeCode.Modules.IsEmpty())
		{
			return Fail(Result, ELoadError::RestartRequired,
			            TEXT("A native module is resident, but its asset registry could not be published."));
		}
		return false;
	}
	Result.Package.PackageNames = Plan.Packages.Array();
	Result.Package.PackageNames.Sort(FNameLexicalLess());
	Resources.ContentDirectory = ContentDirectory;
	if (bShaderOpened)
	{
		Resources.ShaderLibrary = Package.PluginName;
	}
	for (const FContainerMount& Container : Plan.Containers)
	{
		Resources.Containers.Add({Container.PakPath, Container.HeaderChunk});
	}
	bCommitted = true;
	return true;
}

bool ReleaseModPackages(TConstArrayView<FName> PackageNames, FString& Reason)
{
	FlushAsyncLoading();
	TArray<UPackage*> LoadedPackages;
	for (FName Name : PackageNames)
	{
		if (UPackage* Package = FindPackage(nullptr, *Name.ToString()))
		{
			LoadedPackages.Add(Package);
		}
	}
	if (!LoadedPackages.IsEmpty())
	{
#if WITH_EDITOR
		for (UPackage* Package : LoadedPackages)
		{
			if (const UWorld* World = UWorld::FindWorldInPackage(Package); World && World->bIsWorldInitialized)
			{
				Reason = FString::Printf(TEXT("A mod world is still active: %s"), *Package->GetName());
				return false;
			}
		}

		// Cooked packages can have split exports. The editor's package unload path tries to fully load them.
		TArray<TWeakObjectPtr<UObject>> StandaloneObjects;
		auto ClearStandalone = [&StandaloneObjects](UObject* Object)
		{
			if (Object->HasAnyFlags(RF_Standalone))
			{
				StandaloneObjects.Add(Object);
				Object->ClearFlags(RF_Standalone);
			}
		};
		for (UPackage* Package : LoadedPackages)
		{
			ClearStandalone(Package);
			ForEachObjectWithOuter(Package, ClearStandalone);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
		for (const TWeakObjectPtr<UObject>& Reference : StandaloneObjects)
		{
			if (UObject* Object = Reference.Get())
			{
				Object->SetFlags(RF_Standalone);
			}
		}
#else
		CollectGarbage(RF_NoFlags, true);
#endif
		for (FName Name : PackageNames)
		{
			if (FindPackage(nullptr, *Name.ToString()))
			{
				Reason = FString::Printf(TEXT("A mod package is still referenced: %s"), *Name.ToString());
				return false;
			}
		}
	}
	FlushRenderingCommands();
	return true;
}

bool UnmountPackage(const FMountedPackage& Package, const FMountedResources& Resources)
{
	FPakPlatformFile* Platform = GetPakPlatform();
	if (!Platform)
	{
		return false;
	}
	if (!Resources.ShaderLibrary.IsEmpty())
	{
		FShaderCodeLibrary::CloseLibrary(Resources.ShaderLibrary);
	}
	bool bUnmounted = true;
	for (int32 Index = Resources.Containers.Num() - 1; Index >= 0; --Index)
	{
		const FMountedContainer& Container = Resources.Containers[Index];
		bUnmounted = UnmountContainer(*Platform, Container.PakPath, Container.HeaderChunk) && bUnmounted;
	}
	if (bUnmounted)
	{
		FPackageName::UnRegisterMountPoint(Package.PackageRoot, Resources.ContentDirectory);
	}
	return bUnmounted;
}
} // namespace Mod::Loader::Private
