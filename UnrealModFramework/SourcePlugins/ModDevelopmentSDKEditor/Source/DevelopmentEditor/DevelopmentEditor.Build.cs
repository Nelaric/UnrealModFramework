// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class DevelopmentEditor : ModuleRules
{
	public DevelopmentEditor(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"DeveloperSettings",
				"Engine",
				"Json",
				"Projects",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
			}
		);
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
