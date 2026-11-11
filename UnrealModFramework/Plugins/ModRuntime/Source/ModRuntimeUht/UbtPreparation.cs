// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using EpicGames.Core;
using EpicGames.UHT.Utils;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;
using JsonObject = System.Text.Json.Nodes.JsonObject;

namespace Mod.Uht;

/// <summary>Prepares native generation inputs before UBT creates the regular compile actions.</summary>
internal static class UbtPreparation
{
	private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

	internal static string PrepareInput(
		ILogger logger,
		string engine,
		string project,
		string variantDirectory,
		(string Target, string Platform, string Configuration, string Architecture) variant,
		JsonElement generation,
		string? clang,
		int timeout
	)
	{
		if (clang == null || !File.Exists(clang))
		{
			throw new UhtException("ModRuntime MHT: configure libclang or UMF_MHT_LIBCLANG for automatic generation.");
		}
		string llvmRoot = Path.GetFullPath("..", Path.GetDirectoryName(clang)!);
		string resources = FindClangResources(llvmRoot);
		if (
			generation.ValueKind != JsonValueKind.Object
			|| !generation.TryGetProperty("modules", out JsonElement modules)
			|| modules.ValueKind != JsonValueKind.Array
			|| modules.GetArrayLength() == 0
		)
		{
			throw new UhtException("ModRuntime MHT: generation requires at least one export module.");
		}
		JsonElement firstModule = modules[0];
		string owner = MbtHostBuild.Identifier(firstModule, "name");
		string descriptorPath = Path.Combine(variantDirectory, owner + ".target.json");
		using JsonDocument descriptor = JsonDocument.Parse(File.ReadAllText(descriptorPath));
		JsonElement toolchain = descriptor.RootElement.GetProperty("toolchain");
		string database = Path.Combine(variantDirectory, "Ubt", "compile_commands.json");
		string databaseKey = database.ToUpperInvariant();
		string projectRoot = Path.GetDirectoryName(project)!;
		if (AppDomain.CurrentDomain.GetData(databaseKey) is true || !File.Exists(database))
		{
			AppDomain.CurrentDomain.SetData(databaseKey, null);
			HashSet<string> sources = modules
				.EnumerateArray()
				.SelectMany(module => module.GetProperty("translationUnits").EnumerateArray())
				.Select(unit => Path.GetFullPath(unit.GetString()!, projectRoot))
				.ToHashSet(StringComparer.OrdinalIgnoreCase);
			logger.LogInformation(
				"ModRuntime MHT: preparing UBT compile actions for {Target}/{Configuration}.",
				variant.Target,
				variant.Configuration
			);
			Run(
				logger,
				engine,
				project,
				new[]
				{
					"-Mode=GenerateClangDatabase",
					variant.Target,
					variant.Platform,
					variant.Configuration,
					$"-Architecture={variant.Architecture}",
					$"-Compiler={toolchain.GetProperty("compiler").GetString()}",
					$"-CompilerVersion={toolchain.GetProperty("compilerVersion").GetString()}",
					$"-WindowsSDKVersion={toolchain.GetProperty("windowsSdkVersion").GetString()}",
				},
				timeout,
				"ModRuntime MHT compile database",
				path => CaptureDatabase(path, database, sources)
			);
		}
		string source = Path.GetFullPath(firstModule.GetProperty("translationUnits")[0].GetString()!, projectRoot);
		string compatibility = CompilerCompatibility(database, source);
		JsonObject input = JsonNode.Parse(generation.GetRawText())!.AsObject();
		input["schemaVersion"] = 1;
		input["objectReturnPolicy"] = "ErrorIfNull";
		input["project"] = project;
		input["target"] = variant.Target;
		input["platform"] = variant.Platform;
		input["configuration"] = variant.Configuration;
		input["architecture"] = variant.Architecture;
		input["compilationDatabase"] = database;
		JsonArray analysis = input["analysisArguments"]?.AsArray() ?? new JsonArray();
		analysis.Insert(0, "-resource-dir=" + resources);
		analysis.Insert(1, "-fms-compatibility-version=" + compatibility);
		input["analysisArguments"] ??= analysis;
		JsonObject roots = input["pathRoots"]?.AsObject() ?? new JsonObject();
		roots["Engine"] = engine;
		roots["LlvmResource"] = llvmRoot;
		roots["MSVC"] = toolchain.GetProperty("msvcDirectory").GetString();
		roots["WindowsSDK"] = toolchain.GetProperty("windowsSdkDirectory").GetString();
		input["pathRoots"] ??= roots;
		string path = Path.Combine(variantDirectory, "BuildInput.json");
		ModRuntimeExporter.WriteFileIfChanged(path, JsonSerializer.SerializeToUtf8Bytes(input, JsonOptions));
		return path;
	}

	internal static void BuildEditor(ILogger logger, string engine, string project, string target, int timeout)
	{
		logger.LogInformation("ModRuntime MHT: building {Target} Development for the metadata commandlet.", target);
		try
		{
			Run(
				logger,
				engine,
				project,
				new[] { target, "Win64", "Development", "-MhtBootstrap", "-NoUBTMakefiles" },
				timeout,
				"ModRuntime MHT metadata bootstrap"
			);
		}
		finally
		{
			// The commandlet loads the standard DLL names. Make a later regular link replace the bootstrap DLLs.
			string variant = Path.Combine(
				Path.GetDirectoryName(project)!,
				"Intermediate/ModTools/ModHeaderTool/HostTargets",
				target,
				"Win64/Development/x64"
			);
			if (Directory.Exists(variant))
			{
				foreach (string path in Directory.EnumerateFiles(variant, "*.target.json"))
				{
					using JsonDocument descriptor = JsonDocument.Parse(File.ReadAllText(path));
					string header = descriptor.RootElement.GetProperty("hostLinkHeader").GetString()!;
					if (File.Exists(header))
					{
						File.SetLastWriteTimeUtc(header, DateTime.UtcNow);
						FileItem.ResetCachedInfo(header);
					}
				}
			}
		}
	}

