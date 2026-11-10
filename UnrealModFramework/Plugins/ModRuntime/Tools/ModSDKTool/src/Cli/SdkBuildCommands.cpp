// Copyright (c) 2026 Nelaric Contributors
#include "SdkBuildCommands.h"

#include "ModSDKTool/src/Build/SdkBuild.h"
#include "ModSDKTool/src/Cli/CommandOptions.h"
#include "ModSDKTool/src/Cli/ResultOutput.h"

#include <map>
#include <string>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

int SdkBuildCommands::RunSdkBuild(const Options& Options, bool bPlan)
{
	const auto Output = CommandOptions::ResultPath(Options);
	SdkBuild Sdk(CommandOptions::OptionPath(Options, "--request"));
	Sdk.ValidateResultOutput(Output);
	return ResultOutput::ExecuteWithResult(Output, [&] { return bPlan ? Sdk.Plan() : Sdk.Build(); });
}

int SdkBuildCommands::PlanSdkCommand(const Options& Options)
{
	return RunSdkBuild(Options, true);
}

int SdkBuildCommands::BuildSdkCommand(const Options& Options)
{
	return RunSdkBuild(Options, false);
}
} // namespace Mod::SdkTool
