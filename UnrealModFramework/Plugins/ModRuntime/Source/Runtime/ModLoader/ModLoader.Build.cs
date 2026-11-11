// Copyright (c) 2026 Nelaric Contributors
using UnrealBuildTool;

public class ModLoader : ModuleRules
{
	public ModLoader(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new[] { "AssetRegistry", "HAL", "Json", "PakFile", "RenderCore" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
		if (Target.Platform == UnrealTargetPlatform.Win64 && Target.LinkType == TargetLinkType.Modular)
		{
			AdditionalPropertiesForReceipt.Add("UMF.Compiler", Target.WindowsPlatform.Compiler.ToString());
			if (Target.WindowsPlatform.CompilerVersion is string compilerVersion)
			{
				AdditionalPropertiesForReceipt.Add("UMF.CompilerVersion", compilerVersion);
			}
			if (Target.WindowsPlatform.WindowsSdkVersion is string sdkVersion)
			{
				AdditionalPropertiesForReceipt.Add("UMF.WindowsSDKVersion", sdkVersion);
			}
		}
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
