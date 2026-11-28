// Copyright (c) 2026 Nelaric Contributors
#include "BlueprintDiscovery.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "ModBlueprintExportReader.h"
#include "ProjectDescriptor.h"
#include "UObject/Package.h"

namespace Mod::ExportCodeGenerator
{
namespace
{
void AddRuntimeModules(const TArray<FModuleDescriptor>& Modules, TSet<FName>& Names)
{
	for (const auto& Module : Modules)
	{
		if (Module.Type == EHostType::Runtime || Module.Type == EHostType::RuntimeNoCommandlet ||
		    Module.Type == EHostType::RuntimeAndProgram)
		{
			Names.Add(Module.Name);
		}
	}
}
} // namespace

TArray<FAssetData> FindExportCandidates(IAssetRegistry& Registry)
{
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game"));
	for (const auto& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		if (Plugin->GetType() == EPluginType::Project && Plugin->CanContainContent())
		{
			Filter.PackagePaths.Add(FName(*Plugin->GetMountedAssetPath().LeftChop(1)));
		}
	}
	TArray<FString> Roots;
	for (const FName& Path : Filter.PackagePaths)
	{
		Roots.Add(Path.ToString());
	}
	Registry.ScanPathsSynchronous(Roots);
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.bIncludeOnlyOnDiskAssets = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	// Older saved Blueprints import the declaration extension before they have the saved tag.
	TArray<FName> Referencers;
	Registry.GetReferencers(TEXT("/Script/BlueprintIntegration"), Referencers,
	                        UE::AssetRegistry::EDependencyCategory::Package);
	const TSet<FName> DeclarationPackages(Referencers);
	Assets.RemoveAll(
	    [&](const FAssetData& Asset) {
		    return !Asset.FindTag(BlueprintIntegration::ExportAssetTag) &&
		           !DeclarationPackages.Contains(Asset.PackageName);
	    });
	Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
	            { return Left.GetObjectPathString() < Right.GetObjectPathString(); });
	return Assets;
}

FName ResolveExportOwner(const UBlueprint& Blueprint, FName ExplicitOwner)
{
	TSet<FName> ProjectOwners;
	if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
	{
		AddRuntimeModules(Project->Modules, ProjectOwners);
	}
	TSet<FName> Owners = ProjectOwners;
	TSet<FName> MountOwners;
	const FString PackageName = Blueprint.GetOutermost()->GetName();
	for (const auto& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		if (Plugin->GetType() != EPluginType::Project)
		{
			continue;
		}
		AddRuntimeModules(Plugin->GetDescriptor().Modules, Owners);
		if (Plugin->CanContainContent() && PackageName.StartsWith(Plugin->GetMountedAssetPath()))
		{
			AddRuntimeModules(Plugin->GetDescriptor().Modules, MountOwners);
		}
	}
	if (!ExplicitOwner.IsNone())
	{
		return Owners.Contains(ExplicitOwner) ? ExplicitOwner : NAME_None;
	}
	for (const UClass* Class = Blueprint.ParentClass; Class; Class = Class->GetSuperClass())
	{
		if (Class->HasAnyClassFlags(CLASS_Native))
		{
			const FName Module(*Class->GetOutermost()->GetName().RightChop(8));
			if (Owners.Contains(Module))
			{
				return Module;
			}
		}
	}
	if (MountOwners.Num() == 1)
	{
		return *MountOwners.CreateConstIterator();
	}
	return MountOwners.IsEmpty() && ProjectOwners.Num() == 1 ? *ProjectOwners.CreateConstIterator() : NAME_None;
}
} // namespace Mod::ExportCodeGenerator
