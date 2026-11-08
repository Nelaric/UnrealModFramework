// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class RuntimeEditor : ModuleRules
{
	public RuntimeEditor(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "Runtime" });
	}
}
