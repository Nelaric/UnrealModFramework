// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class BlueprintIntegration : ModuleRules
{
	public BlueprintIntegration(ReadOnlyTargetRules Target)
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
				"ExportMetadata",
				"ModAPI",
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"UnrealEd",
				"UMGEditor",
			}
		);
	}
}
