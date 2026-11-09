// Copyright (c) 2026 Nelaric Contributors
using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using EpicGames.Core;
using UnrealBuildTool;

public class ExportMetadata : ModuleRules
{
	public ExportMetadata(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "GameplayTags" });
	}

	/// <summary>Tracks MHT inputs and verifies the host archives in UBT's linked images.</summary>
	public static void ConfigureTarget(TargetRules target)
	{
		if (HasArgument("-MhtBootstrap") || HasArgument("-Mode=GenerateClangDatabase"))
		{
			return;
		}
		RequireAutomaticTarget(target.Platform, target.Architectures);
		var project = target.ProjectFile ?? throw new BuildException("ModRuntime MBT requires a project target.");
		string root = project.Directory.FullName;
		string config = Path.Combine(root, "Config", "ModHeaderTool.json");
		string configured = Environment.GetEnvironmentVariable("UMF_MBT_EXECUTABLE");
		if (configured == null && File.Exists(config))
		{
			JsonObject.Read(new FileReference(config)).TryGetStringField("mbt", out configured);
		}
		string mbt = Path.GetFullPath(
			configured ?? "Plugins/ModRuntime/Tools/build/Release/bin/Release/ModBuildTool.exe",
			root
		);
		string contexts = Path.Combine(
			VariantDirectory(root, target.Name, target.Platform, target.Configuration, target.Architectures),
			"contexts.json"
		);
		string receipt = TargetReceipt
			.GetDefaultPath(project.Directory, target.Name, target.Platform, target.Configuration, target.Architectures)
			.FullName;
		target.PreBuildSteps.Add($"{QuoteCommandPath(mbt)} refresh-ue-watches --contexts {QuoteCommandPath(contexts)}");
		target.PostBuildSteps.Add(
			$"{QuoteCommandPath(mbt)} finalize-ue-target --contexts {QuoteCommandPath(contexts)} "
				+ $"--ue-target-receipt {QuoteCommandPath(receipt)}"
		);
	}

	/// <summary>Links an independently compiled generated host archive.</summary>
	public static void ConfigureHost(ModuleRules owner, ReadOnlyTargetRules target)
	{
		bool bootstrap = HasArgument("-MhtBootstrap");
		if (
			bootstrap
			&& (target.Type != TargetType.Editor || target.Configuration != UnrealTargetConfiguration.Development)
		)
		{
			throw new BuildException("-MhtBootstrap is restricted to a non-publishable Development Editor build.");
		}
		owner.PrivateDefinitions.Add("MOD_API_BOOTSTRAP=" + (bootstrap ? "1" : "0"));
		owner.PCHUsage = PCHUsageMode.NoPCHs;
		owner.bUseUnity = false;
		if (bootstrap)
		{
			return;
		}
		RequireAutomaticTarget(target.Platform, target.Architectures);
		owner.PrivateDefinitions.Add("UMF_MHT_TARGET_CONFIGURATION=" + target.Configuration);
		if (target.bMergeModules || target.bShouldCompileAsDLL)
		{
			throw new BuildException("ModRuntime MBT requires standard modular DLLs or a monolithic executable.");
		}
		string project = target.ProjectFile!.FullName;
		string projectRoot = Path.GetDirectoryName(project)!;
		string pluginRoot = Path.GetFullPath(
			Path.Combine(owner.GetModuleDirectory("ExportMetadata"), "..", "..", "..")
		);
		bool shared = target.BuildEnvironment == TargetBuildEnvironment.Shared;
		string appName = shared && target.Type != TargetType.Program ? "Unreal" + target.Type : target.Name;
		string generated = Path.Combine(
			shared ? pluginRoot : projectRoot,
			"Intermediate",
			"Build",
			target.Platform.ToString(),
			appName,
			"Inc",
			"ExportMetadata",
			"MHT",
			target.Name,
			target.Configuration.ToString(),
			target.Architecture.ToString()
		);
		string variant = VariantDirectory(
			projectRoot,
			target.Name,
			target.Platform,
			target.Configuration,
			target.Architectures
		);
		bool monolithic = target.LinkType == TargetLinkType.Monolithic;
		// UE 5.6's standard Win64 x64 project binaries omit the architecture suffix.
		string binaryName = monolithic ? target.Name : appName + target.DecoratedSeparator + owner.Name;
		if (target.Configuration != target.UndecoratedConfiguration)
		{
			binaryName +=
				target.DecoratedSeparator + target.Platform + target.DecoratedSeparator + target.Configuration;
		}
		binaryName += monolithic ? ".exe" : ".dll";
		string binaryRoot = shared && !monolithic && owner.IsPlugin ? owner.PluginDirectory : projectRoot;
		string binary =
			monolithic && target.OutputFile != null
				? Path.GetFullPath(target.OutputFile, binaryRoot)
				: Path.Combine(
					binaryRoot,
					"Binaries",
					target.Platform.ToString(),
					monolithic ? target.ExeBinariesSubFolder : owner.BinariesSubFolder,
					binaryName
				);
		Directory.CreateDirectory(variant);
		string descriptor = Path.Combine(variant, owner.Name + ".target.json");
		string document = JsonSerializer.Serialize(
			new
			{
				format = "UMF.UeHostTarget",
				schemaVersion = 1,
				project,
				ownerModule = owner.Name,
				sourceDirectory = owner.ModuleDirectory,
				generatedDirectory = generated,
				outputLibrary = LibraryPath(target, owner.Name),
				owningBinary = new
				{
					id = binaryName,
					path = binary,
					kind = monolithic ? "Executable" : "DynamicLibrary",
				},
				target = new
				{
					name = target.Name,
					type = target.Type.ToString(),
					configuration = target.Configuration.ToString(),
					linkType = target.LinkType.ToString(),
					platform = target.Platform.ToString(),
					architecture = target.Architecture.ToString(),
				},
			},
			new JsonSerializerOptions { WriteIndented = true }
		);
		if (!File.Exists(descriptor) || File.ReadAllText(descriptor) != document)
		{
			File.WriteAllText(descriptor, document);
		}
		owner.ExternalDependencies.Add(descriptor);
		string config = Path.Combine(projectRoot, "Config", "ModHeaderTool.json");
		if (File.Exists(config))
		{
			owner.ExternalDependencies.Add(config);
		}
		// UBT resolves include directories before running UHT on a clean build.
		string hostHeaders = Path.Combine(generated, "Host", owner.Name);
		string sdkHeaders = Path.Combine(generated, "SDK", "Public");
		Directory.CreateDirectory(hostHeaders);
		Directory.CreateDirectory(sdkHeaders);
		owner.PrivateIncludePaths.Add(hostHeaders);
		owner.PublicIncludePaths.Add(sdkHeaders);
		owner.PrivateDependencyModuleNames.Add(owner.Name + "Host");
	}

	/// <summary>Exposes one host archive through an owner's private dependency.</summary>
	public static void ConfigureHostLibrary(ModuleRules libraryModule, ReadOnlyTargetRules target, string owner)
	{
		libraryModule.Type = ModuleType.External;
		if (HasArgument("-MhtBootstrap") || HasArgument("-Mode=GenerateClangDatabase"))
		{
			return;
		}
		string library = LibraryPath(target, owner);
		libraryModule.PublicPreBuildLibraries.Add(library);
		libraryModule.ExternalDependencies.Add(library);
		libraryModule.ExternalDependencies.Add(library + ".mbt.json");
	}

	private static string LibraryPath(ReadOnlyTargetRules target, string owner) =>
		Path.Combine(
			target.ProjectFile!.Directory.FullName,
			"Intermediate",
			"MBT",
			target.Name,
			target.Platform.ToString(),
			target.Configuration.ToString(),
			target.Architecture.ToString(),
			owner + ".lib"
		);

	private static string VariantDirectory(
		string root,
		string target,
		UnrealTargetPlatform platform,
		UnrealTargetConfiguration configuration,
		UnrealArchitectures architectures
	) =>
		Path.Combine(
			root,
			"Intermediate",
			"ModHeaderTool",
			"HostTargets",
			target,
			platform.ToString(),
			configuration.ToString(),
			architectures.ToString()
		);

	private static void RequireAutomaticTarget(UnrealTargetPlatform platform, UnrealArchitectures architectures)
	{
		if (platform != UnrealTargetPlatform.Win64 || architectures.ToString() != "x64")
		{
			throw new BuildException("ModRuntime's automatic UE 5.6 recipe adapter requires Win64 x64 MSVC.");
		}
	}

	private static bool HasArgument(string argument) =>
		Environment.GetCommandLineArgs().Contains(argument, StringComparer.OrdinalIgnoreCase);

	private static string QuoteCommandPath(string path) => "\"" + path.Replace("%", "%%") + "\"";
}
