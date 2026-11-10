// Copyright (c) 2026 Nelaric Contributors
/** @file SdkBuildCommands.h */
#pragma once

#include <map>
#include <string>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

class SdkBuildCommands
{
public:
	static int PlanSdkCommand(const Options& Options);
	static int BuildSdkCommand(const Options& Options);

private:
	static int RunSdkBuild(const Options& Options, bool bPlan);
};
} // namespace Mod::SdkTool
