// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "IO/IoChunkId.h"
#include "ModPackageArchive.h"

namespace Mod::Loader::Private
{
struct FMountedContainer
{
	FString PakPath;
	FIoChunkId HeaderChunk = FIoChunkId::InvalidChunkId;
};

struct FMountedResources
{
	FString ContentDirectory;
	FString ShaderLibrary;
	TArray<FMountedContainer> Containers;
};

bool CheckBackend(EBackend Backend, bool bPIE, FLoadResult& Result);
bool MountPrepared(const FPreparedPackage& Package, FLoadResult& Result, FMountedResources* Resources);

#if WITH_EDITOR
bool ReleasePIEPackages(TConstArrayView<FName> PackageNames, FString& Reason);
bool UnmountPIE(const FMountedPackage& Package, const FMountedResources& Resources);
#endif
} // namespace Mod::Loader::Private
