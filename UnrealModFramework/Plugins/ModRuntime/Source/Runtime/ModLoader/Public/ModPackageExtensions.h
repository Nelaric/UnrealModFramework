// Copyright (c) 2026 Nelaric Contributors
/** @file ModPackageExtensions.h */
#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "UObject/SoftObjectPath.h"

namespace Mod::Loader
{
/// Inclusive minimum and exclusive maximum version, when specified.
struct FVersionRange
{
	FString Minimum;
	FString Before;

	MODLOADER_API bool Contains(const FString& Version) const;
};

/// A package dependency resolved before extension registration.
struct FModDependency
{
	FString ModId;
	FVersionRange Versions;
	bool bRequired = true;
};

/// An exported service a mod declares that it will consume.
struct FServiceDependency
{
	FName Id;
	FString Provider;
	FName TypeId;
	FString Signature;
	FVersionRange Versions;
	bool bRequired = true;
};

/// A service advertised before the package contributes any executable code.
struct FServiceOffer
{
	FName Id;
	FName TypeId;
	FString Signature;
	FString Version;
	FName Permission;
	FName Patch;
};

/// The operations supported by a game-defined content slot.
enum class EContentAction : uint8
{
	/// Adds a logical content ID.
	Provide,
	/// Offers a replacement asset.
	Replace,
	/// Explicitly disables the ID.
	Disable,
};

/// A candidate for one logical content ID.
struct FContentCandidate
{
	FName Slot;
	FName Id;
	EContentAction Action = EContentAction::Provide;
	FSoftObjectPath Asset;
	int32 Priority = 0;
};

/// The effects supported at a game-defined World anchor.
enum class EWorldPatchOperation : uint8
{
	/// Creates an Actor owned by the Patch.
	SpawnActor,
	/// Creates a scene component on the anchor's approved attachment.
	AttachComponent,
	/// Registers a cooked data asset with the host.
	RegisterData,
};

/// One cooked effect and its dependencies at a game-owned anchor.
struct FWorldPatch
{
	FName Id;
	FName Anchor;
	FName ConflictKey;
	/// Type of effect requested.
	EWorldPatchOperation Operation = EWorldPatchOperation::SpawnActor;
	/// Cooked Actor class for a spawn.
	FSoftClassPath ActorClass;
	/// Cooked scene component class for an attachment.
	FSoftClassPath ComponentClass;
	/// Stable data key within this anchor.
	FName DataId;
	/// Cooked data asset for a registration.
	FSoftObjectPath DataAsset;
	TArray<FName> After;
	/// Named, declared services that must be available before this Patch.
	TArray<FName> RequiresServices;
	/// Seconds an optional Patch may wait for its first anchor.
	float WaitSeconds = 0;
	bool bRequired = true;
};

/// Pins a required Hook or Event to its generated signature.
struct FExtensionDependency
{
	FName Id;
	FString Signature;
};

/// Verified declarations from the versioned package extension section.
struct FPackageExtensions
{
	TArray<FModDependency> Mods;
	TArray<FServiceDependency> Services;
	TArray<FServiceOffer> Provides;
	TArray<FString> IncompatibleMods;
	TSet<FName> RequiredGrants;
	TArray<FExtensionDependency> Contracts;
	TArray<FContentCandidate> Content;
	TArray<FWorldPatch> Patches;
	/// Independently compiled DLL that exports UMF_RegisterExtensionsV1.
	FString NativeExtensionFile;
	int32 SchemaVersion = 0;
	bool bDeclared = false;
};
} // namespace Mod::Loader
