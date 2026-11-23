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

	/// <summary>Builds the native tools, tracks MHT inputs, and verifies the linked host archives.</summary>
	public static void ConfigureTarget(TargetRules target)
	{
		if (HasArgument("-MhtBootstrap") || HasArgument("-Mode=GenerateClangDatabase"))
		{
			return;
		}
		var project = target.ProjectFile ?? throw new BuildException("ModRuntime tools require a project target.");
		string root = project.Directory.FullName;
		string tools = Path.Combine(root, "Plugins", "ModRuntime", "Tools");
		string buildScript = Path.Combine(tools, OperatingSystem.IsWindows() ? "Build.bat" : "Build.sh");
		if (File.Exists(buildScript))
		{
			// CALL expands percent escapes a second time.
			string command = OperatingSystem.IsWindows()
				? $"call {QuoteCommandPath(buildScript.Replace("%", "%%"))} --target ModRuntimeUhtTools || exit /b 1"
				: GetUnixToolsBuildCommand(root, buildScript);
			target.PreBuildSteps.Add(command);
		}
		bool independent = UsesIndependentArchive(
			target.Platform,
			target.Architectures,
			target.WindowsPlatform.Compiler
		);
		string config = Path.Combine(root, "Config", "ModHeaderTool.json");
		string configured = Environment.GetEnvironmentVariable("UMF_MBT_EXECUTABLE");
		if (configured == null && File.Exists(config))
		{
			JsonObject.Read(new FileReference(config)).TryGetStringField("mbt", out configured);
		}
		string mbt = Path.GetFullPath(
			configured
				?? Path.Combine(
					tools,
					"build",
					"ModBuildTool",
					OperatingSystem.IsWindows() ? "ModBuildTool.exe" : "ModBuildTool"
				),
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
			independent ? "contexts.json" : "watch.json"
		);
		string Quote(string path) => OperatingSystem.IsWindows() ? QuoteCommandPath(path) : QuotePosixPath(path);
		target.PreBuildSteps.Add($"{Quote(mbt)} refresh-ue-watches --contexts {Quote(contexts)}");
		string receipt = TargetReceipt
			.GetDefaultPath(project.Directory, target.Name, target.Platform, target.Configuration, target.Architectures)
			.FullName;
		if (!independent)
		{
			if (target.Platform.IsInGroup(UnrealPlatformGroup.Apple))
			{
				string generated = VariantDirectory(
					Path.Combine(root, "Intermediate", "ModTools", "ModHeaderTool", "Generated"),
					target.Name,
					target.Platform,
					target.Configuration,
					target.Architectures
				);
				string verify = Path.Combine(tools, "verify_macho_hooks.py");
				target.PostBuildSteps.Add(
					$"python3 {Quote(verify)} --project-dir {Quote(root)} --target-receipt {Quote(receipt)} --generated {Quote(generated)}"
				);
			}
			return;
		}
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
		bool independent = UsesIndependentArchive(
			target.Platform,
			target.Architectures,
			target.WindowsPlatform.Compiler
		);
		owner.PrivateDefinitions.Add("MOD_API_BOOTSTRAP=" + (bootstrap ? "1" : "0"));
		owner.PCHUsage = PCHUsageMode.NoPCHs;
		owner.bUseUnity = false;
		if (bootstrap)
		{
			owner.ShortName = (owner.ShortName ?? owner.Name) + "MhtBootstrap";
			return;
		}
		if (target.Architectures.Architectures.Count != 1)
		{
			throw new BuildException(
				"Build each automatic Hook architecture separately using -Architecture=<architecture>."
			);
		}
		owner.PublicDefinitions.Add(
			$"UMF_MHT_BUILD_VARIANT={target.Platform}_{target.Configuration}_{target.Architecture}"
		);
		owner.PublicDefinitions.Add("UMF_MHT_UBT_HOST=" + (independent ? "0" : "1"));
		owner.PrivateDefinitions.Add("UMF_MHT_TARGET_CONFIGURATION=" + target.Configuration);
		if (independent && (target.bMergeModules || target.bShouldCompileAsDLL))
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
		string ownerGenerated = independent
			? VariantDirectory(Path.Combine(ownerTools, "ModHeaderTool", "Generated"), target)
			: generated;
		string variant = VariantDirectory(Path.Combine(tools, "ModHeaderTool", "HostTargets"), target);
		// The UHT outputs include host units by variant-relative path. UBT owns the target compiler and linker.
		Directory.CreateDirectory(variant);
		Directory.CreateDirectory(Path.Combine(ownerGenerated, "Host", owner.Name));
		Directory.CreateDirectory(Path.Combine(generated, "SDK", "Public"));
		owner.PrivateIncludePaths.Add(Path.Combine(ownerGenerated, "Host", owner.Name));
		owner.PublicIncludePaths.Add(Path.Combine(generated, "SDK", "Public"));
		owner.PrivateIncludePaths.Add(generated);
		string config = Path.Combine(projectRoot, "Config", "ModHeaderTool.json");
		if (File.Exists(config))
		{
			owner.ExternalDependencies.Add(config);
			if (!HasArgument("-Mode=GenerateClangDatabase") && UsesAutomaticBuildInput(config, target))
			{
				string database = Path.Combine(variant, "Ubt", "compile_commands.json");
				AppDomain.CurrentDomain.SetData(database, true);
				owner.ExternalDependencies.Add(database);
			}
		}
		if (!independent && target.Platform != UnrealTargetPlatform.Win64)
		{
			return;
		}
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
		if (!independent)
		{
			return;
		}
		if (!HasArgument("-Mode=GenerateClangDatabase"))
		{
			owner.ForceIncludeFiles.Add(hostLink);
		}
		owner.PrivateDependencyModuleNames.Add(owner.Name + "Host");
	}

	/// <summary>Exposes one host archive through an owner's private dependency.</summary>
	public static void ConfigureHostLibrary(ModuleRules libraryModule, ReadOnlyTargetRules target, string owner)
	{
		libraryModule.Type = ModuleType.External;
		if (
			!UsesIndependentArchive(target.Platform, target.Architectures, target.WindowsPlatform.Compiler)
			|| HasArgument("-MhtBootstrap")
			|| HasArgument("-Mode=GenerateClangDatabase")
		)
		{
			return;
		}
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

	private static bool UsesIndependentArchive(
		UnrealTargetPlatform platform,
		UnrealArchitectures architectures,
		WindowsCompiler compiler
	) =>
		platform == UnrealTargetPlatform.Win64
		&& OperatingSystem.IsWindows()
		&& architectures.ToString() == "x64"
		&& !compiler.IsClang();

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

	private static bool HasArgument(string argument) =>
		Environment.GetCommandLineArgs().Contains(argument, StringComparer.OrdinalIgnoreCase);

	private static string QuoteCommandPath(string path) => "\"" + path.Replace("%", "%%") + "\"";

	private static string GetUnixToolsBuildCommand(string projectDirectory, string buildScript)
	{
		string finder = Path.GetFullPath(Path.Combine(projectDirectory, "..", "Setup", "FindCMake.sh"));
		return $"if [ -n \"$UMF_CMAKE_EXECUTABLE\" ] || [ -f {QuotePosixPath(finder)} ] || command -v cmake >/dev/null 2>&1; then "
			+ $"sh {QuotePosixPath(buildScript)} --target ModRuntimeUhtTools || exit $?; "
			+ "else echo 'ModRuntime: skipping native tool build because CMake is unavailable.'; fi";
	}

	// Split literal $(...) so UBT leaves it alone when it expands build-step variables.
	private static string QuotePosixPath(string path) => "'" + path.Replace("'", "'\\''").Replace("$(", "$''(") + "'";
}
