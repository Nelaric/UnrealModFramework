// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class PerformanceBenchmarkEditorTarget : TargetRules
{
	public PerformanceBenchmarkEditorTarget(TargetInfo target)
		: base(target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.AddRange(new[] { "PerformanceBenchmark", "PerformanceBenchmarkEditor" });
	}
}
