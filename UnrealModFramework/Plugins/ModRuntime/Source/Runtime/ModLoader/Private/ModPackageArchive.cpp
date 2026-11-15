// Copyright (c) 2026 Nelaric Contributors
#include "ModPackageArchive.h"

#include "ModPackageExtensions.h"

#include "Algo/AllOf.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformProperties.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace Mod::Loader::Private
{
namespace
{
// The wire layout matches the independently built authoring plugin.
constexpr uint64 HeaderSize = 64;
constexpr uint64 MaxArchiveSize = 64ull * 1024 * 1024 * 1024;
constexpr uint64 MaxManifestSize = 1024 * 1024;
constexpr uint64 MaxIndexSize = 64 * 1024 * 1024;
constexpr int32 MaxEntries = 65536;
constexpr int32 MaxContainers = 128;
constexpr int32 MaxLifecycleClasses = 64;
constexpr int32 MaxTrustedDigests = 4096;
constexpr int64 MaxNativeExtensionBytes = 64ll * 1024 * 1024;
constexpr TCHAR ReceiptPath[] = TEXT("Metadata/CookReceipt.json");
constexpr TCHAR NativePrefix[] = TEXT("Native/");
constexpr uint8 Magic[] = {'U', 'M', 'F', 'M', 'O', 'D', 0, 0};

struct FArchiveLayout
{
	uint8 Version = 0;
	uint64 ManifestSize = 0;
	uint64 IndexOffset = 0;
	uint64 IndexSize = 0;
};

struct FEntry
{
	FString Path;
	FString Digest;
	uint64 Offset = 0;
	uint64 Size = 0;
};

struct FArchiveContents
{
	FString CookPlatform;
	TSharedPtr<FJsonObject> Profile;
	TArray<FEntry> Entries;
};

class FSha256
{
public:
	FSha256()
	{
		bValid = SHA256_Init(&Context) == 1;
	}

	bool Update(const uint8* Data, uint64 Size)
	{
		bValid = bValid && SHA256_Update(&Context, Data, static_cast<SIZE_T>(Size)) == 1;
		return bValid;
	}

	FString Finish()
	{
		uint8 Digest[SHA256_DIGEST_LENGTH];
		return bValid && SHA256_Final(Digest, &Context) == 1 ? BytesToHex(Digest, sizeof(Digest)).ToLower() : FString();
	}

private:
	SHA256_CTX Context;
	bool bValid = false;
};

bool IsAsciiLetter(TCHAR Value)
{
	return (Value >= 'A' && Value <= 'Z') || (Value >= 'a' && Value <= 'z');
}

bool IsAsciiDigit(TCHAR Value)
{
	return Value >= '0' && Value <= '9';
}

bool IsIdentifier(FStringView Value)
{
	return !Value.IsEmpty() && Value.Len() <= 128 && (IsAsciiLetter(Value[0]) || IsAsciiDigit(Value[0])) &&
	       Algo::AllOf(
	           Value, [](TCHAR Char)
	           { return IsAsciiLetter(Char) || IsAsciiDigit(Char) || Char == '_' || Char == '-' || Char == '.'; });
}

bool IsModuleName(FStringView Name)
{
	return !Name.IsEmpty() && Name.Len() <= 64 && IsAsciiLetter(Name[0]) &&
	       Algo::AllOf(Name, [](TCHAR Char) { return IsAsciiLetter(Char) || IsAsciiDigit(Char) || Char == '_'; });
}

bool IsDigest(FStringView Value)
{
	return Value.Len() == SHA256_DIGEST_LENGTH * 2 &&
	       Algo::AllOf(Value, [](TCHAR Char)
	                   { return IsAsciiDigit(Char) || (Char >= 'a' && Char <= 'f') || (Char >= 'A' && Char <= 'F'); });
}

bool IsVersion(const FString& Value)
{
	TArray<FString> Parts;
	if (Value.Len() > 64 || Value.ParseIntoArray(Parts, TEXT("."), false) != 3)
	{
		return false;
	}
	return Algo::AllOf(Parts,
	                   [](const FString& Part) {
		                   return !Part.IsEmpty() && (Part.Len() == 1 || Part[0] != '0') &&
		                          Algo::AllOf(FStringView(Part), IsAsciiDigit);
	                   });
}

bool HasText(const FJsonObject& Object, const TCHAR* Key, FStringView Expected)
{
	FString Value;
	return Object.TryGetStringField(Key, Value) && FStringView(Value).Equals(Expected, ESearchCase::CaseSensitive);
}

bool IsSchema(const FJsonObject& Object, const TCHAR* Format, uint8 ExpectedVersion = 1)
{
	double Version = 0;
	return HasText(Object, TEXT("format"), Format) && Object.TryGetNumberField(TEXT("schemaVersion"), Version) &&
	       Version == ExpectedVersion;
}

uint64 ReadUInt64(const uint8* Data)
{
	uint64 Value = 0;
	for (uint32 Index = 0; Index < 8; ++Index)
	{
		Value |= static_cast<uint64>(Data[Index]) << (Index * 8);
	}
	return Value;
}

bool ReadLayout(TConstArrayView<uint8> Header, uint64 FileSize, FArchiveLayout& OutLayout)
{
	const uint8 Version = Header[8];
	const uint64 VersionAndHeaderSize = (HeaderSize << 32) | Version;
	if (FMemory::Memcmp(Header.GetData(), Magic, sizeof(Magic)) != 0 || (Version < 1 || Version > 3) ||
	    ReadUInt64(Header.GetData() + 8) != VersionAndHeaderSize || ReadUInt64(Header.GetData() + 16) != HeaderSize ||
	    ReadUInt64(Header.GetData() + 48) != FileSize || ReadUInt64(Header.GetData() + 56) != 0)
	{
		return false;
	}
	OutLayout.Version = Version;
	OutLayout.ManifestSize = ReadUInt64(Header.GetData() + 24);
	OutLayout.IndexOffset = ReadUInt64(Header.GetData() + 32);
	OutLayout.IndexSize = ReadUInt64(Header.GetData() + 40);
	return OutLayout.ManifestSize > 0 && OutLayout.ManifestSize <= MaxManifestSize && OutLayout.IndexSize > 0 &&
	       OutLayout.IndexSize <= MaxIndexSize && OutLayout.IndexOffset >= HeaderSize + OutLayout.ManifestSize &&
	       OutLayout.IndexOffset <= FileSize && OutLayout.IndexSize == FileSize - OutLayout.IndexOffset;
}

bool ReadBytes(IFileHandle& File, uint64 Offset, uint64 Size, TArray<uint8>& OutBytes)
{
	OutBytes.SetNumUninitialized(static_cast<int32>(Size));
	return File.Seek(Offset) && File.Read(OutBytes.GetData(), Size);
}

bool ReadObject(TConstArrayView<uint8> Bytes, TSharedPtr<FJsonObject>& OutObject)
{
	if (Bytes.IsEmpty() || Bytes.Contains(0))
	{
		return false;
	}
	const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	const FTCHARToUTF8 RoundTrip(Text.Get(), Text.Length());
	return RoundTrip.Length() == Bytes.Num() && FMemory::Memcmp(RoundTrip.Get(), Bytes.GetData(), Bytes.Num()) == 0 &&
	       FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), OutObject) &&
	       OutObject.IsValid();
}

