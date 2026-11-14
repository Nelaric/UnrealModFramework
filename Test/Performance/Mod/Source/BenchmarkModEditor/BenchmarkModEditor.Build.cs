// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class BenchmarkModEditor : ModuleRules
{
	public BenchmarkModEditor(ReadOnlyTargetRules target)
		: base(target)
	{
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"BlueprintGraph",
				"AIModule",
				"ModLoader",
				"NavigationSystem",
				"Niagara",
				"UMG",
			}
		);
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "Shared"));
	}
}
