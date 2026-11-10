// Copyright (c) 2026 Nelaric Contributors
/** @file CommandLine.h */
#pragma once

#include "ModSourceTool/src/Cli/AnalysisOptions.h"

#include <string>
#include <vector>

namespace Mod::SourceTool
{
class CommandLine
{
public:
	static int RunCommandLine(const std::vector<std::string>& Arguments);

private:
	static int PrintHelp();
	static AnalysisOptions ParseAnalysisOptions(const std::vector<std::string>& Arguments);
	static int RunHostLinkCommand(const std::vector<std::string>& Arguments);
};
} // namespace Mod::SourceTool
