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
		if (IsHostTarget(Target))
		{
			string generated = VariantDirectory(
				Path.Combine(
					Target.ProjectFile!.Directory.FullName,
					"Intermediate",
					"ModTools",
					"ModHeaderTool",
					"Generated"
				),
				Target
			);
			string sdk = Path.Combine(generated, "SDK", "Public");
			Directory.CreateDirectory(sdk);
			PublicIncludePaths.Add(sdk);
			PublicIncludePaths.Add(generated);
		}
	}

	/// <summary>Configures native tool builds, MHT input tracking, and linked host archive verification.</summary>
	/// <param name="target">The host target rules.</param>
	/// <param name="buildNativeTools">Whether to build the native tools in a pre-build step.</param>
	public static void ConfigureTarget(TargetRules target, bool buildNativeTools = true)
	{
		bool bootstrap = HasArgument("-MhtBootstrap");
		if (
			bootstrap
			&& (target.Type != TargetType.Editor || target.Configuration != UnrealTargetConfiguration.Development)
		)
		{
			throw new BuildException("-MhtBootstrap requires a Development Editor target.");
		}
		target.ProjectDefinitions.Add("UMF_MHT_TARGET_CONFIGURATION=" + target.Configuration);
		target.ProjectDefinitions.Add("MOD_API_BOOTSTRAP=" + (bootstrap ? "1" : "0"));
		const string moduleArgument = "-ModExportModules=";
		string selected = Environment
			.GetCommandLineArgs()
			.FirstOrDefault(arg => arg.StartsWith(moduleArgument, StringComparison.Ordinal));
		if (selected != null)
		{
			target.DisableUnityBuildForModules = (target.DisableUnityBuildForModules ?? Array.Empty<string>())
				.Union(selected[moduleArgument.Length..].Split('+', StringSplitOptions.RemoveEmptyEntries))
				.ToArray();
		}
		if (bootstrap || HasArgument("-Mode=GenerateClangDatabase") || HasArgument("-Mode=JsonExport"))
		{
			return;
		}
		var project = target.ProjectFile ?? throw new BuildException("ModRuntime tools require a project target.");
		string root = project.Directory.FullName;
		string tools = Path.Combine(root, "Plugins", "ModRuntime", "Tools");
		string buildScript = Path.Combine(tools, OperatingSystem.IsWindows() ? "Build.bat" : "Build.sh");
		if (buildNativeTools && File.Exists(buildScript))
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
		bool generate = GeneratesHost(config);
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
			generate && independent ? "contexts.json" : "watch.json"
		);
		string Quote(string path) => OperatingSystem.IsWindows() ? QuoteCommandPath(path) : QuotePosixPath(path);
		target.PreBuildSteps.Add($"{Quote(mbt)} refresh-ue-watches --contexts {Quote(contexts)}");
		if (!generate)
		{
			return;
		}
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

	/// <summary>Accepts existing host module rules during migration to automatic linkage.</summary>
	public static void ConfigureHost(ModuleRules owner, ReadOnlyTargetRules target)
	{
		if (
			!owner.PublicDependencyModuleNames.Contains("ModAPI")
			&& !owner.PrivateDependencyModuleNames.Contains("ModAPI")
		)
		{
			owner.PrivateDependencyModuleNames.Add("ModAPI");
		}
	}

	/// <summary>Accepts legacy External rules for projects migrating to automatic host linkage.</summary>
	public static void ConfigureHostLibrary(ModuleRules libraryModule, ReadOnlyTargetRules target, string owner)
	{
		libraryModule.Type = ModuleType.External;
	}

	private static bool GeneratesHost(string config) =>
		Environment.GetEnvironmentVariable("UMF_MHT_BUILD_INPUT") != null
		|| (
			File.Exists(config)
			&& JsonObject.Read(new FileReference(config)).TryGetStringField("mode", out string mode)
			&& mode == "generate"
		);

	private static bool IsHostTarget(ReadOnlyTargetRules target) =>
		target.ProjectFile != null
		&& target.ProjectDefinitions.Contains("UMF_MHT_TARGET_CONFIGURATION=" + target.Configuration);

	/// <summary>Passes the resolved target and toolchain from a reflected framework module to UHT.</summary>
	internal static void ConfigureRuntime(ModuleRules runtime, ReadOnlyTargetRules target)
	{
		if (!IsHostTarget(target))
		{
			return;
		}
		bool independent = UsesIndependentArchive(
			target.Platform,
			target.Architectures,
			target.WindowsPlatform.Compiler
		);
		runtime.PublicDefinitions.Add(
			$"UMF_MHT_BUILD_VARIANT={target.Platform}_{target.Configuration}_{target.Architectures}"
		);
		runtime.PublicDefinitions.Add("UMF_MHT_UBT_HOST=" + (independent ? "0" : "1"));
		if (
			HasArgument("-MhtBootstrap")
			|| HasArgument("-Mode=GenerateClangDatabase")
			|| HasArgument("-Mode=JsonExport")
		)
		{
			return;
		}
		string root = target.ProjectFile!.Directory.FullName;
		string config = Path.Combine(root, "Config", "ModHeaderTool.json");
		if (File.Exists(config))
		{
			runtime.ExternalDependencies.Add(config);
		}
		string variant = VariantDirectory(
			Path.Combine(root, "Intermediate", "ModTools", "ModHeaderTool", "HostTargets"),
			target
		);
		Directory.CreateDirectory(variant);
		string scope = Path.ChangeExtension(target.ProjectFile.FullName, ".modexport");
		if (File.Exists(scope))
		{
			runtime.ExternalDependencies.Add(scope);
		}
		runtime.ExternalDependencies.Add(Path.Combine(variant, ".mht-watch.stamp"));
		if (!GeneratesHost(config))
		{
			return;
		}
		if (
			target.Architectures.Architectures.Count != 1
			|| (independent && (target.bMergeModules || target.bShouldCompileAsDLL))
		)
		{
			throw new BuildException(
				"ModRuntime host generation requires a single architecture and standard host binaries."
			);
		}
		string path = Path.Combine(variant, "Target.json");
		string document = JsonSerializer.Serialize(
			new
			{
				target = new
				{
					name = target.Name,
					type = target.Type.ToString(),
					linkType = target.LinkType.ToString(),
					platform = target.Platform.ToString(),
					configuration = target.Configuration.ToString(),
					architecture = target.Architecture.ToString(),
				},
				toolchain = target.Platform == UnrealTargetPlatform.Win64
					? new
					{
						compiler = target.WindowsPlatform.Compiler.ToString(),
						compilerVersion = target.WindowsPlatform.CompilerVersion,
						windowsSdkVersion = target.WindowsPlatform.WindowsSdkVersion,
						msvcDirectory = target.WindowsPlatform.ToolChainDir,
						windowsSdkDirectory = target.WindowsPlatform.WindowsSdkDir,
					}
					: null,
			}
		);
		if (!File.Exists(path) || File.ReadAllText(path) != document)
		{
			File.WriteAllText(path, document);
		}
		runtime.ExternalDependencies.Add(path);
	}

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
