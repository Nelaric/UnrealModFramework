// Copyright (c) 2026 Nelaric Contributors
#include "ModPackageBuilder.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Internationalization/Regex.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ModPackagingSettings.h"
#include "ModProjectPackage.h"
#include "Modules/ModuleManager.h"
#include "ProjectDescriptor.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "ModPackageBuilder"

DEFINE_LOG_CATEGORY_STATIC(LogModPackaging, Log, All);

namespace Mod::Editor
{
namespace
{
constexpr uint64 HeaderSize = 64;
constexpr uint64 MaxArchiveSize = 64ull * 1024 * 1024 * 1024;
constexpr uint64 MaxManifestSize = 1024 * 1024;
constexpr uint64 MaxIndexSize = 64 * 1024 * 1024;
constexpr int32 MaxEntries = 65536;
constexpr int32 MaxLifecycleClasses = 64;
constexpr uint8 Magic[8] = {'U', 'M', 'F', 'M', 'O', 'D', 0, 0};

class FSha256
{
public:
	/// Initializes digest state.
	FSha256()
	{
		bValid = SHA256_Init(&Context) == 1;
	}
	/// Adds exact payload bytes.
	bool Update(const void* Bytes, int64 Size)
	{
		bValid = bValid && SHA256_Update(&Context, Bytes, static_cast<size_t>(Size)) == 1;
		return bValid;
	}
	/// Produces a hex digest.
	FString Finish()
	{
		uint8 Bytes[32];
		if (!bValid || SHA256_Final(Bytes, &Context) != 1)
		{
			return FString();
		}
		return BytesToHex(Bytes, sizeof(Bytes)).ToLower();
	}

private:
	FSha256(const FSha256&) = delete;
	FSha256& operator=(const FSha256&) = delete;
	SHA256_CTX Context;
	bool bValid = false;
};

FString FullPath(const FString& Path)
{
	FString Result = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	FPaths::NormalizeFilename(Result);
	FPaths::CollapseRelativeDirectories(Result);
	return Result;
}

bool IsCommandArgument(const FString& Value)
{
	return !Value.Contains(TEXT("\"")) && !Value.Contains(TEXT("\r")) && !Value.Contains(TEXT("\n"));
}

bool Matches(const FString& Value, const TCHAR* Pattern)
{
	FRegexMatcher Matcher(FRegexPattern(Pattern), Value);
	return Matcher.FindNext() && Matcher.GetMatchBeginning() == 0 && Matcher.GetMatchEnding() == Value.Len();
}

FString JsonText(const TSharedRef<FJsonObject>& Object)
{
	FString Text;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	return Text;
}

bool SaveJson(const FString& Path, const TSharedRef<FJsonObject>& Object)
{
	return FFileHelper::SaveStringToFile(JsonText(Object), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool StreamBytes(FArchive& Input, uint64 Size, FSha256& Hash, FArchive* Output = nullptr)
{
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(1024 * 1024);
	while (Size > 0)
	{
		const int64 Count = static_cast<int64>(FMath::Min<uint64>(Size, Buffer.Num()));
		Input.Serialize(Buffer.GetData(), Count);
		if (Input.IsError() || !Hash.Update(Buffer.GetData(), Count))
		{
			return false;
		}
		if (Output)
		{
			Output->Serialize(Buffer.GetData(), Count);
			if (Output->IsError())
			{
				return false;
			}
		}
		Size -= Count;
	}
	return true;
}

FString HashFile(const FString& Path)
{
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
	FSha256 Hash;
	return File && File->TotalSize() >= 0 && StreamBytes(*File, File->TotalSize(), Hash) ? Hash.Finish() : FString();
}

FString ConfigurationFingerprint(const FString& PluginName)
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *FPaths::ProjectConfigDir(), TEXT("*.ini"), true, false);
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
	if (Plugin)
	{
		IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config")),
		                                       TEXT("*.ini"), true, false, false);
		Files.Add(Plugin->GetDescriptorFileName());
	}
	Files.Add(FPaths::GetProjectFilePath());
	Files.Sort();
	FSha256 Hash;
	for (const FString& Path : Files)
	{
		FString Relative = FullPath(Path);
		FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectDir());
		FTCHARToUTF8 Name(*Relative);
		const FString Digest = HashFile(Path);
		FTCHARToUTF8 DigestBytes(*Digest);
		if (Digest.IsEmpty() || !Hash.Update(Name.Get(), Name.Length()) || !Hash.Update("\0", 1) ||
		    !Hash.Update(DigestBytes.Get(), DigestBytes.Length()))
		{
			return FString();
		}
	}
	return Hash.Finish();
}

void WriteNumber(FArchive& File, uint64 Value, int32 ByteCount)
{
	for (int32 Index = 0; Index < ByteCount; ++Index)
	{
		uint8 Byte = static_cast<uint8>(Value >> (Index * 8));
		File.Serialize(&Byte, 1);
	}
}

uint64 ReadNumber(FArchive& File, int32 ByteCount)
{
	uint64 Value = 0;
	for (int32 Index = 0; Index < ByteCount; ++Index)
	{
		uint8 Byte = 0;
		File.Serialize(&Byte, 1);
		Value |= static_cast<uint64>(Byte) << (Index * 8);
	}
	return Value;
}

