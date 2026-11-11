// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace Mod::Editor
{
struct FModPackageRequest;

struct FProjectModStage
{
	FString CookProject;
	FString CookPlugin;
	FString Configuration;
	TSharedPtr<FJsonObject> NativeCode;
	TMap<FString, FString> NativePayloads;
	TMap<FName, FString> MigratedPackages;
};

FString FindPackagingDotnet(const FString& EngineDirectory);
bool RunPackagingProcess(const FString& Executable, const FString& Arguments, double Deadline);
bool StageProjectMod(const FModPackageRequest& Request, const FString& WorkDirectory, double Deadline,
                     FProjectModStage& OutStage, FString& Error);
} // namespace Mod::Editor
