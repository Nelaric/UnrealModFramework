// Copyright (c) 2026 Nelaric Contributors
using System.IO;
using UnrealBuildTool;

public class SharedRuntimeChecks : ModuleRules
{
	public SharedRuntimeChecks(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ExportTest",
				"ExportMetadata",
				"ModAPI",
				"ModLoader",
				"UMFDevSDKRuntime",
			}
		);
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ExportTest"));
	}
}
