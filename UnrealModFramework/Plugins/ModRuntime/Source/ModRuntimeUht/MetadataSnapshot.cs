// Copyright (c) 2026 Nelaric Contributors
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using EpicGames.Core;
using EpicGames.UHT.Utils;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace Mod.Uht;

/// <summary>Refreshes the default metadata snapshot using the active engine and the existing project editor.</summary>
internal static class MetadataSnapshot
{
	internal static void EnsureCurrent(
		ILogger logger,
		string engine,
		string project,
		string mht,
		string options,
		string? editorTarget,
		int timeout
	)
	{
		const int refreshRequired = 2; // ModHeaderTool check-options exit code.
		string projectRoot = Path.GetDirectoryName(project)!;
		int Check(string path) =>
			MhtProcess.Run(
				logger,
				mht,
				new[] { "check-options", project, path },
				projectRoot,
				timeout,
				allowedExitCode: refreshRequired
			);

		string lockPath = OperatingSystem.IsWindows() ? options.ToUpperInvariant() : options;
		string lockId = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(lockPath)));
		using Mutex refreshLock = new(false, "UMF-MetadataOptions-" + lockId);
		try
		{
			if (!refreshLock.WaitOne(TimeSpan.FromSeconds(timeout)))
			{
				throw new UhtException($"ModRuntime MHT: timed out waiting to refresh \"{options}\".");
			}
		}
		catch (AbandonedMutexException)
		{
			// A terminated exporter leaves the published snapshot intact. We now own the mutex.
		}

		try
		{
			if (Check(options) == 0)
			{
				return;
			}
			const string bootstrap = "Build the project's Development Editor with -MhtBootstrap if needed.";
			string editor = EditorExecutable(engine);
			if (!File.Exists(editor))
			{
				throw new UhtException(
					$"ModRuntime ModExportOptions: Editor executable not found: \"{editor}\". {bootstrap}"
				);
			}
			if (editorTarget != null && !HasDevelopmentEditor(projectRoot, editorTarget))
			{
				UbtPreparation.BuildEditor(logger, engine, project, editorTarget, timeout);
			}
			string directory = Path.GetDirectoryName(options)!;
			Directory.CreateDirectory(directory);
			string temporary = Path.Combine(directory, $".options-{Guid.NewGuid():N}.json");
			string log = Path.ChangeExtension(temporary, ".log");
			try
			{
				logger.LogInformation("ModRuntime MHT: refreshing metadata snapshot \"{Options}\".", options);
				try
				{
					MhtProcess.Run(
						logger,
						editor,
						new[]
						{
							project,
							"-run=ModExportOptions",
							$"-Output={temporary}",
							$"-abslog={log}",
							"-unattended",
							"-SkipCompile",
							"-NullRHI",
							"-NoSound",
							"-NoSplash",
							"-stdout",
						},
						projectRoot,
						timeout,
						description: "ModRuntime ModExportOptions"
					);
				}
				catch (UhtException exception)
				{
					throw new UhtException($"{exception.Message} {bootstrap}");
				}
				if (Check(temporary) != 0)
				{
					throw new UhtException(
						$"ModRuntime ModExportOptions did not produce a current snapshot. {bootstrap}"
					);
				}
				File.Move(temporary, options, overwrite: true);
				FileItem.ResetCachedInfo(options);
			}
			finally
			{
				File.Delete(temporary);
				File.Delete(log);
			}
		}
		finally
		{
			refreshLock.ReleaseMutex();
		}
	}

	internal static string HostArchitecture =>
		RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? "arm64" : "x64";

	internal static string HostPlatform =>
		OperatingSystem.IsWindows() ? "Win64"
		: OperatingSystem.IsMacOS() ? "Mac"
		: RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? "LinuxArm64"
		: "Linux";

	internal static string EditorExecutable(string engine) =>
		Path.Combine(
			engine,
			"Binaries",
			HostPlatform,
			OperatingSystem.IsWindows() ? "UnrealEditor-Cmd.exe"
				: OperatingSystem.IsMacOS() ? "UnrealEditor.app/Contents/MacOS/UnrealEditor"
				: "UnrealEditor"
		);

	internal static bool HasDevelopmentEditor(string projectRoot, string target)
	{
		const string projectPrefix = "$(ProjectDir)/";
		string directory = Path.Combine(projectRoot, "Binaries", HostPlatform);
		string receipt = Path.Combine(directory, target + ".target");
		if (!File.Exists(receipt))
		{
			receipt = Path.Combine(directory, $"{target}-{HostPlatform}-Development{HostArchitecture}.target");
			if (!File.Exists(receipt))
			{
				return false;
			}
		}
		using JsonDocument document = JsonDocument.Parse(File.ReadAllText(receipt));
		var metadata = document.RootElement;
		if (
			metadata.GetProperty("Platform").GetString() != HostPlatform
			|| metadata.GetProperty("TargetName").GetString() != target
			|| metadata.GetProperty("TargetType").GetString() != "Editor"
			|| metadata.GetProperty("Configuration").GetString() != "Development"
			|| Array.IndexOf((metadata.GetProperty("Architecture").GetString() ?? "").Split('+'), HostArchitecture) < 0
		)
		{
			return false;
		}
		foreach (JsonElement product in metadata.GetProperty("BuildProducts").EnumerateArray())
		{
			string path = product.GetProperty("Path").GetString()!;
			if (
				path.StartsWith(projectPrefix, StringComparison.Ordinal)
				&& (
					product.GetProperty("Type").GetString() == "DynamicLibrary"
					|| path.EndsWith(".modules", StringComparison.OrdinalIgnoreCase)
				)
				&& !File.Exists(Path.Combine(projectRoot, path[projectPrefix.Length..]))
			)
			{
				return false;
			}
		}
		return true;
	}
}