bool ReadSize(const FJsonObject& Object, const TCHAR* Key, uint64& OutSize)
{
	double Number = 0;
	if (!Object.TryGetNumberField(Key, Number) || !FMath::IsFinite(Number) || Number < 0 ||
	    Number > static_cast<double>(MaxArchiveSize))
	{
		return false;
	}
	OutSize = static_cast<uint64>(Number);
	return static_cast<double>(OutSize) == Number;
}

bool Transfer(IFileHandle& Input, uint64 Size, FSha256& Hash, FPreparationBudget Budget, IFileHandle* Output = nullptr)
{
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(static_cast<int32>(FMath::Min<uint64>(Size, 1024 * 1024)));
	while (Size > 0)
	{
		const int64 Count = static_cast<int64>(FMath::Min<uint64>(Size, Buffer.Num()));
		if (Budget.IsExpired() || !Input.Read(Buffer.GetData(), Count) || !Hash.Update(Buffer.GetData(), Count) ||
		    (Output && !Output->Write(Buffer.GetData(), Count)))
		{
			return false;
		}
		Size -= Count;
	}
	return true;
}

bool FailTransfer(FLoadResult& Result, FPreparationBudget Budget, ELoadError Error, FString Detail)
{
	return Budget.IsExpired() ? Fail(Result, ELoadError::TimedOut, TEXT("Mod package preparation timed out."))
	                          : Fail(Result, Error, MoveTemp(Detail));
}

bool ReadLifecycleClasses(const FJsonObject& Manifest, FLoadResult& Result)
{
	if (!Manifest.HasField(TEXT("lifecycleClasses")))
	{
		return true;
	}
	const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
	if (!Manifest.TryGetArrayField(TEXT("lifecycleClasses"), Classes) || Classes->Num() > MaxLifecycleClasses)
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The lifecycle class list is invalid or oversized."));
	}
	FMountedPackage& Package = Result.Package;
	TSet<FString> Seen;
	for (const TSharedPtr<FJsonValue>& Entry : *Classes)
	{
		FString Path, PackageName, ClassName;
		if (!Entry->TryGetString(Path) || Path.Len() > 512 ||
		    !Path.StartsWith(Package.PackageRoot, ESearchCase::CaseSensitive) ||
		    !Path.Split(TEXT("."), &PackageName, &ClassName) ||
		    !FPackageName::IsValidTextForLongPackageName(PackageName) ||
		    !ClassName.Equals(FPackageName::GetShortName(PackageName) + TEXT("_C"), ESearchCase::CaseSensitive) ||
		    Seen.Contains(Path))
		{
			return Fail(Result, ELoadError::InvalidArchive,
			            TEXT("A lifecycle Blueprint class is invalid, duplicated, or outside the mod root."));
		}
		Seen.Add(Path);
		Package.LifecycleClasses.Add(MoveTemp(Path));
	}
	Package.LifecycleClasses.Sort([](const FString& Left, const FString& Right)
	                              { return Left.Compare(Right, ESearchCase::CaseSensitive) < 0; });
	return true;
}

