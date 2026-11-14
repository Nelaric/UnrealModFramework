// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class PerformanceBenchmarkEditor : ModuleRules
{
	public PerformanceBenchmarkEditor(ReadOnlyTargetRules target)
		: base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"BlueprintGraph",
				"KismetCompiler",
				"UMG",
				"UMGEditor",
				"PerformanceBenchmark",
			}
		);
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "Shared"));
	}
}
