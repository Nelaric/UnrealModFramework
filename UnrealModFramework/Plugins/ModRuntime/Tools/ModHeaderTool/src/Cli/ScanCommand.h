// Copyright (c) 2026 Nelaric Contributors
/** @file ScanCommand.h */
#pragma once

#include "ModHeaderTool/src/Cli/CommandLineOptions.h"

namespace Mod::HeaderTool
{
class ScanCommand
{
public:
	static int RunScan(const CommandLineOptions& Options);
};
} // namespace Mod::HeaderTool
