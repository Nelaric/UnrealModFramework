// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;
using JsonObject = System.Text.Json.Nodes.JsonObject;

namespace Mod.Uht;

/// <summary>Prepares native generation inputs before UBT creates the regular compile actions.</summary>
internal static class UbtPreparation
{
	private static readonly StringComparer PathComparer = OperatingSystem.IsWindows()
		? StringComparer.OrdinalIgnoreCase
		: StringComparer.Ordinal;
	private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

	internal static string PrepareInput(
		ILogger logger,
		string engine,
		string project,
		string variantDirectory,
		(string Target, string Platform, string Configuration, string Architecture) variant,
		string executable,
		JsonElement blueprintExports,
		object reflection,
		string? clang,
		int timeout
	)
	{
		if (clang == null || !File.Exists(clang))
		{
			throw new UhtException("ModRuntime MHT: run Setup or configure UMF_MHT_LIBCLANG for automatic generation.");
		}
		string llvmRoot = Path.GetFullPath("..", Path.GetDirectoryName(clang)!);
		string resources =
			Environment.GetEnvironmentVariable("UMF_MHT_CLANG_RESOURCE_DIR") ?? FindClangResources(llvmRoot);
		if (!File.Exists(Path.Combine(resources, "include", "stddef.h")))
		{
			throw new UhtException("ModRuntime MHT: invalid Clang resource directory: " + resources);
		}
		string projectRoot = Path.GetDirectoryName(project)!;
		using MemoryStream discovery = new();
		MhtProcess.Run(
			logger,
			executable,
			new[] { "discover", project },
			projectRoot,
			timeout,
			standardOutput: discovery
		);
		using JsonDocument scope = JsonDocument.Parse(discovery.ToArray());
		var blueprintOwners =
			blueprintExports.ValueKind == JsonValueKind.Array
				? blueprintExports
					.EnumerateArray()
					.Select(entry => Identifier(entry, "module"))
					.ToHashSet(StringComparer.OrdinalIgnoreCase)
				: new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		var owners = scope
			.RootElement.EnumerateArray()
			.Where(module =>
				module.GetProperty("hasMarkers").GetBoolean() || blueprintOwners.Contains(Identifier(module, "name"))
			)
			.ToDictionary(
				module => Path.GetFullPath(module.GetProperty("sourceDirectory").GetString()!),
				module => Identifier(module, "name"),
				PathComparer
			);
		string database = Path.Combine(variantDirectory, "Ubt", "compile_commands.json");
		using JsonDocument descriptor = JsonDocument.Parse(
			File.ReadAllText(Path.Combine(variantDirectory, "Target.json"))
		);
		JsonElement toolchain = descriptor.RootElement.GetProperty("toolchain");
		bool windowsTarget = variant.Platform == "Win64";
		var units = new SortedDictionary<string, List<string>>(StringComparer.Ordinal);
		if (owners.Count > 0)
		{
			logger.LogInformation(
				"ModRuntime MHT: discovering compile actions for {Target}/{Configuration}.",
				variant.Target,
				variant.Configuration
			);
			var arguments = TargetArguments("-Mode=GenerateClangDatabase", variant, toolchain);
			// Installed UBT uses the regular target intermediate directory for GenerateClangDatabase.
			// A cached outer makefile will execute its original response files as soon as UHT returns.
			using var liveInputs = Unreal.IsEngineInstalled()
				? new InstalledBuildInputs(
					project,
					variant.Platform,
					variant.Target,
					descriptor.RootElement.GetProperty("target").GetProperty("type").GetString()!,
					variant.Configuration
				)
				: null;
			Run(
				logger,
				engine,
				project,
				arguments,
				timeout,
				"ModRuntime MHT compile database",
				path => CaptureDatabase(path, database, owners, units, liveInputs != null)
			);
		}
		else
		{
			ModRuntimeExporter.WriteFileIfChanged(database, "[]"u8.ToArray());
		}
		var rootsByName = owners.ToDictionary(pair => pair.Value, pair => pair.Key);
		JsonObject input = new();
		input["gameId"] = Path.GetFileNameWithoutExtension(project);
		input["targetType"] = descriptor.RootElement.GetProperty("target").GetProperty("type").GetString();
		input["reflection"] = JsonSerializer.SerializeToNode(reflection);
		input["autoDiscoverModules"] = true;
		input["modules"] = JsonSerializer.SerializeToNode(
			units.Select(pair => new
			{
				name = pair.Key,
				sourceDirectory = rootsByName[pair.Key],
				translationUnits = pair.Value.OrderBy(path => path, StringComparer.Ordinal),
			})
		);
		input["schemaVersion"] = 1;
		input["objectReturnPolicy"] = "ErrorIfNull";
		input["project"] = project;
		input["target"] = variant.Target;
		input["platform"] = variant.Platform;
		input["configuration"] = variant.Configuration;
		input["architecture"] = variant.Architecture;
		input["compilationDatabase"] = database;
		// libclang only needs a type-correct AST for UE's compiler-specific constexpr declarations.
		JsonArray analysis = new("-resource-dir=" + resources, "-Wno-invalid-constexpr");
		logger.LogInformation(
			"ModRuntime: suppress invalid-constexpr in libclang analysis of UE headers compiled by the target toolchain."
		);
		if (windowsTarget)
		{
			if (units.Count != 0 && CompilerCompatibility(database, units.First().Value[0]) is string compatibility)
			{
				analysis.Insert(1, "-fms-compatibility-version=" + compatibility);
			}
			analysis.Add(
				"--target="
					+ (
						variant.Architecture switch
						{
							"arm64" => "aarch64",
							"x64" => "x86_64",
							_ => variant.Architecture,
						}
					)
					+ "-pc-windows-msvc"
			);
		}
		input["analysisArguments"] = analysis;
		JsonObject roots = new();
		roots["Engine"] = engine;
		roots["LlvmResource"] = llvmRoot;
		if (windowsTarget)
		{
			roots["MSVC"] = toolchain.GetProperty("msvcDirectory").GetString();
			roots["WindowsSDK"] = toolchain.GetProperty("windowsSdkDirectory").GetString();
		}
		input["pathRoots"] = roots;
		string path = Path.Combine(variantDirectory, "BuildInput.json");
		ModRuntimeExporter.WriteFileIfChanged(path, JsonSerializer.SerializeToUtf8Bytes(input, JsonOptions));
		return path;
	}

