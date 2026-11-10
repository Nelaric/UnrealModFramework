// Copyright (c) 2026 Nelaric Contributors
#include "CommandDiagnostics.h"

#include "ModSDKTool/src/Cli/ResultOutput.h"

#include <exception>

namespace Mod::SdkTool
{
int CommandDiagnostics::ReportCommandError(const std::exception& Error)
{
	return ResultOutput::ReportFailure(Error, {});
}
} // namespace Mod::SdkTool
