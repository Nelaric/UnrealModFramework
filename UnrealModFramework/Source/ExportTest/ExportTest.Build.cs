// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ExportTest : ModuleRules
{
	public ExportTest(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ExportMetadata", "ModApi" });
		PrivateIncludePaths.Add(ModuleDirectory);
		ExportMetadata.ConfigureHost(this, Target);
	}
}