	private static void Run(
		ILogger logger,
		string engine,
		string project,
		IReadOnlyList<string> targetArguments,
		int timeout,
		string description,
		Action<string>? readDatabase = null
	)
	{
		DirectoryInfo work = Directory.CreateTempSubdirectory("umf-mht-");
		try
		{
			string ubt = Path.Combine(engine, "Binaries", "DotNET", "UnrealBuildTool", "UnrealBuildTool.dll");
			string launcher = Path.ChangeExtension(ubt, ".exe");
			List<string> arguments = new();
			if (
				Environment.ProcessPath is string process
				&& Path.GetFileName(process).Equals("dotnet.exe", StringComparison.OrdinalIgnoreCase)
			)
			{
				launcher = process;
				arguments.Add(ubt);
			}
			arguments.AddRange(targetArguments);
			// The outer UBT owns the engine mutex while waiting for this child.
			arguments.AddRange(
				new[]
				{
					$"-Project={project}",
					"-NoMutex",
					"-NoHotReload",
					"-SkipRulesCompile",
					$"-Log={Path.Combine(work.FullName, "Ubt.log")}",
				}
			);
			if (readDatabase != null)
			{
				arguments.Add($"-OutputDir={work.FullName}");
			}
			Dictionary<string, string> environment = new(StringComparer.OrdinalIgnoreCase);
			foreach (DictionaryEntry entry in Environment.GetEnvironmentVariables())
			{
				environment[(string)entry.Key] = (string)entry.Value!;
			}
			environment["TEMP"] = work.FullName;
			environment["TMP"] = work.FullName;
			environment["UnrealBuildTool_TMP"] = work.FullName;
			environment["UseSharedCompilation"] = "false";
			MhtProcess.Run(
				logger,
				launcher,
				arguments,
				Path.GetDirectoryName(project)!,
				timeout,
				description,
				environment: environment
			);
			readDatabase?.Invoke(Path.Combine(work.FullName, "compile_commands.json"));
		}
		finally
		{
			work.Delete(recursive: true);
		}
	}

	private static void CaptureDatabase(string raw, string output, HashSet<string> sources)
	{
		using JsonDocument database = JsonDocument.Parse(File.ReadAllText(raw));
		var entries = new List<object>();
		var captured = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (JsonElement entry in database.RootElement.EnumerateArray())
		{
			string directory = entry.GetProperty("directory").GetString()!;
			string file = Path.GetFullPath(entry.GetProperty("file").GetString()!, directory);
			if (!sources.Contains(file))
			{
				continue;
			}
			if (!captured.Add(file))
			{
				throw new UhtException($"ModRuntime MHT: UBT produced multiple compile actions for \"{file}\".");
			}
			string[] command = CommandLineArguments.Split(entry.GetProperty("command").GetString()!);
			List<string> arguments = new() { command[0] };
			MbtHostBuild.Expand(command.Skip(1), directory, arguments, 0);
			entries.Add(
				new
				{
					file,
					directory,
					arguments,
				}
			);
		}
		if (sources.Except(captured, StringComparer.OrdinalIgnoreCase).FirstOrDefault() is string missing)
		{
			throw new UhtException($"ModRuntime MHT: UBT did not produce a compile action for \"{missing}\".");
		}
		ModRuntimeExporter.WriteFileIfChanged(output, JsonSerializer.SerializeToUtf8Bytes(entries, JsonOptions));
	}

	private static string FindClangResources(string llvmRoot)
	{
		string directory = Path.Combine(llvmRoot, "lib", "clang");
		string[] matches = Directory.Exists(directory)
			? Directory
				.GetDirectories(directory)
				.Where(path => File.Exists(Path.Combine(path, "include", "stddef.h")))
				.ToArray()
			: Array.Empty<string>();
		if (matches.Length != 1)
		{
			throw new UhtException($"ModRuntime MHT: expected one LLVM resource directory under \"{directory}\".");
		}
		return matches[0];
	}

	private static string CompilerCompatibility(string database, string source)
	{
		using JsonDocument commands = JsonDocument.Parse(File.ReadAllText(database));
		foreach (JsonElement entry in commands.RootElement.EnumerateArray())
		{
			if (
				!Path.GetFullPath(entry.GetProperty("file").GetString()!)
					.Equals(source, StringComparison.OrdinalIgnoreCase)
			)
			{
				continue;
			}
			string compiler = entry.GetProperty("arguments")[0].GetString()!;
			if (!Path.GetFileName(compiler).Equals("cl.exe", StringComparison.OrdinalIgnoreCase))
			{
				throw new UhtException("ModRuntime MHT: automatic generation requires UBT's MSVC toolchain.");
			}
			FileVersionInfo version = FileVersionInfo.GetVersionInfo(compiler);
			return $"{version.FileMajorPart}.{version.FileMinorPart}";
		}
		throw new UhtException($"ModRuntime MHT: UBT did not produce a compile action for \"{source}\".");
	}
}
