// Copyright (c) 2026 Nelaric Contributors
#include "ModPackageCommandlet.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Packaging/ModPackageBuilder.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogModPackageCommandlet, Log, All);

UModPackageCommandlet::UModPackageCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UModPackageCommandlet::Main(const FString& Params)
{
	FString RequestFile;
	if (!FParse::Value(*Params, TEXT("Request="), RequestFile))
	{
		UE_LOG(LogModPackageCommandlet, Error, TEXT("Usage: -run=ModPackage -Request=<Request.json>"));
		return 1;
	}
	RequestFile = FPaths::ConvertRelativePathToFull(RequestFile);
	Mod::Editor::FModPackageRequest Request;
	FText Error;
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("format"), TEXT("UMF.ModPackageBuildResult"));
	Result->SetNumberField(TEXT("schemaVersion"), 1);
	const FString WorkDirectory = FPaths::GetPath(RequestFile);
	const bool bSuccess = Mod::Editor::FModPackageRequest::Read(RequestFile, Request, Error) &&
	                      Mod::Editor::FModPackageBuilder::Build(Request, WorkDirectory, Result);
	if (!Error.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), Error.ToString());
	}
	Result->SetStringField(TEXT("status"), bSuccess ? TEXT("Packaged") : TEXT("Failed"));
	FString Json;
	FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
	const FString ResultFile = FPaths::Combine(WorkDirectory, TEXT("Result.json"));
	if (!FFileHelper::SaveStringToFile(Json, *ResultFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogModPackageCommandlet, Error, TEXT("Could not write build result: %s"), *ResultFile);
		return 1;
	}
	UE_LOG(LogModPackageCommandlet, Display, TEXT("Mod package result: %s"), *Json);
	return bSuccess ? 0 : 1;
}
