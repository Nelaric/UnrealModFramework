// Copyright (c) 2026 Nelaric Contributors
/** @file HashCommand.h */
#pragma once

#include "ModBuildTool/src/Cli/CommandArguments.h"

namespace Mod::BuildTool
{
class HashCommand
{
public:
	static int HashInput(const CommandArguments& Command);
};
} // namespace Mod::BuildTool
