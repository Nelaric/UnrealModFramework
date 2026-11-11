// Copyright (c) 2026 Nelaric Contributors
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
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
		IReadOnlyDictionary<string, string>? environment = null,
		Stream? standardOutput = null
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
			ProcessPriorityClass.Normal,
			flags: standardOutput == null ? ManagedProcessFlags.MergeOutputPipes : ManagedProcessFlags.None
		);
		using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(timeoutSeconds));
		try
		{
			Task.WhenAll(
					process.WaitForExitAsync(deadline.Token),
					ForwardOutput(process.StdErrText, logger, deadline.Token),
					standardOutput == null ? Task.CompletedTask : process.CopyToAsync(standardOutput, deadline.Token)
				)
				.WaitAsync(deadline.Token)
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

	private static async Task ForwardOutput(StreamReader diagnostics, ILogger logger, CancellationToken cancellation)
	{
		while (await diagnostics.ReadLineAsync(cancellation) is string line)
		{
			logger.LogInformation("{MhtDiagnostic}", line);
		}
	}
}
