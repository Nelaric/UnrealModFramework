// Copyright (c) 2026 Nelaric Contributors
using System.IO;
using UnrealBuildTool;

public class BenchmarkMod : ModuleRules
{
	public BenchmarkMod(ReadOnlyTargetRules target)
		: base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "Shared"));
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ModLoader" });
	}
}
