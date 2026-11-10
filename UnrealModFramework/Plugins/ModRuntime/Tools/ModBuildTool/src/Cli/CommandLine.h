// Copyright (c) 2026 Nelaric Contributors
/** @file CommandLine.h */
#pragma once

#include "ModBuildTool/src/Cli/CommandArguments.h"

#include <string>
#include <vector>

namespace Mod::BuildTool
{
class CommandLine
{
public:
	static int RunCommandLine(const std::vector<std::string>& Arguments);

private:
	struct Command;

	static void PrintHelp();
	static const Command& FindCommand(const std::string& Name);
	static CommandArguments ParseOptions(const std::vector<std::string>& Arguments, const Command& Route);
};
} // namespace Mod::BuildTool
