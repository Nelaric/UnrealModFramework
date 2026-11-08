// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class UnrealModFrameworkDevTarget : TargetRules
{
	public UnrealModFrameworkDevTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("ExportTest");
	}
}
