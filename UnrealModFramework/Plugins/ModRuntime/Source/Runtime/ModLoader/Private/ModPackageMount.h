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

#if WITH_EDITOR && PLATFORM_WINDOWS
void InitializePIEPakPlatform();
#endif
bool CheckBackend(EBackend Backend, bool bPIE, FLoadResult& Result);
bool MountPrepared(const FPreparedPackage& Package, FLoadResult& Result, FMountedResources& Resources);
bool ReleaseModPackages(TConstArrayView<FName> PackageNames, FString& Reason);
bool UnmountPackage(const FMountedPackage& Package, const FMountedResources& Resources);
} // namespace Mod::Loader::Private
