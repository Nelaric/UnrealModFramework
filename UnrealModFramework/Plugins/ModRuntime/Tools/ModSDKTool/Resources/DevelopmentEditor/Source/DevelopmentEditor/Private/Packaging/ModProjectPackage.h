// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "CoreMinimal.h"

namespace Mod::Editor
{
struct FModPackageRequest;

struct FProjectModStage
{
	FString CookProject;
	FString CookPlugin;
	TMap<FName, FString> MigratedPackages;
};

FString FindPackagingDotnet(const FString& EngineDirectory);
bool RunPackagingProcess(const FString& Executable, const FString& Arguments, double Deadline);
bool StageProjectContent(const FModPackageRequest& Request, const FString& WorkDirectory, FProjectModStage& OutStage);
} // namespace Mod::Editor
