// Copyright (c) 2026 Nelaric Contributors
/** @file ModBlueprintExportReader.h */
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Misc/Guid.h"
#include "UObject/NameTypes.h"

class UBlueprint;

namespace Mod::BlueprintIntegration
{
/// An authored Blueprint API and its compiled member name.
struct FExportedAPI
{
	/// Persistent identity of the export declaration.
	FGuid Guid;
	/// Stable name published in the SDK.
	FString PublicName;
	/// Name of the compiled member in the Blueprint class.
	FName Function;
	/// Declared API version.
	FString Version;
	/// Authored scope identifier.
	FString Scope;
	/// Authored thread identifier.
	FString Thread;
	/// Authored failure rule identifier.
	FString FailureRule;
	/// Permissions required by the export.
	TArray<FString> Permissions;
};

/** @brief Reads the explicit API declarations on a Blueprint.
 * @param Blueprint The loaded authoring asset, accessed on the game thread.
 * @return Declarations owned by that asset.
 */
BLUEPRINTINTEGRATION_API TArray<FExportedAPI> ReadAPIs(const UBlueprint& Blueprint);
} // namespace Mod::BlueprintIntegration
