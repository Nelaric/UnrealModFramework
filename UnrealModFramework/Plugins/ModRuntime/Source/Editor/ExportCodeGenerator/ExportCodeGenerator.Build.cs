// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ExportCodeGenerator : ModuleRules
{
	public ExportCodeGenerator(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.Add("ExportMetadata");
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Json", "Projects" });
		PrivateDependencyModuleNames.AddRange(new[] { "AssetRegistry", "BlueprintIntegration", "ModAPI", "UnrealEd" });
	}
}
