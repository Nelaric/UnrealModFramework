// Copyright (c) 2026 Nelaric Contributors
#include "ModContractOptions.h"

#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/ConfigCacheIni.h"

namespace Mod::Contract
{
namespace
{
const TCHAR* GetConfigKey(const EOptionKind Kind)
{
	switch (Kind)
	{
	case EOptionKind::ApiId:
	case EOptionKind::EventId:
		return nullptr;
	case EOptionKind::ApiScope:
		return TEXT("ApiScopes");
	case EOptionKind::ApiThread:
		return TEXT("ApiThreads");
	case EOptionKind::ApiPermission:
		return TEXT("ApiPermissions");
	case EOptionKind::ApiFailureRule:
		return TEXT("ApiFailureRules");
	case EOptionKind::EventSourceScope:
		return TEXT("EventSourceScopes");
	case EOptionKind::EventPublishRule:
		return TEXT("EventPublishRules");
	}
	return nullptr;
}
} // namespace

TArray<FString> FOptions::Load(const EOptionKind Kind)
{
	TArray<FString> Choices;
	if (Kind == EOptionKind::ApiId || Kind == EOptionKind::EventId)
	{
		FGameplayTagContainer Tags;
		UGameplayTagsManager::Get().RequestAllGameplayTags(Tags, false);
		for (const FGameplayTag& Tag : Tags)
		{
			Choices.Add(Tag.ToString());
		}
		Choices.Sort();
		return Choices;
	}
	const TCHAR* Key = GetConfigKey(Kind);
	if (!GConfig || !Key)
	{
		return Choices;
	}
	FString GameModIni;
	if (!FConfigCacheIni::LoadGlobalIniFile(GameModIni, TEXT("GameMod"), nullptr, false, true))
	{
		return Choices;
	}

	TArray<FString> RawChoices;
	GConfig->GetArray(TEXT("ModContract"), Key, RawChoices, GameModIni);
	for (FString& Choice : RawChoices)
	{
		Choice.TrimStartAndEndInline();
		if (!Choice.IsEmpty() && !Choice.Contains(TEXT(",")) && !Choices.Contains(Choice))
		{
			Choices.Add(MoveTemp(Choice));
		}
	}
	return Choices;
}

bool FOptions::Contains(const EOptionKind Kind, const FString& Value)
{
	return !Value.IsEmpty() && Load(Kind).Contains(Value);
}
} // namespace Mod::Contract