void WriteHeader(FArchive& File, uint16 Version, uint64 ManifestSize, uint64 IndexOffset, uint64 IndexSize,
                 uint64 FileSize)
{
	File.Seek(0);
	File.Serialize(const_cast<uint8*>(Magic), sizeof(Magic));
	WriteNumber(File, Version, 2);
	WriteNumber(File, 0, 2);
	WriteNumber(File, HeaderSize, 4);
	WriteNumber(File, HeaderSize, 8);
	WriteNumber(File, ManifestSize, 8);
	WriteNumber(File, IndexOffset, 8);
	WriteNumber(File, IndexSize, 8);
	WriteNumber(File, FileSize, 8);
	WriteNumber(File, 0, 8);
}

uint64 WriteText(FArchive& File, const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	File.Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	return Utf8.Length();
}

bool ReadJson(FArchive& File, uint64 Size, TSharedPtr<FJsonObject>& OutObject)
{
	TArray<ANSICHAR> Bytes;
	Bytes.SetNumUninitialized(static_cast<int32>(Size));
	File.Serialize(Bytes.GetData(), Size);
	if (File.IsError() || Bytes.Contains(0))
	{
		return false;
	}
	const FUTF8ToTCHAR Text(Bytes.GetData(), Bytes.Num());
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), OutObject) &&
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

bool ReadExtensionManifest(const FModPackageRequest& Request, const TMap<FName, FString>& Migrated,
                           TSharedPtr<FJsonObject>& Extensions)
{
	if (Request.ExtensionsFile.IsEmpty())
	{
		return true;
	}
	double Version = 0;
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Request.ExtensionsFile));
	if (!File || File->TotalSize() <= 0 || File->TotalSize() > MaxManifestSize ||
	    !ReadJson(*File, File->TotalSize(), Extensions) ||
	    !Extensions->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1)
	{
		return false;
	}
	for (const auto& Field : Extensions->Values)
	{
		if (Field.Key == TEXT("schemaVersion"))
		{
			continue;
		}
		const bool bContent = Field.Key == TEXT("content");
		const bool bPatch = Field.Key == TEXT("patches");
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if ((!bContent && !bPatch && Field.Key != TEXT("dependencies") && Field.Key != TEXT("services") &&
		     Field.Key != TEXT("contracts")) ||
		    !Field.Value->TryGetArray(Entries) || Entries->Num() > 256)
		{
			return false;
		}
		for (const auto& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Value->TryGetObject(Entry))
			{
				return false;
			}
			if (!bContent && !bPatch)
			{
				continue;
			}
			const TCHAR* Key = bContent ? TEXT("asset") : TEXT("actorClass");
			FString Path, Action;
			if (bContent && (*Entry)->TryGetStringField(TEXT("action"), Action) && Action == TEXT("disable"))
			{
				if ((*Entry)->HasField(Key))
				{
					return false;
				}
				continue;
			}
			if (!(*Entry)->TryGetStringField(Key, Path) || !FPackageName::IsValidObjectPath(Path) ||
			    Path.Contains(TEXT(":")))
			{
				return false;
			}
			const FString Package = FPackageName::ObjectPathToPackageName(Path);
			if (const FString* Moved = Migrated.Find(FName(Package)))
			{
				Path = *Moved + Path.Mid(Package.Len());
				(*Entry)->SetStringField(Key, Path);
			}
			if (!Path.StartsWith(TEXT("/") + Request.PluginName + TEXT("/"), ESearchCase::CaseSensitive) ||
			    (bPatch && !Path.EndsWith(TEXT("_C"))))
			{
				return false;
			}
		}
	}
	return true;
}

bool VerifyArchive(const FString& Path, uint16 Version)
{
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
	if (!File || File->TotalSize() < static_cast<int64>(HeaderSize) ||
	    File->TotalSize() > static_cast<int64>(MaxArchiveSize))
	{
		return false;
	}
	uint8 ActualMagic[8];
	File->Serialize(ActualMagic, sizeof(ActualMagic));
	const uint64 Major = ReadNumber(*File, 2);
	const uint64 Minor = ReadNumber(*File, 2);
	const uint64 HeaderLength = ReadNumber(*File, 4);
	const uint64 ManifestOffset = ReadNumber(*File, 8);
	const uint64 ManifestSize = ReadNumber(*File, 8);
	const uint64 IndexOffset = ReadNumber(*File, 8);
	const uint64 IndexSize = ReadNumber(*File, 8);
	const uint64 TotalSize = ReadNumber(*File, 8);
	const uint64 Flags = ReadNumber(*File, 8);
	if (FMemory::Memcmp(ActualMagic, Magic, sizeof(Magic)) || Major != Version || Minor != 0 ||
	    HeaderLength != HeaderSize || ManifestOffset != HeaderSize || ManifestSize == 0 ||
	    ManifestSize > MaxManifestSize || IndexSize == 0 || IndexSize > MaxIndexSize || Flags != 0 ||
	    TotalSize != static_cast<uint64>(File->TotalSize()) || IndexOffset > TotalSize ||
	    IndexSize != TotalSize - IndexOffset || IndexOffset < HeaderSize + ManifestSize)
	{
		return false;
	}
	TSharedPtr<FJsonObject> Manifest;
	TSharedPtr<FJsonObject> Index;
	if (!ReadJson(*File, ManifestSize, Manifest))
	{
		UE_LOG(LogModPackaging, Error, TEXT("The written manifest is not valid JSON."));
		return false;
	}
	File->Seek(IndexOffset);
	if (!ReadJson(*File, IndexSize, Index))
	{
		UE_LOG(LogModPackaging, Error, TEXT("The written payload index is not valid JSON."));
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Index->TryGetArrayField(TEXT("entries"), Entries) || Entries->IsEmpty() || Entries->Num() > MaxEntries)
	{
		return false;
	}
	TSet<FString> Paths;
	uint64 ExpectedOffset = HeaderSize + ManifestSize;
	FString PreviousPath;
	for (const TSharedPtr<FJsonValue>& Value : *Entries)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		FString EntryPath, Digest, Storage;
		uint64 Offset = 0, Size = 0;
		if (!Value->TryGetObject(Entry) || !Entry->IsValid() || !(*Entry)->TryGetStringField(TEXT("path"), EntryPath) ||
		    !(*Entry)->TryGetStringField(TEXT("sha256"), Digest) ||
		    !(*Entry)->TryGetStringField(TEXT("storage"), Storage) || Storage != TEXT("None") ||
		    !ReadSize(**Entry, TEXT("offset"), Offset) || !ReadSize(**Entry, TEXT("size"), Size) ||
		    Offset != ExpectedOffset || Offset > IndexOffset || Size > IndexOffset - Offset || EntryPath.IsEmpty() ||
		    EntryPath.Len() > 512 || EntryPath.Contains(TEXT("..")) || EntryPath.Contains(TEXT("\\")) ||
		    EntryPath.Contains(TEXT(":")) || EntryPath.StartsWith(TEXT("/")) || Paths.Contains(EntryPath.ToLower()) ||
		    (!PreviousPath.IsEmpty() && PreviousPath.Compare(EntryPath, ESearchCase::CaseSensitive) >= 0) ||
		    !Matches(Digest, TEXT("[a-f0-9]{64}")))
		{
			UE_LOG(LogModPackaging, Error, TEXT("Invalid written payload index entry: %s"), *EntryPath);
			return false;
		}
		Paths.Add(EntryPath.ToLower());
		PreviousPath = EntryPath;
		File->Seek(Offset);
		FSha256 Hash;
		if (!StreamBytes(*File, Size, Hash) || Hash.Finish() != Digest)
		{
			UE_LOG(LogModPackaging, Error, TEXT("The written payload failed its digest check: %s"), *EntryPath);
			return false;
		}
		ExpectedOffset += Size;
	}
	return ExpectedOffset == IndexOffset && !File->IsError();
}

