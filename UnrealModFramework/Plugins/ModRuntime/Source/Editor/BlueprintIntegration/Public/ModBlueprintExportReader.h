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

/// A saved Blueprint extension declaration and its explicit point usage.
struct FExportedExtension
{
	FGuid Guid;
	FName Name;
	FString Version;
	FName Permission;
	FName ScalarProperty;
	FString StructPath;
	FString StructLayout;
	FString ObjectPath;
	FName ObjectViewId;
	bool bHook = false;
	bool bAuthorityOnly = true;
	bool bSupportedPayload = false;
	bool bHasPoint = false;
	bool bImmediateUnsubscribe = false;
	FString Scope;
	FString PublishRule;
	TArray<FString> Stages;
	TArray<FString> Operations;
};

/// Reads a Blueprint's declared Hook and Event records after compiling it.
BLUEPRINTINTEGRATION_API TArray<FExportedExtension> ReadExtensions(const UBlueprint& Blueprint);
} // namespace Mod::BlueprintIntegration
