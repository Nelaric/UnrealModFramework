// Copyright (c) 2026 Nelaric Contributors
#include "SdkInspectCommand.h"

#include "ModSDKTool/src/Build/PathRelations.h"
#include "ModSDKTool/src/Bundle/SdkBundle.h"
#include "ModSDKTool/src/Cli/CommandOptions.h"
#include "ModSDKTool/src/Cli/ResultOutput.h"

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Options = std::map<std::string, std::string>;

int SdkInspectCommand::InspectSdkCommand(const Options& Options)
{
	const auto Output = CommandOptions::ResultPath(Options);
	const auto Bundle = fs::canonical(CommandOptions::OptionPath(Options, "--bundle"));
	if (!Output.empty() && PathRelations::IsInside(Output, Bundle))
	{
		throw std::runtime_error("Inspection output must be outside the inspected bundle.");
	}
	return ResultOutput::ExecuteWithResult(Output, [&] { return SdkBundle::InspectSdk(Bundle); });
}
} // namespace Mod::SdkTool
