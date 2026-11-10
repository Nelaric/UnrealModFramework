// Copyright (c) 2026 Nelaric Contributors
#include "CommandExecution.h"

#include "ModHeaderTool/src/Cli/CommandLine.h"
#include "ModHeaderTool/src/Cli/OptionsCheck.h"
#include "ModHeaderTool/src/Cli/ScanCommand.h"
#include "ModHeaderTool/src/Generation/Generation.h"

#include <iostream>

namespace Mod::HeaderTool
{
int CommandExecution::ExecuteCommand(const CommandLineOptions& Options)
{
	if (Options.bHelp)
	{
		CommandLine::PrintHelp(std::cout);
		return 0;
	}
	if (Options.bCheckOptions)
	{
		return OptionsCheck::CheckOptions(Options);
	}
	if (Options.bGenerate)
	{
		Generation::Generate(Options.BuildInput, Options.OutputDirectory, Options.Options, Options.ClangLibrary,
		                     Options.HostContexts, Options.LifecycleSource);
		return 0;
	}
	return ScanCommand::RunScan(Options);
}
} // namespace Mod::HeaderTool
