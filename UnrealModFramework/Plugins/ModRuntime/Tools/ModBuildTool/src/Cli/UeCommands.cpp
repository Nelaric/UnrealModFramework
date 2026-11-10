// Copyright (c) 2026 Nelaric Contributors
#include "UeCommands.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Build/Unreal/UeBuildAdapter.h"

#include <iostream>
#include <stdexcept>

namespace Mod::BuildTool
{
int UeCommands::RefreshWatches(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	if (!Options.contains("--contexts"))
	{
		throw std::runtime_error("refresh-ue-watches requires --contexts.");
	}
	UeBuildAdapter::RefreshUeWatches(Path::PathFromUtf8(Options.at("--contexts")));
	return 0;
}

int UeCommands::FinalizeTarget(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	if (!Options.contains("--contexts") || !Options.contains("--ue-target-receipt"))
	{
		throw std::runtime_error("finalize-ue-target requires --contexts and --ue-target-receipt.");
	}
	std::cout << UeBuildAdapter::FinalizeUeTarget(Path::PathFromUtf8(Options.at("--contexts")),
	                                              Path::PathFromUtf8(Options.at("--ue-target-receipt")))
	                 .dump(2)
	          << '\n';
	return std::cout ? 0 : 1;
}
} // namespace Mod::BuildTool
