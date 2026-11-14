// Copyright (c) 2026 Nelaric Contributors
#include "ModExportBlueprintsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ModAPIBlueprint.h"
#include "ModBlueprintExportReader.h"
#include "ModExportMetadataOptions.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace
{
using Mod::API::EBlueprintParameter;
using Mod::API::FBlueprintCall;
using Mod::BlueprintIntegration::FExportedAPI;
using Mod::BlueprintIntegration::FExportedExtension;
using Mod::ExportMetadata::EOptionKind;
using Mod::ExportMetadata::FOptions;

void RecordInputs(FName Package, IAssetRegistry& Registry, TSet<FName>& Seen, TSet<FString>& Files)
{
	TArray<FName> Pending{Package};
	while (!Pending.IsEmpty())
	{
		const FName Name = Pending.Pop(EAllowShrinking::No);
		if (Seen.Contains(Name))
		{
			continue;
		}
		Seen.Add(Name);
		FString Filename;
		if (FPackageName::DoesPackageExist(Name.ToString(), &Filename))
		{
			Files.Add(FPaths::ConvertRelativePathToFull(Filename));
			TArray<FName> Dependencies;
			Registry.GetDependencies(Name, Dependencies, UE::AssetRegistry::EDependencyCategory::Package,
			                         UE::AssetRegistry::EDependencyQuery::Hard);
			Pending.Append(Dependencies);
		}
	}
}

bool WriteAPI(const FExportedAPI& API, UBlueprint& Blueprint, TSet<FGuid>& Ids, TArray<TSharedPtr<FJsonValue>>& Output)
{
	UFunction* Function = Blueprint.GeneratedClass->FindFunctionByName(API.Function, EIncludeSuperFlag::ExcludeSuper);
	if (!Function || !API.Guid.IsValid() || Ids.Contains(API.Guid) || API.Scope != TEXT("World") ||
	    API.Thread != TEXT("GameThread") || API.FailureRule != TEXT("ReturnError") ||
	    !FOptions::Contains(EOptionKind::APIScope, API.Scope) ||
	    !FOptions::Contains(EOptionKind::APIThread, API.Thread) ||
	    !FOptions::Contains(EOptionKind::APIFailureRule, API.FailureRule))
	{
		UE_LOG(LogTemp, Error, TEXT("%s: Invalid Blueprint API declaration or policy for %s."),
		       *Blueprint.GetPathName(), *API.Function.ToString());
		return false;
	}
	const auto Shape = FBlueprintCall::Describe(*Function);
	if (!Shape.IsSuccess())
	{
		UE_LOG(
		    LogTemp, Error,
		    TEXT(
		        "%s: %s must be a synchronous instance function with scalar parameters and at most one scalar result."),
		    *Blueprint.GetPathName(), *API.Function.ToString());
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> Permissions;
	TSet<FName> UniquePermissions;
	for (const FString& Permission : API.Permissions)
	{
		const FName Name(*Permission);
		if (!FOptions::Contains(EOptionKind::APIPermission, Permission) || UniquePermissions.Contains(Name))
		{
			UE_LOG(LogTemp, Error, TEXT("%s: Invalid or repeated Blueprint API permission '%s'."),
			       *Blueprint.GetPathName(), *Permission);
			return false;
		}
		UniquePermissions.Add(Name);
		Permissions.Add(MakeShared<FJsonValueString>(Permission));
	}
	TArray<TSharedPtr<FJsonValue>> Parameters;
	for (const auto& Parameter : Shape.GetValue())
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Parameter.Name.ToString());
		Json->SetStringField(TEXT("propertyClass"), Parameter.PropertyClass.ToString());
		Json->SetStringField(TEXT("role"), Parameter.Role == EBlueprintParameter::Input    ? TEXT("Input")
		                                   : Parameter.Role == EBlueprintParameter::Output ? TEXT("Output")
		                                                                                   : TEXT("Return"));
		Parameters.Add(MakeShared<FJsonValueObject>(Json));
	}
	auto Json = MakeShared<FJsonObject>();
	const FString FunctionName = API.Function.ToString();
	Json->SetStringField(TEXT("guid"), API.Guid.ToString(EGuidFormats::Digits));
	Json->SetStringField(TEXT("function"), FunctionName);
	Json->SetStringField(TEXT("name"), API.PublicName.IsEmpty() ? FunctionName.RightChop(7) : API.PublicName);
	Json->SetStringField(TEXT("version"), API.Version.IsEmpty() ? TEXT("1.0.0") : API.Version);
	Json->SetArrayField(TEXT("permissions"), MoveTemp(Permissions));
	Json->SetArrayField(TEXT("parameters"), MoveTemp(Parameters));
	Ids.Add(API.Guid);
	Output.Add(MakeShared<FJsonValueObject>(Json));
	return true;
}

