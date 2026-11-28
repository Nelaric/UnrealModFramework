// Copyright (c) 2026 Nelaric Contributors
#include "ModPackagingSettings.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "ModPackagingSettings"

FString UModPackagingSettings::ResolveOutputDirectory() const
{
	if (OutputDirectory.Path.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PackagedMods")));
	}

	const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	return FPaths::ConvertRelativePathToFull(ProjectDirectory, OutputDirectory.Path);
}

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
	return {TEXT("Win64"), TEXT("Linux"), TEXT("LinuxArm64"), TEXT("Mac")};
}

TArray<FString> UModPackagingSettings::GetTargetArchitectures() const
{
	if (TargetPlatform == TEXT("Linux"))
	{
		return {FString(), TEXT("x64")};
	}
	if (TargetPlatform == TEXT("LinuxArm64"))
	{
		return {FString(), TEXT("arm64")};
	}
	return {FString(), TEXT("x64"), TEXT("arm64")};
}

TArray<FString> UModPackagingSettings::GetCookConfigurations() const
{
	return {TEXT("Development"), TEXT("Shipping")};
}

#undef LOCTEXT_NAMESPACE
