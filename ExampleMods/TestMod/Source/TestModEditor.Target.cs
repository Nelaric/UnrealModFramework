// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class TestModEditorTarget : TargetRules
{
	public TestModEditorTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("TestMod");
	}
}
