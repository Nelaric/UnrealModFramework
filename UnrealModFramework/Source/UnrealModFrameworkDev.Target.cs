// Copyright (c) 2026 Nelaric Contributors
using System;
using UnrealBuildTool;

public class UnrealModFrameworkDevTarget : TargetRules
{
	public UnrealModFrameworkDevTarget(TargetInfo Target)
		: this(Target, TargetLinkType.Monolithic) { }

	protected UnrealModFrameworkDevTarget(TargetInfo Target, TargetLinkType linkType)
		: base(Target)
	{
		Type = TargetType.Game;
		LinkType = linkType;
		ExtraModuleNames.Add("ExportTest");
		ConfigureModHost(this);
	}

	internal static void ConfigureModHost(TargetRules target)
	{
		// Match the shared engine build environment from UE 5.8 onward.
		bool useEngineDefaults = target.Version.MajorVersion > 5 || target.Version.MinorVersion >= 8;
		target.DefaultBuildSettings = useEngineDefaults ? BuildSettingsVersion.Latest : BuildSettingsVersion.V5;
		target.IncludeOrderVersion = useEngineDefaults
			? EngineIncludeOrderVersion.Latest
			: EngineIncludeOrderVersion.Unreal5_6;

		// UAT compiles target rules separately when detecting targets for staging.
		var exportMetadata = typeof(UnrealModFrameworkDevTarget).Assembly.GetType("ExportMetadata");
		exportMetadata?.GetMethod("ConfigureTarget")?.CreateDelegate<Action<TargetRules, bool>>()(target, true);
	}
}
