// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ExportTestHost : ModuleRules
{
	public ExportTestHost(ReadOnlyTargetRules Target)
		: base(Target)
	{
		ExportMetadata.ConfigureHostLibrary(this, Target, "ExportTest");
	}
}