bool CollectLifecycleClasses(const FString& Root, const TMap<FName, FString>& MigratedPackages,
                             TArray<TSharedPtr<FJsonValue>>& Classes)
{
	const UClass* Base = FindObject<UClass>(nullptr, TEXT("/Script/ModLoader.ModInstance"));
	if (!Base)
	{
		return true;
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({Root});
	TSet<FTopLevelAssetPath> Derived;
	Registry.GetDerivedClassNames({Base->GetClassPathName()}, {}, Derived);
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*Root));
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	if (!Registry.GetAssets(Filter, Assets))
	{
		return false;
	}
	TSet<FString> Paths;
	for (const FAssetData& Asset : Assets)
	{
		FString GeneratedClass;
		uint32 Flags = 0;
		Asset.GetTagValue(FBlueprintTags::ClassFlags, Flags);
		if ((Flags & (CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) != 0 ||
		    !Asset.GetTagValue(FBlueprintTags::GeneratedClassPath, GeneratedClass))
		{
			continue;
		}
		FString Path = FPackageName::ExportTextPathToObjectPath(GeneratedClass);
		if (Derived.Contains(FTopLevelAssetPath(Path)))
		{
			if (const FString* Migrated = MigratedPackages.Find(Asset.PackageName))
			{
				Path = *Migrated + Path.Mid(Asset.PackageName.ToString().Len());
			}
			Paths.Add(MoveTemp(Path));
		}
	}
	if (Paths.Num() > MaxLifecycleClasses)
	{
		return false;
	}
	TArray<FString> Sorted = Paths.Array();
	Sorted.Sort([](const FString& Left, const FString& Right)
	            { return Left.Compare(Right, ESearchCase::CaseSensitive) < 0; });
	for (FString& Path : Sorted)
	{
		Classes.Add(MakeShared<FJsonValueString>(MoveTemp(Path)));
	}
	return true;
}

bool RunCook(const FModPackageRequest& Request, const FString& WorkDirectory, const FString& ProjectFile,
             const FString& PluginFile, const FString& Configuration, double Deadline)
{
	const FString EngineDirectory = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString AutomationTool = EngineDirectory / TEXT("Binaries/DotNET/AutomationTool/AutomationTool.dll");
	const FString Dotnet = FindPackagingDotnet(EngineDirectory);
	if (Dotnet.IsEmpty() || !FPaths::FileExists(AutomationTool))
	{
		UE_LOG(LogModPackaging, Error, TEXT("The engine's bundled dotnet or AutomationTool.dll is missing."));
		return false;
	}
	FString Arguments = FString::Printf(
	    TEXT("\"%s\" BuildCookRun -project=\"%s\" -nop4 -utf8output -unattended -nocompile -nocompileeditor ")
	        TEXT("-cook -stage -pak -clientconfig=%s -targetplatform=%s -dlcname=\"%s\" ")
	            TEXT("-basedonreleaseversion=\"%s\" -basedonreleaseversionroot=\"%s\" ")
	                TEXT("-stagingdirectory=\"%s\" -cookoutputdir=\"%s\""),
	    *AutomationTool, *ProjectFile, *Configuration, *Request.TargetPlatform, *PluginFile,
	    *Request.BaseReleaseVersion, *Request.BaseReleaseDirectory, *FPaths::Combine(WorkDirectory, TEXT("Stage")),
	    *FPaths::Combine(WorkDirectory, TEXT("Cooked"), Request.GetCookPlatform()));
	Arguments += Request.bUseIoStore ? TEXT(" -iostore") : TEXT(" -skipiostore");
	if (Request.bIncludeEngineContent)
	{
		Arguments += TEXT(" -DLCIncludeEngineContent");
	}
	UE_LOG(LogModPackaging, Display, TEXT("Cooking Mod %s with UAT: %s"), *Request.PluginName, *Arguments);
	return RunPackagingProcess(Dotnet, Arguments, Deadline);
}
} // namespace

