// Copyright (c) 2026 Nelaric Contributors
/** @file InspectionCommands.h */
#pragma once

#include "ModBuildTool/src/Cli/CommandArguments.h"

namespace Mod::BuildTool
{
class InspectionCommands
{
public:
	static int InspectArchive(const CommandArguments& Command);
	static int InspectBinary(const CommandArguments& Command);
};
} // namespace Mod::BuildTool
