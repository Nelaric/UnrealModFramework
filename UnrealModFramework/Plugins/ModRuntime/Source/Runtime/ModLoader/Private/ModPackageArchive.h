// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "ModLoader.h"
#include "Templates/Atomic.h"

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
	bool bSkipped = false;
};

struct FPreparationBudget
{
	double Deadline;
	const TAtomic<bool>* Cancellation = nullptr;

	bool IsExpired() const;
};

bool Fail(FLoadResult& Result, ELoadError Error, FString Detail);
bool ValidatePolicy(const FLoadPolicy& Policy, FLoadResult& Result);
bool IsSafeRelativePath(FStringView Path);
bool FindArchives(FPreparationBudget Budget, TArray<FString>& Paths, FLoadResult& Result);
bool PrepareArchive(const FLoadPolicy& Policy, FPreparationBudget Budget, FLoadResult& Result,
                    FPreparedPackage& OutPackage, const TSet<FString>* MountedIds = nullptr);
} // namespace Mod::Loader::Private
