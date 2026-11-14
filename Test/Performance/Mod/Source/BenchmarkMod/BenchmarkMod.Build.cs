// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class BenchmarkMod : ModuleRules
{
	public BenchmarkMod(ReadOnlyTargetRules target)
		: base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AIModule",
				"InputCore",
				"ModLoader",
				"NavigationSystem",
				"Niagara",
				"PhysicsCore",
				"UMG",
				"SlateCore",
			}
		);
	}
}
