// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using EpicGames.Core;
using EpicGames.UHT.Utils;
using UnrealBuildBase;

namespace Mod.Uht;

/// <summary>Transports real UBT compile recipes to the independent MBT.</summary>
internal sealed class MbtHostBuild
{
	internal string ContextSet { get; private set; } = "";
	private string Tool = "";
	private readonly List<string> Contexts = new();
	private readonly List<string> Libraries = new();
	private readonly List<string> Inputs = new();
	private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

	internal static MbtHostBuild Prepare(
		IUhtExportFactory factory,
		string projectRoot,
		string pluginRoot,
		string generated,
		string variant,
		string buildInput,
		JsonElement input,
		JsonElement config
	)
	{
		MbtHostBuild result = new();
		string? configured = Environment.GetEnvironmentVariable("UMF_MBT_EXECUTABLE") ?? TextOrNull(config, "mbt");
		result.Tool =
			configured != null
				? Path.GetFullPath(configured, projectRoot)
				: Path.Combine(
					pluginRoot,
					"Tools",
					"build",
					"Release",
					"bin",
					"Release",
					OperatingSystem.IsWindows() ? "ModBuildTool.exe" : "ModBuildTool"
				);
		if (!File.Exists(result.Tool))
		{
			throw new UhtException("ModRuntime MBT: build ModBuildTool or configure UMF_MBT_EXECUTABLE/mbt.");
		}
		string databasePath = Path.GetFullPath(input.GetProperty("compilationDatabase").GetString()!, projectRoot);
		byte[] databaseBytes = File.ReadAllBytes(databasePath);
		using JsonDocument database = JsonDocument.Parse(Encoding.UTF8.GetString(databaseBytes).TrimStart('\uFEFF'));
		string analysisDatabase = Path.Combine(variant, "compile_commands.json");
		List<object> analysisCommands = new();
		Dictionary<string, string> contextPaths = new();
		foreach (JsonElement module in input.GetProperty("modules").EnumerateArray())
		{
			string owner = Identifier(module, "name");
			string descriptorPath = Path.Combine(variant, owner + ".target.json");
			using JsonDocument descriptorDocument = JsonDocument.Parse(File.ReadAllText(descriptorPath));
			JsonElement descriptor = descriptorDocument.RootElement;
			if (
				descriptor.GetProperty("format").GetString() != "UMF.UeHostTarget"
				|| descriptor.GetProperty("schemaVersion").GetInt32() != 1
				|| descriptor.GetProperty("ownerModule").GetString() != owner
				|| !SamePath(descriptor.GetProperty("project").GetString()!, factory.Session.ProjectFile!)
				|| !SamePath(
					descriptor.GetProperty("sourceDirectory").GetString()!,
					Path.GetFullPath(module.GetProperty("sourceDirectory").GetString()!, projectRoot)
				)
				|| !SamePath(descriptor.GetProperty("generatedDirectory").GetString()!, generated)
			)
			{
				throw new UhtException("ModRuntime MBT: owner descriptor does not match the MHT input: " + owner);
			}
			foreach (string key in new[] { "platform", "configuration", "architecture" })
			{
				if (descriptor.GetProperty("target").GetProperty(key).GetString() != input.GetProperty(key).GetString())
				{
					throw new UhtException("ModRuntime MBT: target descriptor mismatch: " + key);
				}
			}
			if (
				descriptor.GetProperty("target").GetProperty("name").GetString() != factory.Session.Manifest!.TargetName
			)
			{
				throw new UhtException("ModRuntime MBT: target name mismatch.");
			}
			HashSet<string> dependencies = new(StringComparer.OrdinalIgnoreCase)
			{
				descriptorPath,
				buildInput,
				databasePath,
				analysisDatabase,
			};
			string? compiler = null,
				working = null;
			List<string>? recipe = null;
			foreach (JsonElement unit in module.GetProperty("translationUnits").EnumerateArray())
			{
				string source = Path.GetFullPath(unit.GetString()!, projectRoot);
				JsonElement[] entries = database
					.RootElement.EnumerateArray()
					.Where(entry => SamePath(entry.GetProperty("file").GetString()!, source))
					.ToArray();
				if (entries.Length != 1)
				{
					throw new UhtException("ModRuntime MBT: expected one actual UBT compile action for " + source);
				}
				JsonElement entry = entries[0];
				string directory = entry.GetProperty("directory").GetString()!;
				string[] command = CommandLineArguments.Split(entry.GetProperty("command").GetString()!);
				string tool = Path.GetFullPath(command[0], directory);
				if (
					!string.Equals(Path.GetFileName(tool), "cl.exe", StringComparison.OrdinalIgnoreCase)
					|| !File.Exists(tool)
				)
				{
					throw new UhtException("ModRuntime MBT: this adapter requires UBT's actual MSVC cl.exe action.");
				}
				List<string> expanded = new();
				Expand(command.Skip(1), directory, expanded, 0);
				analysisCommands.Add(
					new
					{
						file = source,
						directory,
						arguments = new[] { tool }.Concat(expanded).ToArray(),
					}
				);
				List<string> arguments = Recipe(
					expanded,
					source,
					dependencies,
					owner,
					descriptor.GetProperty("target").GetProperty("linkType").GetString()!
				);
				arguments.Add("/I" + Path.Combine(generated, "SDK", "Public"));
				arguments.Add("/I" + Path.Combine(generated, "Host", owner));
				if (
					recipe != null
					&& (
						!recipe.SequenceEqual(arguments) || !SamePath(compiler!, tool) || !SamePath(working!, directory)
					)
				)
				{
					throw new UhtException("ModRuntime MBT: inconsistent per-file compile environments in " + owner);
				}
				compiler = tool;
				working = directory;
				recipe = arguments;
			}
			if (recipe == null)
			{
				throw new UhtException("ModRuntime MBT: no real compile recipe for " + owner);
			}
			string archiver = Path.Combine(Path.GetDirectoryName(compiler!)!, "lib.exe");
			if (!File.Exists(archiver))
			{
				throw new UhtException("ModRuntime MBT: matching MSVC lib.exe is unavailable.");
			}
			string library = descriptor.GetProperty("outputLibrary").GetString()!;
			string temporary = Path.GetDirectoryName(library)!;
			string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
			string contextPath = library + ".context.json";
			Write(
				contextPath,
				JsonSerializer.Serialize(
					new
					{
						format = "UMF.ModHostContext",
						schemaVersion = 2,
						ownerModule = owner,
						moduleAPIMode = "Producer",
						registrationAnchor = "UMF_ModHost_" + owner,
						target = descriptor.GetProperty("target"),
						workingDirectory = working,
						outputLibrary = library,
						owningBinary = descriptor.GetProperty("owningBinary"),
						linkVerification = "PeExportV1",
						compiler = new
						{
							path = compiler,
							arguments = recipe,
							responseFormat = "msvc",
							dependencyFormat = "msvc-json",
						},
						archiver = new
						{
							path = archiver,
							arguments = new[] { "/NOLOGO", "/OUT:{output}", "{objects}" },
							responseFormat = "msvc",
						},
						environmentMode = "Explicit",
						environment = new
						{
							SYSTEMROOT = windows,
							WINDIR = windows,
							PATH = Path.GetDirectoryName(compiler!) + ";" + Path.Combine(windows, "System32"),
							TEMP = temporary,
							TMP = temporary,
						},
						dependencies = dependencies.OrderBy(path => path, StringComparer.Ordinal).ToArray(),
						lto = false,
						pch = false,
						unity = false,
					},
					JsonOptions
				)
			);
			result.Contexts.Add(contextPath);
			result.Libraries.Add(library);
			result.Inputs.AddRange(dependencies);
			contextPaths[owner] = contextPath;
		}
		string game = Identifier(input, "gameId");
		string clientOwner = game + "ModClient";
		if (contextPaths.ContainsKey(clientOwner))
		{
			throw new UhtException("ModRuntime MBT: the reserved client owner conflicts with a host module.");
		}
		string clientContext = ClientBuildRecipe.Prepare(result.Contexts[0], game, clientOwner, generated);
		result.Contexts.Add(clientContext);
		using (JsonDocument client = JsonDocument.Parse(File.ReadAllText(clientContext)))
		{
			result.Libraries.Add(client.RootElement.GetProperty("outputLibrary").GetString()!);
		}
		contextPaths[clientOwner] = clientContext;
		Write(analysisDatabase, JsonSerializer.Serialize(analysisCommands, JsonOptions));
		result.ContextSet = Path.Combine(variant, "contexts.json");
		Write(
			result.ContextSet,
			JsonSerializer.Serialize(
				new
				{
					format = "UMF.ModHostContextSet",
					schemaVersion = 1,
					project = factory.Session.ProjectFile,
					generatedManifest = Path.Combine(generated, game + ".generated-artifacts.json"),
					sourceCompilationDatabase = databasePath,
					sourceCompilationDatabaseHash = Digest(databaseBytes),
					analysisCompilationDatabase = analysisDatabase,
					analysisCompilationDatabaseHash = Digest(File.ReadAllBytes(analysisDatabase)),
					contexts = contextPaths,
				},
				JsonOptions
			)
		);
		factory.AddExternalDependency(result.Tool);
		return result;
	}