FString FModPackageRequest::GetCookPlatform() const
{
	return TargetPlatform == TEXT("Win64") ? TEXT("Windows") : TargetPlatform;
}

TSharedRef<FJsonObject> FModPackageRequest::ToJson() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("format"), TEXT("UMF.ModPackageBuildRequest"));
	Json->SetNumberField(TEXT("schemaVersion"), bPackageProject ? 2 : 1);
	if (bPackageProject)
	{
		Json->SetStringField(TEXT("hostTargetReceipt"), HostTargetReceipt);
		Json->SetStringField(TEXT("hostEngineDirectory"), HostEngineDirectory);
	}
	else
	{
		Json->SetStringField(TEXT("pluginName"), PluginName);
	}
	if (!ExtensionsFile.IsEmpty())
	{
		Json->SetStringField(TEXT("extensionsFile"), ExtensionsFile);
	}
	Json->SetStringField(TEXT("modVersion"), ModVersion);
	Json->SetStringField(TEXT("gameId"), GameId);
	Json->SetStringField(TEXT("baseReleaseVersion"), BaseReleaseVersion);
	Json->SetStringField(TEXT("baseReleaseDirectory"), BaseReleaseDirectory);
	Json->SetStringField(TEXT("targetPlatform"), TargetPlatform);
	Json->SetStringField(TEXT("outputFile"), OutputFile);
	Json->SetBoolField(TEXT("useIoStore"), bUseIoStore);
	Json->SetBoolField(TEXT("includeEngineContent"), bIncludeEngineContent);
	Json->SetNumberField(TEXT("timeoutMinutes"), TimeoutMinutes);
	return Json;
}

bool FModPackageRequest::FromSettings(const UModPackagingSettings& Settings, FModPackageRequest& OutRequest,
                                      FText& OutError)
{
	OutRequest.bPackageProject = Settings.bPackageProject;
	OutRequest.PluginName =
	    Settings.bPackageProject ? FPaths::GetBaseFilename(FPaths::GetProjectFilePath()) : Settings.PluginName;
	if (Settings.bPackageProject)
	{
		OutRequest.HostTargetReceipt = FullPath(Settings.HostTargetReceipt.FilePath);
		OutRequest.HostEngineDirectory = FullPath(Settings.HostEngineDirectory.Path.IsEmpty()
		                                              ? FPaths::ConvertRelativePathToFull(FPaths::EngineDir())
		                                              : Settings.HostEngineDirectory.Path);
	}
	OutRequest.ExtensionsFile =
	    Settings.ExtensionsFile.FilePath.IsEmpty() ? FString() : FullPath(Settings.ExtensionsFile.FilePath);
	OutRequest.ModVersion = Settings.ModVersion;
	OutRequest.GameId = Settings.GameId;
	OutRequest.BaseReleaseVersion = Settings.BaseReleaseVersion;
	OutRequest.BaseReleaseDirectory =
	    FullPath(Settings.BaseReleaseDirectory.Path.IsEmpty() ? TEXT("Releases") : Settings.BaseReleaseDirectory.Path);
	OutRequest.TargetPlatform = Settings.TargetPlatform;
	OutRequest.bUseIoStore = Settings.bUseIoStore;
	OutRequest.bIncludeEngineContent = Settings.bIncludeEngineContent;
	OutRequest.TimeoutMinutes = Settings.TimeoutMinutes;
	const FString OutputDirectory = Settings.ResolveOutputDirectory();
	OutRequest.OutputFile = FPaths::Combine(OutputDirectory, OutRequest.PluginName + TEXT("-") + Settings.ModVersion +
	                                                             TEXT("-") + Settings.TargetPlatform + TEXT(".mod"));
	return OutRequest.Validate(OutError);
}

