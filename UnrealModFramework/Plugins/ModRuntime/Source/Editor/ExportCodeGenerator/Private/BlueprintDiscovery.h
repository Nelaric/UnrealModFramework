// Copyright (c) 2026 Nelaric Contributors
#pragma once

#include "AssetRegistry/AssetData.h"
#include "Containers/Array.h"
#include "UObject/NameTypes.h"

class IAssetRegistry;
class UBlueprint;

namespace Mod::ExportCodeGenerator
{
TArray<FAssetData> FindExportCandidates(IAssetRegistry& Registry);
FName ResolveExportOwner(const UBlueprint& Blueprint, FName ExplicitOwner);
} // namespace Mod::ExportCodeGenerator
