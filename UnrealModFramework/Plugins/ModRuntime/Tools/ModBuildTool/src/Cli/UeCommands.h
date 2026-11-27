// Copyright (c) 2026 Nelaric Contributors
/** @file UeCommands.h */
#pragma once

#include "ModBuildTool/src/Cli/CommandArguments.h"

namespace Mod::BuildTool
{
class UeCommands
{
public:
	static int PrepareMetadata(const CommandArguments& Command);
	static int ValidateMetadata(const CommandArguments& Command);
	static int TestTarget(const CommandArguments& Command);
	static int BuildTarget(const CommandArguments& Command);
	static int RefreshWatches(const CommandArguments& Command);
	static int FinalizeTarget(const CommandArguments& Command);
};
} // namespace Mod::BuildTool
