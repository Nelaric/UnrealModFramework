// Copyright (c) 2026 Nelaric Contributors
using System;
using UnrealBuildTool;

public class UnrealModFrameworkDevTarget : TargetRules
{
	public UnrealModFrameworkDevTarget(TargetInfo Target)
		: this(Target, TargetLinkType.Default) { }

	protected UnrealModFrameworkDevTarget(TargetInfo Target, TargetLinkType linkType)
		: base(Target)
	{
		Type = TargetType.Game;
		LinkType = linkType;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("ExportTest");
		ConfigureModHost(this);
	}

	internal static void ConfigureModHost(TargetRules target)
	{
		// UAT compiles target rules separately when detecting targets for staging.
		var exportMetadata = typeof(UnrealModFrameworkDevTarget).Assembly.GetType("ExportMetadata");
		exportMetadata?.GetMethod("ConfigureTarget")?.CreateDelegate<Action<TargetRules>>()(target);
	}
}
