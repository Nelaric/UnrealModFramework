// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace Mod.Uht;

/// <summary>Dispatches generation and independent native host compilation.</summary>
[UnrealHeaderTool]
internal static class ModRuntimeExporter
{
	[UhtExporter(
		Name = "ModRuntime",
		Description = "Generate Mod APIs and build independent host archives",
		Options = UhtExporterOptions.Default,
		ModuleName = "ExportMetadata"
	)]
	private static void Export(IUhtExportFactory factory)
	{
		if (factory.Session.ProjectFile is not string project || factory.PluginModule == null)
		{
			return;
		}
		if (
			factory.Session.NoOutput
			|| factory.Session.ReferenceMode != UhtReferenceMode.None
			|| Environment
				.GetCommandLineArgs()
				.Any(arg =>
					arg.Equals("-MhtBootstrap", StringComparison.OrdinalIgnoreCase)
					|| arg.Equals("-Mode=GenerateClangDatabase", StringComparison.OrdinalIgnoreCase)
				)
		)
		{
			factory.Session.Logger.LogInformation(
				"ModRuntime MHT: skipped for bootstrap, compile-database preparation, or UHT reference/no-output mode."
			);
			return;
		}
		try
		{
			Run(factory, Path.GetFullPath(project));
		}
		catch (Exception exception) when (exception is not UhtException)
		{
			throw new UhtException($"ModRuntime MHT: {exception.Message}");
		}
	}

