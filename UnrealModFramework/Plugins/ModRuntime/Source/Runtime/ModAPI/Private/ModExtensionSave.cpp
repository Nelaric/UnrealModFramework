// Copyright (c) 2026 Nelaric Contributors
#include "ModExtensionSave.h"

#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ModExtensionWorld.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_UNIX || PLATFORM_MAC
#include <unistd.h>
#endif

namespace Mod::Extension
{
namespace
{
constexpr uint32 SaveMagic = 0x53464D55;
constexpr uint32 FormatVersion = 1;
constexpr int64 MaxSaveBytes = 32 * 1024 * 1024;
constexpr int32 MaxBlockBytes = 4 * 1024 * 1024;
constexpr int32 MaxMods = 128;
constexpr int32 MaxBlocks = 256;
constexpr int32 MaxReferences = 4096;
constexpr uint32 MaxMigrationSteps = 64;

bool Fail(FString& Error, FString Value)
{
	Error = MoveTemp(Value);
	return false;
}

bool BoundedSize(FArchive& Ar, int32& Count, int32 Limit)
{
	Ar << Count;
	return !Ar.IsError() && Count >= 0 && Count <= Limit;
}

bool Bytes(FArchive& Ar, TArray<uint8>& Value, int32 Limit)
{
	int32 Count = Value.Num();
	if (!BoundedSize(Ar, Count, Limit) || (Ar.IsLoading() && Count > Ar.TotalSize() - Ar.Tell()))
	{
		return false;
	}
	if (Ar.IsLoading())
	{
		Value.SetNumUninitialized(Count);
	}
	if (Count)
	{
		Ar.Serialize(Value.GetData(), Count);
	}
	return !Ar.IsError();
}

bool Text(FArchive& Ar, FString& Value)
{
	constexpr int32 MaxTextBytes = 512;
	TArray<uint8, TInlineAllocator<MaxTextBytes>> Buffer;
	if (Ar.IsSaving())
	{
		if (Value.Len() > MaxTextBytes)
		{
			return false;
		}
		const FTCHARToUTF8 Utf8(*Value, Value.Len());
		Buffer.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	int32 Count = Buffer.Num();
	if (!BoundedSize(Ar, Count, MaxTextBytes) || (Ar.IsLoading() && Count > Ar.TotalSize() - Ar.Tell()))
	{
		return false;
	}
	if (Ar.IsLoading())
	{
		Buffer.SetNumUninitialized(Count);
	}
	if (Count)
	{
		Ar.Serialize(Buffer.GetData(), Count);
		if (Buffer.Contains(0))
		{
			return false;
		}
	}
	if (Ar.IsLoading())
	{
		Value.Reset();
		if (Count)
		{
			const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Count);
			Value.AppendChars(Decoded.Get(), Decoded.Length());
			const FTCHARToUTF8 RoundTrip(*Value, Value.Len());
			if (RoundTrip.Length() != Count || FMemory::Memcmp(RoundTrip.Get(), Buffer.GetData(), Count) != 0)
			{
				return false;
			}
		}
	}
	return !Ar.IsError();
}

template <class T, class FSerialize> bool Array(FArchive& Ar, TArray<T>& Values, int32 Limit, FSerialize Visit)
{
	int32 Count = Values.Num();
	if (!BoundedSize(Ar, Count, Limit))
	{
		return false;
	}
	if (Ar.IsLoading())
	{
		Values.SetNum(Count);
	}
	for (auto& Value : Values)
	{
		if (!Visit(Ar, Value))
		{
			return false;
		}
	}
	return true;
}

bool IsKey(FStringView Value)
{
	if (Value.IsEmpty() || Value.Len() > 128)
	{
		return false;
	}
	for (const TCHAR Ch : Value)
	{
		if (!((Ch >= TEXT('a') && Ch <= TEXT('z')) || (Ch >= TEXT('0') && Ch <= TEXT('9')) || Ch == TEXT('.') ||
		      Ch == TEXT('-') || Ch == TEXT('_') || Ch == TEXT('/')))
		{
			return false;
		}
	}
	return true;
}

bool Valid(const FModSaveSnapshot& Snapshot)
{
	int64 ByteTotal = Snapshot.GameData.Num();
	if (!Snapshot.GameVersion || Snapshot.BaseRelease.IsEmpty() || Snapshot.ExportContract.IsEmpty() ||
	    Snapshot.Mods.Num() > MaxMods || Snapshot.References.Num() > MaxReferences ||
	    Snapshot.GameData.Num() > MaxBlockBytes)
	{
		return false;
	}
	TSet<FString> ModIds;
	int32 BlockCount = 0;
	for (const auto& Mod : Snapshot.Mods)
	{
		if (!IsKey(Mod.ModId) || ModIds.Contains(Mod.ModId) || Mod.ReleaseId.IsEmpty() ||
		    Mod.CompatibilityDigest.Len() != 64 || Mod.ArchiveDigest.Len() != 64)
		{
			return false;
		}
		ModIds.Add(Mod.ModId);
		TSet<FString> BlockIds;
		for (const auto& Block : Mod.Blocks)
		{
			if (++BlockCount > MaxBlocks || !IsKey(Block.Id) || BlockIds.Contains(Block.Id) || !Block.Version ||
			    Block.Bytes.Num() > MaxBlockBytes)
			{
				return false;
			}
			BlockIds.Add(Block.Id);
			ByteTotal += Block.Bytes.Num();
		}
	}
	for (const auto& Reference : Snapshot.References)
	{
		if ((!Reference.ModId.IsEmpty() && !ModIds.Contains(Reference.ModId)) ||
		    (!Reference.Slot.IsEmpty() && !IsKey(Reference.Slot)) || !IsKey(Reference.Id))
		{
			return false;
		}
	}
	return ByteTotal <= MaxSaveBytes;
}

bool MigrateBytes(const FModSavePolicy& Policy, const FString& Mod, const FString& Block, uint32 From, uint32 To,
                  TArray<uint8>& Data, FString& Error)
{
	if (!To || From > To || To - From > MaxMigrationSteps)
	{
		return Fail(Error, FString::Printf(TEXT("%s/%s has an unsupported schema %u -> %u."), *Mod, *Block, From, To));
	}
	for (uint32 Version = From; Version < To; ++Version)
	{
		const auto* Step = Policy.FindMigration ? Policy.FindMigration(Mod, Block, Version) : nullptr;
		FString Detail;
		if (!Step || !*Step || !(*Step)(Data, Detail) || Data.Num() > MaxBlockBytes)
		{
			return Fail(Error, FString::Printf(TEXT("%s/%s migration %u -> %u failed. %s"), *Mod, *Block, Version,
			                                   Version + 1, *Detail));
		}
	}
	return true;
}

void UpdateMetadata(FModSaveRecord& Record, const Mod::Loader::FMountedPackage& Package)
{
	Record.ModId = Package.ModId;
	Record.ReleaseId = Package.GameplayRelease.ReleaseId;
	Record.CompatibilityDigest = Package.GameplayRelease.CompatibilityDigest;
	Record.ArchiveDigest = Package.ArchiveSha256;
}
} // namespace

bool FModExtensionSave::Serialize(FArchive& Ar, FModSaveSnapshot& Snapshot)
{
	uint32 Magic = SaveMagic;
	uint32 Format = FormatVersion;
	Ar << Magic << Format << Snapshot.GameVersion;
	int32 BlockCount = 0;
	return !Ar.IsError() && Magic == SaveMagic && Format == FormatVersion && Text(Ar, Snapshot.BaseRelease) &&
	       Text(Ar, Snapshot.ExportContract) && Text(Ar, Snapshot.GameplayDigest) &&
	       Array(Ar, Snapshot.Mods, MaxMods,
	             [&BlockCount](FArchive& Stream, FModSaveRecord& Mod)
	             {
		             uint8 Required = Mod.bRequired ? 1 : 0;
		             Stream << Required;
		             Mod.bRequired = Required != 0;
		             return Required <= 1 && Text(Stream, Mod.ModId) && Text(Stream, Mod.ReleaseId) &&
		                    Text(Stream, Mod.CompatibilityDigest) && Text(Stream, Mod.ArchiveDigest) &&
		                    Array(Stream, Mod.Blocks, MaxBlocks,
		                          [&BlockCount](FArchive& Inner, FModSaveBlock& Block)
		                          {
			                          Inner << Block.Version;
			                          return ++BlockCount <= MaxBlocks && Text(Inner, Block.Id) &&
			                                 Bytes(Inner, Block.Bytes, MaxBlockBytes);
		                          });
	             }) &&
	       Array(Ar, Snapshot.References, MaxReferences, [](FArchive& Stream, FModSaveReference& Ref)
	             { return Text(Stream, Ref.ModId) && Text(Stream, Ref.Slot) && Text(Stream, Ref.Id); }) &&
	       Bytes(Ar, Snapshot.GameData, MaxBlockBytes) && Ar.Tell() <= MaxSaveBytes && Valid(Snapshot);
}

bool FModExtensionSave::Capture(const UModExtensionSubsystem& Extensions, const FModSavePolicy& Policy,
                                FModSaveSnapshot& Snapshot, FString& Error)
{
	Error.Reset();
	if (!Extensions.IsReady() &&
	    (!IsInGameThread() || !Extensions.State || Extensions.State->Phase != FModExtensionWorld::EState::Preparing ||
	     !Extensions.State->bStagingSavedState))
	{
		return Fail(Error, TEXT("The World is not ready to capture save metadata."));
	}
	FModSaveSnapshot Staged = Snapshot;
	Staged.GameVersion = Policy.GameVersion;
	Staged.BaseRelease = Policy.BaseRelease;
	Staged.ExportContract = Policy.ExportContract;
	Staged.GameplayDigest = Extensions.GetGameplayDigest();
	for (const auto& Mod : Extensions.State->Mods)
	{
		const auto* Package = Mod.Package.Get();
		auto* Record =
		    Staged.Mods.FindByPredicate([Package](const auto& Item) { return Item.ModId == Package->ModId; });
		if (!Record)
		{
			Record = &Staged.Mods.AddDefaulted_GetRef();
			Record->bRequired = Package->bRequiredByHost ||
			                    Staged.References.ContainsByPredicate([Package](const auto& Reference)
			                                                          { return Reference.ModId == Package->ModId; });
		}
		Record->bRequired |=
		    Package->bRequiredByHost || Extensions.State->Policy.Selection.Required.Contains(Package->ModId);
		UpdateMetadata(*Record, *Package);
	}
	for (const auto& Record : Staged.Mods)
	{
		if (Record.bRequired && !Extensions.State->Mods.ContainsByPredicate(
		                            [&Record](const auto& Mod) { return Mod.Package->ModId == Record.ModId; }))
		{
			return Fail(Error, Record.ModId + TEXT(": required save package is unavailable."));
		}
	}
	Staged.Mods.Sort([](const auto& A, const auto& B) { return A.ModId < B.ModId; });
	if (!Valid(Staged))
	{
		return Fail(Error, TEXT("The captured save exceeds its format bounds."));
	}
	Snapshot = MoveTemp(Staged);
	return true;
}

bool FModExtensionSave::Admit(const UModExtensionSubsystem& Extensions, const FModSavePolicy& Policy,
                              const FModSaveSnapshot& Original, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || !Extensions.State ||
	    (Extensions.State->Phase != FModExtensionWorld::EState::Preparing && !Extensions.IsReady()) || !Valid(Original))
	{
		return Fail(Error, TEXT("The save or current World is unavailable."));
	}
	if ((Original.BaseRelease != Policy.BaseRelease || Original.ExportContract != Policy.ExportContract) &&
	    (!Policy.AcceptOlderHost || !Policy.AcceptOlderHost(Original)))
	{
		return Fail(Error, TEXT("The save requires a different base release or export contract."));
	}
	const auto State = Extensions.State;
	for (const auto& Record : Original.Mods)
	{
		const auto* Found =
		    State->Mods.FindByPredicate([&Record](const auto& Mod) { return Mod.Package->ModId == Record.ModId; });
		if (!Found)
		{
			if (Record.bRequired)
			{
				return Fail(Error, Record.ModId + TEXT(": required save package is unavailable."));
			}
			continue;
		}
		const auto& Release = Found->Package->GameplayRelease;
		if ((Record.ReleaseId != Release.ReleaseId || Record.CompatibilityDigest != Release.CompatibilityDigest) &&
		    (!Policy.AcceptOlderMod || !Policy.AcceptOlderMod(Record, Release.ReleaseId, Release.CompatibilityDigest)))
		{
			return Fail(Error, Record.ModId + TEXT(": no migration for the saved gameplay release."));
		}
	}
	return true;
}