	private static List<string> TargetArguments(
		string mode,
		(string Target, string Platform, string Configuration, string Architecture) variant,
		JsonElement toolchain
	)
	{
		List<string> arguments = new()
		{
			mode,
			variant.Target,
			variant.Platform,
			variant.Configuration,
			$"-Architecture={variant.Architecture}",
		};
		if (variant.Platform == "Win64")
		{
			arguments.Add($"-Compiler={toolchain.GetProperty("compiler").GetString()}");
			arguments.Add($"-CompilerVersion={toolchain.GetProperty("compilerVersion").GetString()}");
			arguments.Add($"-WindowsSDKVersion={toolchain.GetProperty("windowsSdkVersion").GetString()}");
		}
		return arguments;
	}

	internal static JsonElement ReadTargetGraph(
		ILogger logger,
		string engine,
		string project,
		JsonElement target,
		JsonElement toolchain,
		int timeout
	)
	{
		var args = TargetArguments(
			"-Mode=JsonExport",
			(
				target.GetProperty("name").GetString()!,
				target.GetProperty("platform").GetString()!,
				target.GetProperty("configuration").GetString()!,
				target.GetProperty("architecture").GetString()!
			),
			toolchain
		);
		JsonElement graph = default;
		Run(
			logger,
			engine,
			project,
			args,
			timeout,
			"ModRuntime UBT module ownership",
			path =>
			{
				using JsonDocument document = JsonDocument.Parse(File.ReadAllText(path));
				graph = document.RootElement.Clone();
			}
		);
		return graph;
	}

	internal static string HostArchitecture =>
		RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? "arm64" : "x64";