bool ReadNativeCode([[maybe_unused]] const FJsonObject& Manifest, [[maybe_unused]] const FLoadPolicy& Policy,
                    FLoadResult& Result, [[maybe_unused]] FNativeModuleSet& OutCode)
{
#if !PLATFORM_WINDOWS || !PLATFORM_CPU_X86_FAMILY || !PLATFORM_64BITS || IS_MONOLITHIC
	return Fail(Result, ELoadError::Incompatible, TEXT("Native mods require a modular Win64 x64 host."));
#else
	constexpr int32 MaxNativeModules = 64;
	if (!Policy.bAllowNativeModules || Policy.Backend != EBackend::Pak)
	{
		return Fail(Result, ELoadError::Incompatible, TEXT("This host has not enabled native Pak mods."));
	}
	const TSharedPtr<FJsonObject>* Code = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
	if (!Manifest.TryGetObjectField(TEXT("nativeCode"), Code) || !Code->IsValid() ||
	    !(*Code)->TryGetStringField(TEXT("buildId"), OutCode.BuildId) || !IsIdentifier(OutCode.BuildId) ||
	    !(*Code)->TryGetStringField(TEXT("manifest"), OutCode.ManifestName) || !IsIdentifier(OutCode.ManifestName) ||
	    !OutCode.ManifestName.EndsWith(TEXT(".modules")) || !(*Code)->TryGetArrayField(TEXT("modules"), Modules) ||
	    Modules->IsEmpty() || Modules->Num() > MaxNativeModules)
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The native module manifest is invalid."));
	}
	if (!HasText(**Code, TEXT("architecture"), TEXT("x64")) ||
	    !HasText(**Code, TEXT("configuration"), LexToString(FApp::GetBuildConfiguration())) ||
	    !HasText(**Code, TEXT("targetType"), LexToString(FApp::GetBuildTargetType())) ||
	    !HasText(**Code, TEXT("target"), FPlatformMisc::GetUBTTargetName()))
	{
		return Fail(Result, ELoadError::Incompatible, TEXT("The native module target does not match the host."));
	}
	TSet<FName> Names;
	TSet<FName> Files;
	for (const TSharedPtr<FJsonValue>& Value : *Modules)
	{
		const TSharedPtr<FJsonObject>* Module = nullptr;
		FNativeModule Entry;
		if (!Value->TryGetObject(Module) || !Module->IsValid() ||
		    !(*Module)->TryGetStringField(TEXT("name"), Entry.Name) || !IsModuleName(Entry.Name) ||
		    !(*Module)->TryGetStringField(TEXT("file"), Entry.FileName) || !IsIdentifier(Entry.FileName) ||
		    !Entry.FileName.EndsWith(TEXT(".dll")) || Names.Contains(FName(*Entry.Name)) ||
		    Files.Contains(FName(*Entry.FileName)))
		{
			return Fail(Result, ELoadError::InvalidArchive, TEXT("A native module name or DLL filename is invalid."));
		}
		Names.Add(FName(*Entry.Name));
		Files.Add(FName(*Entry.FileName));
		OutCode.Modules.Add(MoveTemp(Entry));
	}
	return true;
#endif
}

bool ReadManifest(const FJsonObject& Manifest, const FArchiveLayout& Layout, const FLoadPolicy& Policy,
                  FLoadResult& Result, FArchiveContents& Contents, FPreparedPackage& OutPackage)
{
	FMountedPackage& Package = Result.Package;
	Package.Extensions.bDeclared = Layout.Version == 3;
	const TSharedPtr<FJsonObject>* Profile = nullptr;
	if (!IsSchema(Manifest, TEXT("UMF.ModPackage"), Layout.Version) ||
	    !HasText(Manifest, TEXT("cookReceipt"), ReceiptPath) ||
	    !Manifest.TryGetStringField(TEXT("pluginName"), OutPackage.PluginName) || OutPackage.PluginName.IsEmpty() ||
	    !IsModuleName(OutPackage.PluginName) || !Manifest.TryGetStringField(TEXT("modId"), Package.ModId) ||
	    !Package.ModId.Equals(OutPackage.PluginName.ToLower(), ESearchCase::CaseSensitive) ||
	    !Manifest.TryGetStringField(TEXT("version"), Package.Version) || !IsVersion(Package.Version) ||
	    !Manifest.TryGetStringField(TEXT("packageRoot"), Package.PackageRoot) ||
	    !Package.PackageRoot.Equals(TEXT("/") + OutPackage.PluginName + TEXT("/"), ESearchCase::CaseSensitive) ||
	    !Manifest.TryGetStringField(TEXT("cookProfileId"), Package.CookProfileId) || !IsDigest(Package.CookProfileId) ||
	    !Manifest.TryGetObjectField(TEXT("cookProfile"), Profile) || !Profile->IsValid())
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The mod manifest is missing a valid identity."));
	}
	Contents.Profile = *Profile;
	const FJsonObject& Cook = **Profile;
#if WITH_EDITOR && PLATFORM_WINDOWS
	// Windows PIE reads content cooked for the game target, whose platform is Windows.
	const FString HostCookPlatform = ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName());
#else
	const FString HostCookPlatform = ANSI_TO_TCHAR(FPlatformProperties::PlatformName());