bool FModExtensionSave::Migrate(const UModExtensionSubsystem& Extensions, const FModSavePolicy& Policy,
                                const FModSaveSnapshot& Original, FModSaveSnapshot& Result, FString& Error)
{
	if (!Admit(Extensions, Policy, Original, Error) || (!Extensions.IsReady() && !Extensions.State->bStagingSavedState))
	{
		return false;
	}
	const auto State = Extensions.State;
	++State->Depth;
	ON_SCOPE_EXIT
	{
		State->FinishDispatch();
	};
	FModSaveSnapshot Staged = Original;
	for (auto& Reference : Staged.References)
	{
		const auto* Owner =
		    Staged.Mods.FindByPredicate([&Reference](const auto& Record) { return Record.ModId == Reference.ModId; });
		if (Owner && !Owner->bRequired &&
		    !State->Mods.ContainsByPredicate([&Reference](const auto& Mod)
		                                     { return Mod.Package->ModId == Reference.ModId; }))
		{
			continue;
		}
		const auto Resolve = [&]
		{
			if (!IsKey(Reference.Id) || (!Reference.Slot.IsEmpty() && !IsKey(Reference.Slot)))
			{
				return false;
			}
			if (Reference.Slot.IsEmpty())
			{
				return Policy.ValidateObjectReference && Policy.ValidateObjectReference(Reference);
			}
			const auto Content = Extensions.ResolveContent(FName(*Reference.Slot), FName(*Reference.Id));
			return Content.Status == EContentStatus::Available && Content.ModId == Reference.ModId;
		};
		if (!Resolve() && (!Policy.MigrateReference || !Policy.MigrateReference(Reference, Error) || !Resolve()))
		{
			return Fail(Error,
			            Reference.ModId + TEXT(": unresolved save reference ") + Reference.Id + TEXT(". ") + Error);
		}
	}
	if (!MigrateBytes(Policy, {}, TEXT("game"), Original.GameVersion, Policy.GameVersion, Staged.GameData, Error))
	{
		return false;
	}
	for (auto& Record : Staged.Mods)
	{
		if (!State->Mods.ContainsByPredicate([&Record](const auto& Mod) { return Mod.Package->ModId == Record.ModId; }))
		{
			continue;
		}
		for (auto& Block : Record.Blocks)
		{
			const uint32 Target = Policy.BlockVersion ? Policy.BlockVersion(Record.ModId, Block.Id) : 0;
			if (!MigrateBytes(Policy, Record.ModId, Block.Id, Block.Version, Target, Block.Bytes, Error))
			{
				return false;
			}
			Block.Version = Target;
		}
	}
	if (!Capture(Extensions, Policy, Staged, Error))
	{
		return false;
	}
	Result = MoveTemp(Staged);
	return true;
}

