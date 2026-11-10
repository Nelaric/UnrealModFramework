// Copyright (c) 2026 Nelaric Contributors
#include "ModExportOptionsCommandlet.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "GameplayTagsSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "ModExportMetadataOptions.h"
#include "ModExportMetadataSchema.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/CoreRedirects.h"

namespace
{
namespace Schema = Mod::ExportMetadata::Schema;
using Mod::ExportMetadata::EOptionKind;

struct FOptionBinding
{
	const TCHAR* Key;
	EOptionKind Kind;
};

FString AbsolutePath(const FString& Path)
{
	FString Result = FPaths::ConvertRelativePathToFull(Path);
	FPaths::NormalizeFilename(Result);
	FPaths::CollapseRelativeDirectories(Result);
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonStrings(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& String : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(String));
	}
	return Values;
}

bool AddFingerprint(const FString& Path, FJsonObject& Inputs)
{
	if (!IFileManager::Get().FileExists(*Path))
	{
		Inputs.SetStringField(Path, TEXT(""));
		return true;
	}
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot read metadata configuration input: %s"), *Path);
		return false;
	}
	const std::string_view View(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<std::size_t>(Bytes.Num()));
	const uint64 Hash = Schema::Fingerprint(View);
	Inputs.SetStringField(Path, FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(Hash)));
	return true;
}

bool AddFunctionRedirects(FConfigContext&& Context, FConfigFile& Config, const FString& IniName,
                          TMap<FString, FString>& Redirects)
{
	// UE consumes [CoreRedirects] during startup. Reload the merged configuration into a local file.
	Context.bForceReload = true;
	Context.bWriteDestIni = false;
	FString IniFile;
	if (!Context.Load(*IniName, IniFile))
	{
		return false;
	}
	TArray<FString> Entries;
	Config.GetArray(TEXT("CoreRedirects"), TEXT("FunctionRedirects"), Entries);
	for (const FString& Entry : Entries)
	{
		Redirects.FindOrAdd(Entry) = AbsolutePath(IniFile);
	}
	return true;
}

void LocateFunctionRedirects(const TArray<FString>& ConfigFiles, TMap<FString, FString>& Redirects)
{
	for (const FString& File : ConfigFiles)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *File))
		{
			continue;
		}
		bool bCoreRedirects = false;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FString Line = Lines[Index].TrimStartAndEnd();
			if (Line.StartsWith(TEXT("[")))
			{
				bCoreRedirects = Line.Equals(TEXT("[CoreRedirects]"), ESearchCase::IgnoreCase);
			}
			FString Key;
			FString Value;
			if (bCoreRedirects && Line.Split(TEXT("="), &Key, &Value))
			{
				Key.TrimEndInline();
				Key.RemoveFromStart(TEXT("+"));
				Key.RemoveFromStart(TEXT("."));
				if (Key.Equals(TEXT("FunctionRedirects"), ESearchCase::IgnoreCase))
				{
					if (FString* Location = Redirects.Find(Value.TrimStartAndEnd()))
					{
						*Location = FString::Printf(TEXT("%s(%d,1)"), *AbsolutePath(File), Index + 1);
					}
				}
			}
		}
	}
}

TSharedRef<FJsonObject> JsonRedirectName(const FCoreRedirectObjectName& Name)
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Package"), Name.PackageName.IsNone() ? FString() : Name.PackageName.ToString());
	Json->SetStringField(TEXT("Class"), Name.OuterName.IsNone() ? FString() : Name.OuterName.ToString());
	Json->SetStringField(TEXT("Function"), Name.ObjectName.IsNone() ? FString() : Name.ObjectName.ToString());
	return Json;
}

TArray<TSharedPtr<FJsonValue>> JsonFunctionRedirects(const TMap<FString, FString>& Redirects)
{
	TArray<FString> Entries;
	Redirects.GetKeys(Entries);
	Entries.Sort();
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& Entry : Entries)
	{
		bool bRemoved = false;
		FParse::Bool(*Entry, TEXT("Removed="), bRemoved);
		if (bRemoved)
		{
			continue;
		}
		bool bSubstring = false;
		bool bWildcard = false;
		FString OldName;
		FString NewName;
		FParse::Bool(*Entry, TEXT("MatchSubstring="), bSubstring);
		FParse::Bool(*Entry, TEXT("MatchWildcard="), bWildcard);
		FParse::Value(*Entry, TEXT("OldName="), OldName);
		const bool bHasNewName = FParse::Value(*Entry, TEXT("NewName="), NewName) && !NewName.IsEmpty();
		const FCoreRedirect Redirect(ECoreRedirectFlags::Type_Function, OldName, NewName);
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("OldName"), JsonRedirectName(Redirect.OldName));
		Json->SetObjectField(TEXT("NewName"), JsonRedirectName(Redirect.NewName));
		Json->SetStringField(TEXT("Location"), Redirects.FindChecked(Entry));
		Json->SetStringField(TEXT("Entry"), Entry);
		Json->SetBoolField(TEXT("Pattern"), bSubstring || bWildcard);
		Json->SetBoolField(TEXT("HasNewName"), bHasNewName);
		Values.Add(MakeShared<FJsonValueObject>(Json));
	}
	return Values;
}
} // namespace

