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
		PublicDependencyModuleNames.Add("Core");
	}

	/// <summary>Tracks MHT inputs and verifies the host archives in UBT's linked images.</summary>
	public static void ConfigureTarget(TargetRules target)
	{
		if (
			!IsAutomaticHostPlatform(target.Platform)
			|| HasArgument("-MhtBootstrap")
			|| HasArgument("-Mode=GenerateClangDatabase")
		)
		{
			return;
		}
		RequireAutomaticArchitecture(target.Architectures);
		var project = target.ProjectFile ?? throw new BuildException("ModRuntime MBT requires a project target.");
		string root = project.Directory.FullName;
		string config = Path.Combine(root, "Config", "ModHeaderTool.json");
		string configured = Environment.GetEnvironmentVariable("UMF_MBT_EXECUTABLE");
		if (configured == null && File.Exists(config))
		{
			JsonObject.Read(new FileReference(config)).TryGetStringField("mbt", out configured);
		}
		string mbt = Path.GetFullPath(
			configured ?? "Plugins/ModRuntime/Tools/build/ModBuildTool/ModBuildTool.exe",
			root
		);
		string contexts = Path.Combine(
			VariantDirectory(
				Path.Combine(root, "Intermediate", "ModTools", "ModHeaderTool", "HostTargets"),
				target.Name,
				target.Platform,
				target.Configuration,
				target.Architectures
			),
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
		if (
			!owner.PublicDependencyModuleNames.Contains("ModAPI")
			&& !owner.PrivateDependencyModuleNames.Contains("ModAPI")
		)
		{
			owner.PrivateDependencyModuleNames.Add("ModAPI");
		}
		bool bootstrap = HasArgument("-MhtBootstrap");
		if (
			bootstrap
			&& (target.Type != TargetType.Editor || target.Configuration != UnrealTargetConfiguration.Development)
		)
		{
			throw new BuildException("-MhtBootstrap is restricted to a non-publishable Development Editor build.");
		}
		bool useGeneratedHost = !bootstrap && IsAutomaticHostPlatform(target.Platform);
		owner.PrivateDefinitions.Add("MOD_API_BOOTSTRAP=" + (useGeneratedHost ? "0" : "1"));
		owner.PCHUsage = PCHUsageMode.NoPCHs;
		owner.bUseUnity = false;
		if (bootstrap)
		{
			owner.ShortName = (owner.ShortName ?? owner.Name) + "MhtBootstrap";
		}
		if (!useGeneratedHost)
		{
			return;
		}
		RequireAutomaticArchitecture(target.Architectures);
		owner.PrivateDefinitions.Add("UMF_MHT_TARGET_CONFIGURATION=" + target.Configuration);
		if (target.bMergeModules || target.bShouldCompileAsDLL)
		{
			throw new BuildException("ModRuntime MBT requires standard modular DLLs or a monolithic executable.");
		}
		string project = target.ProjectFile!.FullName;
		string projectRoot = Path.GetDirectoryName(project)!;
		string tools = Path.Combine(projectRoot, "Intermediate", "ModTools");
		string ownerTools = OwnerToolsDirectory(projectRoot, owner.ModuleDirectory);
		bool shared = target.BuildEnvironment == TargetBuildEnvironment.Shared;
		string appName = shared && target.Type != TargetType.Program ? "Unreal" + target.Type : target.Name;
		string generated = VariantDirectory(Path.Combine(tools, "ModHeaderTool", "Generated"), target);
		string ownerGenerated = VariantDirectory(Path.Combine(ownerTools, "ModHeaderTool", "Generated"), target);
		string variant = VariantDirectory(Path.Combine(tools, "ModHeaderTool", "HostTargets"), target);
		string hostLink = Path.Combine(
			VariantDirectory(Path.Combine(ownerTools, "ModSourceTool"), target),
			owner.Name + ".modlink.h"
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
				ownerGeneratedDirectory = ownerGenerated,
				hostLinkHeader = hostLink,
				outputLibrary = LibraryPath(ownerTools, target, owner.Name),
				toolchain = new
				{
					compiler = target.WindowsPlatform.Compiler.ToString(),
					compilerVersion = target.WindowsPlatform.CompilerVersion,
					windowsSdkVersion = target.WindowsPlatform.WindowsSdkVersion,
					msvcDirectory = target.WindowsPlatform.ToolChainDir,
					windowsSdkDirectory = target.WindowsPlatform.WindowsSdkDir,
				},
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
		if (!HasArgument("-Mode=GenerateClangDatabase"))
		{
			owner.ForceIncludeFiles.Add(hostLink);
		}
		string config = Path.Combine(projectRoot, "Config", "ModHeaderTool.json");
		if (File.Exists(config))
		{
			owner.ExternalDependencies.Add(config);
			if (!HasArgument("-Mode=GenerateClangDatabase") && UsesAutomaticBuildInput(config, target))
			{
				string database = Path.Combine(variant, "Ubt", "compile_commands.json");
				// A new action graph can refresh the database. A reused makefile must keep its response files.
				AppDomain.CurrentDomain.SetData(database.ToUpperInvariant(), true);
				owner.ExternalDependencies.Add(database);
			}
		}
		// UBT resolves include directories before running UHT on a clean build.
		string hostHeaders = Path.Combine(ownerGenerated, "Host", owner.Name);
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
		if (
			!IsAutomaticHostPlatform(target.Platform)
			|| HasArgument("-MhtBootstrap")
			|| HasArgument("-Mode=GenerateClangDatabase")
		)
		{
			return;
		}
		RequireAutomaticArchitecture(target.Architectures);
		string tools = OwnerToolsDirectory(
			target.ProjectFile!.Directory.FullName,
			libraryModule.GetModuleDirectory(owner)
		);
		string library = LibraryPath(tools, target, owner);
		libraryModule.PublicPreBuildLibraries.Add(library);
		libraryModule.ExternalDependencies.Add(library);
		libraryModule.ExternalDependencies.Add(library + ".mbt.json");
	}

	private static string OwnerToolsDirectory(string projectRoot, string moduleDirectory)
	{
		for (
			DirectoryInfo directory = new(moduleDirectory);
			directory != null && !directory.FullName.Equals(projectRoot, StringComparison.OrdinalIgnoreCase);
			directory = directory.Parent
		)
		{
			if (directory.EnumerateFiles("*.uplugin").Any())
			{
				return Path.Combine(directory.FullName, "Intermediate", "ModTools");
			}
		}
		return Path.Combine(projectRoot, "Intermediate", "ModTools");
	}

	private static string LibraryPath(string tools, ReadOnlyTargetRules target, string owner) =>
		Path.Combine(VariantDirectory(Path.Combine(tools, "ModBuildTool"), target), owner + ".lib");

	private static string VariantDirectory(string directory, ReadOnlyTargetRules target) =>
		VariantDirectory(directory, target.Name, target.Platform, target.Configuration, target.Architectures);

	private static string VariantDirectory(
		string directory,
		string target,
		UnrealTargetPlatform platform,
		UnrealTargetConfiguration configuration,
		UnrealArchitectures architectures
	) => Path.Combine(directory, target, platform.ToString(), configuration.ToString(), architectures.ToString());

	private static bool IsAutomaticHostPlatform(UnrealTargetPlatform platform) =>
		platform == UnrealTargetPlatform.Win64 && OperatingSystem.IsWindows();

	private static bool UsesAutomaticBuildInput(string configPath, ReadOnlyTargetRules target)
	{
		JsonObject config = JsonObject.Read(new FileReference(configPath));
		if (
			!config.TryGetStringField("mode", out string mode)
			|| mode != "generate"
			|| !config.TryGetObjectField("generation", out _)
			|| Environment.GetEnvironmentVariable("UMF_MHT_BUILD_INPUT") != null
		)
		{
			return false;
		}
		if (
			!config.TryGetObjectField("targets", out JsonObject targets)
			|| !targets.TryGetObjectField(target.Name, out JsonObject variant)
		)
		{
			return true;
		}
		if (
			variant.TryGetObjectField("variants", out JsonObject variants)
			&& !variants.TryGetObjectField(
				$"{target.Platform}/{target.Configuration}/{target.Architectures}",
				out variant
			)
		)
		{
			return true;
		}
		return !variant.TryGetStringField("buildInput", out _);
	}

	private static void RequireAutomaticArchitecture(UnrealArchitectures architectures)
	{
		if (architectures.ToString() != "x64")
		{
			throw new BuildException("ModRuntime's automatic UE 5.6 recipe adapter requires Win64 x64 MSVC.");
		}
	}

	private static bool HasArgument(string argument) =>
		Environment.GetCommandLineArgs().Contains(argument, StringComparer.OrdinalIgnoreCase);

	private static string QuoteCommandPath(string path) => "\"" + path.Replace("%", "%%") + "\"";
}
