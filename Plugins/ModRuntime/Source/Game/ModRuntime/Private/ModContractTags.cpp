// Copyright (c) 2026 Nelaric Contributors
#include "ModContractTags.h"

namespace
{
bool IsRegisteredTag(const FGameplayTag& Tag)
{
	return Tag.IsValid() && FGameplayTag::RequestGameplayTag(Tag.GetTagName(), false).IsValid();
}
} // namespace

FModApiTag::FModApiTag(const FGameplayTag& Tag)
{
	TagName = Tag.GetTagName();
}

FModApiTag FModApiTag::Request(const FName TagName, const bool bErrorIfNotFound)
{
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, bErrorIfNotFound);
	return Tag.IsValid() ? FModApiTag(Tag) : FModApiTag();
}

FModApiTag FModApiTag::TryConvert(const FGameplayTag& Tag)
{
	return IsRegisteredTag(Tag) ? FModApiTag(Tag) : FModApiTag();
}

bool FModApiTag::IsRegistered() const
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

bool FModApiTagContainer::AddTag(const FModApiTag& Tag)
{
	if (!Tag.IsRegistered())
	{
		return false;
	}
	FGameplayTagContainer::AddTag(Tag);
	return true;
}

bool FModApiTagContainer::TryConvert(const FGameplayTagContainer& Tags, FModApiTagContainer& OutTags)
{
	FModApiTagContainer Converted;
	for (const FGameplayTag& Tag : Tags)
	{
		if (!Converted.AddTag(FModApiTag::TryConvert(Tag)))
		{
			OutTags = FModApiTagContainer();
			return false;
		}
	}
	OutTags = MoveTemp(Converted);
	return true;
}

bool FModApiTagContainer::IsRegistered() const
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
