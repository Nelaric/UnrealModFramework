// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class UnrealModFrameworkDev : ModuleRules
{
	public UnrealModFrameworkDev(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
	}
}
