// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class BenchmarkModEditorTarget : TargetRules
{
	public BenchmarkModEditorTarget(TargetInfo target)
		: base(target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("BenchmarkMod");
	}
}
