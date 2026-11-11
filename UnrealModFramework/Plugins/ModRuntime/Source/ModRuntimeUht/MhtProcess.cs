// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.UHT.Utils;
using Microsoft.Extensions.Logging;

namespace Mod.Uht;

/// <summary>Owns a child process and forwards its diagnostics to UHT.</summary>
internal static class MhtProcess
{
	internal static int Run(
		ILogger logger,
		string executable,
		IReadOnlyList<string> arguments,
		string workingDirectory,
		int timeoutSeconds,
		string description = "ModRuntime MHT",
		int allowedExitCode = 0,
		IReadOnlyDictionary<string, string>? environment = null
	)
	{
		using ManagedProcessGroup group = new();
		using ManagedProcess process = new(
			group,
			executable,
			CommandLineArguments.Join(arguments),
			workingDirectory,
			environment,
			Array.Empty<byte>(),
			ProcessPriorityClass.Normal
		);
		using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(timeoutSeconds));
		try
		{
			Task.WhenAll(process.WaitForExitAsync(deadline.Token), ForwardOutput(process, logger, deadline.Token))
				.GetAwaiter()
				.GetResult();
		}
		catch (OperationCanceledException)
		{
			throw new UhtException($"{description}: process exceeded {timeoutSeconds} seconds.");
		}
		if (process.ExitCode != 0 && process.ExitCode != allowedExitCode)
		{
			throw new UhtException(
				$"{description} failed with exit code {process.ExitCode}. See the process diagnostics above."
			);
		}
		return process.ExitCode;
	}

	private static async Task ForwardOutput(ManagedProcess process, ILogger logger, CancellationToken cancellation)
	{
		while (await process.ReadLineAsync(cancellation) is string line)
		{
			logger.LogInformation("{MhtDiagnostic}", line);
		}
	}
}
