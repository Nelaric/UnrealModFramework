// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class UnrealModFrameworkDevModularTarget : UnrealModFrameworkDevTarget
{
	public UnrealModFrameworkDevModularTarget(TargetInfo Target)
		: base(Target, TargetLinkType.Modular)
	{
		bExplicitTargetForType = true;
	}
}
