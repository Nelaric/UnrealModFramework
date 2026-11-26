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
	internal Dictionary<string, string> PluginGeneratedDirectories { get; } = new();
	private string Tool = "";
	private string SourceTool = "";
	private readonly List<string> Contexts = new();
	private readonly Dictionary<string, (string Header, string Library, string Archiver, bool Modular)> HostArchives =
		new();
	private readonly List<string> Libraries = new();
	private readonly List<string> Inputs = new();
	private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

	internal static MbtHostBuild Prepare(
		IUhtExportFactory factory,
		string projectRoot,
		string pluginRoot,
		string clientLibraryDirectory,
		string generated,
		string variant,
		string buildInput,
		JsonElement input,
		JsonElement config,
		int timeout
	)
	{
		MbtHostBuild result = new();
		string? configured = Environment.GetEnvironmentVariable("UMF_MBT_EXECUTABLE") ?? TextOrNull(config, "mbt");
		result.Tool =
			configured != null
				? Path.GetFullPath(configured, projectRoot)
				: ModRuntimeExporter.LocalExecutable(pluginRoot, "ModBuildTool");
		if (!File.Exists(result.Tool))
		{
			throw new UhtException("ModRuntime MBT: build ModBuildTool or configure UMF_MBT_EXECUTABLE/mbt.");
		}
		string? sourceTool =
			Environment.GetEnvironmentVariable("UMF_MOD_SOURCE_TOOL_EXECUTABLE") ?? TextOrNull(config, "sourceTool");
		result.SourceTool =
			sourceTool != null
				? Path.GetFullPath(sourceTool, projectRoot)
				: ModRuntimeExporter.LocalExecutable(pluginRoot, "ModSourceTool");
		if (!File.Exists(result.SourceTool))
		{
			throw new UhtException(
				"ModRuntime: build ModSourceTool or configure UMF_MOD_SOURCE_TOOL_EXECUTABLE/sourceTool."
			);
		}
		string databasePath = Path.GetFullPath(input.GetProperty("compilationDatabase").GetString()!, projectRoot);
		byte[] databaseBytes = File.ReadAllBytes(databasePath);
		using JsonDocument database = JsonDocument.Parse(Encoding.UTF8.GetString(databaseBytes).TrimStart('\uFEFF'));
		string analysisDatabase = Path.Combine(variant, "compile_commands.json");
		List<object> analysisCommands = new();
		Dictionary<string, string> contextPaths = new();
		string descriptorPath = Path.Combine(variant, "Target.json");
		using JsonDocument descriptor = JsonDocument.Parse(File.ReadAllText(descriptorPath));
		JsonElement target = descriptor.RootElement.GetProperty("target");
		JsonElement graph = UbtPreparation.ReadTargetGraph(
			factory.Session.Logger,
			factory.Session.EngineDirectory!,
			factory.Session.ProjectFile!,
			target,
			descriptor.RootElement.GetProperty("toolchain"),
			timeout
		);
		string targetVariant = Path.Combine(
			target.GetProperty("name").GetString()!,
			target.GetProperty("platform").GetString()!,
			target.GetProperty("configuration").GetString()!,
			target.GetProperty("architecture").GetString()!
		);
		foreach (JsonElement module in input.GetProperty("modules").EnumerateArray())
		{
			string owner = Identifier(module, "name");
			string root = Path.GetFullPath(module.GetProperty("sourceDirectory").GetString()!, projectRoot);
			if (
				!graph.GetProperty("Modules").TryGetProperty(owner, out JsonElement ubtModule)
				|| !SamePath(root, ubtModule.GetProperty("Directory").GetString()!)
			)
			{
				throw new UhtException("ModRuntime MBT: source directory differs from UBT for " + owner);
			}
			JsonElement binary = graph
				.GetProperty("Binaries")
				.EnumerateArray()
				.Single(item => item.GetProperty("Modules").EnumerateArray().Any(name => name.GetString() == owner));
			string binaryPath = binary.GetProperty("File").GetString()!;
			var owningBinary = new
			{
				id = Path.GetFileName(binaryPath),
				path = binaryPath,
				kind = binary.GetProperty("Type").GetString() switch
				{
					"Executable" => "Executable",
					"DynamicLinkLibrary" => "DynamicLibrary",
					_ => throw new UhtException("Unsupported UBT binary for " + owner),
				},
			};
			string tools = Path.Combine(projectRoot, "Intermediate", "ModTools");
			for (
				DirectoryInfo? directory = new(root);
				directory != null && !SamePath(directory.FullName, projectRoot);
				directory = directory.Parent
			)
			{
				if (directory.EnumerateFiles("*.uplugin").Any())
				{
					tools = Path.Combine(directory.FullName, "Intermediate", "ModTools");
					result.PluginGeneratedDirectories.Add(
						owner,
						Path.Combine(tools, "ModHeaderTool", "Generated", targetVariant)
					);
					break;
				}
			}
			string library = Path.Combine(tools, "ModBuildTool", targetVariant, owner + ".lib");
			string linkHeader = Path.Combine(tools, "ModSourceTool", targetVariant, owner + ".modlink.h");
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
				string[] command = entry.TryGetProperty("arguments", out JsonElement tokens)
					? tokens.EnumerateArray().Select(argument => argument.GetString()!).ToArray()
					: CommandLineArguments.Split(entry.GetProperty("command").GetString()!);
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
					target.GetProperty("linkType").GetString()!
				);
				arguments.Add("/I" + root);
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
			result.HostArchives.Add(owner, (linkHeader, library, archiver, owningBinary.kind == "DynamicLibrary"));
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
						target,
						workingDirectory = working,
						outputLibrary = library,
						owningBinary,
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
		string clientContext = ClientBuildRecipe.Prepare(
			result.Contexts[0],
			game,
			clientOwner,
			generated,
			clientLibraryDirectory
		);
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
					sourceTool = result.SourceTool,
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
		string variant = Path.GetDirectoryName(ContextSet)!;
		string hookMap = Path.Combine(Path.GetDirectoryName(manifest)!, "Diagnostics", "HookSources.json");
		using JsonDocument? hooks = File.Exists(hookMap) ? JsonDocument.Parse(File.ReadAllText(hookMap)) : null;
		foreach ((string owner, var archive) in HostArchives)
		{
			string header = archive.Header;
			List<string> arguments = new() { "generate-host-link", owner, header };
			if (hooks != null)
			{
				arguments.AddRange(
					hooks
						.RootElement.GetProperty("units")
						.EnumerateArray()
						.Where(unit => unit.GetProperty("owner").GetString() == owner)
						.Select(unit => unit.GetProperty("witness").GetString()!)
				);
			}
			MhtProcess.Run(factory.Session.Logger, SourceTool, arguments, projectRoot, timeout);
			FileItem.ResetCachedInfo(header);
			factory.AddExternalDependency(header);
			string link = Path.Combine(Path.GetDirectoryName(manifest)!, "Link", owner + ".h");
			string libraryHash = Digest(File.ReadAllBytes(archive.Library));
			string archivePath = archive.Library.Replace('\\', '/');
			string linkHeader = header.Replace('\\', '/');
			StringBuilder directives = new(
				$"// Archive {libraryHash}\n#pragma comment(lib, \"{archivePath}\")\n#include \"{linkHeader}\"\n"
			);
			if (archive.Modular)
			{
				// UBT creates the DLL import library from the owner's objects before linking the DLL.
				// Forward the archive's actual decorated exports into that same import-library action.
				string dumpbin = Path.Combine(Path.GetDirectoryName(archive.Archiver)!, "dumpbin.exe");
				using MemoryStream exports = new();
				MhtProcess.Run(
					factory.Session.Logger,
					dumpbin,
					new[] { "/nologo", "/directives", archive.Library },
					projectRoot,
					timeout,
					standardOutput: exports
				);
				foreach (
					string directive in Encoding
						.UTF8.GetString(exports.ToArray())
						.Split('\n')
						.Select(line => line.Trim())
						.Where(line => line.StartsWith("/EXPORT:", StringComparison.OrdinalIgnoreCase))
						.Distinct(StringComparer.Ordinal)
						.Order(StringComparer.Ordinal)
				)
				{
					directives
						.Append("#pragma comment(linker, \"")
						.Append(directive.Replace("\\", "\\\\").Replace("\"", "\\\""))
						.Append("\")\n");
				}
			}
			Write(link, directives.ToString());
			FileItem.ResetCachedInfo(link);
			factory.AddExternalDependency(link);
		}
		factory.AddExternalDependency(SourceTool);
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
		string stamp = Path.Combine(variant, ".mht-watch.stamp");
		if (!File.Exists(stamp))
		{
			Write(stamp, "UMF watched inputs\n");
		}
		factory.AddExternalDependency(stamp);
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

	internal static string Identifier(JsonElement value, string key)
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
