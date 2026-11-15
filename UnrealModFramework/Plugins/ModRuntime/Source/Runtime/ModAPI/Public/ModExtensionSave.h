// Copyright (c) 2026 Nelaric Contributors
/** @file ModExtensionSave.h */
#pragma once

#include "CoreMinimal.h"

class FArchive;
class UModExtensionSubsystem;

namespace Mod::Extension
{
/// One opaque gameplay block owned by a single Mod and schema.
struct FModSaveBlock
{
	/// Stable block name within the owning Mod.
	FString Id;
	/// Positive, sequential data schema version.
	uint32 Version = 1;
	/// Bounded game-defined bytes.
	TArray<uint8> Bytes;
};

/// A persistent cross-Mod or base game reference.
struct FModSaveReference
{
	/// Stable owning Mod ID. Empty identifies the base game.
	FString ModId;
	/// Stable content slot. Empty for persistent object references.
	FString Slot;
	/// Logical content ID or persistent object ID.
	FString Id;
};

/// A Mod's data and persisted release identity.
struct FModSaveRecord
{
	/// Stable lowercase Mod ID.
	FString ModId;
	/// Shared gameplay release identity.
	FString ReleaseId;
	/// Compatibility digest for the shared gameplay contract.
	FString CompatibilityDigest;
	/// Exact original artifact digest for provenance.
	FString ArchiveDigest;
	/// Whether a missing Mod must prevent loading.
	bool bRequired = true;
	/// Namespaced opaque data blocks.
	TArray<FModSaveBlock> Blocks;
};

/// A versioned save envelope containing only persistent identities and bytes.
struct FModSaveSnapshot
{
	/// Game-specific save schema.
	uint32 GameVersion = 1;
	/// Base release used to write this save.
	FString BaseRelease;
	/// Generated host export contract identity.
	FString ExportContract;
	/// Authoritative gameplay plan at save time.
	FString GameplayDigest;
	/// Data for installed and retained optional Mods.
	TArray<FModSaveRecord> Mods;
	/// Logical content and game-owned persistent references.
	TArray<FModSaveReference> References;
	/// Opaque base-game payload.
	TArray<uint8> GameData;
};

/// Transforms one schema version into its successor. Runs on the game thread.
using FModSaveMigration = TFunction<bool(TArray<uint8>& Bytes, FString& Error)>;

/// Game-owned version targets, compatibility and reference migrations.
struct FModSavePolicy
{
	/// Current game save version.
	uint32 GameVersion = 1;
	/// Current base release identity.
	FString BaseRelease;
	/// Current generated export contract identity.
	FString ExportContract;
	/// Explicitly approves an obsolete base or export contract.
	TFunction<bool(const FModSaveSnapshot&)> AcceptOlderHost;
	/// Explicitly approves a prior gameplay release for an installed Mod.
	TFunction<bool(const FModSaveRecord&, FStringView, FStringView)> AcceptOlderMod;
	/// Returns the current version for a known Mod block. Zero rejects it.
	TFunction<uint32(FStringView, FStringView)> BlockVersion;
	/// Resolves each version step to a migration registered by the game.
	TFunction<const FModSaveMigration*(FStringView, FStringView, uint32)> FindMigration;
	/// Rewrites or rejects an unresolved persistent reference.
	TFunction<bool(FModSaveReference&, FString&)> MigrateReference;
	/// Checks game-owned persistent IDs before gameplay is restored.
	TFunction<bool(const FModSaveReference&)> ValidateObjectReference;
};

/// Reads and publishes complete save envelopes on the game thread.
class MODAPI_API FModExtensionSave
{
public:
	/// Captures current package and plan metadata and retains optional blocks.
	static bool Capture(const UModExtensionSubsystem& Extensions, const FModSavePolicy& Policy,
	                    FModSaveSnapshot& Snapshot, FString& Error);
	/** @brief Migrates a copy, then publishes it only after validation.
	 * @details Invoke before restoring gameplay. Missing optional Mod records
	 * retain their original bytes, versions and provenance.
	 */
	static bool Migrate(const UModExtensionSubsystem& Extensions, const FModSavePolicy& Policy,
	                    const FModSaveSnapshot& Original, FModSaveSnapshot& Result, FString& Error);
	/// Checks save host and package requirements before World effects.
	static bool Admit(const UModExtensionSubsystem& Extensions, const FModSavePolicy& Policy,
	                  const FModSaveSnapshot& Original, FString& Error);
	/// Deserializes a bounded binary save without invoking gameplay code.
	static bool ReadFile(const FString& Source, FModSaveSnapshot& Snapshot, FString& Error);
	/** @brief Publishes a complete save at a new destination.
	 * @details Use a new slot for a migrated save. Existing files are preserved.
	 */
	static bool WriteNewFile(const FString& Destination, const FModSaveSnapshot& Snapshot, FString& Error);

private:
	static bool Serialize(FArchive& Ar, FModSaveSnapshot& Snapshot);
};
} // namespace Mod::Extension
