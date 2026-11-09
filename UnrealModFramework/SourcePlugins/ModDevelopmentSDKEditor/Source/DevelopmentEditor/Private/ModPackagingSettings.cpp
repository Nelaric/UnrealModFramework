// Copyright (c) 2026 Nelaric Contributors
#include "ModPackagingSettings.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "ModPackagingSettings"

FName UModPackagingSettings::GetContainerName() const
{
	return TEXT("Project");
}

FName UModPackagingSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UModPackagingSettings::GetSectionName() const
{
	return TEXT("ModPackaging");
}

FText UModPackagingSettings::GetSectionText() const
{
	return LOCTEXT("Section", "Mod 打包");
}

TArray<FString> UModPackagingSettings::GetModPluginNames() const
{
	TArray<FString> Names;
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		if (Plugin->GetType() == EPluginType::Project && Plugin->CanContainContent() &&
		    Plugin->GetDescriptor().Modules.IsEmpty())
		{
			Names.Add(Plugin->GetName());
		}
	}
	Names.Sort();
	return Names;
}

TArray<FString> UModPackagingSettings::GetTargetPlatforms() const
{
	return {TEXT("Win64"), TEXT("Linux"), TEXT("Mac")};
}

#undef LOCTEXT_NAMESPACE
