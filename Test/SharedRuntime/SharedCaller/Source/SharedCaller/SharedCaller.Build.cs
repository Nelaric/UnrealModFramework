// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class SharedCaller : ModuleRules
{
	public SharedCaller(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new[] { "Core", "CoreUObject", "Engine", "ModLoader", "UMFDevSDKRuntime" }
		);
	}
}
