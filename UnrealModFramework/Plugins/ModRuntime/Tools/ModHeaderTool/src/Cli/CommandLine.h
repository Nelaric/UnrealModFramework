// Copyright (c) 2026 Nelaric Contributors
/** @file CommandLine.h */
#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
class CommandLine
{
public:
	/// Runs an explicit tool command or displays help.
	static int RunCommandLine(const std::vector<std::string>& Arguments);

private:
	/// Writes command-line usage without accessing project files.
	static void PrintHelp(std::ostream& Output);
};
} // namespace Mod::HeaderTool
