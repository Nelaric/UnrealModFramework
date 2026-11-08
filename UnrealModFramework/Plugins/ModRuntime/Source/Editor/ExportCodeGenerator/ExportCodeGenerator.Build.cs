// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ExportCodeGenerator : ModuleRules
{
	public ExportCodeGenerator(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.Add("Core");
	}
}
