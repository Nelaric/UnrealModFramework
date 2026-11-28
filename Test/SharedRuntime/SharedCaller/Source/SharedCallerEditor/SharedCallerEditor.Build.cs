// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class SharedCallerEditor : ModuleRules
{
	public SharedCallerEditor(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd" });
	}
}
