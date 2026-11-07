// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModRuntimeEditor : ModuleRules
{
	public ModRuntimeEditor(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "ModRuntime" });
	}
}