bool FModPackageRequest::Read(const FString& File, FModPackageRequest& OutRequest, FText& OutError)
{
	FString Text;
	TSharedPtr<FJsonObject> Json;
	FString Format;
	int32 Version = 0;
	if (IFileManager::Get().FileSize(*File) > static_cast<int64>(MaxManifestSize) ||
	    !FFileHelper::LoadFileToString(Text, *File) ||
	    !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid() ||
	    !Json->TryGetStringField(TEXT("format"), Format) || Format != TEXT("UMF.ModPackageBuildRequest") ||
	    !Json->TryGetNumberField(TEXT("schemaVersion"), Version) || (Version != 1 && Version != 2) ||
	    !Json->TryGetStringField(TEXT("modVersion"), OutRequest.ModVersion) ||
	    !Json->TryGetStringField(TEXT("gameId"), OutRequest.GameId) ||
	    !Json->TryGetStringField(TEXT("baseReleaseVersion"), OutRequest.BaseReleaseVersion) ||
	    !Json->TryGetStringField(TEXT("baseReleaseDirectory"), OutRequest.BaseReleaseDirectory) ||
	    !Json->TryGetStringField(TEXT("targetPlatform"), OutRequest.TargetPlatform) ||
	    !Json->TryGetStringField(TEXT("outputFile"), OutRequest.OutputFile) ||
	    !Json->TryGetBoolField(TEXT("useIoStore"), OutRequest.bUseIoStore) ||
	    !Json->TryGetBoolField(TEXT("includeEngineContent"), OutRequest.bIncludeEngineContent) ||
	    !Json->TryGetNumberField(TEXT("timeoutMinutes"), OutRequest.TimeoutMinutes))
	{
		OutError = LOCTEXT("BadRequest", "Mod 打包请求损坏或版本不受支持。");
		return false;
	}
	if (Json->HasField(TEXT("extensionsFile")))
	{
		if (!Json->TryGetStringField(TEXT("extensionsFile"), OutRequest.ExtensionsFile) ||
		    OutRequest.ExtensionsFile.IsEmpty())
		{
			OutError = LOCTEXT("InvalidExtensionPath", "Mod 扩展清单路径无效。");
			return false;
		}
		OutRequest.ExtensionsFile = FullPath(OutRequest.ExtensionsFile);
	}
	OutRequest.bPackageProject = Version == 2;
	if (OutRequest.bPackageProject)
	{
		FString Receipt, EngineDirectory;
		if (!Json->TryGetStringField(TEXT("hostTargetReceipt"), Receipt) || Receipt.IsEmpty())
		{
			OutError = LOCTEXT("MissingHostTarget", "打包当前工程需要宿主模块化构建的 Target 回执。");
			return false;
		}
		Json->TryGetStringField(TEXT("hostEngineDirectory"), EngineDirectory);
		OutRequest.PluginName = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());
		OutRequest.HostTargetReceipt = FullPath(Receipt);
		OutRequest.HostEngineDirectory = FullPath(
		    EngineDirectory.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::EngineDir()) : EngineDirectory);
	}
	else if (!Json->TryGetStringField(TEXT("pluginName"), OutRequest.PluginName))
	{
		OutError = LOCTEXT("MissingPlugin", "请指定要打包的 Mod 内容插件。");
		return false;
	}
	OutRequest.BaseReleaseDirectory = FullPath(OutRequest.BaseReleaseDirectory);
	OutRequest.OutputFile = FullPath(OutRequest.OutputFile);
	return OutRequest.Validate(OutError);
}

bool FModPackageRequest::Validate(FText& OutError) const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
	FString ContentDirectory;
	if (!Matches(PluginName, TEXT("[A-Za-z][A-Za-z0-9_]{0,63}")))
	{
		OutError = LOCTEXT("BadName", "工程或内容插件名称须以字母开头，且最多包含 64 个字母、数字或下划线。");
		return false;
	}
	if (bPackageProject)
	{
		const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
		if (!Project || Project->Modules.IsEmpty() || Plugin ||
		    PluginName != FPaths::GetBaseFilename(FPaths::GetProjectFilePath()) || TargetPlatform != TEXT("Win64") ||
		    bUseIoStore || bIncludeEngineContent || !IsCommandArgument(HostEngineDirectory) ||
		    !FPaths::DirectoryExists(HostEngineDirectory) || !IsCommandArgument(HostTargetReceipt) ||
		    !FPaths::FileExists(HostTargetReceipt))
		{
			OutError = LOCTEXT("BadProject",
			                   "请打开含 C++ 模块的工程，选择 Win64 Pak 和宿主 Target 回执。工程名须有独立包根。");
			return false;
		}
		ContentDirectory = FPaths::ProjectContentDir();
	}
	else
	{
		if (!Plugin || !Plugin->IsEnabled() || Plugin->GetType() != EPluginType::Project ||
		    !Plugin->CanContainContent() || !Plugin->GetDescriptor().Modules.IsEmpty() ||
		    !IsCommandArgument(Plugin->GetDescriptorFileName()))
		{
			OutError = LOCTEXT("BadPlugin", "请选择已启用且不包含 C++ 模块的项目内容插件。");
			return false;
		}
		FConfigFile PluginConfig;
		PluginConfig.Read(
		    *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config"), TEXT("Default") + PluginName + TEXT(".ini")));
		bool bRemapContent = false;
		PluginConfig.GetBool(TEXT("PluginSettings"), TEXT("RemapPluginContentToGame"), bRemapContent);
		if (bRemapContent)
		{
			OutError = LOCTEXT("RemappedPlugin", "Mod 必须保留自己的包路径，请关闭 RemapPluginContentToGame。");
			return false;
		}
		ContentDirectory = Plugin->GetContentDir();
	}
	if (!Matches(ModVersion, TEXT("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)")) || ModVersion.Len() > 64)
	{
		OutError = LOCTEXT("BadVersion", "Mod 版本应为三段数字，例如 1.0.0。");
		return false;
	}
	if (!Matches(GameId, TEXT("[A-Za-z0-9][A-Za-z0-9_.-]{0,127}")) ||
	    !Matches(BaseReleaseVersion, TEXT("[A-Za-z0-9][A-Za-z0-9_.-]{0,127}")) ||
	    BaseReleaseVersion.Contains(TEXT("..")))
	{
		OutError = LOCTEXT("BadRelease", "请填写游戏标识和基础发布版本，仅使用字母、数字、点、下划线或短横线。");
		return false;
	}
	if (TargetPlatform != TEXT("Win64") && TargetPlatform != TEXT("Linux") && TargetPlatform != TEXT("Mac"))
	{
		OutError = LOCTEXT("BadPlatform", "请选择 Win64、Linux 或 Mac 目标平台。");
		return false;
	}
	const FString Registry =
	    FPaths::Combine(BaseReleaseDirectory, BaseReleaseVersion, GetCookPlatform(), TEXT("AssetRegistry.bin"));
	if (!FPaths::FileExists(Registry))
	{
		OutError = FText::Format(LOCTEXT("MissingRelease", "找不到基础发布的 AssetRegistry.bin：{0}"),
		                         FText::FromString(Registry));
		return false;
	}
	if (TimeoutMinutes < 1 || TimeoutMinutes > 1440 || FPaths::GetExtension(OutputFile) != TEXT("mod") ||
	    !IsCommandArgument(OutputFile) || !IsCommandArgument(BaseReleaseDirectory) ||
	    !IsCommandArgument(FullPath(FPaths::GetProjectFilePath())) || FPaths::FileExists(OutputFile) ||
	    FPaths::IsUnderDirectory(OutputFile, FullPath(ContentDirectory)) ||
	    FPaths::IsUnderDirectory(OutputFile, BaseReleaseDirectory))
	{
		OutError =
		    LOCTEXT("BadOutput",
		            "请使用有效的输出目录和超时设置。已有同名包不会覆盖，且输出不能放在 Mod 内容或基础发布目录内。");
		return false;
	}
	TArray<FString> Assets;
	IFileManager::Get().FindFilesRecursive(Assets, *ContentDirectory, TEXT("*.uasset"), true, false);
	IFileManager::Get().FindFilesRecursive(Assets, *ContentDirectory, TEXT("*.umap"), true, false, false);
	if (Assets.IsEmpty())
	{
		OutError = LOCTEXT("NoAssets", "所选 Mod 来源中没有可烘焙的资产或地图。");
		return false;
	}
	return true;
}

