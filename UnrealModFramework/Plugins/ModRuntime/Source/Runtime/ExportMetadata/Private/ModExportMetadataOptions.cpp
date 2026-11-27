// Copyright (c) 2026 Nelaric Contributors
#include "ModExportMetadataOptions.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"

namespace Mod::ExportMetadata
{
namespace
{
const TCHAR* GetConfigKey(const EOptionKind Kind)
{
	switch (Kind)
	{
	case EOptionKind::APIScope:
		return TEXT("APIScopes");
	case EOptionKind::APIThread:
		return TEXT("APIThreads");
	case EOptionKind::APIPermission:
		return TEXT("APIPermissions");
	case EOptionKind::APIFailureRule:
		return TEXT("APIFailureRules");
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
	const TCHAR* Key = GetConfigKey(Kind);
	if (!Key)
	{
		return Choices;
	}
	FConfigFile GameMod;
	if (!FConfigContext::ReadIntoLocalFile(GameMod).Load(TEXT("GameMod")))
	{
		return Choices;
	}

	TArray<FString> RawChoices;
	GameMod.GetArray(TEXT("ModExportMetadata"), Key, RawChoices);
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
} // namespace Mod::ExportMetadata
