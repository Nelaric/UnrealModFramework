// Copyright (c) 2026 Nelaric Contributors
#include "UeCommands.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Build/Unreal/UeBuildAdapter.h"

#include <iostream>

namespace Mod::BuildTool
{
int UeCommands::RefreshWatches(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	UeBuildAdapter::RefreshUeWatches(Path::PathFromUtf8(Options.at("--contexts")));
	return 0;
}

int UeCommands::FinalizeTarget(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	std::cout << UeBuildAdapter::FinalizeUeTarget(Path::PathFromUtf8(Options.at("--contexts")),
	                                              Path::PathFromUtf8(Options.at("--ue-target-receipt")))
	                 .dump(2)
	          << '\n';
	return std::cout ? 0 : 1;
}
} // namespace Mod::BuildTool
