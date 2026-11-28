// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModLoader : ModuleRules
{
	public ModLoader(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		ExportMetadata.ConfigureRuntime(this, Target);
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] { "AssetRegistry", "HAL", "Json", "PakFile", "RenderCore" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
