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

/// <summary>Owns a native MHT process and forwards its diagnostics to UHT.</summary>
internal static class MhtProcess
{
	internal static void Run(
		ILogger logger,
		string executable,
		IReadOnlyList<string> arguments,
		string workingDirectory,
		int timeoutSeconds
	)
	{
		using ManagedProcessGroup group = new();
		using ManagedProcess process = new(
			group,
			executable,
			CommandLineArguments.Join(arguments),
			workingDirectory,
			null,
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
			throw new UhtException($"ModRuntime MHT: native process exceeded {timeoutSeconds} seconds.");
		}
		if (process.ExitCode != 0)
		{
			throw new UhtException(
				$"ModRuntime MHT failed with exit code {process.ExitCode}. See the native diagnostics above."
			);
		}
	}

	private static async Task ForwardOutput(ManagedProcess process, ILogger logger, CancellationToken cancellation)
	{
		while (await process.ReadLineAsync(cancellation) is string line)
		{
			logger.LogInformation("{MhtDiagnostic}", line);
		}
	}
}
