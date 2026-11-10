// Copyright (c) 2026 Nelaric Contributors
/** @file ModExportMetadataOptions.h */
#pragma once

#include "CoreMinimal.h"

namespace Mod::ExportMetadata
{
/// An export metadata field backed by GameplayTags or GameMod config.
enum class EOptionKind : uint8
{
	/// Registered GameplayTag API IDs.
	APIId,
	/// API instance scopes.
	APIScope,
	/// API calling-thread policies.
	APIThread,
	/// Individual API permission identifiers.
	APIPermission,
	/// API failure policies.
	APIFailureRule,
	/// Registered GameplayTag Event IDs.
	EventId,
	/// Event source scopes.
	EventSourceScope,
	/// Event publication policies.
	EventPublishRule,
};

/// Reads the same project choices used by Blueprint export details.
class FOptions
{
public:
	/** @brief Returns the available choices for an export metadata field.
	 *
	 * @details Reads API and Event IDs from the global GameplayTags
	 * dictionary and other choices from the ModExportMetadata config section
	 * on the game thread. IDs are sorted; config choices retain their order.
	 * Empty and duplicate entries are ignored.
	 *
	 * @param Kind Which export metadata field to read.
	 * @return Configured identifiers, or an empty array if none exist.
	 */
	static EXPORTMETADATA_API TArray<FString> Load(EOptionKind Kind);

	/** @brief Checks one identifier against the shared project choices.
	 *
	 * @details Call on the game thread after Unreal config is ready. The
	 * comparison is case sensitive. An empty value is never allowed.
	 *
	 * @param Kind Which export metadata field to check.
	 * @param Value Identifier to check.
	 * @return Whether the identifier is configured for that field.
	 */
	static EXPORTMETADATA_API bool Contains(EOptionKind Kind, const FString& Value);
};
} // namespace Mod::ExportMetadata
