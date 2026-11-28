// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class PerformanceBenchmarkTarget : TargetRules
{
	public PerformanceBenchmarkTarget(TargetInfo target)
		: base(target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.AddRange(new[] { "PerformanceBenchmark", "BenchmarkMod" });
	}
}
