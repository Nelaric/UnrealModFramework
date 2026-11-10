// Copyright (c) 2026 Nelaric Contributors
/** @file CommandLine.h */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

class CommandLine
{
public:
	static int RunCommandLine(const std::vector<std::string>& Arguments);

private:
	struct Command;

	static int PrintHelp();
	static const Command& FindCommand(const std::string& Name);
	static Options ParseOptions(const std::vector<std::string>& Arguments, const Command& Route);
};
} // namespace Mod::SdkTool
