// Copyright (c) 2026 Nelaric Contributors
#include "ModExportOptionsCommandlet.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "GameplayTagsSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "ModExportMetadataOptions.h"
#include "ModExportMetadataSchema.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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
	    {TEXT("ExportIds"), EOptionKind::ApiId},
	    {TEXT("ApiScopes"), EOptionKind::ApiScope},
	    {TEXT("ApiThreads"), EOptionKind::ApiThread},
	    {TEXT("ApiPermissions"), EOptionKind::ApiPermission},
	    {TEXT("ApiFailureRules"), EOptionKind::ApiFailureRule},
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

	TArray<FString> ConfigRoots;
	ConfigRoots.AddUnique(AbsolutePath(FPaths::EngineConfigDir()));
	ConfigRoots.AddUnique(AbsolutePath(FPaths::ProjectConfigDir()));
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
