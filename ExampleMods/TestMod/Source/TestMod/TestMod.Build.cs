// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class TestMod : ModuleRules
{
	public TestMod(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(
			new[] { "Core", "CoreUObject", "Engine", "ModLoader", "UMFDevSDKRuntime" }
		);
	}
}
