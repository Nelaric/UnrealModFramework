// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class HAL : ModuleRules
{
	public HAL(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.Add("Core");
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("CoreServices");
		}
	}
}
