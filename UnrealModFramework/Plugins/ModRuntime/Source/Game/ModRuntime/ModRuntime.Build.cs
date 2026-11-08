// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModRuntime : ModuleRules
{
	public ModRuntime(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "GameplayTags" });
	}
}
