// Copyright Epic Games, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

public class TP_FirstPerson : ModuleRules
{
	public TP_FirstPerson(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG" });
		PrivateDependencyModuleNames.AddRange(new[] { "EnhancedInput", "InputCore" });
		PublicIncludePaths.AddRange(
			new[]
			{
				ModuleDirectory,
				Path.Combine(ModuleDirectory, "Variant_Shooter", "UI"),
				Path.Combine(ModuleDirectory, "Variant_Shooter", "Weapons"),
			}
		);
	}
}
