// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModApi : ModuleRules
{
	public ModApi(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
	}
}