bool WriteExtension(const FExportedExtension& Entry, UBlueprint& Blueprint, TSet<FGuid>& Ids,
                    TArray<TSharedPtr<FJsonValue>>& Output)
{
	const auto Fail = [&](const TCHAR* Reason)
	{
		UE_LOG(LogTemp, Error, TEXT("%s: %s: %s"), *Blueprint.GetPathName(), *Entry.Name.ToString(), Reason);
		return false;
	};
	if (!Entry.bHasPoint)
	{
		UE_LOG(LogTemp, Display, TEXT("%s: %s is declared; connect its extension node to export it."),
		       *Blueprint.GetPathName(), *Entry.Name.ToString());
		return true;
	}
	if (!Entry.Guid.IsValid() || Ids.Contains(Entry.Guid) || !Entry.bSupportedPayload)
	{
		return Fail(TEXT("Expected a unique scalar declaration for the connected extension node."));
	}
	auto Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("guid"), Entry.Guid.ToString(EGuidFormats::Digits).ToLower());
	Json->SetStringField(TEXT("name"), Entry.Name.ToString());
	Json->SetStringField(TEXT("version"), Entry.Version);
	Json->SetStringField(TEXT("permission"), Entry.Permission.IsNone() ? FString() : Entry.Permission.ToString());
	Json->SetBoolField(TEXT("hook"), Entry.bHook);
	Json->SetBoolField(TEXT("authorityOnly"), Entry.bAuthorityOnly);
	if (Entry.bHook)
	{
		UFunction* Function = Blueprint.GeneratedClass->FindFunctionByName(Entry.Name, EIncludeSuperFlag::ExcludeSuper);
		if (!Function || Entry.Stages.Contains(TEXT("On")) || Entry.Stages.IsEmpty())
		{
			return Fail(TEXT("Expected a compiled original graph and Pre/Post stages."));
		}
		const auto Shape = FBlueprintCall::Describe(*Function);
		if (!Shape.IsSuccess() || Shape.GetValue().Num() != 2)
		{
			return Fail(TEXT("Hook original graph must take one Float and produce one Float."));
		}
		const auto& Parameters = Shape.GetValue();
		if (Parameters[0].PropertyClass != TEXT("FloatProperty") || Parameters[0].Role != EBlueprintParameter::Input ||
		    Parameters[1].PropertyClass != TEXT("FloatProperty") || Parameters[1].Role == EBlueprintParameter::Input)
		{
			return Fail(TEXT("Hook original graph must take one Float and produce one Float."));
		}
		Json->SetStringField(TEXT("input"), Parameters[0].Name.ToString());
		Json->SetStringField(TEXT("output"), Parameters[1].Name.ToString());
		Json->SetStringField(TEXT("outputRole"),
		                     Parameters[1].Role == EBlueprintParameter::Return ? TEXT("Return") : TEXT("Output"));
		Json->SetBoolField(TEXT("pre"), Entry.Stages.Contains(TEXT("Pre")));
		Json->SetBoolField(TEXT("post"), Entry.Stages.Contains(TEXT("Post")));
		const int32 Operations = (Entry.Operations.Contains(TEXT("ModifyInputs")) ? 1 : 0) |
		                         (Entry.Operations.Contains(TEXT("Cancel")) ? 2 : 0) |
		                         (Entry.Operations.Contains(TEXT("ModifyResult")) ? 4 : 0);
		Json->SetNumberField(TEXT("operations"), Operations);
	}
	else if (Entry.Scope != TEXT("World") || Entry.PublishRule != TEXT("Immediate"))
	{
		return Fail(TEXT("Event must select World scope and Immediate publication."));
	}
	Ids.Add(Entry.Guid);
	Output.Add(MakeShared<FJsonValueObject>(Json));
	return true;
}

