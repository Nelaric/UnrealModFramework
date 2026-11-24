// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class UnrealModFrameworkDevEditorTarget : TargetRules
{
	public UnrealModFrameworkDevEditorTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("ExportTest");
		UnrealModFrameworkDevTarget.ConfigureModHost(this);
	}
}
