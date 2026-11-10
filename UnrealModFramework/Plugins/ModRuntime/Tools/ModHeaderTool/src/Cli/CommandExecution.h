// Copyright (c) 2026 Nelaric Contributors
/** @file CommandExecution.h */
#pragma once

#include "ModHeaderTool/src/Cli/CommandLineOptions.h"

namespace Mod::HeaderTool
{
class CommandExecution
{
public:
	static int ExecuteCommand(const CommandLineOptions& Options);
};
} // namespace Mod::HeaderTool
