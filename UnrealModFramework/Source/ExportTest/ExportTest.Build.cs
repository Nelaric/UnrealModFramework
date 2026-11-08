// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ExportTest : ModuleRules
{
	public ExportTest(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
	}
}
