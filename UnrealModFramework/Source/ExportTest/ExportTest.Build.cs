// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ExportTest : ModuleRules
{
	public ExportTest(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ModAPI" });
		PrivateDependencyModuleNames.Add("ModLoader");
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new[] { "BlueprintGraph", "UnrealEd" });
		}
		PrivateIncludePaths.Add(ModuleDirectory);
	}
}
