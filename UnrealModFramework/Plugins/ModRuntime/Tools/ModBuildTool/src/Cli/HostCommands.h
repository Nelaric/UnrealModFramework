// Copyright (c) 2026 Nelaric Contributors
/** @file HostCommands.h */
#pragma once

#include "ModBuildTool/src/Cli/CommandArguments.h"

namespace Mod::BuildTool
{
class HostCommands
{
public:
	static int RunHostCommand(const CommandArguments& Command);
};
} // namespace Mod::BuildTool
