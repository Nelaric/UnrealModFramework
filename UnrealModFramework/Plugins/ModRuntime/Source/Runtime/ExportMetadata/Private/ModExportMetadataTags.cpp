// Copyright (c) 2026 Nelaric Contributors
#include "ModExportMetadataTags.h"

namespace
{
bool IsRegisteredTag(const FGameplayTag& Tag)
{
	return Tag.IsValid() && FGameplayTag::RequestGameplayTag(Tag.GetTagName(), false).IsValid();
}
} // namespace

FModAPITag::FModAPITag(const FGameplayTag& Tag)
{
	TagName = Tag.GetTagName();
}

FModAPITag FModAPITag::Request(const FName TagName, const bool bErrorIfNotFound)
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, bErrorIfNotFound);
	return Tag.IsValid() ? FModAPITag(Tag) : FModAPITag();
}

FModAPITag FModAPITag::TryConvert(const FGameplayTag& Tag)
{
	return IsRegisteredTag(Tag) ? FModAPITag(Tag) : FModAPITag();
}

bool FModAPITag::IsRegistered() const
{
	return IsRegisteredTag(*this);
}

FModEventTag::FModEventTag(const FGameplayTag& Tag)
{
	TagName = Tag.GetTagName();
}

FModEventTag FModEventTag::Request(const FName TagName, const bool bErrorIfNotFound)
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, bErrorIfNotFound);
	return Tag.IsValid() ? FModEventTag(Tag) : FModEventTag();
}

FModEventTag FModEventTag::TryConvert(const FGameplayTag& Tag)
{
	return IsRegisteredTag(Tag) ? FModEventTag(Tag) : FModEventTag();
}

bool FModEventTag::IsRegistered() const
{
	return IsRegisteredTag(*this);
}

bool FModAPITagContainer::AddTag(const FModAPITag& Tag)
{
	if (!Tag.IsRegistered())
	{
		return false;
	}
	FGameplayTagContainer::AddTag(Tag);
	return true;
}

bool FModAPITagContainer::TryConvert(const FGameplayTagContainer& Tags, FModAPITagContainer& OutTags)
{
	FModAPITagContainer Converted;
	for (const FGameplayTag& Tag : Tags)
	{
		if (!Converted.AddTag(FModAPITag::TryConvert(Tag)))
		{
			OutTags = FModAPITagContainer();
			return false;
		}
	}
	OutTags = MoveTemp(Converted);
	return true;
}

bool FModAPITagContainer::IsRegistered() const
{
	for (const FGameplayTag& Tag : *this)
	{
		if (!IsRegisteredTag(Tag))
		{
			return false;
		}
	}
	return true;
}

bool FModEventTagContainer::AddTag(const FModEventTag& Tag)
{
	if (!Tag.IsRegistered())
	{
		return false;
	}
	FGameplayTagContainer::AddTag(Tag);
	return true;
}

bool FModEventTagContainer::TryConvert(const FGameplayTagContainer& Tags, FModEventTagContainer& OutTags)
{
	FModEventTagContainer Converted;
	for (const FGameplayTag& Tag : Tags)
	{
		if (!Converted.AddTag(FModEventTag::TryConvert(Tag)))
		{
			OutTags = FModEventTagContainer();
			return false;
		}
	}
	OutTags = MoveTemp(Converted);
	return true;
}

bool FModEventTagContainer::IsRegistered() const
{
	for (const FGameplayTag& Tag : *this)
	{
		if (!IsRegisteredTag(Tag))
		{
			return false;
		}
	}
	return true;
}
