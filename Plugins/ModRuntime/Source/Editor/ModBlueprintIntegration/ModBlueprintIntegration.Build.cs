// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModBlueprintIntegration : ModuleRules
{
	public ModBlueprintIntegration(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"BlueprintGraph",
				"GraphEditor",
				"Kismet",
				"KismetCompiler",
				"ModRuntime",
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"UnrealEd",
				"UMGEditor",
			}
		);
	}
}
