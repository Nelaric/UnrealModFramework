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
	// UHT requires the exporter module to have reflected headers in both targets.
	[UhtExporter(
		Name = "ModRuntime",
		Description = "Generate Mod APIs and build independent host archives",
		Options = UhtExporterOptions.Default,
		ModuleName = "ModLoader"
	)]
	private static void Export(IUhtExportFactory factory)
	{
		if (factory.Session.ProjectFile is not string project || factory.PluginModule == null)
		{
			return;
		}
		bool preparing = Environment
			.GetCommandLineArgs()
			.Any(arg =>
				arg.Equals("-MhtBootstrap", StringComparison.OrdinalIgnoreCase)
				|| arg.Equals("-Mode=GenerateClangDatabase", StringComparison.OrdinalIgnoreCase)
			);
		if (factory.Session.NoOutput || factory.Session.ReferenceMode != UhtReferenceMode.None || preparing)
		{
			if (preparing && Path.GetDirectoryName(factory.Session.Manifest?.ExternalDependenciesFile) is string deps)
			{
				// UBT must run UHT again after a preparation run that skipped MHT.
				factory.AddExternalDependency(Path.Combine(deps, ".mht-deferred"));
			}
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
		string tools = Path.Combine(projectRoot, "Intermediate", "ModTools");
		string mht = Path.Combine(tools, "ModHeaderTool");
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
		AddDependency(factory, configPath);
		UHTManifest manifest = factory.Session.Manifest!;
		string targetName = manifest.TargetName;
		// UE 5.6 places the UHT deps at Build/<platform>/<target>/<configuration>/<target>.deps.
		DirectoryInfo configurationDirectory = new(Path.GetDirectoryName(manifest.ExternalDependenciesFile)!);
		string platform = configurationDirectory.Parent!.Parent!.Name;
		string configuration = configurationDirectory.Name;
		if (mode == "generate" && (platform != "Win64" || !OperatingSystem.IsWindows()))
		{
			factory.Session.Logger.LogInformation(
				"ModRuntime MHT: skipping automatic native generation for {Platform}.",
				platform
			);
			return;
		}
		if (mode == "generate" && factory.Session.FailIfGeneratedCodeChanges)
		{
			throw new UhtException(
				"ModRuntime MHT: native generation is not supported with -FailIfGeneratedCodeChanges."
			);
		}
		int timeout = config.TryGetProperty("timeoutSeconds", out JsonElement seconds) ? seconds.GetInt32() : 600;
		if (timeout < 1 || timeout > 86400)
		{
			throw new UhtException("ModRuntime MHT: timeoutSeconds must be between 1 and 86400.");
		}
		string executable = FindExecutable(pluginRoot, projectRoot, config);
		string defaultOptions = Path.Combine(mht, "MetadataOptions.json");
		string options = OptionalText(config, "options") is string configuredOptions
			? Resolve(projectRoot, configuredOptions)
			: defaultOptions;
		if (
			SamePath(options, defaultOptions)
			&& !factory.Session.FailIfGeneratedCodeChanges
			&& (mode == "generate" || File.Exists(options))
		)
		{
			MetadataSnapshot.EnsureCurrent(
				factory.Session.Logger,
				factory.Session.EngineDirectory!,
				project,
				executable,
				options,
				mode == "generate" ? OptionalText(config, "metadataEditorTarget") : null,
				timeout
			);
		}
		AddDependency(factory, project);
		AddDependency(factory, executable);
		AddDependency(factory, options);
		foreach (UHTManifest.Module module in manifest.Modules)
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
		if (mode == "generate")
		{
			// Its manifest has no architecture; ConfigureHost restricts this adapter to the actual x64 target.
			const string architecture = "x64";
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
			string targetVariant = Path.Combine(targetName, platform, configuration, architecture);
			string output = Path.Combine(mht, "Generated", targetVariant);
			string variantDirectory = Path.Combine(mht, "HostTargets", targetVariant);
			Directory.CreateDirectory(variantDirectory);
			using FileStream pipeline = new(
				Path.Combine(variantDirectory, ".pipeline.lock"),
				FileMode.OpenOrCreate,
				FileAccess.ReadWrite,
				FileShare.None,
				1,
				FileOptions.DeleteOnClose
			);
			string? configuredClang =
				Environment.GetEnvironmentVariable("UMF_MHT_LIBCLANG")
				?? OptionalText(variant, "libclang")
				?? OptionalText(target, "libclang")
				?? OptionalText(config, "libclang");
			string? clang = configuredClang == null ? null : Resolve(projectRoot, configuredClang);
			string? configuredInput = inputOverride ?? OptionalText(variant, "buildInput");
			string buildInput =
				configuredInput != null ? Resolve(projectRoot, configuredInput)
				: config.TryGetProperty("generation", out JsonElement generation)
					? UbtPreparation.PrepareInput(
						factory.Session.Logger,
						factory.Session.EngineDirectory!,
						project,
						variantDirectory,
						(targetName, platform, configuration, architecture),
						generation,
						clang,
						timeout
					)
				: throw new UhtException($"ModRuntime MHT: no build input for {targetName}/{variantKey}.");
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
					$"ModRuntime MHT: build input does not match {targetName}/{variantKey}: \"{buildInput}\""
				);
			}
			MbtHostBuild hostBuild = MbtHostBuild.Prepare(
				factory,
				projectRoot,
				pluginRoot,
				Path.Combine(tools, "ModBuildTool", targetVariant),
				output,
				variantDirectory,
				buildInput,
				context,
				config
			);
			string lifecycleSource = Path.Combine(pluginRoot, "Source", "Runtime", "ModLoader", "Public");
			string lifecycleHeader = Path.Combine(lifecycleSource, "ModInstance.h");
			bool exportLifecycle = manifest.Modules.Any(module =>
				module.PublicHeaders.Any(header => SamePath(header, lifecycleHeader))
			);
			string request = Path.Combine(variantDirectory, "GenerationRequest.json");
			WriteFileIfChanged(
				request,
				JsonSerializer.SerializeToUtf8Bytes(
					new
					{
						format = "UMF.ModHeaderGenerationRequest",
						schemaVersion = 1,
						buildInput,
						outputDirectory = output,
						hostContexts = hostBuild.ContextSet,
						metadataOptions = File.Exists(options) ? options : "",
						libclang = clang ?? "",
						lifecycleSource = exportLifecycle ? lifecycleSource : "",
					}
				)
			);
			AddDependency(factory, request);
			AddDependency(factory, buildInput);
			foreach (string key in new[] { "compilationDatabase", "typeIdentities" })
			{
				if (OptionalText(context, key) is string path)
				{
					AddDependency(factory, Resolve(projectRoot, path));
				}
			}
			if (clang != null)
			{
				AddDependency(factory, clang);
			}
			factory.Session.Logger.LogInformation(
				"ModRuntime MHT: generate for {Target}; output {Output}",
				targetName,
				output
			);
			MhtProcess.Run(factory.Session.Logger, executable, new[] { "generate", request }, projectRoot, timeout);
			RegisterGeneratedDependencies(factory, output, context, hostBuild.PluginGeneratedDirectories);
			hostBuild.Build(
				factory,
				Path.Combine(output, RequiredText(context, "gameId") + ".generated-artifacts.json"),
				projectRoot,
				timeout
			);
		}
		else
		{
			string output = Path.Combine(mht, "Scan", targetName, platform, configuration);
			Directory.CreateDirectory(output);
			using MemoryStream result = new();
			factory.Session.Logger.LogInformation("ModRuntime MHT: scan for {Target}", targetName);
			MhtProcess.Run(
				factory.Session.Logger,
				executable,
				new[] { "scan", project, options },
				projectRoot,
				timeout,
				standardOutput: result
			);
			string content = Encoding.UTF8.GetString(result.ToArray());
			using JsonDocument report = JsonDocument.Parse(content);
			if (!report.RootElement.GetProperty("ParametersValidated").GetBoolean())
			{
				throw new UhtException("ModRuntime MHT: scan did not validate export parameters.");
			}
			if (report.RootElement.GetProperty("MetadataOptions").GetString() is string scannedOptions)
			{
				using JsonDocument catalog = ReadJson(scannedOptions);
				foreach (JsonProperty input in catalog.RootElement.GetProperty("Inputs").EnumerateObject())
				{
					AddDependency(factory, input.Name);
				}
			}
			factory.CommitOutput(Path.Combine(output, "Scan.json"), new StringBuilder(content));
		}
	}

	private static void RegisterGeneratedDependencies(
		IUhtExportFactory factory,
		string output,
		JsonElement context,
		IReadOnlyDictionary<string, string> pluginGeneratedDirectories
	)
	{
		string manifestPath = Resolve(output, RequiredText(context, "gameId") + ".generated-artifacts.json");
		using JsonDocument manifest = ReadJson(manifestPath);
		JsonElement batch = manifest.RootElement;
		if (
			batch.GetProperty("schemaVersion").GetInt32() != 3
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
		List<(string Path, byte[] Bytes)> pluginFiles = new();
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
			byte[] contents = File.ReadAllBytes(path);
			string hash = Convert.ToHexString(SHA256.HashData(contents)).ToLowerInvariant();
			if (hash != RequiredText(file, "contentHash"))
			{
				throw new UhtException($"ModRuntime MHT: generated file hash mismatch: \"{path}\"");
			}
			AddDependency(factory, path);
			FileItem.ResetCachedInfo(path);
			if (
				file.GetProperty("ownerModule").GetString() is string owner
				&& pluginGeneratedDirectories.TryGetValue(owner, out string? directory)
			)
			{
				pluginFiles.Add((Path.Combine(directory, relative), contents));
			}
		}
		// Keep the shared manifest rooted at the project for MBT and SDK consumers.
		foreach ((string path, byte[] contents) in pluginFiles)
		{
			WriteFileIfChanged(path, contents);
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

	internal static void WriteFileIfChanged(string path, byte[] contents)
	{
		if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(contents))
		{
			return;
		}
		Directory.CreateDirectory(Path.GetDirectoryName(path)!);
		string temporary = Path.Combine(Path.GetDirectoryName(path)!, ".mht-" + Guid.NewGuid().ToString("N"));
		try
		{
			File.WriteAllBytes(temporary, contents);
			File.Move(temporary, path, overwrite: true);
		}
		finally
		{
			File.Delete(temporary);
		}
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
			return File.Exists(path)
				? path
				: throw new UhtException($"ModRuntime MHT: executable not found: \"{path}\"");
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
				LocalExecutable(pluginRoot, "ModHeaderTool"),
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

	internal static string LocalExecutable(string pluginRoot, string tool) =>
		Path.Combine(pluginRoot, "Tools", "build", tool, OperatingSystem.IsWindows() ? tool + ".exe" : tool);

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
