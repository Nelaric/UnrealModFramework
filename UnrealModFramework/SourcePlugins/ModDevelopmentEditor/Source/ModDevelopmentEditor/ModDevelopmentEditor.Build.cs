// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModDevelopmentEditor : ModuleRules
{
	public ModDevelopmentEditor(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.Add("Core");
	}
}