	internal static string HostPlatform =>
		OperatingSystem.IsWindows() ? "Win64"
		: OperatingSystem.IsMacOS() ? "Mac"
		: HostArchitecture == "arm64" ? "LinuxArm64"
		: "Linux";

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
			string launcher = Path.ChangeExtension(ubt, OperatingSystem.IsWindows() ? ".exe" : null);
			List<string> arguments = new();
			if (
				Environment.ProcessPath is string process
				&& Path.GetFileNameWithoutExtension(process).Equals("dotnet", StringComparison.OrdinalIgnoreCase)
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
					"-NoEngineChanges",
					"-NoHotReload",
					"-SkipRulesCompile",
					$"-Log={Path.Combine(work.FullName, "Ubt.log")}",
				}
			);
			if (readDatabase != null)
			{
				arguments.Add(
					targetArguments.Contains("-Mode=JsonExport")
						? $"-OutputFile={Path.Combine(work.FullName, "target.json")}"
						: $"-OutputDir={work.FullName}"
				);
			}
			Dictionary<string, string> environment = new(StringComparer.OrdinalIgnoreCase);
			foreach (DictionaryEntry entry in Environment.GetEnvironmentVariables())
			{
				environment[(string)entry.Key] = (string)entry.Value!;
			}
			environment["TEMP"] = work.FullName;
			environment["TMP"] = work.FullName;
			environment["TMPDIR"] = work.FullName;
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
			readDatabase?.Invoke(
				Path.Combine(
					work.FullName,
					targetArguments.Contains("-Mode=JsonExport") ? "target.json" : "compile_commands.json"
				)
			);
		}
		finally
		{
			work.Delete(recursive: true);
		}
	}

	private static void CaptureDatabase(
		string raw,
		string output,
		Dictionary<string, string> owners,
		SortedDictionary<string, List<string>> units,
		bool pinDefinitions
	)
	{
		using JsonDocument database = JsonDocument.Parse(File.ReadAllText(raw));
		var entries = new List<object>();
		var captured = new HashSet<string>(PathComparer);
		foreach (JsonElement entry in database.RootElement.EnumerateArray())
		{
			string directory = entry.GetProperty("directory").GetString()!;
			string file = Path.GetFullPath(entry.GetProperty("file").GetString()!, directory);
			string ext = Path.GetExtension(file).ToLowerInvariant();
			if (ext is not (".cpp" or ".cc" or ".cxx"))
			{
				continue;
			}
			string? owner = null;
			for (string? parent = Path.GetDirectoryName(file); parent != null; parent = Path.GetDirectoryName(parent))
			{
				if (owners.TryGetValue(parent, out owner))
				{
					break;
				}
			}
			if (owner == null)
			{
				continue;
			}
			if (!captured.Add(file))
			{
				throw new UhtException($"ModRuntime MHT: UBT produced multiple compile actions for \"{file}\".");
			}
			if (!units.TryGetValue(owner, out var ownerUnits))
			{
				units.Add(owner, ownerUnits = new());
			}
			ownerUnits.Add(file);
			string[] command = CommandLineArguments.Split(entry.GetProperty("command").GetString()!);
			List<string> arguments = new() { command[0] };
			string driver = Path.GetFileNameWithoutExtension(command[0]);
			Expand(command.Skip(1), directory, arguments, 0, driver != "cl" && driver != "clang-cl");
			if (pinDefinitions)
			{
				InstalledBuildInputs.PinDefinitions(
					arguments,
					directory,
					Path.Combine(Path.GetDirectoryName(output)!, "Definitions", owner)
				);
			}
			entries.Add(
				new
				{
					file,
					directory,
					arguments,
				}
			);
		}
		ModRuntimeExporter.WriteFileIfChanged(output, JsonSerializer.SerializeToUtf8Bytes(entries, JsonOptions));
	}

	internal static List<string> SplitGnuResponse(string text)
	{
		List<string> tokens = new();
		StringBuilder token = new();
		char quote = '\0';
		for (int i = 0; i < text.Length; ++i)
		{
			char c = text[i];
			if (c == '\\' && i + 1 < text.Length)
			{
				token.Append(text[++i]);
			}
			else if (quote != '\0')
			{
				if (c == quote)
				{
					quote = '\0';
				}
				else
				{
					token.Append(c);
				}
			}
			else if (c == '"' || c == '\'')
			{
				quote = c;
			}
			else if (char.IsWhiteSpace(c))
			{
				if (token.Length > 0)
				{
					tokens.Add(token.ToString());
					token.Clear();
				}
				continue;
			}
			else
			{
				token.Append(c);
			}
		}
		if (quote != '\0')
		{
			throw new UhtException("Unterminated quote in compiler response file.");
		}
		if (token.Length > 0)
		{
			tokens.Add(token.ToString());
		}
		return tokens;
	}

	internal static string? FindHostLibClang(string pluginRoot, IUhtExportFactory factory)
	{
		string system =
			OperatingSystem.IsWindows() ? "Windows"
			: OperatingSystem.IsMacOS() ? "Darwin"
			: "Linux";
		string build = Path.Combine(pluginRoot, "Tools", "build");
		string receipt = Path.Combine(build, $"libclang-{system}-{HostArchitecture}.path");
		if (File.Exists(receipt))
		{
			factory.AddExternalDependency(receipt);
			string library = Path.GetFullPath(File.ReadAllText(receipt).Trim(), build);
			if (File.Exists(library))
			{
				return library;
			}
		}
		if (OperatingSystem.IsWindows())
		{
			string library = Path.Combine(
				Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
				"LLVM",
				"bin",
				"libclang.dll"
			);
			return File.Exists(library) ? library : null;
		}
		var libraries = OperatingSystem.IsMacOS()
			? new[]
			{
				"/opt/homebrew/opt/llvm/lib/libclang.dylib",
				"/usr/local/opt/llvm/lib/libclang.dylib",
				"/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib",
			}
			: Directory
				.EnumerateDirectories("/usr/lib", "llvm-*")
				.OrderByDescending(path => int.TryParse(Path.GetFileName(path)[5..], out int major) ? major : 0)
				.SelectMany(path =>
					new[] { Path.Combine(path, "lib/libclang.so.1"), Path.Combine(path, "lib/libclang.so") }
				)
				.ToArray();
		return libraries.FirstOrDefault(File.Exists);
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

	private static string? CompilerCompatibility(string database, string source)
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
				return null;
			}
			FileVersionInfo version = FileVersionInfo.GetVersionInfo(compiler);
			return $"{version.FileMajorPart}.{version.FileMinorPart}";
		}
		throw new UhtException($"ModRuntime MHT: UBT did not produce a compile action for \"{source}\".");
	}

	internal static void Expand(
		IEnumerable<string> arguments,
		string directory,
		List<string> result,
		int depth,
		bool gnu = false
	)
	{
		if (depth > 16)
		{
			throw new UhtException("ModRuntime MBT: response file nesting is too deep.");
		}
		foreach (string argument in arguments)
		{
			if (argument.StartsWith('@'))
			{
				string path = Path.GetFullPath(argument[1..].Trim('"'), directory);
				string contents;
				try
				{
					contents = File.ReadAllText(path);
				}
				catch (IOException exception) when (exception is FileNotFoundException or DirectoryNotFoundException)
				{
					throw new UhtException(
						$"ModRuntime MBT: missing UBT response file \"{path}\". Refresh the supplied compilation database."
					);
				}
				Expand(
					gnu
						? UbtPreparation.SplitGnuResponse(contents)
						: CommandLineArguments.Split(contents.Replace('\r', ' ').Replace('\n', ' ')),
					directory,
					result,
					depth + 1,
					gnu
				);
			}
			else
			{
				result.Add(argument);
			}
		}
	}

	internal static string Identifier(JsonElement value, string key)
	{
		string name = value.GetProperty(key).GetString()!;
		if (!Regex.IsMatch(name, "\\A[A-Za-z_][A-Za-z0-9_]*\\z"))
		{
			throw new UhtException("ModRuntime MBT: invalid producer identifier: " + name);
		}
		return name;
	}
}
