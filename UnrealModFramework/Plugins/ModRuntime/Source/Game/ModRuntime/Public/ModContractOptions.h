// Copyright (c) 2026 Nelaric Contributors
/** @file ModContractOptions.h */
#pragma once

#include "CoreMinimal.h"

namespace Mod::Contract
{
/// A contract field backed by GameplayTags or shared GameMod config.
enum class EOptionKind : uint8
{
	/// Registered GameplayTag API IDs.
	ApiId,
	/// API instance scopes.
	ApiScope,
	/// API calling-thread policies.
	ApiThread,
	/// Individual API permission identifiers.
	ApiPermission,
	/// API failure policies.
	ApiFailureRule,
	/// Registered GameplayTag Event IDs.
	EventId,
	/// Event source scopes.
	EventSourceScope,
	/// Event publication policies.
	EventPublishRule,
};

/// Reads the same project choices used by Blueprint contract details.
class FOptions
{
public:
	/** @brief Returns the available choices for a contract field.
	 *
	 * @details Reads API and Event IDs from the global GameplayTags
	 * dictionary and other choices from the active GameMod config on the
	 * game thread. IDs are sorted; config choices retain their order.
	 * Empty and duplicate entries are ignored.
	 *
	 * @param Kind Which contract field to read.
	 * @return Configured identifiers, or an empty array if none exist.
	 */
	static MODRUNTIME_API TArray<FString> Load(EOptionKind Kind);

	/** @brief Checks one identifier against the shared project choices.
	 *
	 * @details Call on the game thread after Unreal config is ready. The
	 * comparison is case sensitive. An empty value is never allowed.
	 *
	 * @param Kind Which contract field to check.
	 * @param Value Identifier to check.
	 * @return Whether the identifier is configured for that field.
	 */
	static MODRUNTIME_API bool Contains(EOptionKind Kind, const FString& Value);
};
} // namespace Mod::Contract