	internal void Build(IUhtExportFactory factory, string manifest, string projectRoot, int timeout)
	{
		foreach (string context in Contexts)
		{
			MhtProcess.Run(
				factory.Session.Logger,
				Tool,
				new[] { "build-host", "--context", context, "--generated-manifest", manifest },
				projectRoot,
				timeout
			);
			factory.AddExternalDependency(context);
		}
		foreach (string library in Libraries)
		{
			// UHT and UBT share these cached items in UE 5.6's internal UHT mode.
			FileItem.ResetCachedInfo(library);
			FileItem.ResetCachedInfo(library + ".mbt.json");
			factory.AddExternalDependency(library);
			factory.AddExternalDependency(library + ".mbt.json");
			using JsonDocument receipt = JsonDocument.Parse(File.ReadAllText(library + ".mbt.json"));
			foreach (JsonElement file in receipt.RootElement.GetProperty("compilerDependencies").EnumerateArray())
			{
				string path = file.GetProperty("path").GetString()!;
				if (!Path.IsPathFullyQualified(path) || !File.Exists(path))
				{
					throw new UhtException("ModRuntime MBT: compiler dependency is missing: " + path);
				}
				factory.AddExternalDependency(path);
			}
		}
		foreach (string dependency in Inputs)
		{
			if (File.Exists(dependency))
			{
				factory.AddExternalDependency(dependency);
			}
		}
		factory.AddExternalDependency(ContextSet);
		string stamp = Path.Combine(Path.GetDirectoryName(ContextSet)!, ".mht-watch.stamp");
		if (!File.Exists(stamp))
		{
			Write(stamp, "UMF watched inputs\n");
		}
		factory.AddExternalDependency(stamp);
	}