#endif
	const FString HostTarget = HostCookPlatform == TEXT("Windows") ? TEXT("Win64") : HostCookPlatform;
	const TCHAR* Backend = Policy.Backend == EBackend::Pak ? TEXT("Pak") : TEXT("IoStore");
	if (!HasText(Cook, TEXT("gameId"), Policy.GameId) ||
	    !HasText(Cook, TEXT("baseReleaseVersion"), Policy.BaseReleaseVersion) ||
	    !HasText(Cook, TEXT("baseRegistrySha256"), Policy.BaseRegistrySha256.ToLower()) ||
	    !HasText(Cook, TEXT("engineVersion"), FEngineVersion::Current().ToString()) ||
	    !HasText(Cook, TEXT("targetPlatform"), HostTarget) || !HasText(Cook, TEXT("cookPlatform"), HostCookPlatform) ||
	    !HasText(Cook, TEXT("backend"), Backend))
	{
		return Fail(Result, ELoadError::Incompatible,
		            TEXT("The game, base release, UE build, target platform, or container backend does not match."));
	}
	bool bIncludesEngineContent = false;
	if (!Cook.TryGetBoolField(TEXT("includeEngineContent"), bIncludesEngineContent) || bIncludesEngineContent)
	{
		return Fail(Result, ELoadError::Incompatible,
		            TEXT("The mod must keep cooked content in its own package root."));
	}
	Package.Backend = Policy.Backend;
	Contents.CookPlatform = HostCookPlatform;
	if (Layout.Version < 3 && Manifest.HasField(TEXT("extensions")))
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("Extension declarations require archive version 3."));
	}
	if (Layout.Version == 1 && Manifest.HasField(TEXT("nativeCode")))
	{
		return Fail(Result, ELoadError::InvalidArchive,
		            TEXT("Native module declarations require archive version 2 or 3."));
	}
	if (!ReadLifecycleClasses(Manifest, Result) || (Layout.Version == 3 && !ReadExtensions(Manifest, Result)))
	{
		return false;
	}
	if (!Package.Extensions.NativeExtensionFile.IsEmpty())
	{
#if PLATFORM_WINDOWS && PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
		if (!Policy.bAllowNativeExtensions || Package.Backend != EBackend::Pak)
		{
			return Fail(Result, ELoadError::Incompatible, TEXT("This host has not enabled native Pak extensions."));
		}
#else
		return Fail(Result, ELoadError::Incompatible, TEXT("Independent extensions require a Win64 x64 host."));
#endif
	}
	const bool bHasNativeCode = Layout.Version == 2 || Manifest.HasField(TEXT("nativeCode"));
	return !bHasNativeCode || (Layout.Version > 1 && ReadNativeCode(Manifest, Policy, Result, OutPackage.NativeCode));
}