bool FModPackageBuilder::Build(const FModPackageRequest& Request, const FString& WorkDirectory,
                               TSharedRef<FJsonObject> OutResult)
{
	auto Fail = [&OutResult](const FString& Message)
	{
		UE_LOG(LogModPackaging, Error, TEXT("%s"), *Message);
		OutResult->SetStringField(TEXT("error"), Message);
		return false;
	};
	FText Error;
	if (!Request.Validate(Error))
	{
		return Fail(Error.ToString());
	}
	const FString Stage = FPaths::Combine(WorkDirectory, TEXT("Stage"));
	if (FPaths::DirectoryExists(Stage) || !IsCommandArgument(WorkDirectory) ||
	    (Request.bPackageProject && (FPaths::DirectoryExists(WorkDirectory / TEXT("Code")) ||
	                                 FPaths::DirectoryExists(WorkDirectory / TEXT("Project")))))
	{
		return Fail(TEXT("This task already has staging files. Start a new packaging task."));
	}
	const FString BaseRegistry = FPaths::Combine(Request.BaseReleaseDirectory, Request.BaseReleaseVersion,
	                                             Request.GetCookPlatform(), TEXT("AssetRegistry.bin"));
	const FString BaseRegistryHash = HashFile(BaseRegistry);
	const FString ConfigurationHash = ConfigurationFingerprint(Request.PluginName);
	const FString HostReceiptHash = Request.bPackageProject ? HashFile(Request.HostTargetReceipt) : FString();
	if (BaseRegistryHash.IsEmpty() || ConfigurationHash.IsEmpty() ||
	    (Request.bPackageProject && HostReceiptHash.IsEmpty()))
	{
		return Fail(TEXT("Could not fingerprint the project configuration, base release, or host receipt."));
	}
	const double Deadline = FPlatformTime::Seconds() + Request.TimeoutMinutes * 60.0;
	FProjectModStage ProjectStage;
	FString StageError;
	if (Request.bPackageProject && !StageProjectMod(Request, WorkDirectory, Deadline, ProjectStage, StageError))
	{
		return Fail(StageError);
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Request.PluginName);
	TSharedPtr<FJsonObject> Extensions;
	if (!ReadExtensionManifest(Request, ProjectStage.MigratedPackages, Extensions))
	{
		return Fail(TEXT(
		    "Invalid extension manifest or content path. Expected schemaVersion 1 and assets in the mod package."));
	}
	const FString CookProject =
	    Request.bPackageProject ? ProjectStage.CookProject : FullPath(FPaths::GetProjectFilePath());
	const FString CookPlugin = Request.bPackageProject ? ProjectStage.CookPlugin : Plugin->GetDescriptorFileName();
	const FString CookConfiguration = Request.bPackageProject ? ProjectStage.Configuration : TEXT("Shipping");
	TArray<TSharedPtr<FJsonValue>> LifecycleClasses;
	const FString SourceRoot = Request.bPackageProject ? TEXT("/Game") : TEXT("/") + Request.PluginName;
	if (!CollectLifecycleClasses(SourceRoot, ProjectStage.MigratedPackages, LifecycleClasses))
	{
		return Fail(TEXT("Could not collect up to 64 global Mod lifecycle Blueprints."));
	}
	if (!RunCook(Request, WorkDirectory, CookProject, CookPlugin, CookConfiguration, Deadline))
	{
		return Fail(TEXT("UE Cook/Stage failed or timed out. See the UAT output in Editor.log."));
	}
	if (HashFile(BaseRegistry) != BaseRegistryHash ||
	    ConfigurationFingerprint(Request.PluginName) != ConfigurationHash ||
	    (Request.bPackageProject && HashFile(Request.HostTargetReceipt) != HostReceiptHash))
	{
		return Fail(TEXT("The base release, project configuration, or host receipt changed during packaging."));
	}
	TArray<FString> StageFiles;
	IFileManager::Get().FindFilesRecursive(StageFiles, *Stage, TEXT("*"), true, false);
	TMap<FString, FString> Payloads;
	TSet<FString> ContainerFiles;
	int32 PakCount = 0, TocCount = 0;
	uint64 PayloadSize = 0;
	const FString ContentSegment = TEXT("/") + Request.PluginName + TEXT("/Content/");
	for (FString File : StageFiles)
	{
		FPaths::NormalizeFilename(File);
		if (!File.Contains(ContentSegment, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FString Relative = File;
		if (!FPaths::MakePathRelativeTo(Relative, *(Stage + TEXT("/"))) || Relative.Contains(TEXT("..")) ||
		    FTCHARToUTF8(*Relative).Length() > 480)
		{
			return Fail(TEXT("A staged path cannot be represented in the Mod package."));
		}
		const int64 Size = IFileManager::Get().FileSize(*File);
		if (Size < 0 || static_cast<uint64>(Size) > MaxArchiveSize - PayloadSize)
		{
			return Fail(TEXT("The staged payload exceeds the 64 GiB package limit."));
		}
		PayloadSize += Size;
		Payloads.Add(TEXT("Content/") + Relative, File);
		const FString Extension = FPaths::GetExtension(File).ToLower();
		ContainerFiles.Add(File.ToLower());
		PakCount += Extension == TEXT("pak");
		TocCount += Extension == TEXT("utoc");
	}
	if (PakCount == 0 || Payloads.Num() + ProjectStage.NativePayloads.Num() > MaxEntries - 1 ||
	    (Request.bUseIoStore ? TocCount == 0 : TocCount != 0))
	{
		return Fail(TEXT("Cook/Stage did not produce the selected Mod container backend."));
	}
	for (const TPair<FString, FString>& Payload : Payloads)
	{
		if (FPaths::GetExtension(Payload.Value).Equals(TEXT("utoc"), ESearchCase::IgnoreCase) &&
		    !ContainerFiles.Contains(FPaths::ChangeExtension(Payload.Value, TEXT("ucas")).ToLower()))
		{
			return Fail(TEXT("An IoStore container is missing its .ucas payload."));
		}
	}
	TSharedRef<FJsonObject> Profile = MakeShared<FJsonObject>();
	Profile->SetStringField(TEXT("gameId"), Request.GameId);
	Profile->SetStringField(TEXT("baseReleaseVersion"), Request.BaseReleaseVersion);
	Profile->SetStringField(TEXT("baseRegistrySha256"), BaseRegistryHash);
	Profile->SetStringField(TEXT("configurationSha256"), ConfigurationHash);
	Profile->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Profile->SetStringField(TEXT("targetPlatform"), Request.TargetPlatform);
	Profile->SetStringField(TEXT("cookPlatform"), Request.GetCookPlatform());
	Profile->SetStringField(TEXT("backend"), Request.bUseIoStore ? TEXT("IoStore") : TEXT("Pak"));
	Profile->SetBoolField(TEXT("includeEngineContent"), Request.bIncludeEngineContent);
	TArray<FString> ConfigFiles;
	IFileManager::Get().FindFilesRecursive(ConfigFiles, *FPaths::ProjectConfigDir(), TEXT("*.ini"), true, false);
	ConfigFiles.Sort();
	TArray<TSharedPtr<FJsonValue>> ConfigHashes;
	for (const FString& File : ConfigFiles)
	{
		TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
		FString Relative = File;
		FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectDir());
		const FString Digest = HashFile(File);
		if (Digest.IsEmpty())
		{
			return Fail(TEXT("Could not fingerprint a project configuration file."));
		}
		Config->SetStringField(TEXT("path"), Relative);
		Config->SetStringField(TEXT("sha256"), Digest);
		ConfigHashes.Add(MakeShared<FJsonValueObject>(Config));
	}
	Profile->SetArrayField(TEXT("projectConfig"), ConfigHashes);
	const FString ProfileText = JsonText(Profile);
	FTCHARToUTF8 ProfileBytes(*ProfileText);
	FSha256 ProfileHash;
	ProfileHash.Update(ProfileBytes.Get(), ProfileBytes.Length());
	const FString ProfileId = ProfileHash.Finish();
	if (ProfileId.IsEmpty())
	{
		return Fail(TEXT("Could not fingerprint the cook profile."));
	}
	TSharedRef<FJsonObject> Receipt = MakeShared<FJsonObject>();
	Receipt->SetStringField(TEXT("format"), TEXT("UMF.ModCookReceipt"));
	Receipt->SetNumberField(TEXT("schemaVersion"), 1);
	Receipt->SetStringField(TEXT("pluginName"), Request.PluginName);
	Receipt->SetObjectField(TEXT("cookProfile"), Profile);
	Receipt->SetStringField(TEXT("cookProfileId"), ProfileId);
	Receipt->SetStringField(TEXT("packageRoot"), TEXT("/") + Request.PluginName + TEXT("/"));
	Receipt->SetBoolField(TEXT("shippingValidated"), false);
	TArray<TSharedPtr<FJsonValue>> StagedPaths;
	TArray<FString> Paths;
	Payloads.GetKeys(Paths);
	Paths.Sort([](const FString& Left, const FString& Right)
	           { return Left.Compare(Right, ESearchCase::CaseSensitive) < 0; });
	for (const FString& Path : Paths)
	{
		StagedPaths.Add(MakeShared<FJsonValueString>(Path.RightChop(8)));
	}
	Receipt->SetArrayField(TEXT("stagedFiles"), StagedPaths);
	const FString ReceiptFile = FPaths::Combine(WorkDirectory, TEXT("CookReceipt.json"));
	if (!SaveJson(ReceiptFile, Receipt))
	{
		return Fail(TEXT("Could not write CookReceipt.json."));
	}
	Payloads.Add(TEXT("Metadata/CookReceipt.json"), ReceiptFile);
	Payloads.Append(MoveTemp(ProjectStage.NativePayloads));
	Payloads.GetKeys(Paths);
	Paths.Sort([](const FString& Left, const FString& Right)
	           { return Left.Compare(Right, ESearchCase::CaseSensitive) < 0; });
	TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
	const uint16 ArchiveVersion = Extensions ? 3 : Request.bPackageProject ? 2 : 1;
	if (Extensions)
	{
		Manifest->SetObjectField(TEXT("extensions"), Extensions);
	}
	Manifest->SetStringField(TEXT("format"), TEXT("UMF.ModPackage"));
	Manifest->SetNumberField(TEXT("schemaVersion"), ArchiveVersion);
	Manifest->SetStringField(TEXT("modId"), Request.PluginName.ToLower());
	Manifest->SetStringField(TEXT("version"), Request.ModVersion);
	Manifest->SetStringField(TEXT("displayName"), Plugin ? Plugin->GetDescriptor().FriendlyName : Request.PluginName);
	Manifest->SetStringField(TEXT("pluginName"), Request.PluginName);
	Manifest->SetStringField(TEXT("packageRoot"), TEXT("/") + Request.PluginName + TEXT("/"));
	Manifest->SetObjectField(TEXT("cookProfile"), Profile);
	Manifest->SetStringField(TEXT("cookProfileId"), ProfileId);
	Manifest->SetStringField(TEXT("cookReceipt"), TEXT("Metadata/CookReceipt.json"));
	Manifest->SetBoolField(TEXT("shippingValidated"), false);
	Manifest->SetStringField(TEXT("exportRequirements"), TEXT("NotVerified"));
	if (ProjectStage.NativeCode)
	{
		Manifest->SetObjectField(TEXT("nativeCode"), ProjectStage.NativeCode);
	}
	if (!LifecycleClasses.IsEmpty())
	{
		Manifest->SetArrayField(TEXT("lifecycleClasses"), MoveTemp(LifecycleClasses));
	}
	const FString ManifestText = JsonText(Manifest);
	if (FTCHARToUTF8(*ManifestText).Length() > MaxManifestSize ||
	    !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Request.OutputFile), true))
	{
		return Fail(TEXT("The manifest is too large or the output directory is not writable."));
	}
	const FString Temporary =
	    Request.OutputFile + TEXT(".") + FPaths::GetCleanFilename(WorkDirectory) + TEXT(".partial");
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Temporary, false, true, true);
	};
	TUniquePtr<FArchive> Output(IFileManager::Get().CreateFileWriter(*Temporary, FILEWRITE_NoReplaceExisting));
	if (!Output)
	{
		return Fail(TEXT("Could not create the temporary Mod package."));
	}
	WriteHeader(*Output, ArchiveVersion, 0, 0, 0, 0);
	const uint64 ManifestLength = WriteText(*Output, ManifestText);
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FString& Path : Paths)
	{
		TUniquePtr<FArchive> Input(IFileManager::Get().CreateFileReader(*Payloads[Path]));
		if (!Input || Input->TotalSize() < 0 ||
		    static_cast<uint64>(Input->TotalSize()) > MaxArchiveSize - static_cast<uint64>(Output->Tell()))
		{
			return Fail(TEXT("A staged payload is unreadable or exceeds the package limit."));
		}
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);
		Entry->SetNumberField(TEXT("offset"), Output->Tell());
		Entry->SetNumberField(TEXT("size"), Input->TotalSize());
		Entry->SetStringField(TEXT("storage"), TEXT("None"));
		FSha256 Hash;
		if (!StreamBytes(*Input, Input->TotalSize(), Hash, Output.Get()))
		{
			return Fail(TEXT("Could not copy a staged payload into the Mod package."));
		}
		const FString Digest = Hash.Finish();
		if (Digest.IsEmpty())
		{
			return Fail(TEXT("Could not hash a staged payload."));
		}
		Entry->SetStringField(TEXT("sha256"), Digest);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedRef<FJsonObject> Index = MakeShared<FJsonObject>();
	Index->SetArrayField(TEXT("entries"), Entries);
	const FString IndexText = JsonText(Index);
	const uint64 IndexOffset = Output->Tell();
	const uint64 IndexLength = WriteText(*Output, IndexText);
	const uint64 FileSize = Output->Tell();
	WriteHeader(*Output, ArchiveVersion, ManifestLength, IndexOffset, IndexLength, FileSize);
	if (IndexLength > MaxIndexSize || FileSize > MaxArchiveSize || Output->IsError() || !Output->Close())
	{
		return Fail(TEXT("Could not finish the Mod package."));
	}
	Output.Reset();
	UE_LOG(LogModPackaging, Display, TEXT("Verifying Mod package (%llu bytes)..."), FileSize);
	if (!VerifyArchive(Temporary, ArchiveVersion))
	{
		return Fail(TEXT("The written Mod package failed verification."));
	}
	const FString ArchiveHash = HashFile(Temporary);
	if (ArchiveHash.IsEmpty() || !IFileManager::Get().Move(*Request.OutputFile, *Temporary, false, false, false, true))
	{
		return Fail(TEXT("Could not commit the package. The output may already exist."));
	}
	OutResult->SetStringField(TEXT("artifact"), Request.OutputFile);
	OutResult->SetStringField(TEXT("sha256"), ArchiveHash);
	OutResult->SetStringField(TEXT("cookProfileId"), ProfileId);
	OutResult->SetNumberField(TEXT("size"), FileSize);
	OutResult->SetBoolField(TEXT("shippingValidated"), false);
	UE_LOG(LogModPackaging, Display, TEXT("Mod package committed: %s"), *Request.OutputFile);
	return true;
}
} // namespace Mod::Editor

#undef LOCTEXT_NAMESPACE
