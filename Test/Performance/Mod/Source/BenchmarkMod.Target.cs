// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class BenchmarkModTarget : TargetRules
{
	public BenchmarkModTarget(TargetInfo target)
		: base(target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("BenchmarkMod");
	}
}