bool FModExtensionSave::ReadFile(const FString& Source, FModSaveSnapshot& Snapshot, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread())
	{
		return Fail(Error, TEXT("Save loading requires the game thread."));
	}
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Source));
	FModSaveSnapshot Staged;
	if (!Reader || Reader->TotalSize() <= 0 || Reader->TotalSize() > MaxSaveBytes || !Serialize(*Reader, Staged) ||
	    Reader->Tell() != Reader->TotalSize())
	{
		return Fail(Error, TEXT("The save file is invalid or unavailable."));
	}
	Snapshot = MoveTemp(Staged);
	return true;
}

bool FModExtensionSave::WriteNewFile(const FString& Destination, const FModSaveSnapshot& Snapshot, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || Destination.IsEmpty() || !Valid(Snapshot))
	{
		return Fail(Error, TEXT("The save cannot be written with the supplied snapshot and destination."));
	}
	auto& Files = IFileManager::Get();
	if (Files.FileExists(*Destination))
	{
		return Fail(Error, TEXT("A save already exists at the destination."));
	}
	const FString Stage = Destination + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
	FModSaveSnapshot Staged = Snapshot;
	TUniquePtr<FArchive> Writer(Files.CreateFileWriter(*Stage, FILEWRITE_NoReplaceExisting));
	if (!Writer)
	{
		return Fail(Error, TEXT("The temporary save could not be opened."));
	}
	const bool bSerialized = Serialize(*Writer, Staged);
	const bool bClosed = Writer->Close();
	const bool bWritten = bSerialized && bClosed && !Writer->IsError();
	Writer.Reset();
	bool bPublished = false;
	if (bWritten)
	{
#if PLATFORM_WINDOWS
		const FString ResolvedStage = FPaths::ConvertRelativePathToFull(Stage);
		const FString ResolvedDestination = FPaths::ConvertRelativePathToFull(Destination);
		bPublished = ::MoveFileExW(*ResolvedStage, *ResolvedDestination, MOVEFILE_WRITE_THROUGH) != 0;
#elif PLATFORM_UNIX || PLATFORM_MAC
		const FString ResolvedStage = FPaths::ConvertRelativePathToFull(Stage);
		const FString ResolvedDestination = FPaths::ConvertRelativePathToFull(Destination);
		bPublished = ::link(TCHAR_TO_UTF8(*ResolvedStage), TCHAR_TO_UTF8(*ResolvedDestination)) == 0;
#else
		bPublished = Files.Move(*Destination, *Stage, false, false, false, true);
#endif
	}
	Files.Delete(*Stage, false, false, true);
	if (bPublished)
	{
		return true;
	}
	return Fail(Error, TEXT("The save could not be published."));
}
} // namespace Mod::Extension
