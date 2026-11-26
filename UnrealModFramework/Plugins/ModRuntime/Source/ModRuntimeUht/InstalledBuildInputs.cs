// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EpicGames.Core;
using UnrealBuildBase;

namespace Mod.Uht;

/// <summary>Preserves live UBT action inputs while an installed engine writes its no-PCH analysis actions.</summary>
internal sealed class InstalledBuildInputs : IDisposable
{
	private readonly Dictionary<string, (byte[] Bytes, DateTime Time)> originals = new();

	internal InstalledBuildInputs(string project, string platform, string target, string type, string configuration)
	{
		var descriptor = new FileReference(project);
		IEnumerable<FileReference> plugins = PluginsBase.EnumeratePlugins(descriptor);
		if (JsonObject.Read(descriptor).TryGetStringArrayField("AdditionalPluginDirectories", out var additional))
		{
			plugins = plugins.Concat(
				additional.SelectMany(path =>
					PluginsBase.EnumeratePlugins(
						new DirectoryReference(Path.GetFullPath(path, descriptor.Directory.FullName))
					)
				)
			);
		}
		var roots = plugins
			.Select(file => file.Directory)
			.Append(descriptor.Directory)
			.Append(Unreal.EngineDirectory)
			.Distinct();
		foreach (var root in roots)
		{
			foreach (string tree in new[] { "Intermediate/Build", "Intermediate/External/Build" })
			{
				string platformDir = Path.Combine(root.FullName, tree, platform);
				if (!Directory.Exists(platformDir))
				{
					continue;
				}
				foreach (string parent in Directory.EnumerateDirectories(platformDir).Append(platformDir))
				{
					foreach (string app in new[] { target, "Unreal" + type }.Distinct())
					{
						SaveDirectory(Path.Combine(parent, app, configuration));
						if (configuration == "DebugGame")
						{
							SaveDirectory(Path.Combine(parent, app, "Development"));
						}
					}
				}
			}
		}
	}

	private void SaveDirectory(string directory)
	{
		if (!Directory.Exists(directory))
		{
			return;
		}
		foreach (string pattern in new[] { "*.rsp", "Definitions*.h", "Module.*.cpp" })
		{
			foreach (string file in Directory.EnumerateFiles(directory, pattern, SearchOption.AllDirectories))
			{
				originals.TryAdd(file, (File.ReadAllBytes(file), File.GetLastWriteTimeUtc(file)));
			}
		}
	}

	internal static void PinDefinitions(List<string> arguments, string working, string destination)
	{
		for (int index = 1; index < arguments.Count; ++index)
		{
			string value = arguments[index];
			bool inline = value.StartsWith("/FI", StringComparison.OrdinalIgnoreCase) && value.Length > 3;
			if (!inline && arguments[index - 1] is not ("-include" or "/FI"))
			{
				continue;
			}
			string path = Path.GetFullPath(inline ? value[3..] : value, working);
			string name = Path.GetFileName(path);
			string portable = path.Replace('\\', '/');
			if (
				name.StartsWith("Definitions.", StringComparison.Ordinal)
				&& name.EndsWith(".h", StringComparison.Ordinal)
				&& (
					portable.Contains("/Intermediate/Build/", StringComparison.OrdinalIgnoreCase)
					|| portable.Contains("/Intermediate/External/Build/", StringComparison.OrdinalIgnoreCase)
				)
			)
			{
				string copy = Path.Combine(destination, name);
				ModRuntimeExporter.WriteFileIfChanged(copy, File.ReadAllBytes(path));
				arguments[index] = inline ? "/FI" + copy : copy;
			}
		}
	}

	public void Dispose()
	{
		foreach ((string path, var saved) in originals)
		{
			if (
				!File.Exists(path)
				|| File.GetLastWriteTimeUtc(path) != saved.Time
				|| !File.ReadAllBytes(path).AsSpan().SequenceEqual(saved.Bytes)
			)
			{
				ModRuntimeExporter.WriteFileIfChanged(path, saved.Bytes);
				File.SetLastWriteTimeUtc(path, saved.Time);
				FileItem.ResetCachedInfo(path);
			}
		}
	}
}