bool WriteType(const FJsonObject& Config, IAssetRegistry& Registry, TSet<FGuid>& Ids, TSet<FName>& Seen,
               TSet<FString>& Files, TArray<TSharedPtr<FJsonValue>>& Output)
{
	FString Asset, TypeId, Module;
	if (!Config.TryGetStringField(TEXT("asset"), Asset) || !Config.TryGetStringField(TEXT("typeId"), TypeId) ||
	    !Config.TryGetStringField(TEXT("module"), Module) || TypeId.IsEmpty() || Module.IsEmpty() ||
	    !FPackageName::IsValidObjectPath(Asset))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid Blueprint export configuration. Expected asset, typeId and module."));
		return false;
	}
	const auto Data = Registry.GetAssetByObjectPath(FSoftObjectPath(Asset), true);
	auto* Blueprint = Cast<UBlueprint>(Data.GetAsset());
	FString Filename;
	if (!Blueprint || Blueprint->GetPathName() != Asset || Blueprint->GetOutermost()->IsDirty() ||
	    Blueprint->BlueprintType == BPTYPE_LevelScript || Blueprint->BlueprintType == BPTYPE_FunctionLibrary ||
	    !FPackageName::DoesPackageExist(Blueprint->GetOutermost()->GetName(), &Filename))
	{
		UE_LOG(LogTemp, Error, TEXT("%s: Expected a saved, loadable host Blueprint asset."), *Asset);
		return false;
	}
	RecordInputs(Data.PackageName, Registry, Seen, Files);
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &Results);
	if (Results.NumErrors || !Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass) ||
	    Blueprint->Status == BS_Error || Blueprint->GeneratedClass->IsEditorOnly())
	{
		UE_LOG(LogTemp, Error, TEXT("%s: Blueprint compilation did not produce a runtime class."), *Asset);
		return false;
	}
	auto APIs = Mod::BlueprintIntegration::ReadAPIs(*Blueprint);
	APIs.Sort([](const FExportedAPI& A, const FExportedAPI& B) { return A.Guid < B.Guid; });
	auto Extensions = Mod::BlueprintIntegration::ReadExtensions(*Blueprint);
	Extensions.Sort([](const FExportedExtension& A, const FExportedExtension& B) { return A.Guid < B.Guid; });
	if (APIs.IsEmpty() && Extensions.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("%s: Configured Blueprint has no Mod declarations."), *Asset);
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> ApiRecords;
	for (const FExportedAPI& API : APIs)
	{
		if (!WriteAPI(API, *Blueprint, Ids, ApiRecords))
		{
			return false;
		}
	}
	auto Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("typeId"), TypeId);
	Json->SetStringField(TEXT("module"), Module);
	Json->SetStringField(TEXT("asset"), Asset);
	Json->SetStringField(TEXT("class"), Blueprint->GeneratedClass->GetPathName());
	Json->SetStringField(TEXT("file"), FPaths::ConvertRelativePathToFull(Filename));
	Json->SetArrayField(TEXT("apis"), MoveTemp(ApiRecords));
	TArray<TSharedPtr<FJsonValue>> ExtensionRecords;
	for (const auto& Entry : Extensions)
	{
		if (!WriteExtension(Entry, *Blueprint, Ids, ExtensionRecords))
		{
			return false;
		}
	}
	if (APIs.IsEmpty() && ExtensionRecords.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("%s: No declaration has a runtime implementation."), *Asset);
		return false;
	}
	Json->SetArrayField(TEXT("extensions"), MoveTemp(ExtensionRecords));
	Output.Add(MakeShared<FJsonValueObject>(Json));
	return true;
}
} // namespace

UModExportBlueprintsCommandlet::UModExportBlueprintsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UModExportBlueprintsCommandlet::Main(const FString& Params)
{
	FString Input, Output, Text;
	FParse::Value(*Params, TEXT("Input="), Input);
	FParse::Value(*Params, TEXT("Output="), Output);
	TSharedPtr<FJsonObject> Config;
	const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
	if (Output.IsEmpty() || !FFileHelper::LoadFileToString(Text, *Input) ||
	    !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config ||
	    !Config->TryGetArrayField(TEXT("blueprintExports"), Types))
	{
		UE_LOG(LogTemp, Error,
		       TEXT("Expected -Input=<ModHeaderTool.json> with blueprintExports and -Output=<snapshot>."));
		return 1;
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.SearchAllAssets(true);
	TSet<FGuid> Ids;
	TSet<FName> Seen;
	TSet<FString> Files;
	TArray<TSharedPtr<FJsonValue>> ExportedTypes;
	for (const auto& Type : *Types)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!Type->TryGetObject(Entry) || !WriteType(**Entry, Registry, Ids, Seen, Files, ExportedTypes))
		{
			return 1;
		}
	}
	auto Document = MakeShared<FJsonObject>();
	Document->SetStringField(TEXT("format"), TEXT("UMF.BlueprintExports"));
	Document->SetNumberField(TEXT("schemaVersion"), 1);
	Document->SetArrayField(TEXT("types"), MoveTemp(ExportedTypes));
	TArray<FString> SortedFiles = Files.Array();
	SortedFiles.Sort();
	TArray<TSharedPtr<FJsonValue>> Inputs;
	for (const FString& File : SortedFiles)
	{
		Inputs.Add(MakeShared<FJsonValueString>(File));
	}
	Document->SetArrayField(TEXT("inputs"), MoveTemp(Inputs));
	Text.Reset();
	if (!FJsonSerializer::Serialize(Document, TJsonWriterFactory<>::Create(&Text)) ||
	    !FFileHelper::SaveStringToFile(Text, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not write Blueprint export snapshot '%s'."), *Output);
		return 1;
	}
	return 0;
}