UModExportOptionsCommandlet::UModExportOptionsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UModExportOptionsCommandlet::Main(const FString& Params)
{
	using Mod::ExportMetadata::FOptions;
	const FString ProjectFile = AbsolutePath(FPaths::GetProjectFilePath());
	if (!IFileManager::Get().FileExists(*ProjectFile))
	{
		UE_LOG(LogTemp, Error, TEXT("An existing project is required to export metadata options."));
		return 1;
	}
	FString Output = FPaths::Combine(FPaths::ProjectDir(), UTF8_TO_TCHAR(Schema::SnapshotPath));
	FString OutputArgument;
	if (FParse::Value(*Params, TEXT("Output="), OutputArgument))
	{
		Output =
		    FPaths::IsRelative(OutputArgument) ? FPaths::Combine(FPaths::ProjectDir(), OutputArgument) : OutputArgument;
	}
	Output = AbsolutePath(Output);
	if (!FPaths::GetExtension(Output).Equals(TEXT("json"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Error, TEXT("Metadata option output must be a JSON file: %s"), *Output);
		return 1;
	}
	const FOptionBinding Bindings[] = {
	    {TEXT("ExportIds"), EOptionKind::APIId},
	    {TEXT("APIScopes"), EOptionKind::APIScope},
	    {TEXT("APIThreads"), EOptionKind::APIThread},
	    {TEXT("APIPermissions"), EOptionKind::APIPermission},
	    {TEXT("APIFailureRules"), EOptionKind::APIFailureRule},
	    {TEXT("EventSourceScopes"), EOptionKind::EventSourceScope},
	    {TEXT("EventPublishRules"), EOptionKind::EventPublishRule},
	};
	const TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	for (const FOptionBinding& Binding : Bindings)
	{
		Options->SetArrayField(Binding.Key, JsonStrings(FOptions::Load(Binding.Kind)));
	}
	TArray<FString> Stages;
	for (const char* Stage : Schema::HookStages)
	{
		Stages.Add(UTF8_TO_TCHAR(Stage));
	}
	Options->SetArrayField(TEXT("HookStages"), JsonStrings(Stages));

	TMap<FString, FString> Redirects;
	FConfigFile EngineConfig;
	if (!AddFunctionRedirects(FConfigContext::ReadIntoLocalFile(EngineConfig), EngineConfig, TEXT("Engine"), Redirects))
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot load the Engine configuration to export function redirects."));
		return 1;
	}
	TArray<FString> ConfigRoots;
	ConfigRoots.AddUnique(AbsolutePath(FPaths::EngineConfigDir()));
	ConfigRoots.AddUnique(AbsolutePath(FPaths::ProjectConfigDir()));
	for (const FString& Root : {FPaths::EngineDir(), FPaths::ProjectDir()})
	{
		for (const FString& ConfigDir : FPaths::GetExtensionDirs(Root, TEXT("Config")))
		{
			ConfigRoots.AddUnique(AbsolutePath(ConfigDir));
		}
	}
	TArray<FString> InputFiles;
	InputFiles.Add(ProjectFile);
	for (const FSoftObjectPath& Table : GetDefault<UGameplayTagsSettings>()->GameplayTagTableList)
	{
		FString Filename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Table.GetLongPackageName(), Filename,
		                                                      FPackageName::GetAssetPackageExtension()))
		{
			InputFiles.AddUnique(AbsolutePath(Filename));
		}
	}
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		ConfigRoots.AddUnique(AbsolutePath(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Config"))));
		const TArray<FString> Extensions = Plugin->GetExtensionBaseDirs();
		for (const FString& Extension : Extensions)
		{
			ConfigRoots.AddUnique(AbsolutePath(FPaths::Combine(Extension, TEXT("Config"))));
		}
		FConfigFile PluginConfig;
		AddFunctionRedirects(FConfigContext::ReadIntoPluginFile(PluginConfig, Plugin->GetBaseDir(), Extensions),
		                     PluginConfig, Plugin->GetName(), Redirects);
		InputFiles.AddUnique(AbsolutePath(Plugin->GetDescriptorFileName()));
	}
	FString GameModIni;
	if (FConfigCacheIni::LoadGlobalIniFile(GameModIni, TEXT("GameMod"), nullptr, false, true))
	{
		GConfig->Flush(false, GameModIni);
		InputFiles.AddUnique(AbsolutePath(GameModIni));
	}
	ConfigRoots.Sort();
	InputFiles.Sort();
	TArray<FString> ConfigFiles;
	for (const FString& Root : ConfigRoots)
	{
		if (IFileManager::Get().DirectoryExists(*Root))
		{
			IFileManager::Get().FindFilesRecursive(ConfigFiles, *Root, TEXT("*.ini"), true, false, false);
		}
	}
	ConfigFiles.Sort();
	LocateFunctionRedirects(ConfigFiles, Redirects);
	const TSharedRef<FJsonObject> Inputs = MakeShared<FJsonObject>();
	for (const FString& File : ConfigFiles)
	{
		if (!AddFingerprint(AbsolutePath(File), *Inputs))
		{
			return 1;
		}
	}
	for (const FString& File : InputFiles)
	{
		if (!AddFingerprint(File, *Inputs))
		{
			return 1;
		}
	}
	const TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
	Document->SetNumberField(TEXT("SchemaVersion"), Schema::SnapshotVersion);
	Document->SetStringField(TEXT("Project"), ProjectFile);
	Document->SetObjectField(TEXT("Options"), Options);
	Document->SetArrayField(TEXT("FunctionRedirects"), JsonFunctionRedirects(Redirects));
	Document->SetArrayField(TEXT("ConfigRoots"), JsonStrings(ConfigRoots));
	Document->SetArrayField(TEXT("InputFiles"), JsonStrings(InputFiles));
	Document->SetObjectField(TEXT("Inputs"), Inputs);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Document, Writer) ||
	    !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true) ||
	    !FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot write metadata option catalog: %s"), *Output);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("Exported shared metadata options to %s"), *Output);
	return 0;
}
