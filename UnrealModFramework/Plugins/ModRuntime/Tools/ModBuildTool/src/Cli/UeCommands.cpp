// Copyright (c) 2026 Nelaric Contributors
#include "UeCommands.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Build/Unreal/UeBuildAdapter.h"
#include "ModBuildTool/src/Build/Unreal/UeHookBuild.h"

#include <iostream>

namespace Mod::BuildTool
{
int UeCommands::BuildTarget(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	UeHookBuild::Run({.Engine = Path::PathFromUtf8(Options.at("--engine")),
	                  .Project = Path::PathFromUtf8(Options.at("--project")),
	                  .Target = Options.at("--target"),
	                  .Platform = Options.at("--platform"),
	                  .Configuration = Options.at("--configuration"),
	                  .Architecture = Options.at("--architecture"),
	                  .Mode = Options.at("--mode")});
	return 0;
}

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
