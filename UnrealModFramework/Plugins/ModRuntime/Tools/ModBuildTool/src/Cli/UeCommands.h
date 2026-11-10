// Copyright (c) 2026 Nelaric Contributors
/** @file UeCommands.h */
#pragma once

#include "ModBuildTool/src/Cli/CommandArguments.h"

namespace Mod::BuildTool
{
class UeCommands
{
public:
	static int RefreshWatches(const CommandArguments& Command);
	static int FinalizeTarget(const CommandArguments& Command);
};
} // namespace Mod::BuildTool
