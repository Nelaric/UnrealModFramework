// Copyright (c) 2026 Nelaric Contributors
/** @file ModPackageExtensions.h */
#pragma once

#include "Containers/Array.h"
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

/// A request to instantiate a cooked class at a game-owned anchor.
struct FWorldPatch
{
	FName Id;
	FName Anchor;
	FName ConflictKey;
	FSoftClassPath ActorClass;
	TArray<FName> After;
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
	TArray<FExtensionDependency> Contracts;
	TArray<FContentCandidate> Content;
	TArray<FWorldPatch> Patches;
	bool bDeclared = false;
};
} // namespace Mod::Loader
