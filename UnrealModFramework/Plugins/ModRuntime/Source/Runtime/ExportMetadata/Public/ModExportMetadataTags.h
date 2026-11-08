// Copyright (c) 2026 Nelaric Contributors
/** @file ModExportMetadataTags.h */
#pragma once

#include "GameplayTagContainer.h"

#include "ModExportMetadataTags.generated.h"

/// A typed API ID backed by the global GameplayTags dictionary.
USTRUCT(BlueprintType)
struct EXPORTMETADATA_API FModApiTag : public FGameplayTag
{
	GENERATED_BODY()

public:
	/// Creates an empty API ID.
	FModApiTag() = default;

	/** @brief Resolves a registered API ID on the game thread.
	 *
	 * @param TagName Full GameplayTag path.
	 * @param bErrorIfNotFound Report a missing tag.
	 * @return Valid API ID, or an invalid tag.
	 */
	static FModApiTag Request(FName TagName, bool bErrorIfNotFound = false);

	/** @brief Converts a registered GameplayTag to an API ID.
	 *
	 * @param Tag Tag to check in the global dictionary.
	 * @return Valid API ID, or an invalid tag.
	 */
	static FModApiTag TryConvert(const FGameplayTag& Tag);

	/// Returns whether this tag exists in the global dictionary.
	bool IsRegistered() const;

private:
	explicit FModApiTag(const FGameplayTag& Tag);
};

template <> struct TStructOpsTypeTraits<FModApiTag> : public TStructOpsTypeTraitsBase2<FModApiTag>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithPostSerialize = true,
		WithStructuredSerializeFromMismatchedTag = true,
		WithImportTextItem = true,
	};
};

/// A typed Event ID backed by the global GameplayTags dictionary.
USTRUCT(BlueprintType)
struct EXPORTMETADATA_API FModEventTag : public FGameplayTag
{
	GENERATED_BODY()

public:
	/// Creates an empty Event ID.
	FModEventTag() = default;

	/** @brief Resolves a registered Event ID on the game thread.
	 *
	 * @param TagName Full GameplayTag path.
	 * @param bErrorIfNotFound Report a missing tag.
	 * @return Valid Event ID, or an invalid tag.
	 */
	static FModEventTag Request(FName TagName, bool bErrorIfNotFound = false);

	/** @brief Converts a registered GameplayTag to an Event ID.
	 *
	 * @param Tag Tag to check in the global dictionary.
	 * @return Valid Event ID, or an invalid tag.
	 */
	static FModEventTag TryConvert(const FGameplayTag& Tag);

	/// Returns whether this tag exists in the global dictionary.
	bool IsRegistered() const;

private:
	explicit FModEventTag(const FGameplayTag& Tag);
};

template <> struct TStructOpsTypeTraits<FModEventTag> : public TStructOpsTypeTraitsBase2<FModEventTag>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithPostSerialize = true,
		WithStructuredSerializeFromMismatchedTag = true,
		WithImportTextItem = true,
	};
};

/// A typed container for API IDs from the global GameplayTags dictionary.
USTRUCT(BlueprintType)
struct EXPORTMETADATA_API FModApiTagContainer : public FGameplayTagContainer
{
	GENERATED_BODY()

public:
	/** @brief Adds a valid API ID on the game thread.
	 *
	 * @param Tag API ID to add.
	 * @return Whether the tag is registered.
	 */
	bool AddTag(const FModApiTag& Tag);

	/** @brief Converts a container only if every tag is registered.
	 *
	 * @param Tags Source GameplayTags.
	 * @param[out] OutTags Converted tags, or empty on failure.
	 * @return Whether all source tags are registered.
	 */
	static bool TryConvert(const FGameplayTagContainer& Tags, FModApiTagContainer& OutTags);

	/// Returns whether every contained tag is registered.
	bool IsRegistered() const;
};

template <> struct TStructOpsTypeTraits<FModApiTagContainer> : public TStructOpsTypeTraitsBase2<FModApiTagContainer>
{
	enum
	{
		WithStructuredSerializer = true,
		WithIdenticalViaEquality = true,
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithImportTextItem = true,
		WithCopy = true,
		WithPostScriptConstruct = true,
	};
	static constexpr EPropertyObjectReferenceType WithSerializerObjectReferences = EPropertyObjectReferenceType::None;
};

/// A typed container for Event IDs from the global GameplayTags dictionary.
USTRUCT(BlueprintType)
struct EXPORTMETADATA_API FModEventTagContainer : public FGameplayTagContainer
{
	GENERATED_BODY()

public:
	/** @brief Adds a valid Event ID on the game thread.
	 *
	 * @param Tag Event ID to add.
	 * @return Whether the tag is registered.
	 */
	bool AddTag(const FModEventTag& Tag);

	/** @brief Converts a container only if every tag is registered.
	 *
	 * @param Tags Source GameplayTags.
	 * @param[out] OutTags Converted tags, or empty on failure.
	 * @return Whether all source tags are registered.
	 */
	static bool TryConvert(const FGameplayTagContainer& Tags, FModEventTagContainer& OutTags);

	/// Returns whether every contained tag is registered.
	bool IsRegistered() const;
};

template <> struct TStructOpsTypeTraits<FModEventTagContainer> : public TStructOpsTypeTraitsBase2<FModEventTagContainer>
{
	enum
	{
		WithStructuredSerializer = true,
		WithIdenticalViaEquality = true,
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithImportTextItem = true,
		WithCopy = true,
		WithPostScriptConstruct = true,
	};
	static constexpr EPropertyObjectReferenceType WithSerializerObjectReferences = EPropertyObjectReferenceType::None;
};
