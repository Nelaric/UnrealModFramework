// Copyright (c) 2026 Nelaric Contributors
/** @file OptionsCheck.h */
#pragma once

#include "ModHeaderTool/src/Cli/CommandLineOptions.h"

namespace Mod::HeaderTool
{
class OptionsCheck
{
public:
	static int CheckOptions(const CommandLineOptions& Options);
};
} // namespace Mod::HeaderTool
