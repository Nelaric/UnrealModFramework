// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModLoader.h"

namespace Mod::Loader::Private
{
struct FNativeContainer
{
	FString PakPath;
	FString TocPath;
};

struct FPreparedPackage
{
	FString PluginName;
	FString StageRoot;
	FString ContentRelativePath;
	TArray<FNativeContainer> Containers;
};

bool Fail(FLoadResult& Result, ELoadError Error, FString Detail);
bool ValidatePolicy(const FLoadPolicy& Policy, FLoadResult& Result);
bool IsSafeRelativePath(FStringView Path);
bool PrepareArchive(const FLoadPolicy& Policy, double Deadline, FLoadResult& Result, FPreparedPackage& OutPackage);
} // namespace Mod::Loader::Private
