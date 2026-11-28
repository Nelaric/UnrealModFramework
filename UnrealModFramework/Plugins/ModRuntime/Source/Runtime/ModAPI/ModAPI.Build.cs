// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModAPI : ModuleRules
{
	public ModAPI(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ModLoader", "ExportMetadata" });
		PrivateDependencyModuleNames.Add("Json");
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PublicSystemLibraries.Add("dl");
		}
	}
}