bool ReadIndex(const FJsonObject& Index, const FArchiveLayout& Layout, FLoadResult& Result, FArchiveContents& Contents,
               FPreparedPackage& OutPackage)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Index.TryGetArrayField(TEXT("entries"), Values) || Values->IsEmpty() || Values->Num() > MaxEntries)
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The v1 payload index has an invalid entry count."));
	}
	const FString StagePrefix = TEXT("Content/") + Contents.CookPlatform + TEXT("/");
	const FString ContentSegment = TEXT("/") + OutPackage.PluginName + TEXT("/Content/");
	TSet<FString> ExpectedNativeFiles;
	const FString NativeExtension = Result.Package.Extensions.NativeExtensionFile.IsEmpty()
	                                    ? FString()
	                                    : FString(NativePrefix) + Result.Package.Extensions.NativeExtensionFile;
	if (!NativeExtension.IsEmpty())
	{
		ExpectedNativeFiles.Add(NativeExtension);
	}
	if (!OutPackage.NativeCode.ManifestName.IsEmpty())
	{
		ExpectedNativeFiles.Add(FString(NativePrefix) + OutPackage.NativeCode.ManifestName);
		for (const FNativeModule& Module : OutPackage.NativeCode.Modules)
		{
			const FString File = FString(NativePrefix) + Module.FileName;
			if (!NativeExtension.IsEmpty() && File.Equals(NativeExtension, ESearchCase::IgnoreCase))
			{
				return Fail(Result, ELoadError::InvalidArchive, TEXT("An independent DLL duplicates a UE module."));
			}
			ExpectedNativeFiles.Add(File);
		}
	}
	TSet<FString> Paths;
	TSet<FString> FoldedPaths;
	FString PreviousPath;
	uint64 Offset = HeaderSize + Layout.ManifestSize;
	int32 TocCount = 0;
	bool bHasReceipt = false;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FEntry Entry;
		if (!Value->TryGetObject(Object) || !Object->IsValid() ||
		    !(*Object)->TryGetStringField(TEXT("path"), Entry.Path) || !IsSafeRelativePath(Entry.Path) ||
		    !(*Object)->TryGetStringField(TEXT("sha256"), Entry.Digest) || !IsDigest(Entry.Digest) ||
		    !HasText(**Object, TEXT("storage"), TEXT("None")) || !ReadSize(**Object, TEXT("offset"), Entry.Offset) ||
		    !ReadSize(**Object, TEXT("size"), Entry.Size) || Entry.Offset != Offset || Offset > Layout.IndexOffset ||
		    Entry.Size > Layout.IndexOffset - Offset ||
		    (!PreviousPath.IsEmpty() && PreviousPath.Compare(Entry.Path, ESearchCase::CaseSensitive) >= 0) ||
		    FoldedPaths.Contains(Entry.Path.ToLower()))
		{
			return Fail(Result, ELoadError::InvalidArchive,
			            TEXT("The index contains an invalid path or payload range."));
		}
		Offset += Entry.Size;
		PreviousPath = Entry.Path;
		Paths.Add(Entry.Path);
		FoldedPaths.Add(Entry.Path.ToLower());
		if (Entry.Path.Equals(ReceiptPath, ESearchCase::CaseSensitive))
		{
			bHasReceipt = Entry.Size > 0 && Entry.Size <= MaxManifestSize;
		}
		else if (Entry.Path.StartsWith(NativePrefix, ESearchCase::CaseSensitive))
		{
			if (Entry.Size == 0 || (Entry.Path.EndsWith(TEXT(".modules")) && Entry.Size > MaxManifestSize) ||
			    ExpectedNativeFiles.Remove(Entry.Path) != 1)
			{
				return Fail(Result, ELoadError::InvalidArchive,
				            TEXT("An indexed native payload is undeclared or empty."));
			}
			if (Entry.Path == NativeExtension)
			{
				if (Entry.Size > MaxNativeExtensionBytes)
				{
					return Fail(Result, ELoadError::LimitExceeded, TEXT("The native extension exceeds 64 MiB."));
				}
				Result.Package.NativeExtensionDigest = Entry.Digest;
			}
		}
		else
		{
			const int32 ContentStart = Entry.Path.Find(ContentSegment, ESearchCase::IgnoreCase);
			if (!Entry.Path.StartsWith(StagePrefix, ESearchCase::CaseSensitive) || ContentStart < StagePrefix.Len())
			{
				return Fail(Result, ELoadError::InvalidArchive, TEXT("A staged file is outside the mod content root."));
			}
			const FString ContentPath =
			    Entry.Path.Mid(StagePrefix.Len(), ContentStart + ContentSegment.Len() - StagePrefix.Len());
			if (OutPackage.ContentRelativePath.IsEmpty())
			{
				OutPackage.ContentRelativePath = ContentPath;
			}
			else if (!OutPackage.ContentRelativePath.Equals(ContentPath, ESearchCase::CaseSensitive))
			{
				return Fail(Result, ELoadError::InvalidArchive,
				            TEXT("The archive contains more than one content root."));
			}
			const FString Extension = FPaths::GetExtension(Entry.Path).ToLower();
			if (Extension == TEXT("pak"))
			{
				OutPackage.Containers.Add({Entry.Path, {}});
			}
			if (Extension == TEXT("utoc"))
			{
				++TocCount;
			}
			if ((Extension == TEXT("utoc") || Extension == TEXT("ucas")) && Result.Package.Backend == EBackend::Pak)
			{
				return Fail(Result, ELoadError::InvalidArchive, TEXT("A Pak package contains IoStore payloads."));
			}
		}
		Contents.Entries.Add(MoveTemp(Entry));
	}
	if (!bHasReceipt || Offset != Layout.IndexOffset || OutPackage.Containers.IsEmpty() ||
	    OutPackage.Containers.Num() > MaxContainers || !ExpectedNativeFiles.IsEmpty())
	{
		return Fail(Result, ELoadError::InvalidArchive,
		            TEXT("The receipt, payload range, or container set is invalid."));
	}
	int32 PairedCount = 0;
	for (FNativeContainer& Container : OutPackage.Containers)
	{
		const FString Toc = FPaths::ChangeExtension(Container.PakPath, TEXT("utoc"));
		if (Paths.Contains(Toc))
		{
			if (!Paths.Contains(FPaths::ChangeExtension(Toc, TEXT("ucas"))))
			{
				return Fail(Result, ELoadError::InvalidArchive, TEXT("An IoStore table is missing its data payload."));
			}
			Container.TocPath = Toc;
			++PairedCount;
		}
	}
	if (PairedCount != TocCount || (Result.Package.Backend == EBackend::IoStore && PairedCount == 0))
	{
		return Fail(Result, ELoadError::InvalidArchive,
		            TEXT("The IoStore and Pak payloads do not form container pairs."));
	}
	return true;
}

bool VerifyReceipt(IFileHandle& Input, const FArchiveContents& Contents, const FPreparedPackage& Package,
                   FLoadResult& Result)
{
	const FEntry* Entry = Contents.Entries.FindByPredicate(
	    [](const FEntry& Value) { return Value.Path.Equals(ReceiptPath, ESearchCase::CaseSensitive); });
	TArray<uint8> Bytes;
	TSharedPtr<FJsonObject> Receipt;
	if (!Entry || !ReadBytes(Input, Entry->Offset, Entry->Size, Bytes))
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The cook receipt could not be read."));
	}
	FSha256 Hash;
	Hash.Update(Bytes.GetData(), Bytes.Num());
	if (!Hash.Finish().Equals(Entry->Digest, ESearchCase::IgnoreCase))
	{
		return Fail(Result, ELoadError::IntegrityFailure, TEXT("The cook receipt does not match its payload digest."));
	}
	const TSharedPtr<FJsonObject>* Profile = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Staged = nullptr;
	const int32 NativeCount = (Package.NativeCode.Modules.IsEmpty() ? 0 : Package.NativeCode.Modules.Num() + 1) +
	                          (Result.Package.Extensions.NativeExtensionFile.IsEmpty() ? 0 : 1);
	if (!ReadObject(Bytes, Receipt) || !IsSchema(*Receipt, TEXT("UMF.ModCookReceipt")) ||
	    !HasText(*Receipt, TEXT("pluginName"), Package.PluginName) ||
	    !HasText(*Receipt, TEXT("packageRoot"), Result.Package.PackageRoot) ||
	    !HasText(*Receipt, TEXT("cookProfileId"), Result.Package.CookProfileId) ||
	    !Receipt->TryGetObjectField(TEXT("cookProfile"), Profile) || !Profile->IsValid() ||
	    !FJsonValue::CompareEqual(FJsonValueObject(Contents.Profile), FJsonValueObject(*Profile)) ||
	    !Receipt->TryGetArrayField(TEXT("stagedFiles"), Staged) ||
	    Staged->Num() != Contents.Entries.Num() - 1 - NativeCount)
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The cook receipt disagrees with the archive manifest."));
	}
	int32 Index = 0;
	for (const FEntry& Item : Contents.Entries)
	{
		if (Item.Path.Equals(ReceiptPath, ESearchCase::CaseSensitive) ||
		    Item.Path.StartsWith(NativePrefix, ESearchCase::CaseSensitive))
		{
			continue;
		}
		FString Relative;
		if (!(*Staged)[Index++]->TryGetString(Relative) ||
		    !FStringView(Item.Path).RightChop(8).Equals(Relative, ESearchCase::CaseSensitive))
		{
			return Fail(Result, ELoadError::InvalidArchive, TEXT("The cook receipt lists a different staged payload."));
		}
	}
	return true;
}