	private static void Run(IUhtExportFactory factory, string project)
	{
		string projectRoot = Path.GetDirectoryName(project)!;
		string pluginRoot = FindPluginRoot(factory.PluginModule!.BaseDirectory);
		string configPath = Path.Combine(projectRoot, "Config", "ModHeaderTool.json");
		using JsonDocument settings = File.Exists(configPath)
			? ReadJson(configPath)
			: JsonDocument.Parse("{\"schemaVersion\":1,\"mode\":\"scan\"}");
		JsonElement config = settings.RootElement;
		if (config.GetProperty("schemaVersion").GetInt32() != 1)
		{
			throw new UhtException("ModRuntime MHT: unsupported ModHeaderTool.json schemaVersion.");
		}
		string? inputOverride = Environment.GetEnvironmentVariable("UMF_MHT_BUILD_INPUT");
		string mode = inputOverride != null ? "generate" : OptionalText(config, "mode") ?? "scan";
		if (mode != "scan" && mode != "generate")
		{
			throw new UhtException("ModRuntime MHT: mode must be scan or generate.");
		}
		int timeout = config.TryGetProperty("timeoutSeconds", out JsonElement seconds) ? seconds.GetInt32() : 600;
		if (timeout < 1 || timeout > 86400)
		{
			throw new UhtException("ModRuntime MHT: timeoutSeconds must be between 1 and 86400.");
		}
		string executable = FindExecutable(pluginRoot, projectRoot, config);
		string options = Resolve(
			projectRoot,
			OptionalText(config, "options") ?? "Saved/ModHeaderTool/MetadataOptions.json"
		);
		AddDependency(factory, configPath);
		AddDependency(factory, project);
		AddDependency(factory, executable);
		AddDependency(factory, options);
		foreach (UHTManifest.Module module in factory.Session.Manifest!.Modules)
		{
			foreach (
				string header in module
					.ClassesHeaders.Concat(module.PublicHeaders)
					.Concat(module.InternalHeaders)
					.Concat(module.PrivateHeaders)
			)
			{
				AddDependency(factory, header);
			}
		}
		// UE 5.6 uses Inc/<Module>/UHT. MHT owns the sibling directory.
		string output = Path.Combine(
			Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(factory.PluginModule.OutputDirectory))!,
			"MHT"
		);
		if (
			!IsWithin(Path.Combine(pluginRoot, "Intermediate"), output)
			&& !IsWithin(Path.Combine(projectRoot, "Intermediate"), output)
		)
		{
			throw new UhtException(
				"ModRuntime MHT: output must remain in the plugin or project Intermediate directory."
			);
		}
		List<string> arguments = new() { mode };
		if (mode == "generate")
		{
			if (factory.Session.FailIfGeneratedCodeChanges)
			{
				throw new UhtException(
					"ModRuntime MHT: native generation is not supported with -FailIfGeneratedCodeChanges."
				);
			}
			string targetName = factory.Session.Manifest.TargetName;
			// UE 5.6 places the UHT deps at Build/<platform>/<target>/<configuration>/<target>.deps.
			// Its manifest has no architecture; ConfigureHost restricts this adapter to the actual x64 target.
			string deps = factory.Session.Manifest.ExternalDependenciesFile;
			DirectoryInfo configurationDirectory = new(Path.GetDirectoryName(deps)!);
			string configuration = configurationDirectory.Name;
			string platform = configurationDirectory.Parent!.Parent!.Name;
			const string architecture = "x64";
			if (platform != "Win64" || !OperatingSystem.IsWindows())
			{
				throw new UhtException("ModRuntime MHT: automatic UE 5.6 generation requires Win64 x64 MSVC.");
			}
			string variantKey = $"{platform}/{configuration}/{architecture}";
			JsonElement target =
				config.TryGetProperty("targets", out JsonElement targets)
				&& targets.TryGetProperty(targetName, out JsonElement selected)
					? selected
					: default;
			JsonElement variant = target;
			if (target.ValueKind == JsonValueKind.Object && target.TryGetProperty("variants", out JsonElement variants))
			{
				variant = variants.TryGetProperty(variantKey, out JsonElement choice) ? choice : default;
			}
			string buildInput = Resolve(
				projectRoot,
				inputOverride
					?? OptionalText(variant, "buildInput")
					?? throw new UhtException($"ModRuntime MHT: no build input for {targetName}/{variantKey}.")
			);
			using JsonDocument input = ReadJson(buildInput);
			JsonElement context = input.RootElement;
			string inputProject = Resolve(Path.GetDirectoryName(buildInput)!, RequiredText(context, "project"));
			if (
				!SamePath(inputProject, project)
				|| RequiredText(context, "target") != targetName
				|| RequiredText(context, "platform") != platform
				|| RequiredText(context, "configuration") != configuration
				|| RequiredText(context, "architecture") != architecture
			)
			{
				throw new UhtException(
					$"ModRuntime MHT: build input does not match {targetName}/{variantKey}: {buildInput}"
				);
			}
			output = Path.Combine(output, targetName, configuration, architecture);
			string variantDirectory = Path.Combine(
				projectRoot,
				"Intermediate",
				"ModHeaderTool",
				"HostTargets",
				targetName,
				platform,
				configuration,
				architecture
			);
			Directory.CreateDirectory(variantDirectory);
			using FileStream pipeline = new(
				Path.Combine(variantDirectory, ".pipeline.lock"),
				FileMode.OpenOrCreate,
				FileAccess.ReadWrite,
				FileShare.None,
				1,
				FileOptions.DeleteOnClose
			);
			MbtHostBuild hostBuild = MbtHostBuild.Prepare(
				factory,
				projectRoot,
				pluginRoot,
				output,
				variantDirectory,
				buildInput,
				context,
				config
			);
			arguments.AddRange(new[] { "--build-input", buildInput, "--output-dir", output });
			arguments.AddRange(new[] { "--host-contexts", hostBuild.ContextSet });
			AddDependency(factory, buildInput);
			foreach (string key in new[] { "compilationDatabase", "typeIdentities" })
			{
				if (OptionalText(context, key) is string path)
				{
					AddDependency(factory, Resolve(projectRoot, path));
				}
			}
			if (
				(
					Environment.GetEnvironmentVariable("UMF_MHT_LIBCLANG")
					?? OptionalText(variant, "libclang")
					?? OptionalText(target, "libclang")
				)
				is string clang
			)
			{
				string library = Resolve(projectRoot, clang);
				arguments.AddRange(new[] { "--libclang", library });
				AddDependency(factory, library);
			}
			if (File.Exists(options))
			{
				arguments.AddRange(new[] { "--options", options });
			}
			factory.Session.Logger.LogInformation(
				"ModRuntime MHT: generate for {Target}; output {Output}",
				targetName,
				output
			);
			MhtProcess.Run(factory.Session.Logger, executable, arguments, projectRoot, timeout);
			RegisterGeneratedDependencies(factory, output, context);
			hostBuild.Build(
				factory,
				Path.Combine(output, RequiredText(context, "gameId") + ".generated-artifacts.json"),
				projectRoot,
				timeout
			);
		}
		else
		{
			Directory.CreateDirectory(output);
			string temporary = Path.Combine(output, ".scan-" + Guid.NewGuid().ToString("N") + ".json");
			try
			{
				arguments.AddRange(new[] { "--project", project, "--output", temporary });
				if (File.Exists(options))
				{
					arguments.AddRange(new[] { "--options", options });
				}
				factory.Session.Logger.LogInformation(
					"ModRuntime MHT: scan for {Target}",
					factory.Session.Manifest.TargetName
				);
				MhtProcess.Run(factory.Session.Logger, executable, arguments, projectRoot, timeout);
				using JsonDocument report = ReadJson(temporary);
				if (!report.RootElement.GetProperty("ParametersValidated").GetBoolean())
				{
					throw new UhtException("ModRuntime MHT: scan did not validate export parameters.");
				}
				factory.CommitOutput(Path.Combine(output, "Scan.json"), new StringBuilder(File.ReadAllText(temporary)));
			}
			finally
			{
				File.Delete(temporary);
			}
		}
	}

	private static void RegisterGeneratedDependencies(IUhtExportFactory factory, string output, JsonElement context)
	{
		string manifestPath = Resolve(output, RequiredText(context, "gameId") + ".generated-artifacts.json");
		using JsonDocument manifest = ReadJson(manifestPath);
		JsonElement batch = manifest.RootElement;
		if (
			batch.GetProperty("schemaVersion").GetInt32() != 2
			|| RequiredText(batch, "format") != "UMF.GeneratedArtifacts"
			|| RequiredText(batch, "outputLayout") != "Flat"
			|| RequiredText(batch, "status") != "Complete"
			|| batch.GetProperty("unresolvedTypeCount").GetInt32() != 0
		)
		{
			throw new UhtException("ModRuntime MHT: generation did not publish a complete flat manifest.");
		}
		foreach (string key in new[] { "gameId", "target", "platform", "configuration", "architecture" })
		{
			if (RequiredText(batch, key) != RequiredText(context, key))
			{
				throw new UhtException($"ModRuntime MHT: generated {key} does not match the requested target.");
			}
		}
		AddDependency(factory, manifestPath);
		FileItem.ResetCachedInfo(manifestPath);
		foreach (JsonElement file in batch.GetProperty("generatedFiles").EnumerateArray())
		{
			string path = Resolve(output, RequiredText(file, "path"));
			string relative = Path.GetRelativePath(output, path);
			if (
				Path.IsPathRooted(relative)
				|| relative == "."
				|| relative == ".."
				|| relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
			)
			{
				throw new UhtException("ModRuntime MHT: generated file escapes the output directory.");
			}
			string hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
			if (hash != RequiredText(file, "contentHash"))
			{
				throw new UhtException($"ModRuntime MHT: generated file hash mismatch: {path}");
			}
			AddDependency(factory, path);
			FileItem.ResetCachedInfo(path);
		}
		foreach (JsonElement input in batch.GetProperty("inputFiles").EnumerateArray())
		{
			AddDependency(factory, RequiredText(input, "path"));
		}
		factory.Session.Logger.LogInformation(
			"ModRuntime MHT: checked complete generation {Generation}",
			RequiredText(batch, "generationId")
		);
	}

	private static string FindPluginRoot(string moduleDirectory)
	{
		for (DirectoryInfo? directory = new(moduleDirectory); directory != null; directory = directory.Parent)
		{
			if (File.Exists(Path.Combine(directory.FullName, "ModRuntime.uplugin")))
			{
				return directory.FullName;
			}
		}
		throw new UhtException("ModRuntime MHT: cannot locate ModRuntime.uplugin.");
	}

	private static string FindExecutable(string pluginRoot, string projectRoot, JsonElement config)
	{
		string? configured =
			Environment.GetEnvironmentVariable("UMF_MHT_EXECUTABLE") ?? OptionalText(config, "executable");
		if (configured != null)
		{
			string path = Resolve(projectRoot, configured);
			return File.Exists(path) ? path : throw new UhtException($"ModRuntime MHT: executable not found: {path}");
		}
		string platform =
			OperatingSystem.IsWindows() ? "Win64"
			: OperatingSystem.IsMacOS() ? "Mac"
			: "Linux";
		string suffix = OperatingSystem.IsWindows() ? ".exe" : "";
		foreach (
			string candidate in new[]
			{
				Path.Combine(pluginRoot, "Binaries", "ThirdParty", "ModHeaderTool", platform, "MHT" + suffix),
				Path.Combine(pluginRoot, "Binaries", "ThirdParty", "ModHeaderTool", platform, "ModHeaderTool" + suffix),
				Path.Combine(pluginRoot, "Tools", "build", "Release", "bin", "Release", "ModHeaderTool" + suffix),
			}
		)
		{
			if (File.Exists(candidate))
			{
				return candidate;
			}
		}
		throw new UhtException(
			"ModRuntime MHT: build the native tool and set UMF_MHT_EXECUTABLE or Config/ModHeaderTool.json executable."
		);
	}

	private static JsonDocument ReadJson(string path) => JsonDocument.Parse(File.ReadAllText(path));

	private static string Resolve(string root, string path) => Path.GetFullPath(path, root);

	private static bool IsWithin(string root, string path)
	{
		string relative = Path.GetRelativePath(root, path);
		return !Path.IsPathRooted(relative)
			&& relative != ".."
			&& !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal);
	}

	private static bool SamePath(string left, string right) =>
		string.Equals(
			left,
			right,
			OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal
		);

	private static string? OptionalText(JsonElement value, string name) =>
		value.ValueKind == JsonValueKind.Object && value.TryGetProperty(name, out _) ? RequiredText(value, name) : null;

	private static string RequiredText(JsonElement value, string name)
	{
		string? text = value.GetProperty(name).GetString();
		if (string.IsNullOrWhiteSpace(text) || text.Any(character => char.IsControl(character) || character == '"'))
		{
			throw new UhtException(
				$"ModRuntime MHT: {name} must be nonempty text without control characters or quotes."
			);
		}
		return text;
	}

	private static void AddDependency(IUhtExportFactory factory, string path)
	{
		if (File.Exists(path))
		{
			factory.AddExternalDependency(Path.GetFullPath(path));
		}
	}
}
