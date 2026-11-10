// Copyright (c) 2026 Nelaric Contributors
/** @file CommandLine.h */
#pragma once

#include "ModHeaderTool/src/Cli/CommandLineOptions.h"

#include <ostream>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
class CommandLine
{
public:
	/// Parses command-line arguments, including the executable name.
	static CommandLineOptions ParseArguments(const std::vector<std::string>& Arguments);
	/// Writes command-line usage without accessing project files.
	static void PrintHelp(std::ostream& Output);
};
} // namespace Mod::HeaderTool