bool IsLink(IPlatformFile& Files, const FString& Path)
{
	return Files.IsSymlink(*Path) == ESymlinkResult::Symlink;
}

bool VerifyCache(IPlatformFile& Files, const FString& Directory, const TArray<FEntry>& Entries,
                 FPreparationBudget Budget)
{
	if (IsLink(Files, Directory))
	{
		return false;
	}
	TSet<FString> Expected;
	for (const FEntry& Entry : Entries)
	{
		Expected.Add(FPaths::Combine(Directory, Entry.Path));
	}
	int32 Visited = 0;
	if (!Files.IterateDirectoryRecursively(*Directory,
	                                       [&Files, &Expected, &Visited, Budget](const TCHAR* Path, bool bIsDirectory)
	                                       {
		                                       FString Normalized(Path);
		                                       FPaths::NormalizeFilename(Normalized);
		                                       return !Budget.IsExpired() && ++Visited <= MaxEntries * 16 &&
		                                              !IsLink(Files, Normalized) &&
		                                              (bIsDirectory || Expected.Remove(Normalized) == 1);
	                                       }) ||
	    !Expected.IsEmpty())
	{
		return false;
	}
	for (const FEntry& Entry : Entries)
	{
		const FString Path = FPaths::Combine(Directory, Entry.Path);
		TUniquePtr<IFileHandle> File(Files.OpenRead(*Path));
		FSha256 Hash;
		if (!File || File->Size() != static_cast<int64>(Entry.Size) || !Transfer(*File, Entry.Size, Hash, Budget) ||
		    !Hash.Finish().Equals(Entry.Digest, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}
	return true;
}

bool PrepareCache(IFileHandle& Input, const TArray<FEntry>& Entries, FPreparationBudget Budget, FLoadResult& Result,
                  FString& OutCacheDirectory)
{
	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	const FString Root =
	    FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ModCache")));
	if (!Files.CreateDirectoryTree(*Root) || IsLink(Files, Root))
	{
		return Fail(Result, ELoadError::CacheFailure, TEXT("The mod cache root could not be prepared."));
	}
	OutCacheDirectory = FPaths::Combine(Root, Result.Package.ArchiveSha256);
	if (Files.DirectoryExists(*OutCacheDirectory))
	{
		return VerifyCache(Files, OutCacheDirectory, Entries, Budget) ||
		       FailTransfer(Result, Budget, ELoadError::CacheFailure,
		                    TEXT("The existing mod cache differs from the verified archive."));
	}
	const FString Pending = FPaths::Combine(Root, TEXT(".pending-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (Files.DirectoryExists(*Pending) || IsLink(Files, Pending) || !Files.CreateDirectory(*Pending))
	{
		return Fail(Result, ELoadError::CacheFailure, TEXT("The temporary mod cache could not be created."));
	}
	ON_SCOPE_EXIT
	{
		// Only this operation's unpublished directory is eligible for deletion.
		if (Files.DirectoryExists(*Pending) && !IsLink(Files, Pending))
		{
			Files.DeleteDirectoryRecursively(*Pending);
		}
	};
	for (const FEntry& Entry : Entries)
	{
		const FString Path = FPaths::Combine(Pending, Entry.Path);
		if (!Files.CreateDirectoryTree(*FPaths::GetPath(Path)) || !Input.Seek(Entry.Offset))
		{
			return Fail(Result, ELoadError::CacheFailure, TEXT("A payload path could not be prepared."));
		}
		TUniquePtr<IFileHandle> Output(Files.OpenWrite(*Path, false, false));
		FSha256 Hash;
		if (!Output || !Transfer(Input, Entry.Size, Hash, Budget, Output.Get()) || !Output->Flush())
		{
			return FailTransfer(Result, Budget, ELoadError::CacheFailure, TEXT("A payload could not be extracted."));
		}
		if (!Hash.Finish().Equals(Entry.Digest, ESearchCase::IgnoreCase))
		{
			return Fail(Result, ELoadError::IntegrityFailure, TEXT("A payload does not match its SHA-256 digest."));
		}
	}
	if (!Files.MoveFile(*OutCacheDirectory, *Pending) && !VerifyCache(Files, OutCacheDirectory, Entries, Budget))
	{
		return FailTransfer(Result, Budget, ELoadError::CacheFailure,
		                    TEXT("The verified cache directory could not be committed."));
	}
	return true;
}

bool ShouldEnforcePackageAllowlist(const FLoadPolicy& Policy)
{
	return Policy.bEnforcePackageAllowlist.Get(!Policy.TrustedPackageSha256.IsEmpty());
}
} // namespace

bool Fail(FLoadResult& Result, ELoadError Error, FString Detail)
{
	Result.Error = Error;
	Result.Detail = MoveTemp(Detail);
	return false;
}

bool ValidatePolicy(const FLoadPolicy& Policy, FLoadResult& Result)
{
	if (!IsIdentifier(Policy.GameId) || !IsIdentifier(Policy.BaseReleaseVersion) ||
	    !IsDigest(Policy.BaseRegistrySha256) ||
	    (Policy.Backend != EBackend::Pak && Policy.Backend != EBackend::IoStore) ||
	    !FMath::IsFinite(Policy.PreparationTimeoutSeconds) || Policy.PreparationTimeoutSeconds < 1.0 ||
	    Policy.PreparationTimeoutSeconds > 3600.0)
	{
		return Fail(Result, ELoadError::InvalidPolicy,
		            TEXT("Supply a valid game ID, release, registry hash, and policy."));
	}
	if (Policy.Selection.Archives.Num() > 128 || (Policy.Selection.Enabled && Policy.Selection.Enabled->Num() > 128) ||
	    Policy.Selection.Required.Num() > 128)
	{
		return Fail(Result, ELoadError::InvalidPolicy, TEXT("The package selection exceeds the configured limit."));
	}
	for (const auto& [Id, Digest] : Policy.Selection.Archives)
	{
		if (!IsIdentifier(Id) || Id != Id.ToLower() || !IsDigest(Digest))
		{
			return Fail(Result, ELoadError::InvalidPolicy, TEXT("A package pin has an invalid Mod ID or digest."));
		}
	}
	if (Policy.GameplayReleases.Num() > MaxTrustedDigests)
	{
		return Fail(Result, ELoadError::InvalidPolicy, TEXT("The gameplay release catalog is too large."));
	}
	for (const auto& [Digest, Release] : Policy.GameplayReleases)
	{
		if (Digest != Digest.ToLower() || !IsDigest(Digest) || !IsIdentifier(Release.ReleaseId) ||
		    !IsDigest(Release.CompatibilityDigest))
		{
			return Fail(Result, ELoadError::InvalidPolicy, TEXT("The gameplay release catalog has an invalid entry."));
		}
	}
	if (ShouldEnforcePackageAllowlist(Policy) &&
	    (Policy.TrustedPackageSha256.Num() > MaxTrustedDigests ||
	     !Algo::AllOf(Policy.TrustedPackageSha256, [](const FString& Value) { return IsDigest(Value); })))
	{
		return Fail(Result, ELoadError::InvalidPolicy,
		            TEXT("TrustedPackageSha256 exceeds the digest limit or contains an invalid SHA-256."));
	}
	return true;
}

bool IsSafeRelativePath(FStringView Path)
{
	if (Path.IsEmpty() || Path.Len() > 512 || Path[0] == '/' || Path.EndsWith(TEXT("/")) || Path.Contains(TEXT("..")) ||
	    Path.Contains(TEXT("\\")) || Path.Contains(TEXT(":")) ||
	    !Algo::AllOf(Path, [](TCHAR Char) { return Char >= 32 && Char != 127; }))
	{
		return false;
	}
	const FString String(Path);
	if (FTCHARToUTF8(*String).Length() > 512 || !FPaths::ValidatePath(String))
	{
		return false;
	}
	TArray<FString> Components;
	String.ParseIntoArray(Components, TEXT("/"), false);
	return Components.Num() <= 32 && Algo::AllOf(Components,
	                                             [](const FString& Component)
	                                             {
		                                             return !Component.IsEmpty() && !Component.EndsWith(TEXT(".")) &&
		                                                    !FChar::IsWhitespace(Component[0]) &&
		                                                    !FChar::IsWhitespace(Component[Component.Len() - 1]);
	                                             });
}

bool FPreparationBudget::IsExpired() const
{
	return (Cancellation && Cancellation->Load()) || FPlatformTime::Seconds() >= Deadline;
}

bool PrepareArchive(const FLoadPolicy& Policy, FPreparationBudget Budget, FLoadResult& Result,
                    FPreparedPackage& OutPackage, const TSet<FString>* MountedIds)
{
	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	TUniquePtr<IFileHandle> Input;
	if (FPaths::GetExtension(Result.Path).Equals(TEXT("mod"), ESearchCase::IgnoreCase) && !IsLink(Files, Result.Path))
	{
		Input.Reset(Files.OpenRead(*Result.Path));
	}
	if (!Input)
	{
		return Fail(Result, ELoadError::InputUnavailable, TEXT("A physical .mod archive could not be opened."));
	}
	const int64 FileSize = Input->Size();
	if (FileSize > static_cast<int64>(MaxArchiveSize))
	{
		return Fail(Result, ELoadError::LimitExceeded, TEXT("The mod archive exceeds the 64 GiB v1 limit."));
	}
	FArchiveLayout Layout;
	TArray<uint8> Header;
	TArray<uint8> ManifestBytes;
	TArray<uint8> IndexBytes;
	if (FileSize < static_cast<int64>(HeaderSize) || !ReadBytes(*Input, 0, HeaderSize, Header) ||
	    !ReadLayout(Header, FileSize, Layout) || !ReadBytes(*Input, HeaderSize, Layout.ManifestSize, ManifestBytes))
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The UMFMOD v1 header or metadata bounds are invalid."));
	}
	TSharedPtr<FJsonObject> Manifest;
	FString DeclaredId;
	if (MountedIds && ReadObject(ManifestBytes, Manifest) && Manifest->TryGetStringField(TEXT("modId"), DeclaredId) &&
	    MountedIds->Contains(DeclaredId))
	{
		// An untrusted identity can only cause a skip, never a mount.
		Result.Package.ModId = MoveTemp(DeclaredId);
		OutPackage.bSkipped = true;
		return true;
	}
	if (!ReadBytes(*Input, Layout.IndexOffset, Layout.IndexSize, IndexBytes))
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The UMFMOD v1 header or metadata bounds are invalid."));
	}
	const uint64 PayloadOffset = HeaderSize + Layout.ManifestSize;
	FSha256 Hash;
	// Hash the exact metadata bytes that will be parsed, including the index.
	if (!Hash.Update(Header.GetData(), Header.Num()) || !Hash.Update(ManifestBytes.GetData(), ManifestBytes.Num()) ||
	    !Input->Seek(PayloadOffset) || !Transfer(*Input, Layout.IndexOffset - PayloadOffset, Hash, Budget) ||
	    !Hash.Update(IndexBytes.GetData(), IndexBytes.Num()))
	{
		return FailTransfer(Result, Budget, ELoadError::InputUnavailable, TEXT("The archive could not be hashed."));
	}
	Result.Package.ArchiveSha256 = Hash.Finish();
	if (Result.Package.ArchiveSha256.IsEmpty())
	{
		return Fail(Result, ELoadError::InputUnavailable, TEXT("The archive SHA-256 could not be computed."));
	}
	if (ShouldEnforcePackageAllowlist(Policy) &&
	    !Policy.TrustedPackageSha256.ContainsByPredicate(
	        [&Result](const FString& Trusted)
	        { return Trusted.Equals(Result.Package.ArchiveSha256, ESearchCase::IgnoreCase); }))
	{
		return Fail(Result, ELoadError::Untrusted, TEXT("The archive SHA-256 is absent from TrustedPackageSha256."));
	}
	TSharedPtr<FJsonObject> Index;
	if ((!Manifest.IsValid() && !ReadObject(ManifestBytes, Manifest)) || !ReadObject(IndexBytes, Index))
	{
		return Fail(Result, ELoadError::InvalidArchive, TEXT("The manifest or index is not valid UTF-8 JSON."));
	}
	FArchiveContents Contents;
	if (!ReadManifest(*Manifest, Layout, Policy, Result, Contents, OutPackage) ||
	    !ReadIndex(*Index, Layout, Result, Contents, OutPackage) ||
	    !VerifyReceipt(*Input, Contents, OutPackage, Result))
	{
		return false;
	}
	FString CacheDirectory;
	if (!PrepareCache(*Input, Contents.Entries, Budget, Result, CacheDirectory))
	{
		return false;
	}
	OutPackage.StageRoot = FPaths::Combine(CacheDirectory, TEXT("Content"), Contents.CookPlatform);
	if (!Result.Package.Extensions.NativeExtensionFile.IsEmpty())
	{
		Result.Package.NativeExtensionPath =
		    FPaths::Combine(CacheDirectory, TEXT("Native"), Result.Package.Extensions.NativeExtensionFile);
	}
	if (!OutPackage.NativeCode.Modules.IsEmpty())
	{
		OutPackage.NativeCode.Directory = FPaths::Combine(CacheDirectory, TEXT("Native"));
	}
	for (FNativeContainer& Container : OutPackage.Containers)
	{
		Container.PakPath = FPaths::Combine(CacheDirectory, Container.PakPath);
		if (!Container.TocPath.IsEmpty())
		{
			Container.TocPath = FPaths::Combine(CacheDirectory, Container.TocPath);
		}
	}
	return true;
}
} // namespace Mod::Loader::Private

bool Mod::Loader::VerifyNativeExtension(const FMountedPackage& Package)
{
	using namespace Mod::Loader::Private;
	IPlatformFile& Files = IPlatformFile::GetPlatformPhysical();
	if (!IsDigest(Package.NativeExtensionDigest) || Package.NativeExtensionPath.IsEmpty() ||
	    IsLink(Files, Package.NativeExtensionPath))
	{
		return false;
	}
	TUniquePtr<IFileHandle> File(Files.OpenRead(*Package.NativeExtensionPath));
	FSha256 Hash;
	return File && File->Size() > 0 && File->Size() <= MaxNativeExtensionBytes &&
	       Transfer(*File, File->Size(), Hash, {FPlatformTime::Seconds() + 5.0}) &&
	       Hash.Finish().Equals(Package.NativeExtensionDigest, ESearchCase::IgnoreCase);
}

FString Mod::Loader::HashPackagePlan(const FString& CanonicalPlan)
{
	const FTCHARToUTF8 Bytes(*CanonicalPlan);
	Private::FSha256 Hash;
	return Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) ? Hash.Finish() : FString();
}
