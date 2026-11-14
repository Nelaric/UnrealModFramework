// Copyright (c) 2026 Nelaric Contributors
using System.IO;
using UnrealBuildTool;

public class PerformanceBenchmark : ModuleRules
{
	public PerformanceBenchmark(ReadOnlyTargetRules target)
		: base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "Shared"));
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"AIModule",
				"InputCore",
				"NavigationSystem",
				"Niagara",
				"PhysicsCore",
				"Slate",
				"SlateCore",
				"TraceLog",
				"UMG",
				"ModLoader",
				"AssetRegistry",
			}
		);
	}
}
