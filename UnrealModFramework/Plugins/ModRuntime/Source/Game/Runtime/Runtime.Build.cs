// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class Runtime : ModuleRules
{
	public Runtime(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "GameplayTags" });
	}
}