	private static void Expand(IEnumerable<string> arguments, string directory, List<string> result, int depth)
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
				Expand(
					CommandLineArguments.Split(File.ReadAllText(path).Replace('\r', ' ').Replace('\n', ' ')),
					directory,
					result,
					depth + 1
				);
			}
			else
			{
				result.Add(argument);
			}
		}
	}

	private static List<string> Recipe(
		List<string> sourceArguments,
		string source,
		HashSet<string> dependencies,
		string owner,
		string linkType
	)
	{
		List<string> result = new();
		bool producer = false;
		string api = owner.ToUpperInvariant() + "_API";
		for (int index = 0; index < sourceArguments.Count; ++index)
		{
			string argument = sourceArguments[index];
			if (Path.IsPathFullyQualified(argument) && SamePath(argument, source))
			{
				continue;
			}
			if (
				argument.StartsWith("/Yu", StringComparison.OrdinalIgnoreCase)
				|| argument.StartsWith("/Yc", StringComparison.OrdinalIgnoreCase)
				|| (
					argument.StartsWith("/Fp", StringComparison.OrdinalIgnoreCase)
					&& !argument.StartsWith("/fp:", StringComparison.OrdinalIgnoreCase)
				)
				|| argument.Equals("/GL", StringComparison.OrdinalIgnoreCase)
				|| argument.StartsWith("/reference", StringComparison.OrdinalIgnoreCase)
			)
			{
				throw new UhtException("ModRuntime MBT: PCH, LTO and C++ modules are not allowed in the host recipe.");
			}
			if (
				argument.Equals("/sourceDependencies", StringComparison.OrdinalIgnoreCase)
				|| argument.Equals("/experimental:log", StringComparison.OrdinalIgnoreCase)
			)
			{
				++index;
				continue;
			}
			if (
				argument.StartsWith("/Fo", StringComparison.OrdinalIgnoreCase)
				|| argument.StartsWith("/Fd", StringComparison.OrdinalIgnoreCase)
			)
			{
				if (argument.Length == 3)
				{
					++index;
				}
				continue;
			}
			if (argument.StartsWith("/FI", StringComparison.OrdinalIgnoreCase))
			{
				string include = argument.Length == 3 ? sourceArguments[++index] : argument[3..];
				if (!Path.IsPathFullyQualified(include))
				{
					throw new UhtException("ModRuntime MBT: forced include must be an actual absolute UBT path.");
				}
				dependencies.Add(include);
				string definitions = File.ReadAllText(include);
				Match match = Regex.Match(definitions, @"(?m)^\s*#\s*define\s+" + Regex.Escape(api) + @"\b([^\r\n]*)");
				if (match.Success)
				{
					string value = match.Groups[1].Value.Trim();
					producer |= linkType == "Monolithic" ? value.Length == 0 : value == "DLLEXPORT";
				}
				result.Add("/FI" + include);
				continue;
			}
			if (argument.StartsWith("/D" + api + "=", StringComparison.OrdinalIgnoreCase))
			{
				string value = argument[(api.Length + 3)..];
				producer |= linkType == "Monolithic" ? value.Length == 0 : value == "DLLEXPORT";
			}
			result.Add(argument);
		}
		if (!producer)
		{
			throw new UhtException("ModRuntime MBT: actual module producer API definition is missing for " + owner);
		}
		result.AddRange(
			new[] { "{source}", "/Fo{object}", "/Fd{object}.pdb", "/sourceDependencies", "{dependencies}" }
		);
		return result;
	}

	private static bool SamePath(string left, string right) =>
		string.Equals(Path.GetFullPath(left), Path.GetFullPath(right), StringComparison.OrdinalIgnoreCase);

	private static string? TextOrNull(JsonElement value, string key) =>
		value.TryGetProperty(key, out JsonElement item) ? item.GetString() : null;

	private static string Digest(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

	private static string Identifier(JsonElement value, string key)
	{
		string name = value.GetProperty(key).GetString()!;
		if (!Regex.IsMatch(name, "\\A[A-Za-z_][A-Za-z0-9_]*\\z"))
		{
			throw new UhtException("ModRuntime MBT: invalid producer identifier: " + name);
		}
		return name;
	}

	private static void Write(string path, string text)
	{
		Directory.CreateDirectory(Path.GetDirectoryName(path)!);
		if (!File.Exists(path) || File.ReadAllText(path) != text)
		{
			File.WriteAllText(path, text);
		}
	}
}
